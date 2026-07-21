# ADR 001 — UEFN backend architecture

- Status: **Accepted** (Phase 0)
- Date: 2026-07-21
- Branch: `feature/uefn-current-adapter`
- Supersedes: none
- Related: [`PHASE_0_AUDIT.md`](./PHASE_0_AUDIT.md), [`TOOL_COMPATIBILITY.md`](./TOOL_COMPATIBILITY.md), [`PORTING_LEDGER.md`](./PORTING_LEDGER.md)

## Context

The existing project ("BlueprintMCP") is a UE5 MCP server: a TypeScript MCP layer (`Tools/`)
talking over unauthenticated localhost HTTP (port 9847) to a compiled **C++ editor plugin**
(`Source/BlueprintMCP/`). We must add a first-class **UEFN** (Unreal Editor for Fortnite) backend
that works alongside the UE5 backend without breaking it, targeting the current rolling UEFN
release (Fortnite Ecosystem 41.20 at time of writing) and future releases.

UEFN is not "UE with a version bump." It forbids custom C++ editor plugins, exposes a
Fortnite-curated environment, gates everything through a Validation & Fix-Up Tool, uses Verse and
a Beta Scene Graph, and (per Epic docs) explicitly endorses "an MCP integration with an Agentic
AI" through its early-preview Python Editor Scripting.

## Decision

Add a **second editor backend** behind a thin TypeScript backend abstraction. The UEFN backend is
an **authenticated, loopback-only Python listener running inside UEFN's embedded Python**, driven
by the *existing* TypeScript MCP server. Concretely:

1. **Preserve the TypeScript MCP layer.** Keep `index.ts`, all `tools/*.ts`, Zod schemas,
   response formatting, and resources.
2. **Introduce an `EditorBackend` interface** (`ensure()/health()/capabilities()/get()/post()`).
   Wrap the current `ue-bridge.ts` behaviour as a `ue5-backend`; add a `uefn-backend`.
3. **Keep the UE5 HTTP + commandlet path exactly as-is**, isolated inside `ue5-backend`.
4. **Add a UEFN Python listener** (`UEFN/listener/`) that reuses the reference project's proven
   main-thread command-queue + `register_slate_post_tick_callback` pump + serializer + handler
   registry — **but re-implements security** (auth token, size/timeouts, no default RCE).
5. **Runtime capability detection.** Tools are gated by an actual capability manifest; unsupported
   tools fail with a structured `CAPABILITY_UNAVAILABLE` error rather than silently substituting.
6. **Backend selection** via `MCP_BACKEND=auto|ue5|uefn`, defaulting to unambiguous auto-detect.

## Rationale, point by point (as required by the master prompt)

### Why the existing C++ plugin cannot be the UEFN bridge
`Source/BlueprintMCP/` is a compiled **Editor-type** C++ module depending on `UnrealEd`,
`BlueprintGraph`, `KismetCompiler`, `MaterialEditor`, `UMGEditor`, `AnimGraph`,
`PythonScriptPlugin`, and UE's native `HTTPServer`/`Sockets`/`Networking`. It must be built with
UnrealBuildTool. **UEFN forbids arbitrary custom C++ plugins** and does not expose these
editor-only modules or a native HTTP server to creators. Therefore the entire C++ backend — and
the `UnrealEditor-Cmd.exe -run=BlueprintMCP` commandlet-spawn path in `ue-bridge.ts` — is
categorically non-portable. The UEFN bridge must use a **supported UEFN mechanism**: Python Editor
Scripting.

### Why the TypeScript MCP layer should be preserved (not re-implemented)
The tool layer is already backend-agnostic: all ~196 tools call exactly three functions
(`ensureUE`, `ueGet`, `uePost`). Re-implementing a second MCP protocol server would duplicate 196
tool schemas, the resource layer, and response formatting for zero benefit and guaranteed drift.
Introducing an `EditorBackend` seam behind those three functions is a **small, low-risk refactor**
that unlocks both backends. This satisfies the prompt's "do not introduce a second independent MCP
protocol implementation unless a written ADR shows it is necessary" — it is **not** necessary.

### Why the selected Python-listener architecture is safe
- **Loopback only** (`127.0.0.1`), never `0.0.0.0`.
- **Shared auth token** required on every request; token generated at runtime into a user-local
  path outside source control; requests without a valid token are rejected before dispatch.
- **Request-size limit** and **per-command timeouts**; unknown commands rejected; no shell
  execution; no arbitrary filesystem access from the generic command surface.
- **Arbitrary Python is OFF by default** and, when enabled, double-gated (token + explicit flag).
  We treat editor Python as *not* a security boundary and document it as supervised-local-dev only.
- The listener runs the HTTP server on a **daemon thread** but performs **all `unreal.*` work on
  the editor main thread** via a thread-safe queue drained by the slate-post-tick callback — this
  is required for editor-API correctness and matches how the reference project already runs inside
  UEFN.

### What is reused from the reference repository
From `KirChuvakov/uefn-mcp-server` (MIT, © 2025 KirChuvakov): the **main-thread command-queue
pattern**, the **`register_slate_post_tick_callback` drain loop**, the **`unreal.*` serializer**,
and the **`@register`/dispatch handler registry** — adapted, not vendored wholesale, with the MIT
notice retained. **Not reused:** its (absent) security model, `execute_python` RCE, broken
`init_unreal.py`, tkinter status window, and fragile stale-GC. Full ledger:
[`PORTING_LEDGER.md`](./PORTING_LEDGER.md).

### How main-thread dispatch works
```
HTTP daemon thread                    Editor main thread (slate post-tick)
------------------                    ------------------------------------
recv request                          register_slate_post_tick_callback(tick)
auth + size check                     tick(delta):
enqueue (req_id, cmd, params)  ---->    drain N commands/queue
poll _responses[req_id] until           run unreal.* handler
  deadline (timeout)           <----     _responses[req_id] = result
send JSON response                      GC stale responses by stored timestamp
```
The callback presence is probed at startup with `hasattr(unreal,
'register_slate_post_tick_callback')`; if absent the listener refuses to start editor-dispatch and
reports a structured error (rather than doing `unreal.*` work off-thread).

### How backend selection works
- `MCP_BACKEND=ue5` → UE5 backend only (current behaviour, unchanged).
- `MCP_BACKEND=uefn` → UEFN backend only; connect to the configured local listener; **never** spawn
  the UE5 commandlet or look for `UnrealEditor-Cmd.exe`.
- `MCP_BACKEND=auto` (default) → probe for a running UEFN listener, then a running UE5 server,
  then environment/project markers. Select only when identification is **unambiguous**; if both are
  active, return an explicit error asking the user to set `MCP_BACKEND`. Never launch UE5 for a
  UEFN project; never launch UEFN headlessly (no officially supported workflow).

### How unsupported tools are represented
A capability registry maps each tool → a status from `TOOL_COMPATIBILITY.md`. When a tool is
invoked against a backend that lacks the capability, the backend returns:
```json
{ "success": false, "error_code": "CAPABILITY_UNAVAILABLE", "tool": "...", "backend": "uefn",
  "reason": "...", "alternatives": ["Implement in Verse", "Use a supported device", "Do it manually in-editor"] }
```
Never a silent substitution of unrelated behaviour.

### Security implications
The UEFN backend **raises** the security baseline relative to the UE5 backend: it adds auth and
size/timeout limits the UE5 side never had. The UE5 backend's unauthenticated-localhost posture is
left unchanged (out of scope; its threat model is a compiled plugin in a trusted local editor). New
token/state files are git-ignored. Arbitrary Python defaults off.

## Alternatives considered

1. **Port the C++ plugin to UEFN.** Rejected — UEFN forbids custom C++ plugins; impossible.
2. **A second, independent MCP server for UEFN** (e.g. adopt the reference's `mcp_server.py`).
   Rejected — duplicates the entire tool/protocol layer, guarantees drift, and abandons 196
   existing schemas. The backend-abstraction reuse is strictly better.
3. **UI automation (screen/input scripting) for UEFN.** Rejected — the master prompt forbids UI
   automation where a supported Python/filesystem API exists; brittle and unsafe.
4. **Headless UEFN commandlet** mirroring the UE5 path. Rejected — no officially supported
   headless UEFN launch workflow exists; the listener assumes the editor is already open.
5. **Vendor the reference listener wholesale.** Rejected — its security gaps (no auth, RCE, no size
   cap) and broken/ fragile pieces make wholesale vendoring irresponsible; we adapt the good parts.

## Consequences

- One MCP server, two backends; adding UEFN touches `ue-bridge.ts`/backend layer, not tools.
- A new `UEFN/listener/` Python tree (stdlib-only, no pip deps inside UEFN) plus `UEFN/tests/`.
- Real UEFN behaviour cannot be verified in CI — every UEFN-touching capability carries a
  `HUMAN_UEFN_GATE` before it can be marked `SUPPORTED`.
- The UE5 backend and its Vitest integration suite remain the regression anchor; they must stay
  green at every phase boundary (baseline verified green in Phase 0).
