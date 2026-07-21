# Phase 0 Audit — UEFN Compatibility for BlueprintMCP (`ue5-mcp`)

Status: **Complete**
Branch: `feature/uefn-current-adapter`
Date: 2026-07-21
Scope: audit the existing UE5 MCP repository, the reference UEFN MCP implementation, and the
current official UEFN documentation, to plan a first-class UEFN backend that lives *alongside*
the existing Unreal Engine backend without breaking it.

> This document records findings only. Design decisions live in
> [`ARCHITECTURE_DECISION_001.md`](./ARCHITECTURE_DECISION_001.md). Per-tool status lives in
> [`TOOL_COMPATIBILITY.md`](./TOOL_COMPATIBILITY.md). Reuse/attribution lives in
> [`PORTING_LEDGER.md`](./PORTING_LEDGER.md).

---

## 1. Primary repository — `hoodtronik/ue5-mcp` (this repo)

A UE5 editor plugin ("BlueprintMCP") exposing MCP tools for inspecting/mutating Blueprints,
materials, animation, Niagara, UMG, levels, actors, PIE, and more. Two halves:

- **`Tools/`** — a TypeScript MCP server (the AI-facing half).
- **`Source/BlueprintMCP/`** — a compiled **C++ UE5 _editor_ plugin** (the engine-facing half).

`BlueprintMCP.uplugin` sits at the repo root; the repo is meant to be dropped into a host
project's `Plugins/` directory (there is **no `.uproject`** in this repo).

### 1.1 TypeScript MCP server (`Tools/`)

| Aspect | Finding |
|---|---|
| SDK | `@modelcontextprotocol/sdk ^1.12.1`, `McpServer` + `StdioServerTransport` (**stdio only**) |
| Module system | ESM (`"type": "module"`), Node16 resolution (imports use `.js` extensions) |
| Schemas | Zod (via the SDK's transitive dependency; not a direct dep) |
| Server bridge | `Tools/src/ue-bridge.ts` — talks to the C++ backend over **plain localhost HTTP** |
| Port | `UE_PORT` env, default **9847**; base URL hardcoded to `http://localhost:${UE_PORT}` |
| Auth | **None** — unauthenticated localhost HTTP |
| Timeouts | 5 min (`AbortSignal.timeout(300000)`) on `ueGet`/`uePost`; 2 s health; 3 s shutdown |
| Retries | None per-call; `waitForHealthy(180)` polls `/api/health` every 2 s at startup |
| Health contract | `/api/health` → `{ status, mode: "editor"\|"commandlet", blueprintCount, ... }` |
| Build | `npm run build` (`tsc` → `dist/index.js`); type-check `npx tsc --noEmit` |
| Tests | **Vitest**, integration (not mocked): boots a real headless commandlet on port **19847**, requires UE5 5.4+ installed |

**The backend seam is thin and already isolated.** Every one of the ~196 tools uses exactly
three functions from `ue-bridge.ts`: `ensureUE()`, `ueGet(endpoint, params)`,
`uePost(endpoint, body)`. Tools are 100% agnostic to *how* the backend runs. This is the single
most important architectural fact for the UEFN work: **a new backend only has to reimplement
those three functions plus health** — no tool file must change to add a backend.

Non-portable pieces inside `ue-bridge.ts` that assume the C++ backend:
`findUProject`, `readEngineVersion`, `findEditorCmd`, `spawnAndWait` (spawns
`UnrealEditor-Cmd.exe -run=BlueprintMCP`), `ensureModulesFile`. These are the commandlet
lifecycle and must be isolated behind the abstraction, not shared with UEFN.

### 1.2 Tool surface

**196 MCP tools + 2 resources**, registered per-file (each `Tools/src/tools/*.ts` exports a
`registerXxxTools(server)` called from `index.ts`). Full per-tool inventory and UEFN status is in
[`TOOL_COMPATIBILITY.md`](./TOOL_COMPATIBILITY.md). Category headline:

- Blueprint graph/variables/params/graphs/interfaces/dispatchers/components: ~35 tools — **editor-only C++, not portable**.
- Material read/mutation (25), Animation BP (13), Niagara (20), UMG widgets (7): **editor-only C++, not portable**.
- Level / actors / actor-query / actor-state / selection / camera / sublevels (~30): **candidate UEFN parity** (spawn/transform/query/tags/selection/viewport).
- Assets read/mutation via reflection: **candidate UEFN parity with validation limits**.
- PIE lifecycle/runtime (7), view-mode (5), cvars (3), output-log (2), screenshot (2), content-browser (2), editor-utils (4), undo-redo (4): **mixed — investigate individually**.
- `run_python` (1): arbitrary UE editor Python — **development-only escape hatch in UEFN, off by default**.

### 1.3 C++ plugin (`Source/BlueprintMCP/`) — confirmed non-portable to UEFN

- `BlueprintMCP.uplugin`: single **Editor-type** module, `CanContainContent: false`.
- `Build.cs` depends on editor-only C++ modules: `UnrealEd, BlueprintGraph, KismetCompiler,
  MaterialEditor, UMGEditor, AnimGraph, AssetTools, PythonScriptPlugin`, plus UE's native
  `HTTPServer, Sockets, Networking`.
- `FBlueprintMCPServer` (plain C++ class) binds `/api/*` routes on UE's `FHttpServerModule` at
  port 9847. **Main-thread model:** HTTP worker thread enqueues `FPendingRequest` into a
  `TQueue`; `ProcessOneRequest()` runs once per tick on the game thread, dispatches via a
  `TMap<FString,FRequestHandler>`, and wraps mutation endpoints in
  `GEditor->BeginTransaction/EndTransaction` (66-entry `MutationEndpoints` set).
- Two hosts share one server: `UBlueprintMCPEditorSubsystem` (`FTickableEditorObject`, editor
  mode) and `UBlueprintMCPCommandlet` (headless loop, commandlet mode).
- **Auth: none** at the C++ layer either. `run_python`/`exec_command` = unauthenticated local RCE.

**Conclusion:** the entire `Source/` tree requires UnrealBuildTool and editor-only modules that
**UEFN forbids** (no custom C++ plugins, no `UnrealEd`, no native HTTP server, no
`IPythonScriptPlugin`). None of it can be reused in UEFN. The UEFN backend must reimplement the
relevant slice of the `/api/*` contract behind a completely different mechanism (Python listener).

### 1.4 `.gitignore`

Ignores `Binaries/`, `Intermediate/`, `Tools/node_modules/`, `Tools/dist/`, OS/IDE files.
**No token/secret/state entries** — because the current system has no secrets (unauthenticated).
The UEFN backend introduces a local auth token and listener state; Phase 0 adds ignore entries
for those (see §4).

---

## 2. Reference implementation — `KirChuvakov/uefn-mcp-server`

MIT, © 2025 KirChuvakov, single-author 5-commit snapshot (all 2026-03-20), **no vendored
third-party code** — clean to adapt under MIT with notice retention. 13 files, ~4,576 lines.
Full reuse/attribution detail: [`PORTING_LEDGER.md`](./PORTING_LEDGER.md).

Two-process design mirrors ours: an external `mcp_server.py` (FastMCP, stdio) talks over
localhost HTTP to an in-editor `uefn_listener.py` running inside UEFN's embedded Python.

**The load-bearing, reusable pattern** (validated by a working project running *inside UEFN*):

- A daemon-thread `http.server.HTTPServer` bound to `127.0.0.1` (default port **8765**, scans
  8765–8770) enqueues `(req_id, command, params)` onto a `queue.Queue`, then polls a
  `_responses` dict until a deadline (30 s).
- The queue is **drained on the editor main thread** by a callback registered with
  **`unreal.register_slate_post_tick_callback(_tick_handler)`** — this empirically works in UEFN.
- Requests correlate by `req_id`; responses are `{"success": bool, "result"|"error": ...}`.
- A `_serialize` helper converts `unreal.Vector/Rotator/Transform/LinearColor/AssetData/Object`
  and containers to JSON, `hasattr`-guarded for engine-version drift.
- Handlers register via an `@_register` decorator into a `_HANDLERS` dict; `_dispatch` runs them.

**What must NOT be copied as-is** (re-implement instead):

- **No authentication of any kind** on the HTTP endpoint (localhost-only is the sole control).
- **`execute_python` = unconditional `exec()` RCE** with real builtins.
- **No request-size limit** (`rfile.read(int(Content-Length))` trusts the header — memory DoS).
- **`init_unreal.py` is broken** (references non-existent attributes; import side-effect starts
  the listener then raises).
- **tkinter status window** is fragile (a second `tk.Tk()` crashes the editor).
- Fragile stale-response GC via `key.split("_")[2]`; O(n) actor lookup duplicated across handlers.

The reference's **22/28/29 tool count** discrepancy (README vs. `@mcp.tool` vs. handlers) is noted;
it exposes actors, assets, levels, viewport, project, and python/system commands — a small,
sane first-wave surface that maps closely onto our Phase 1–3 targets.

---

## 3. Current official UEFN facts (July 2026)

Sourced from `dev.epicgames.com` (Epic official). Confidence flagged per item.

| # | Finding | Confidence |
|---|---|---|
| 1 | **Fortnite Ecosystem 41.20**, released **2026-07-16**, is the current public release. | Verified |
| 2 | UEFN has **no published per-release UE-version mapping** (Fortnite-specific fork). UE5.8 is the last major UE5; **UE6 (2027) merges UE5+UEFN**. → **version-gate on the Ecosystem number, never a hardcoded UE version.** | Verified |
| 3 | Embedded **Python 3.11** (standard Python Editor Script Plugin; exact patch to be probed at runtime). | Verified (patch inferred) |
| 4 | **Python Editor Scripting in UEFN is an early preview** (introduced 40.00), enabled in **Project Settings → Python Editor Scripting**. Run via Output Log (Python mode), **Tools → Execute Python Script**, and possibly `init_unreal.py` autostart (confirm in UEFN). | Verified (run mechanics inferred) |
| 5 | **Epic explicitly endorses "an MCP integration with an Agentic AI"** as a supported Python-tools use case. | Verified |
| 6 | **The API surface is _not_ trimmed** — "the Python implementation in UEFN doesn't have any restrictions." Enforcement happens at **validation**, not at the Python layer. | Verified |
| 7 | Documented "don'ts" that fail validation / risk crashes: modifying **non-allow-listed properties**; placing content **not visible in your Content Browser / asset pickers**; modifying content **outside your project**. | Verified |
| 8 | **Validation & Fix-Up Tool** gates session-launch and publish (memory limits, allowed types/assets, texture size, per-platform property validity, reference validation). **No documented Python entry point**; `unreal.EditorValidatorSubsystem` *may* be reachable — unverified. | Verified (Python API unverified) |
| 9 | **`register_slate_post_tick_callback` / `unregister_slate_post_tick_callback`** are standard UE Python APIs; presence in UEFN not separately documented but **empirically used by the reference project inside UEFN**. Still gate behind a runtime `hasattr` probe. | Verified in UE; empirically works in UEFN |
| 10 | Named subsystems (`EditorActorSubsystem`, `EditorAssetLibrary`, `LevelEditorSubsystem`/`EditorLevelLibrary`, `AssetRegistry`, viewport camera) are **not enumerated by UEFN docs** — expected but must be **probed at runtime**. | Inferred — probe required |
| 11 | **Verse** compiles **editor-only** (Verse → Build Verse Code, Ctrl+Shift+B; Push Changes for live). **No CLI compiler.** `.verse` files live in the project (folder = module). → a "compile Verse" tool is **MANUAL_GATE_REQUIRED**; safe read/search/atomic-write is feasible. | Verified |
| 12 | **Scene Graph** = Verse-native ECS (Beta). **No documented Python access** → treat as Verse/editor-only pending probing. | Verified (no Python path documented) |
| 13 | **"Lore"** is the new product name for **Unreal Revision Control (URC)**. | Verified |

**Overarching implication:** a Python call succeeding in UEFN does **not** prove the result will
validate or publish. Capability statuses and the mutation policy (Phase 4) must treat validation
as a first-class, separate stage — never inferred from "the Python call returned success."

---

## 4. `.gitignore` additions made in Phase 0

To keep the new local auth token and listener state out of source control:

```
# UEFN listener local state (never commit)
UEFN/**/.mcp_token
UEFN/**/*.local.token
UEFN/**/listener_state.json
**/uefn_mcp_token*
*.uefn.local.json
```

Tokens are generated at runtime into a user-local path (not the repo) by default; these entries
are a belt-and-suspenders guard.

---

## 5. Phase gate

Audit + architecture decision + tool matrix + porting ledger exist and are committed together as
the Phase 0 deliverable. Feature porting does **not** begin until these are in place (satisfied).
Phase 1 (minimal authenticated listener + repo-level tests) follows.
