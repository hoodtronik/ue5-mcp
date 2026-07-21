# UEFN Tool Compatibility Matrix

Companion to [`PHASE_0_AUDIT.md`](./PHASE_0_AUDIT.md). Tracks, for every tool in the existing UE5
MCP surface, its **UEFN status**. This is a living document — statuses move as tools are
implemented and probed in a live UEFN editor.

## Status legend

| Status | Meaning |
|---|---|
| `SUPPORTED` | Implemented + works in UEFN, verified with editor evidence. |
| `SUPPORTED_WITH_LIMITATIONS` | Works but constrained (validation allow-list, subset of params, UEFN semantics differ). |
| `EXPERIMENTAL` | Implemented, plausibly works, not yet confirmed with editor evidence. |
| `FILESYSTEM_ONLY` | Achievable only via project-local file I/O, not a live editor API. |
| `MANUAL_GATE_REQUIRED` | Requires a human editor action (e.g. Verse compile, publish, validation UI). |
| `UNSUPPORTED` | UEFN does not expose the required API; will fail with a structured `CAPABILITY_UNAVAILABLE` error. |
| `NOT_YET_INVESTIGATED` | Default. Not yet probed against UEFN. |

> **Rule:** a tool is never marked `SUPPORTED` on the basis that the equivalent UE5 tool exists, or
> that a Python call returned success. `SUPPORTED` requires live UEFN editor evidence, and for
> mutations, evidence that the result does not break validation. Until then, tools stay
> `NOT_YET_INVESTIGATED` / `EXPERIMENTAL`.

## Risk columns

- **Validation risk** — likelihood the operation produces content that fails the UEFN Validation &
  Fix-Up Tool (non-allow-listed property, non-browsable asset, out-of-project edit).
- **Destructive risk** — deletes/replaces/bulk-mutates; must be gated behind explicit permission
  flags (`allow_destructive`, `allow_bulk_mutation`, `confirm_asset_paths`).

---

## Part A — Category-level status (all 196 tools)

Full per-file counts. `R` read-only, `M` mutation, `D` destructive (subset of M).

| Source file | Count | Category | UEFN status (initial) | Rationale |
|---|---|---|---|---|
| `read.ts` | 12 | Blueprint/skeleton inspection | **UNSUPPORTED** (BP graph) / probe skeleton | BP graph reads need editor-only `BlueprintGraph`; not in UEFN. |
| `mutation.ts` | 15 | BP node/pin/asset editing | **UNSUPPORTED** | Blueprint graph mutation API absent in UEFN. |
| `variables.ts` | 4 | BP variables | **UNSUPPORTED** | Same. |
| `params.ts` | 3 | BP function params | **UNSUPPORTED** | Same. |
| `graphs.ts` | 5 | BP create/graph mgmt | **UNSUPPORTED** | Same. |
| `interfaces.ts` | 3 | BP interfaces | **UNSUPPORTED** | Same. |
| `dispatchers.ts` | 2 | BP event dispatchers | **UNSUPPORTED** | Same. |
| `components.ts` | 3 | BP components | **UNSUPPORTED** | Same. |
| `snapshot.ts` | 5 | BP graph snapshot/diff | **UNSUPPORTED** | Operates on BP graphs. |
| `validation.ts` | 2 | BP compile-validate | **UNSUPPORTED** (as BP validation) | UEFN validation ≠ BP compile; see `uefn_run_validation` (separate, `NOT_YET_INVESTIGATED`). |
| `user-types.ts` | 4 | Struct/enum authoring | **UNSUPPORTED** | Editor-only asset factories. |
| `material-read.ts` | 8 | Material graph inspection | **UNSUPPORTED** (graph) / **EXPERIMENTAL** (list/exist) | Material graph needs `MaterialEditor`; asset listing may work. |
| `material-mutation.ts` | 17 | Material graph editing | **UNSUPPORTED** / instance params `NOT_YET_INVESTIGATED` | Graph editing absent; material-instance params *may* be reflectable — Phase 5 probe. |
| `animation-mutation.ts` | 13 | Anim BP / blend space | **UNSUPPORTED** | Editor-only `AnimGraph`. |
| `niagara.ts` | 20 | Niagara authoring | **UNSUPPORTED** | Editor-only Niagara editor modules. |
| `widgets.ts` | 7 | UMG authoring | **UNSUPPORTED** (authoring) / inspection `NOT_YET_INVESTIGATED` | UMG editor absent; runtime widget inspection is a Phase 5 probe. |
| `level.ts` | 8 | Level actors (spawn/query/transform/props) | **First-wave — see Part B** | Core UEFN parity target. |
| `level-actors.ts` | 4 | Attach/detach/duplicate/rename actor | **First-wave — see Part B** | Core UEFN parity target. |
| `actor-query.ts` | 5 | Find by tag/class/radius, bounds, set tags | **First-wave — see Part B** | Core UEFN parity target. |
| `actor-state.ts` | 3 | Mobility/visibility/physics | **SUPPORTED_WITH_LIMITATIONS** (probe) | Property-set on actors; allow-list dependent. |
| `spatial.ts` | 1 | Raycast | **NOT_YET_INVESTIGATED** | Line-trace API availability unknown in UEFN. |
| `selection.ts` | 3 | Editor selection get/set/clear | **First-wave — see Part B** | Core UEFN parity target. |
| `camera.ts` | 2 | Viewport camera get/set | **First-wave — see Part B** | Core UEFN parity target. |
| `view-mode.ts` | 5 | View mode / show flags / realtime | **NOT_YET_INVESTIGATED** | Viewport-only; API presence unknown. |
| `pie-lifecycle.ts` | 4 | PIE start/stop/pause | **NOT_YET_INVESTIGATED** | UEFN uses "Launch Session", not PIE — likely `MANUAL_GATE_REQUIRED`. |
| `pie-runtime.ts` | 3 | PIE player/actor runtime | **NOT_YET_INVESTIGATED** | Same as above. |
| `sublevels.ts` | 4 | Level info / sublevel load | **NOT_YET_INVESTIGATED** | Level info likely OK; sublevel streaming UEFN-specific. |
| `cvars.ts` | 3 | Console variables | **UNSUPPORTED** (likely) | CVar access typically restricted for creators. |
| `output-log.ts` | 2 | Output log read/clear | **EXPERIMENTAL** | Log-dir read works in reference; `clear` unlikely. |
| `screenshot.ts` | 2 | Viewport screenshots | **NOT_YET_INVESTIGATED** | API presence unknown in UEFN. |
| `content-browser.ts` | 2 | Navigate CB / open asset editor | **NOT_YET_INVESTIGATED** | Editor-UI automation; Phase 5 probe. |
| `editor-utils.ts` | 4 | Focus/notify/save-all/dirty-packages | **First-wave (save/dirty) — see Part B** | Save + dirty-package reporting are core. |
| `undo-redo.ts` | 4 | Undo/redo/transactions | **NOT_YET_INVESTIGATED** (Phase 4) | Transaction API availability drives mutation safety. |
| `groom.ts` | 3 | Groom bindings | **UNSUPPORTED** | Editor-only `HairStrandsCore`. |
| `run-python.ts` | 1 | Arbitrary editor Python | **SUPPORTED_WITH_LIMITATIONS — dev only, OFF by default** | Escape hatch; see §Arbitrary Python policy. |
| Resources | 2 | blueprint-list, workflow-recipes | **UNSUPPORTED** (BP list) / port recipes | Recipes resource can be UEFN-specific. |

**Net:** of 196 tools, ~130 are BP/material/anim/Niagara/UMG/groom graph tooling that is
**UNSUPPORTED** in UEFN (editor-only C++). The realistic UEFN surface is the **~40 tools** across
level/actor/asset/selection/viewport/system, plus **new UEFN-native tools** (Verse FS, capability
manifest, version info) that have no UE5 equivalent.

---

## Part B — First-wave detailed matrix (Phase 1–3 targets)

These are the tools targeted for the first usable milestone. Endpoints reuse the existing
`/api/*` contract where semantics are genuinely equivalent; UEFN-unique tools get a `uefn_` prefix.

| Tool | UE5 endpoint | UEFN status | UEFN API candidate | Val. risk | Destr. risk | Phase |
|---|---|---|---|---|---|---|
| **System** |
| `ping` | (new) | EXPERIMENTAL | listener-internal | none | none | 1 |
| `get_listener_status` | (new) | EXPERIMENTAL | listener-internal | none | none | 1 |
| `get_project_info` | `/api/… ` (n/a) | EXPERIMENTAL | `unreal.Paths`, world path | none | none | 1 |
| `get_capabilities` | (new) | EXPERIMENTAL | capability registry + runtime probe | none | none | 1 |
| `get_listener_log` | (new) | EXPERIMENTAL | ring buffer | none | none | 1 |
| `shutdown_listener` | (new) | EXPERIMENTAL | listener lifecycle | none | none | 1 |
| `server_status` | `/api/health` | EXPERIMENTAL | health endpoint | none | none | 3 |
| `uefn_get_version_info` | (new) | EXPERIMENTAL | `sys.version`, Ecosystem #, engine ver probe | none | none | 3 |
| `get_editor_log` | `/api/get-output-log` | EXPERIMENTAL | `Paths.project_log_dir()` newest `.log` | none | none | 3 |
| **Actor inspection** |
| `list_actors` | `/api/list-actors` | NOT_YET_INVESTIGATED | `EditorActorSubsystem.get_all_level_actors` | none | none | 3 |
| `get_selected_actors` | `/api/selected-actors` | NOT_YET_INVESTIGATED | `EditorActorSubsystem.get_selected_level_actors` | none | none | 3 |
| `get_actor_properties` | `/api/actor-properties` | NOT_YET_INVESTIGATED | `get_editor_property` (allow-listed) | none | none | 3 |
| `find_actors_by_class` | `/api/find-actors-by-class` | NOT_YET_INVESTIGATED | filter over get_all_level_actors | none | none | 3 |
| `find_actors_by_tag` | `/api/find-actors-by-tag` | NOT_YET_INVESTIGATED | tag filter | none | none | 3 |
| `get_actor_bounds` | `/api/get-actor-bounds` | NOT_YET_INVESTIGATED | actor bounds API | none | none | 3 |
| **Actor mutation** |
| `spawn_actor` | `/api/spawn-actor` | NOT_YET_INVESTIGATED | `spawn_actor_from_class/object` (allow-listed class only) | **high** | low | 3 |
| `duplicate_actor` | `/api/duplicate-actor` | NOT_YET_INVESTIGATED | duplicate API | med | low | 3 |
| `rename_actor` | `/api/rename-actor` | NOT_YET_INVESTIGATED | `set_actor_label` | low | low | 3 |
| `set_actor_transform` | `/api/set-actor-transform` | NOT_YET_INVESTIGATED | `set_actor_location/rotation/scale3d` | low | low | 3 |
| `set_actor_tags` | `/api/set-actor-tags` | NOT_YET_INVESTIGATED | tags property | low | low | 3 |
| `set_actor_visibility` | `/api/set-actor-visibility` | NOT_YET_INVESTIGATED | property-set | med | low | 3 |
| `delete_actor` | `/api/delete-actor` | NOT_YET_INVESTIGATED | `destroy_actor` | med | **high** (`allow_destructive`) | 3 |
| **Asset inspection** |
| `list_assets` | `/api/list` (variant) | NOT_YET_INVESTIGATED | `EditorAssetLibrary.list_assets` | none | none | 3 |
| `search_assets` | (new) | NOT_YET_INVESTIGATED | list + filter (ARFilter set-prop forbidden per ref) | none | none | 3 |
| `get_asset_info` | (new) | NOT_YET_INVESTIGATED | `find_asset_data` | none | none | 3 |
| `get_selected_assets` | (new) | NOT_YET_INVESTIGATED | `EditorUtilityLibrary.get_selected_assets` | none | none | 3 |
| `does_asset_exist` | (new) | NOT_YET_INVESTIGATED | `does_asset_exist` | none | none | 3 |
| **Asset mutation** |
| `duplicate_asset` | (new) | NOT_YET_INVESTIGATED | `duplicate_asset` (project paths only) | med | low | 3 |
| `rename_asset` | `/api/rename-asset` | NOT_YET_INVESTIGATED | `rename_asset` | med | low | 3 |
| `save_asset` | (new) | NOT_YET_INVESTIGATED | `save_asset` | low | low | 3 |
| `delete_asset` | `/api/delete-asset` | NOT_YET_INVESTIGATED | `delete_asset` | med | **high** (`allow_destructive`) | 3 |
| **Level / selection / viewport** |
| `get_current_level` | `/api/current-level` | NOT_YET_INVESTIGATED | `get_editor_world` | none | none | 3 |
| `get_level_info` | `/api/get-level-info` | NOT_YET_INVESTIGATED | world/level API | none | none | 3 |
| `save_current_level` | (new) | NOT_YET_INVESTIGATED | `save_current_level`/`LevelEditorSubsystem` | low | low | 3 |
| `get_dirty_packages` | `/api/get-dirty-packages` | NOT_YET_INVESTIGATED | dirty-package query | none | none | 3 |
| `get_editor_selection` | `/api/get-editor-selection` | NOT_YET_INVESTIGATED | selection API | none | none | 3 |
| `set_editor_selection` | `/api/set-editor-selection` | NOT_YET_INVESTIGATED | `set_selected_level_actors` | none | none | 3 |
| `clear_selection` | `/api/clear-selection` | NOT_YET_INVESTIGATED | clear selection | none | none | 3 |
| `focus_actor` | `/api/focus-actor` | NOT_YET_INVESTIGATED | viewport camera to bounds | none | none | 3 |
| `get_viewport_camera` | `/api/get-viewport-camera` | NOT_YET_INVESTIGATED | `get_level_viewport_camera_info` | none | none | 3 |
| `set_viewport_camera` | `/api/set-viewport-camera` | NOT_YET_INVESTIGATED | `set_level_viewport_camera_info` | none | none | 3 |

---

## Part C — UEFN-native tools (no UE5 equivalent)

| Tool | Status | Notes | Phase |
|---|---|---|---|
| `uefn_get_capabilities` | EXPERIMENTAL | Runtime capability manifest. | 1/3 |
| `uefn_get_version_info` | EXPERIMENTAL | Ecosystem #, embedded Python, engine ver (all runtime-probed). | 3 |
| `uefn_list_verse_files` | FILESYSTEM_ONLY | Enumerate `.verse` in project (path-restricted). | 5 |
| `uefn_read_verse_file` | FILESYSTEM_ONLY | Read a project `.verse` file. | 5 |
| `uefn_search_verse` | FILESYSTEM_ONLY | Search Verse source. | 5 |
| `uefn_write_verse_file` | FILESYSTEM_ONLY | Atomic write, backup, never digest files. | 5 |
| `uefn_build_verse` | MANUAL_GATE_REQUIRED | No CLI compiler; editor-only Build/Push. | 5 |
| `uefn_run_validation` | NOT_YET_INVESTIGATED | Probe `EditorValidatorSubsystem`; else `MANUAL_GATE_REQUIRED`. | 4 |
| Scene Graph inspection | NOT_YET_INVESTIGATED | No documented Python path; probe. | 5 |

---

## Arbitrary Python policy (`run_python` → `uefn_run_python`)

- Default: **`UEFN_MCP_ALLOW_ARBITRARY_PYTHON=false`** → tool is not registered (or returns a
  structured security error).
- When enabled: requires auth token **and** a second explicit `allow_arbitrary_python` flag,
  max script length, per-command timeout, request hash logged (never the script body verbatim if
  it may contain secrets), and rejection of obvious process/socket/filesystem-escape patterns.
- Documented as a **supervised local-development escape hatch only** — Python inside the editor is
  not a reliable security boundary. Prefer narrow typed tools.
