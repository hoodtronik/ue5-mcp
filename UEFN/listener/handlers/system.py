# UEFN MCP listener — system/diagnostic command handlers (Phase 1).
#
# Handlers take (params: dict, ctx) and return a JSON-serializable result (or an
# unreal.* object the serializer understands). They may raise; the dispatcher wraps
# exceptions into structured error envelopes. Never top-level imports `unreal`.

from .. import capabilities as C
from ..protocol import ProtocolError, ERR_BAD_REQUEST


def register_system_handlers(registry):
    registry.add("ping", ping, "listener.ping")
    registry.add("get_listener_status", get_listener_status, "listener.status")
    registry.add("get_project_info", get_project_info, "system.project_info")
    registry.add("get_capabilities", get_capabilities, "system.capabilities")
    registry.add("get_listener_log", get_listener_log, "listener.log")
    registry.add("shutdown_listener", shutdown_listener, "listener.shutdown")


def _security_summary(ctx):
    return {
        "loopback_only": True,
        "authentication": True,
        "arbitrary_python_enabled": bool(getattr(ctx, "allow_arbitrary_python", False)),
    }


def ping(params, ctx):
    return {
        "pong": True,
        "listener_version": C.LISTENER_VERSION,
        "port": ctx.port,
        "command_count": len(ctx.registry.names()),
    }


def get_listener_status(params, ctx):
    m = ctx.metrics
    return {
        "status": "ok",
        "backend": "uefn",
        "listener_version": C.LISTENER_VERSION,
        "port": ctx.port,
        "uptime_ms": round(ctx.uptime_ms(), 1),
        "main_thread_dispatch": bool(ctx.main_thread_dispatch),
        "requests_received": m.get("requests_received", 0),
        "requests_ok": m.get("requests_ok", 0),
        "requests_error": m.get("requests_error", 0),
        "queue_pending": ctx.queue.pending_count(),
        "queue_responses_waiting": ctx.queue.response_count(),
        "security": _security_summary(ctx),
    }


def get_project_info(params, ctx):
    u = ctx.unreal
    if u is None:
        # Honest: the listener is running but not inside a UEFN editor.
        return {"available": False, "reason": "unreal module not available (not running inside UEFN)"}
    paths = getattr(u, "Paths", None)
    info = {"available": True}
    if paths is not None:
        info["project_dir"] = _safe_call(paths, "project_dir")
        info["project_content_dir"] = _safe_call(paths, "project_content_dir")
        info["project_log_dir"] = _safe_call(paths, "project_log_dir")
        pfp = _safe_call(paths, "get_project_file_path")
        if pfp:
            info["project_file"] = pfp
    return info


def get_capabilities(params, ctx):
    """The rich health + capability manifest (spec section 7)."""
    u = ctx.unreal
    manifest = C.effective_manifest(u)
    versions = C.version_info(u)
    proj = get_project_info(params, ctx)
    return {
        "status": "ok",
        "backend": "uefn",
        "editor_product": "Unreal Editor for Fortnite",
        "listener_version": versions["listener_version"],
        "python_version": versions["python_version"],
        "engine_version": versions["engine_version"],
        "fortnite_ecosystem_version": versions["fortnite_ecosystem_version"],
        "fortnite_ecosystem_source": versions["fortnite_ecosystem_source"],
        "project": proj,
        "port": ctx.port,
        "main_thread_dispatch": bool(ctx.main_thread_dispatch),
        "security": _security_summary(ctx),
        "capabilities": manifest["statuses"],
        "runtime_probes": manifest["probes"],
    }


def get_listener_log(params, ctx):
    n = params.get("last_n", 100)
    if not isinstance(n, int) or n <= 0:
        raise ProtocolError(ERR_BAD_REQUEST, "'last_n' must be a positive integer")
    return {"entries": ctx.log.tail(n)}


def shutdown_listener(params, ctx):
    ctx.log.info("shutdown_listener requested")
    ctx.request_shutdown()
    return {"shutting_down": True}


def _safe_call(obj, method):
    fn = getattr(obj, method, None)
    if callable(fn):
        try:
            return str(fn())
        except Exception:  # noqa: BLE001
            return None
    return None
