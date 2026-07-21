# Human UEFN Test Plan

Repo-level work is verified in CI-style suites (Python `unittest`, TypeScript `tsc`/Vitest).
Anything that touches a live UEFN editor **cannot** be verified without a human — those steps are
gated here. A tool is never promoted to `SUPPORTED` in
[`TOOL_COMPATIBILITY.md`](./TOOL_COMPATIBILITY.md) without evidence captured from one of these gates.

---

## HUMAN_UEFN_GATE_1 — Minimal listener smoke test (Phase 1)

**Goal:** confirm the authenticated listener runs inside UEFN, dispatches on the editor main
thread, and reports accurate health/capability info.

**You need:** a **disposable** UEFN test project (never a production island).

### Steps

1. Open the disposable UEFN test project.
2. **Project Settings → Python Editor Scripting** → enable (early preview). Restart if prompted.
3. **Tools → Execute Python Script…** → select `UEFN/execute_in_uefn.py`.
4. In the **Output Log**, confirm a line like:
   `[uefn-mcp] listener running on 127.0.0.1:8765 (token at …; arbitrary_python=False)`.
5. From a terminal on the same machine, run:
   ```bash
   python UEFN/tools/smoke_test.py
   ```
   Confirm it ends with `SMOKE TEST: PASS`.
6. (Evidence for the compatibility matrix) **Tools → Execute Python Script…** →
   `UEFN/tools/probe_capabilities.py`. It prints a JSON report and writes
   `uefn_mcp_probe.json` into the project's `Saved/Logs/`.

### What to send back

- The full Output Log lines from steps 4 and 6 (including the `probe_capabilities` JSON).
- The complete `smoke_test.py` terminal output (all three command responses).
- Confirm `get_capabilities` shows `main_thread_dispatch: true` and a real `python_version`
  (expected `3.11.x`) and `engine_version`.

### Pass criteria

- `ping`, `get_listener_status`, `get_capabilities` all return `success: true`.
- `runtime_probes.slate_post_tick` is `true` (main-thread pump registered).
- No editor crash; re-running `execute_in_uefn.py` cleanly replaces the instance.

> This gate is **not** considered passed without the actual Output Log + smoke-test evidence
> above. Do not mark Phase 1 UEFN-verified from repo tests alone.

---

## HUMAN_UEFN_GATE_2 — First actor/asset operations (Phase 3)

Defined now for reference; exercised after Phase 3 tools land. Will verify: actor enumeration,
selection, spawning an allowed actor, moving it, duplicating, saving the level, undo/removal,
asset query, viewport camera get/set, and reviewing the editor log for validation warnings — all
in a disposable project, cleaning up test content afterward.
