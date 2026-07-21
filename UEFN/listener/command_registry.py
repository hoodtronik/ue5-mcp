# UEFN MCP listener — command handler registry + dispatch.
#
# Pattern adapted from KirChuvakov/uefn-mcp-server (MIT, (c) 2025 KirChuvakov);
# see UEFN/THIRD_PARTY_NOTICES.md. Extended with per-command capability keys and
# structured error envelopes. Pure stdlib; never imports `unreal`.
#
# A handler has signature: fn(params: dict, ctx) -> result (JSON-serializable or an
# unreal.* object that ctx.serializer can handle). Handlers raise on error; the
# dispatcher converts exceptions into structured error envelopes.

from . import protocol as P
from .capabilities import STATIC_MANIFEST, UNSUPPORTED, MANUAL_GATE_REQUIRED


class UnknownCommandError(Exception):
    pass


class CommandRegistry:
    def __init__(self):
        self._handlers = {}   # name -> (fn, capability_key)

    def register(self, name: str, capability: str | None = None):
        def deco(fn):
            self._handlers[name] = (fn, capability)
            return fn
        return deco

    def add(self, name: str, fn, capability: str | None = None):
        self._handlers[name] = (fn, capability)

    def has(self, name: str) -> bool:
        return name in self._handlers

    def names(self) -> list:
        return sorted(self._handlers.keys())

    def get(self, name: str):
        return self._handlers.get(name)


def make_dispatcher(registry: CommandRegistry, ctx):
    """
    Build the dispatch(command, params, request_id) -> response-envelope function that
    CommandQueue.pump() calls on the main thread. Never raises.
    """
    def dispatch(command: str, params: dict, request_id) -> dict:
        started = P.now_ms()
        entry = registry.get(command)
        if entry is None:
            ctx.log.warn(f"unknown command: {command}")
            return P.error_response(
                request_id, P.ERR_UNKNOWN_COMMAND, f"unknown command '{command}'",
                {"known_commands": registry.names()},
            )
        fn, capability = entry

        # Capability gate: a command wired to a capability that is declared
        # unsupported/manual-gate fails explicitly rather than silently substituting.
        if capability is not None:
            status = STATIC_MANIFEST.get(capability)
            if status in (UNSUPPORTED, MANUAL_GATE_REQUIRED):
                return P.capability_unavailable_response(
                    request_id, command,
                    f"capability '{capability}' is '{status}' in UEFN",
                    _alternatives_for(capability),
                )

        try:
            result = fn(params, ctx)
        except P.ProtocolError as pe:
            return P.error_response(request_id, pe.code, pe.message, pe.details)
        except Exception as exc:  # noqa: BLE001 - must convert to envelope, never crash the tick
            ctx.log.error(f"handler '{command}' failed: {exc!r}")
            return P.error_response(
                request_id, P.ERR_HANDLER_ERROR, str(exc), {"command": command},
            )

        serialized = ctx.serializer.serialize(result)
        timing = round(P.now_ms() - started, 3)
        return P.success_response(request_id, serialized, timing_ms=timing)

    return dispatch


def _alternatives_for(capability: str) -> list:
    table = {
        "blueprints.graph_mutation": [
            "Implement gameplay logic in Verse",
            "Use a supported UEFN device",
            "Perform this operation manually in the editor",
        ],
        "verse.compile": [
            "Build Verse in the editor (Verse > Build Verse Code)",
            "Use uefn_write_verse_file then compile manually, then Push Changes",
        ],
        "python.arbitrary": [
            "Use a narrow typed tool instead",
            "Enable UEFN_MCP_ALLOW_ARBITRARY_PYTHON for supervised local dev only",
        ],
    }
    return table.get(capability, ["Perform this operation manually in the editor"])
