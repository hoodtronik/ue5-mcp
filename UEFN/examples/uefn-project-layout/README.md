# Placing the listener in a UEFN project

The listener is pure-stdlib Python; it does not need to live inside the project, but the editor's
Python must be able to import the `listener` package and (optionally) auto-run `init_unreal.py`.

Two supported setups:

## A. Point "Execute Python Script" at the repo (simplest, manual start)

1. UEFN → **Project Settings → Python Editor Scripting** → enable (early preview).
2. UEFN → **Tools → Execute Python Script…** → select
   `.../ue5-mcp/UEFN/execute_in_uefn.py`.
3. The listener starts on `127.0.0.1:8765` (scans up to 8773 if busy) and writes/uses a token at
   `~/.uefn-mcp/token`.

## B. Autostart via `init_unreal.py` (opt-in)

1. Add the `.../ue5-mcp/UEFN/` folder to **Project Settings → Python → Additional Paths**.
2. Copy or symlink `UEFN/listener/init_unreal.py` into a directory Unreal scans at startup
   (e.g. the project's `Content/Python/`).
3. Set env `UEFN_MCP_AUTOSTART=1` before launching UEFN. Autostart is opt-in — the hook does
   nothing unless that variable is set.

Typical project tree (only the relevant bits):

```
MyIsland/
├── MyIsland.uefnproject
├── Content/
│   └── Python/
│       └── init_unreal.py        # (setup B) copy of UEFN/listener/init_unreal.py
└── Plugins/ or external checkout of ue5-mcp/UEFN/  # the listener package
```

Never commit `~/.uefn-mcp/token` or any listener state (already covered by `.gitignore`).
