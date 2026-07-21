# UEFN autostart hook.
#
# Unreal auto-executes any `init_unreal.py` found on its Python path at editor
# startup. Autostart is OPT-IN: this hook only starts the listener when the
# environment variable UEFN_MCP_AUTOSTART is truthy, so merely having the file on the
# path does not force a server to run.
#
# Setup: add this file's directory's PARENT (the `UEFN/` folder) to the UEFN Python
# path (Project Settings > Python > Additional Paths), or copy the `listener/` package
# into your project's Content/Python folder. Then set UEFN_MCP_AUTOSTART=1.
#
# NOTE: re-implemented from scratch — the reference project's init_unreal.py was broken.

import os
import sys


def _bootstrap_and_start():
    if os.environ.get("UEFN_MCP_AUTOSTART", "").lower() not in ("1", "true", "yes"):
        return
    here = os.path.dirname(os.path.abspath(__file__))          # .../UEFN/listener
    uefn_root = os.path.dirname(here)                          # .../UEFN
    if uefn_root not in sys.path:
        sys.path.insert(0, uefn_root)
    try:
        from listener.uefn_listener import main
        main()
    except Exception as exc:  # noqa: BLE001 - never break editor startup
        print(f"[uefn-mcp] autostart failed: {exc!r}")


_bootstrap_and_start()
