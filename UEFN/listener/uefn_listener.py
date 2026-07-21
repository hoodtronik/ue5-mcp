# UEFN MCP listener — loopback HTTP server + main-thread command pump.
#
# Portions adapted from KirChuvakov/uefn-mcp-server (MIT, (c) 2025 KirChuvakov);
# see UEFN/THIRD_PARTY_NOTICES.md. Security (auth token, size/timeout limits), the
# lifecycle, autostart, and capability model are re-implemented.
#
# Runs inside UEFN's embedded Python 3.11 (stdlib only). Imports cleanly under system
# Python too: the `unreal` import is optional and all editor work is injected.
#
# THREADING: the HTTP server runs on a daemon thread and only enqueues commands. All
# `unreal.*` work is drained on the editor MAIN THREAD by a slate-post-tick callback.

import json
import socket
import threading
import time
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer

from . import capabilities as C
from . import protocol as P
from . import security
from .command_queue import CommandQueue
from .command_registry import CommandRegistry, make_dispatcher
from .logbuffer import LogRing
from .serialization import Serializer
from .handlers.system import register_system_handlers

try:  # optional — absent under system Python / outside UEFN
    import unreal as _UNREAL_DEFAULT  # type: ignore
except Exception:  # noqa: BLE001
    _UNREAL_DEFAULT = None

DEFAULT_PORT = 8765
PORT_SCAN_RANGE = 8  # scan DEFAULT_PORT .. DEFAULT_PORT+range


class Context:
    """Shared state passed to handlers and the dispatcher."""

    def __init__(self, *, unreal_module, token, port, log, queue, registry, serializer,
                 allow_arbitrary_python=False):
        self.unreal = unreal_module
        self.token = token
        self.port = port
        self.log = log
        self.queue = queue
        self.registry = registry
        self.serializer = serializer
        self.allow_arbitrary_python = allow_arbitrary_python
        self.main_thread_dispatch = False
        self.metrics = {"requests_received": 0, "requests_ok": 0, "requests_error": 0}
        self._started = time.monotonic()
        self._shutdown_cb = None

    def uptime_ms(self):
        return (time.monotonic() - self._started) * 1000.0

    def request_shutdown(self):
        if self._shutdown_cb:
            self._shutdown_cb()


class _Handler(BaseHTTPRequestHandler):
    # `server.ctx` and `server.dispatch_timeout` are set by start_listener.

    def log_message(self, fmt, *args):  # noqa: A003 - silence stderr; route to ring log
        try:
            self.server.ctx.log.info("http: " + (fmt % args))
        except Exception:  # noqa: BLE001
            pass

    def _send(self, status, payload):
        body = json.dumps(payload).encode("utf-8")
        self.send_response(status)
        self.send_header("Content-Type", "application/json")
        self.send_header("Content-Length", str(len(body)))
        self.end_headers()
        try:
            self.wfile.write(body)
        except (BrokenPipeError, ConnectionResetError):
            pass

    def do_GET(self):  # noqa: N802
        # Minimal, UNAUTHENTICATED liveness beacon so the client can discover a
        # listener/port. Deliberately leaks no project/capability detail.
        if self.path.rstrip("/") in ("", "/health"):
            self._send(200, {
                "status": "ok", "backend": "uefn",
                "protocol_version": P.PROTOCOL_VERSION,
                "listener_version": C.LISTENER_VERSION,
                "auth_required": True,
            })
        else:
            self._send(404, P.error_response(None, P.ERR_BAD_REQUEST, "not found"))

    def do_POST(self):  # noqa: N802
        ctx = self.server.ctx
        ctx.metrics["requests_received"] += 1

        # 1. Size gate (trust nothing; cap the declared body).
        try:
            length = int(self.headers.get("Content-Length", -1))
            security.check_size(length, self.server.max_request_bytes)
        except security.RequestTooLargeError as e:
            ctx.metrics["requests_error"] += 1
            return self._send(413, P.error_response(None, P.ERR_REQUEST_TOO_LARGE, str(e)))
        except (TypeError, ValueError):
            ctx.metrics["requests_error"] += 1
            return self._send(400, P.error_response(None, P.ERR_BAD_REQUEST, "invalid Content-Length"))

        # 2. Read + decode.
        try:
            raw = self.rfile.read(length)
            payload = json.loads(raw.decode("utf-8"))
        except Exception:  # noqa: BLE001
            ctx.metrics["requests_error"] += 1
            return self._send(400, P.error_response(None, P.ERR_BAD_REQUEST, "malformed JSON body"))

        # 3. Validate shape.
        try:
            req = P.parse_request(payload)
        except P.ProtocolError as pe:
            ctx.metrics["requests_error"] += 1
            return self._send(400, P.error_response(payload.get("request_id"), pe.code, pe.message, pe.details))

        # 4. Authenticate (header preferred; body token accepted).
        token = self.headers.get("X-MCP-Token") or req.get("auth_token")
        try:
            security.check_auth(token, ctx.token)
        except security.AuthError as ae:
            ctx.metrics["requests_error"] += 1
            return self._send(401, P.error_response(req["request_id"], P.ERR_UNAUTHORIZED, str(ae)))

        # 5. Enqueue for main-thread execution and wait for the response.
        req_id = ctx.queue.submit(req["command"], req["params"], req.get("request_id"))
        response = ctx.queue.wait(req_id, self.server.dispatch_timeout)
        if response is None:
            ctx.metrics["requests_error"] += 1
            return self._send(504, P.error_response(
                req_id, P.ERR_TIMEOUT,
                f"command '{req['command']}' timed out after {self.server.dispatch_timeout}s "
                "(is the editor main-thread pump running?)"))

        if response.get("success"):
            ctx.metrics["requests_ok"] += 1
        else:
            ctx.metrics["requests_error"] += 1
        self._send(200, response)


def _find_free_port(host, start, span):
    for p in range(start, start + span + 1):
        with socket.socket(socket.AF_INET, socket.SOCK_STREAM) as s:
            s.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1)
            try:
                s.bind((host, p))
                return p
            except OSError:
                continue
    raise OSError(f"no free port in {start}..{start + span}")


def build_listener(*, unreal_module="__default__", token=None, port=None,
                   allow_arbitrary_python=False, log_capacity=500):
    """
    Construct the queue/registry/serializer/context WITHOUT binding a socket. Useful
    for tests and for callers that want to drive pump() manually. Returns (ctx, dispatch).
    """
    if unreal_module == "__default__":
        unreal_module = _UNREAL_DEFAULT
    token = token or security.load_or_create_token()
    log = LogRing(log_capacity)
    queue = CommandQueue()
    registry = CommandRegistry()
    register_system_handlers(registry)
    serializer = Serializer(unreal_module)
    ctx = Context(unreal_module=unreal_module, token=token, port=port or DEFAULT_PORT,
                  log=log, queue=queue, registry=registry, serializer=serializer,
                  allow_arbitrary_python=allow_arbitrary_python)
    dispatch = make_dispatcher(registry, ctx)
    return ctx, dispatch


def start_listener(*, unreal_module="__default__", token=None, desired_port=DEFAULT_PORT,
                   host="127.0.0.1", register_tick=True, background_pump=False,
                   allow_arbitrary_python=False,
                   max_request_bytes=security.DEFAULT_MAX_REQUEST_BYTES,
                   dispatch_timeout=security.DEFAULT_COMMAND_TIMEOUT_SEC):
    """
    Start the loopback HTTP listener and (by default) register the editor main-thread
    pump. Returns the Context (holds .server, .token, .port).

    - register_tick: register unreal.register_slate_post_tick_callback to drain the
      queue on the main thread. Requires the API; probed via hasattr.
    - background_pump: drain the queue from a daemon thread instead. ONLY permitted
      when unreal_module is None (tests / no editor), because running unreal.* off the
      main thread is unsafe.
    """
    if unreal_module == "__default__":
        unreal_module = _UNREAL_DEFAULT

    if background_pump and unreal_module is not None:
        raise ValueError("background_pump is only allowed when unreal_module is None "
                         "(unreal.* must run on the editor main thread)")

    port = _find_free_port(host, desired_port, PORT_SCAN_RANGE)
    ctx, dispatch = build_listener(unreal_module=unreal_module, token=token, port=port,
                                   allow_arbitrary_python=allow_arbitrary_python)

    httpd = ThreadingHTTPServer((host, port), _Handler)
    httpd.ctx = ctx
    httpd.dispatch_timeout = dispatch_timeout
    httpd.max_request_bytes = max_request_bytes
    ctx.server = httpd

    server_thread = threading.Thread(target=httpd.serve_forever, name="uefn-mcp-http", daemon=True)
    server_thread.start()
    ctx._server_thread = server_thread

    # Wire main-thread execution.
    tick_handle = None
    if register_tick and unreal_module is not None and hasattr(unreal_module, "register_slate_post_tick_callback"):
        def _tick(_delta):
            ctx.queue.pump(dispatch)
        tick_handle = unreal_module.register_slate_post_tick_callback(_tick)
        ctx.main_thread_dispatch = True
        ctx.log.info("registered slate post-tick pump")
    elif background_pump:
        stop = threading.Event()

        def _pump_loop():
            while not stop.is_set():
                ctx.queue.pump(dispatch)
                time.sleep(0.005)
        pump_thread = threading.Thread(target=_pump_loop, name="uefn-mcp-pump", daemon=True)
        pump_thread.start()
        ctx._pump_stop = stop
        ctx._pump_thread = pump_thread
        ctx.main_thread_dispatch = False
        ctx.log.info("using background pump (no editor main thread)")
    else:
        ctx.main_thread_dispatch = False
        ctx.log.warn("no main-thread pump registered: commands will time out until a "
                     "pump drains the queue")

    # Shutdown wiring (deferred so the in-flight request can still return).
    def _shutdown():
        ctx.log.info("listener shutting down")

        def _do():
            time.sleep(0.3)
            stop_listener(ctx, tick_handle=tick_handle)
        threading.Thread(target=_do, name="uefn-mcp-shutdown", daemon=True).start()
    ctx._shutdown_cb = _shutdown

    # Preserve state across re-exec inside UEFN so repeated Execute-Python-Script calls
    # don't leak servers/callbacks.
    if unreal_module is not None:
        _store_singleton(unreal_module, ctx, tick_handle)

    ctx.log.info(f"UEFN MCP listener started on {host}:{port}")
    return ctx


def stop_listener(ctx, tick_handle=None):
    """Cleanly stop the HTTP server, background pump, and tick callback."""
    u = getattr(ctx, "unreal", None)
    if tick_handle is not None and u is not None and hasattr(u, "unregister_slate_post_tick_callback"):
        try:
            u.unregister_slate_post_tick_callback(tick_handle)
        except Exception:  # noqa: BLE001
            pass
    stop = getattr(ctx, "_pump_stop", None)
    if stop is not None:
        stop.set()
    server = getattr(ctx, "server", None)
    if server is not None:
        try:
            server.shutdown()
            server.server_close()
        except Exception:  # noqa: BLE001
            pass
    ctx.log.info("listener stopped")


def _store_singleton(unreal_module, ctx, tick_handle):
    # Replace any prior instance first.
    prior = getattr(unreal_module, "_uefn_mcp_ctx", None)
    if prior is not None and prior is not ctx:
        try:
            stop_listener(prior, tick_handle=getattr(unreal_module, "_uefn_mcp_tick", None))
        except Exception:  # noqa: BLE001
            pass
    unreal_module._uefn_mcp_ctx = ctx
    unreal_module._uefn_mcp_tick = tick_handle


def main():
    """Entry used by execute_in_uefn.py / init_unreal.py inside the editor."""
    import os
    allow_py = os.environ.get("UEFN_MCP_ALLOW_ARBITRARY_PYTHON", "").lower() in ("1", "true", "yes")
    port = int(os.environ.get("UEFN_MCP_PORT", DEFAULT_PORT))
    ctx = start_listener(desired_port=port, allow_arbitrary_python=allow_py)
    print(f"[uefn-mcp] listener running on 127.0.0.1:{ctx.port} "
          f"(token at {security.default_token_path()}; arbitrary_python={allow_py})")
    return ctx


if __name__ == "__main__":  # pragma: no cover
    # Direct execution isn't the supported path (relative imports need the package).
    # Use UEFN/execute_in_uefn.py, which bootstraps sys.path. Fail loudly if misused.
    raise SystemExit(
        "Run UEFN/execute_in_uefn.py via 'Execute Python Script' instead of this module directly."
    )
