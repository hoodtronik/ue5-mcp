# UEFN backend examples

- `mcp.json` — **target** MCP client config that selects the UEFN backend. The
  `MCP_BACKEND=uefn` selector is introduced in Phase 2 (TypeScript backend refactor); until then
  the listener is exercised directly via `UEFN/tools/smoke_test.py`.
- `uefn-project-layout/` — where to place the listener inside a UEFN project.

The listener itself (`UEFN/listener/`) is usable now: start it inside UEFN with
`UEFN/execute_in_uefn.py` and verify with `python UEFN/tools/smoke_test.py`.
