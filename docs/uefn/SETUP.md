# UEFN backend — setup (Phase 1 preview)

> ⚠️ **Successful Python execution does not guarantee that a UEFN project will validate or
> publish.** Use source control, test in a disposable project, modify only supported content, and
> validate frequently.

## Prerequisites

- UEFN installed and updatable (current: Fortnite Ecosystem 41.20; the listener version-gates on
  the Ecosystem number, not a UE version).
- **Python Editor Scripting** enabled: UEFN → *Project Settings → Python Editor Scripting* (early
  preview). Epic explicitly lists "an MCP integration with an Agentic AI" as a supported use case.
- Python 3 on the host machine for the external smoke-test client (the in-editor listener uses
  UEFN's embedded Python 3.11 and needs **no pip packages**).

## Start the listener (manual)

1. In UEFN: **Tools → Execute Python Script…** → select `UEFN/execute_in_uefn.py`.
2. The Output Log prints:
   `[uefn-mcp] listener running on 127.0.0.1:<port> (token at <path>; arbitrary_python=False)`.
3. The listener binds **loopback only** and requires an auth token (auto-generated at
   `~/.uefn-mcp/token`, git-ignored).

## Verify from a terminal

```bash
python UEFN/tools/smoke_test.py
```

Expected: `ping`, `get_listener_status`, and `get_capabilities` return `success: true`, ending
with `SMOKE TEST: PASS`.

## Security defaults (all on)

| Control | Default |
|---|---|
| Bind address | `127.0.0.1` only |
| Auth token | required on every command (`X-MCP-Token` header or `auth_token` field) |
| Request size cap | 1 MiB |
| Command timeout | 30 s |
| Arbitrary Python (`run_python`) | **disabled** (`UEFN_MCP_ALLOW_ARBITRARY_PYTHON=false`) |

## Environment variables

| Var | Default | Purpose |
|---|---|---|
| `UEFN_MCP_PORT` | `8765` | Desired listener port (scans +8 if busy). |
| `UEFN_MCP_TOKEN` / `UEFN_MCP_TOKEN_FILE` | `~/.uefn-mcp/token` | Shared secret / its file. |
| `UEFN_MCP_AUTOSTART` | unset | `1` to let `init_unreal.py` auto-start the listener. |
| `UEFN_MCP_ALLOW_ARBITRARY_PYTHON` | `false` | Supervised local-dev escape hatch only. |
| `UEFN_ECOSYSTEM_VERSION` | unset | Operator-supplied Ecosystem label for reporting (never fabricated). |

## Stop the listener

Call the `shutdown_listener` command, or close the editor. Re-running
`execute_in_uefn.py` cleanly replaces any prior instance.
