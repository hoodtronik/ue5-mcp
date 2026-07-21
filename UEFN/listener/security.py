# UEFN MCP listener — security layer (auth token, size/timeout limits).
#
# Portions of this backend are adapted from KirChuvakov/uefn-mcp-server
# (MIT, (c) 2025 KirChuvakov); see UEFN/THIRD_PARTY_NOTICES.md. The reference
# project had NO authentication — this security layer is re-implemented, not ported.
#
# Pure Python stdlib only (must import cleanly inside UEFN's embedded Python 3.11
# and under system Python for repo-level tests). Never imports `unreal`.

import hmac
import os
import secrets

# CLAUDE-NOTE: defaults are conservative and overridable via env. Size cap guards
# against a memory-DoS from a lying Content-Length header (the reference listener
# trusted the header unbounded). Timeout bounds a single command's main-thread wait.
DEFAULT_MAX_REQUEST_BYTES = 1_048_576          # 1 MiB
DEFAULT_COMMAND_TIMEOUT_SEC = 30.0
TOKEN_ENV = "UEFN_MCP_TOKEN"
TOKEN_FILE_ENV = "UEFN_MCP_TOKEN_FILE"


class AuthError(Exception):
    """Raised when a request fails authentication."""


class RequestTooLargeError(Exception):
    """Raised when a request body exceeds the configured size limit."""


def generate_token(nbytes: int = 32) -> str:
    """Generate a cryptographically-strong URL-safe token."""
    return secrets.token_urlsafe(nbytes)


def default_token_path() -> str:
    """User-local token path, deliberately OUTSIDE the repo/source control."""
    override = os.environ.get(TOKEN_FILE_ENV)
    if override:
        return os.path.abspath(os.path.expanduser(override))
    base = os.path.join(os.path.expanduser("~"), ".uefn-mcp")
    return os.path.join(base, "token")


def load_or_create_token(path: str | None = None) -> str:
    """
    Resolve the shared secret. Precedence:
      1. UEFN_MCP_TOKEN env var (if set and non-empty)
      2. token file (created with a fresh token if absent)
    The token file is written user-locally with best-effort restrictive perms.
    """
    env_token = os.environ.get(TOKEN_ENV)
    if env_token:
        return env_token.strip()

    path = path or default_token_path()
    try:
        with open(path, "r", encoding="utf-8") as fh:
            existing = fh.read().strip()
        if existing:
            return existing
    except FileNotFoundError:
        pass

    token = generate_token()
    os.makedirs(os.path.dirname(path), exist_ok=True)
    # CLAUDE-NOTE: write then chmod 0600 (best effort; no-op semantics on Windows).
    with open(path, "w", encoding="utf-8") as fh:
        fh.write(token)
    try:
        os.chmod(path, 0o600)
    except OSError:
        pass
    return token


def tokens_match(provided: str | None, expected: str) -> bool:
    """Constant-time comparison; empty/None provided always fails."""
    if not provided or not expected:
        return False
    return hmac.compare_digest(str(provided), str(expected))


def check_auth(provided: str | None, expected: str) -> None:
    """Raise AuthError unless the provided token matches."""
    if not tokens_match(provided, expected):
        raise AuthError("missing or invalid authentication token")


def check_size(content_length: int, limit: int = DEFAULT_MAX_REQUEST_BYTES) -> None:
    """Raise RequestTooLargeError if the declared body size exceeds the limit."""
    if content_length is None or content_length < 0:
        raise RequestTooLargeError("missing or invalid Content-Length")
    if content_length > limit:
        raise RequestTooLargeError(
            f"request body {content_length} exceeds limit {limit}"
        )
