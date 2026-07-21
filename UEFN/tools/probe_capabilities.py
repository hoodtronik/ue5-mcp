"""
Run INSIDE UEFN (Tools > Execute Python Script) to gather real editor evidence about
which `unreal` APIs are exposed. Prints a JSON report and writes it to the project log
dir. Use its output to promote/downgrade statuses in docs/uefn/TOOL_COMPATIBILITY.md.

This does NOT mutate anything.
"""
import json
import sys

try:
    import unreal  # type: ignore
except Exception as exc:  # noqa: BLE001
    print("This script must run inside UEFN (no `unreal` module):", exc)
    raise SystemExit(1)

# APIs the first-wave UEFN tools depend on. Extend as needed.
ATTRS = [
    "register_slate_post_tick_callback",
    "unregister_slate_post_tick_callback",
    "register_python_shutdown_callback",
    "EditorActorSubsystem",
    "EditorAssetLibrary",
    "EditorLevelLibrary",
    "LevelEditorSubsystem",
    "EditorUtilityLibrary",
    "EditorUtilitySubsystem",
    "AssetRegistryHelpers",
    "EditorValidatorSubsystem",
    "SystemLibrary",
    "Paths",
    "Vector", "Rotator", "Transform", "AssetData",
]


def _engine_version():
    try:
        return str(unreal.SystemLibrary.get_engine_version())
    except Exception:  # noqa: BLE001
        return None


def main():
    report = {
        "python_version": sys.version,
        "engine_version": _engine_version(),
        "has": {a: hasattr(unreal, a) for a in ATTRS},
        "unreal_attr_count": len(dir(unreal)),
    }
    text = json.dumps(report, indent=2)
    print(text)
    try:
        out = unreal.Paths.project_log_dir() + "uefn_mcp_probe.json"
        with open(out, "w", encoding="utf-8") as fh:
            fh.write(text)
        print(f"\nWrote report to: {out}")
    except Exception as exc:  # noqa: BLE001
        print(f"(could not write report file: {exc})")


main()
