# UEFN MCP listener — thread-safe command queue with main-thread draining.
#
# Portions adapted from KirChuvakov/uefn-mcp-server (MIT, (c) 2025 KirChuvakov);
# see UEFN/THIRD_PARTY_NOTICES.md. Reworked: explicit stored timestamps for stale GC
# (the reference parsed timestamps out of the id string, which was fragile), a bounded
# per-tick batch, and no dependency on `unreal`.
#
# THREADING MODEL (critical): the HTTP server runs on a daemon thread and only ever
# calls submit()/wait(). All real editor work happens when pump() is invoked from the
# editor MAIN THREAD (via register_slate_post_tick_callback). Handlers must never be
# run off the main thread.

import itertools
import queue
import threading
import time

DEFAULT_TICK_BATCH_LIMIT = 8
DEFAULT_STALE_SEC = 60.0


class CommandQueue:
    def __init__(self, tick_batch_limit: int = DEFAULT_TICK_BATCH_LIMIT,
                 stale_sec: float = DEFAULT_STALE_SEC):
        self._q: "queue.Queue" = queue.Queue()
        self._responses: dict = {}
        self._lock = threading.Lock()
        self._counter = itertools.count(1)
        self._id_lock = threading.Lock()
        self._tick_batch_limit = tick_batch_limit
        self._stale_sec = stale_sec

    def new_request_id(self) -> str:
        with self._id_lock:
            n = next(self._counter)
        return f"req_{n}_{time.time_ns()}"

    # --- HTTP (daemon) thread side --------------------------------------

    def submit(self, command: str, params: dict, request_id: str | None = None) -> str:
        """Enqueue a command for main-thread execution; returns its request id."""
        req_id = request_id or self.new_request_id()
        self._q.put((req_id, command, params))
        return req_id

    def wait(self, request_id: str, timeout: float, poll_interval: float = 0.01):
        """
        Block until pump() produces a response for request_id, or timeout (seconds).
        Returns the response dict, or None on timeout. Pops the response on success.
        """
        deadline = time.monotonic() + timeout
        while time.monotonic() < deadline:
            with self._lock:
                if request_id in self._responses:
                    return self._responses.pop(request_id)[0]
            time.sleep(poll_interval)
        # One last check to avoid a race at the deadline boundary.
        with self._lock:
            if request_id in self._responses:
                return self._responses.pop(request_id)[0]
        return None

    # --- MAIN (editor) thread side --------------------------------------

    def pump(self, dispatch, batch_limit: int | None = None) -> int:
        """
        Drain up to batch_limit queued commands, running `dispatch(command, params)`
        for each on the calling (main) thread and storing the result for wait().
        `dispatch` returns the full response envelope (dict). Returns count processed.
        MUST be called on the editor main thread. Never raises out of a handler —
        dispatch is expected to convert handler errors into an error envelope.
        """
        limit = batch_limit or self._tick_batch_limit
        processed = 0
        while processed < limit:
            try:
                req_id, command, params = self._q.get_nowait()
            except queue.Empty:
                break
            response = dispatch(command, params, req_id)
            with self._lock:
                self._responses[req_id] = (response, time.monotonic())
            processed += 1
        if processed:
            self._gc_stale()
        return processed

    def _gc_stale(self):
        cutoff = time.monotonic() - self._stale_sec
        with self._lock:
            stale = [k for k, (_, ts) in self._responses.items() if ts < cutoff]
            for k in stale:
                self._responses.pop(k, None)

    # --- introspection (tests / metrics) --------------------------------

    def pending_count(self) -> int:
        return self._q.qsize()

    def response_count(self) -> int:
        with self._lock:
            return len(self._responses)
