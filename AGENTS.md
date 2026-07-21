# AGENTS.md — cross-agent conventions for `ue5-mcp` / BlueprintMCP

This file is the cross-tool standard read by Cursor, Codex, Gemini CLI, and most other coding
agents. It complements (does not replace) `CLAUDE.md`.

## CLAUDE-NOTE convention — source of truth

Changes authored by Claude Code are marked inline with `CLAUDE-NOTE:` (or the language's comment
equivalent, e.g. `# CLAUDE-NOTE:`, `// CLAUDE-NOTE:`, `<!-- CLAUDE-NOTE: -->`). These notes record
*why* non-obvious code is the way it is and are the **source of truth**.

**Non-Claude-Code agents must not modify, remove, or alter any code marked with a `CLAUDE-NOTE:`
without first alerting the user and receiving explicit permission.** If you (a non-Claude-Code
agent) encounter a `CLAUDE-NOTE:` on code you want to change, stop and ask the user first.

## Project shape

- `Tools/` — TypeScript MCP server (stdio). Build: `cd Tools && npm run build`. Type-check:
  `npx tsc --noEmit`. Tests: `npm test` (Vitest; requires UE5 installed for the UE5 backend).
- `Source/BlueprintMCP/` — compiled C++ UE5 **editor** plugin (the UE5 backend). Built with
  UnrealBuildTool.
- `UEFN/` — Python listener + tests for the **UEFN backend** (added on
  `feature/uefn-current-adapter`). Stdlib-only inside UEFN; repo-level tests run without UEFN via
  test doubles. See `docs/uefn/`.

## UEFN backend rules (important)

- **Never** try to compile or install the C++ `BlueprintMCP.uplugin` inside UEFN — UEFN forbids
  custom C++ plugins. The UEFN backend uses UEFN Python Editor Scripting only.
- **Never** hardcode a UEFN/UE engine version. Version-gate on the Fortnite Ecosystem release
  number and probe embedded Python/engine versions at runtime.
- All `unreal.*` work runs on the editor **main thread** via the command queue; the HTTP listener
  thread only enqueues.
- Security defaults stay on: loopback-only bind, auth token required, arbitrary Python **off**.
- A tool is only `SUPPORTED` in UEFN with live editor evidence — never because the UE5 equivalent
  exists or a Python call returned success. See `docs/uefn/TOOL_COMPATIBILITY.md`.
- Do not commit auth tokens or listener state (see `.gitignore`).
