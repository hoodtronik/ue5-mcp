# Porting Ledger — reused / adapted third-party code

Tracks every component imported or substantially adapted from an external project into the UEFN
backend, with its license and attribution obligation. Update this whenever code is ported.

## Source: `KirChuvakov/uefn-mcp-server`

- Repo: https://github.com/KirChuvakov/uefn-mcp-server
- License: **MIT**
- Copyright: **© 2025 KirChuvakov**
- Provenance: single-author snapshot (5 commits, 2026-03-20); **no third-party code vendored** in
  it (pure Python stdlib + `unreal`), so the only obligation is retaining the MIT copyright +
  permission notice in copies or substantial portions.

### Attribution mechanism

- A `UEFN/THIRD_PARTY_NOTICES.md` (added when the first code is ported in Phase 1) reproduces the
  full MIT license text with the above copyright line.
- Each source file that adapts reference code carries a header note:
  ```
  # Portions adapted from KirChuvakov/uefn-mcp-server (MIT, © 2025 KirChuvakov).
  # See UEFN/THIRD_PARTY_NOTICES.md. Security model, auth, and lifecycle re-implemented.
  ```
- `CLAUDE-NOTE:` comments mark the adapted regions inline.

### What is adapted (patterns/approach, re-implemented cleanly — not copied verbatim)

| Component | Reference location | Our target | Adaptation |
|---|---|---|---|
| Main-thread command queue | `uefn_listener.py` `_init_shared_state`, `_run_on_main_thread`, `do_POST` enqueue/poll | `UEFN/listener/command_queue.py` | Add auth-checked enqueue; store explicit timestamp (drop fragile `key.split("_")[2]` GC). |
| Slate post-tick drain loop | `uefn_listener.py` `_tick_handler` + `register_slate_post_tick_callback` | `UEFN/listener/uefn_listener.py` | Probe `hasattr` before registering; structured error if absent; bounded batch per tick. |
| `unreal.*` serializer | `uefn_listener.py` `_serialize` / `_serialize_actor` | `UEFN/listener/serialization.py` | Add recursion/collection limits, cycle detection, structured object refs, deterministic fields. |
| Handler registry + dispatch | `uefn_listener.py` `@_register` / `_HANDLERS` / `_dispatch` | `UEFN/listener/command_registry.py` | Keep pattern; add capability status + param validation per handler. |
| Client port discovery/retry | `mcp_server.py` `_discover_port` / `_send_command` | `Tools/src/backend/uefn-backend.ts` | Reimplemented in TS with auth header + our protocol. |

### What is explicitly NOT reused (re-implemented from scratch)

| Rejected piece | Reason |
|---|---|
| Security model | Reference has **no auth**; we require a shared token, size limit, timeouts. |
| `execute_python` (`exec()` RCE) | Unconditional RCE; ours is OFF by default and double-gated. |
| `init_unreal.py` | Broken (references non-existent attributes; import side-effect starts server). Rewritten. |
| tkinter status window | Fragile (second `tk.Tk()` crashes editor); replaced with log/Slate-based status. |
| Single-threaded `HTTPServer` blocking design | Kept simple but revisited for timeout semantics. |
| O(n) duplicated actor lookup | Factored into one helper. |

## Source: `KirChuvakov/uefn-mcp-server` protocol

We do **not** preserve the reference's weak protocol decisions purely for compatibility. Our
protocol is versioned (`protocol_version: 1`), carries `request_id`, `auth_token`, and structured
`error{code,message,details}` + `verification{}` blocks. Compatibility with the reference listener
is not a goal.

## Internal reuse (not third-party): existing UE5 backend

The `EditorBackend` refactor wraps existing `ue-bridge.ts` logic; that code is first-party
(this repo's own, MIT per repo `LICENSE`) and is moved/wrapped, not attributed here.
