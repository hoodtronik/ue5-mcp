# UEFN MCP listener — in-memory ring buffer log. Pure stdlib, no `unreal`.

import collections
import threading
import time


class LogRing:
    """Thread-safe fixed-capacity log. Newest entries kept; oldest dropped."""

    def __init__(self, capacity: int = 500):
        self._buf: "collections.deque" = collections.deque(maxlen=capacity)
        self._lock = threading.Lock()

    def add(self, level: str, message: str) -> None:
        entry = {"t": time.time(), "level": level, "message": str(message)}
        with self._lock:
            self._buf.append(entry)

    def info(self, message: str) -> None:
        self.add("info", message)

    def warn(self, message: str) -> None:
        self.add("warn", message)

    def error(self, message: str) -> None:
        self.add("error", message)

    def tail(self, n: int = 100) -> list:
        with self._lock:
            items = list(self._buf)
        if n and n > 0:
            items = items[-n:]
        return items

    def __len__(self) -> int:
        with self._lock:
            return len(self._buf)
