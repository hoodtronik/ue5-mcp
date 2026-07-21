# UEFN MCP listener — versioned command protocol (request parse + response/error envelopes).
#
# Pure stdlib. Never imports `unreal`. Protocol is versioned and intentionally does
# NOT preserve the reference project's weaker envelope for compatibility's sake.

import time

PROTOCOL_VERSION = 1

# Structured error codes surfaced to the MCP client.
ERR_BAD_REQUEST = "BAD_REQUEST"
ERR_UNAUTHORIZED = "UNAUTHORIZED"
ERR_REQUEST_TOO_LARGE = "REQUEST_TOO_LARGE"
ERR_UNKNOWN_COMMAND = "UNKNOWN_COMMAND"
ERR_CAPABILITY_UNAVAILABLE = "CAPABILITY_UNAVAILABLE"
ERR_TIMEOUT = "TIMEOUT"
ERR_HANDLER_ERROR = "HANDLER_ERROR"
ERR_MAIN_THREAD_UNAVAILABLE = "MAIN_THREAD_UNAVAILABLE"


class ProtocolError(Exception):
    """Raised on a malformed request; carries a structured code."""

    def __init__(self, code: str, message: str, details: dict | None = None):
        super().__init__(message)
        self.code = code
        self.message = message
        self.details = details or {}


def parse_request(payload: dict) -> dict:
    """
    Validate a decoded request dict and return a normalized envelope:
        {protocol_version, request_id, command, params, auth_token}
    Raises ProtocolError(BAD_REQUEST) on structural problems. Auth is checked
    separately by the security layer — this only validates shape.
    """
    if not isinstance(payload, dict):
        raise ProtocolError(ERR_BAD_REQUEST, "request body must be a JSON object")

    command = payload.get("command")
    if not isinstance(command, str) or not command:
        raise ProtocolError(ERR_BAD_REQUEST, "missing or invalid 'command'")

    params = payload.get("params", {})
    if params is None:
        params = {}
    if not isinstance(params, dict):
        raise ProtocolError(ERR_BAD_REQUEST, "'params' must be an object if provided")

    version = payload.get("protocol_version", PROTOCOL_VERSION)
    if not isinstance(version, int):
        raise ProtocolError(ERR_BAD_REQUEST, "'protocol_version' must be an integer")

    return {
        "protocol_version": version,
        "request_id": payload.get("request_id"),
        "command": command,
        "params": params,
        "auth_token": payload.get("auth_token"),
    }


def success_response(
    request_id,
    result,
    warnings=None,
    verification=None,
    timing_ms=None,
) -> dict:
    """Build a success envelope. `verification` distinguishes what actually happened."""
    return {
        "protocol_version": PROTOCOL_VERSION,
        "request_id": request_id,
        "success": True,
        "result": result,
        "warnings": warnings or [],
        "verification": verification or {"state_inspected": False, "validation_run": False},
        "timing_ms": timing_ms,
    }


def error_response(request_id, code: str, message: str, details: dict | None = None) -> dict:
    """Build a structured error envelope."""
    return {
        "protocol_version": PROTOCOL_VERSION,
        "request_id": request_id,
        "success": False,
        "error": {"code": code, "message": message, "details": details or {}},
    }


def capability_unavailable_response(request_id, tool: str, reason: str, alternatives=None) -> dict:
    """The canonical 'this tool is not supported on this backend' error."""
    return error_response(
        request_id,
        ERR_CAPABILITY_UNAVAILABLE,
        reason,
        {"tool": tool, "backend": "uefn", "alternatives": alternatives or []},
    )


def now_ms() -> float:
    return time.monotonic() * 1000.0
