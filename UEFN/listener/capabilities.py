# UEFN MCP listener — capability manifest + runtime probing + version info.
#
# Pure stdlib except for the INJECTED unreal module used in probing. Never top-level
# imports `unreal`. Statuses mirror docs/uefn/TOOL_COMPATIBILITY.md.

import os
import sys

LISTENER_VERSION = "0.1.0"

# Status vocabulary (see TOOL_COMPATIBILITY.md).
SUPPORTED = "supported"
SUPPORTED_WITH_LIMITATIONS = "supported_with_limitations"
EXPERIMENTAL = "experimental"
FILESYSTEM_ONLY = "filesystem_only"
MANUAL_GATE_REQUIRED = "manual_gate_required"
UNSUPPORTED = "unsupported"
NOT_YET_INVESTIGATED = "not_yet_investigated"

# Static, declared capability statuses for the Phase-1 surface. This is the honest
# baseline; runtime probing (below) can only DOWNGRADE a capability to unsupported if
# the required API is missing — it never upgrades to `supported` (that needs editor
# evidence + validation, recorded by a human gate).
STATIC_MANIFEST = {
    # System / listener-internal (do not need unreal).
    "listener.ping": EXPERIMENTAL,
    "listener.status": EXPERIMENTAL,
    "listener.log": EXPERIMENTAL,
    "listener.shutdown": EXPERIMENTAL,
    "system.project_info": EXPERIMENTAL,
    "system.version_info": EXPERIMENTAL,
    "system.capabilities": EXPERIMENTAL,
    # First-wave editor surface — investigated in later phases.
    "actors.read": NOT_YET_INVESTIGATED,
    "actors.transform": NOT_YET_INVESTIGATED,
    "actors.spawn": NOT_YET_INVESTIGATED,
    "actors.delete": NOT_YET_INVESTIGATED,
    "assets.read": NOT_YET_INVESTIGATED,
    "assets.rename": NOT_YET_INVESTIGATED,
    "levels.read": NOT_YET_INVESTIGATED,
    "levels.save": NOT_YET_INVESTIGATED,
    "selection.read": NOT_YET_INVESTIGATED,
    "selection.write": NOT_YET_INVESTIGATED,
    "viewport.camera": NOT_YET_INVESTIGATED,
    # Known non-parity / gated.
    "blueprints.graph_mutation": UNSUPPORTED,
    "materials.graph_mutation": UNSUPPORTED,
    "niagara.authoring": UNSUPPORTED,
    "verse.filesystem_edit": FILESYSTEM_ONLY,
    "verse.compile": MANUAL_GATE_REQUIRED,
    "validation.run": NOT_YET_INVESTIGATED,
    "python.arbitrary": UNSUPPORTED,  # off unless explicitly enabled; see listener.
}

# Maps a probe key -> the unreal attribute/subsystem that must exist for it.
_PROBE_TARGETS = {
    "slate_post_tick": ("register_slate_post_tick_callback", "attr"),
    "editor_actor_subsystem": ("EditorActorSubsystem", "attr"),
    "editor_asset_library": ("EditorAssetLibrary", "attr"),
    "level_editor_subsystem": ("LevelEditorSubsystem", "attr"),
    "editor_util_library": ("EditorUtilityLibrary", "attr"),
    "asset_registry_helpers": ("AssetRegistryHelpers", "attr"),
    "editor_validator_subsystem": ("EditorValidatorSubsystem", "attr"),
    "paths": ("Paths", "attr"),
}


def probe_runtime(unreal_module) -> dict:
    """
    Return {probe_key: bool} for each capability-relevant unreal API. Safe when the
    module is a test double or None (everything reports False for None).
    """
    result = {}
    for key, (attr, _kind) in _PROBE_TARGETS.items():
        result[key] = bool(unreal_module is not None and hasattr(unreal_module, attr))
    return result


def effective_manifest(unreal_module) -> dict:
    """
    Static manifest, with a probe summary attached. Does NOT auto-upgrade statuses;
    it exposes probe results so clients and humans can see what the editor actually
    offers before promotion.
    """
    return {
        "statuses": dict(STATIC_MANIFEST),
        "probes": probe_runtime(unreal_module),
    }


def version_info(unreal_module) -> dict:
    """
    Runtime-detected version info. Deliberately does NOT hardcode or fabricate the
    Fortnite Ecosystem version — Python cannot reliably query it, so it is reported
    as null unless supplied via UEFN_ECOSYSTEM_VERSION (operator-provided, labeled).
    """
    py = f"{sys.version_info.major}.{sys.version_info.minor}.{sys.version_info.micro}"
    engine = _engine_version(unreal_module)
    eco_env = os.environ.get("UEFN_ECOSYSTEM_VERSION")
    return {
        "listener_version": LISTENER_VERSION,
        "python_version": py,
        "engine_version": engine,               # None if not queryable
        "fortnite_ecosystem_version": eco_env,   # None unless operator-provided
        "fortnite_ecosystem_source": "env:UEFN_ECOSYSTEM_VERSION" if eco_env else "not_queryable_via_python",
    }


def _engine_version(unreal_module):
    if unreal_module is None:
        return None
    syslib = getattr(unreal_module, "SystemLibrary", None)
    getver = getattr(syslib, "get_engine_version", None) if syslib else None
    if callable(getver):
        try:
            return str(getver())
        except Exception:  # noqa: BLE001
            return None
    return None
