# UE5 MCP — Give AI agents full access to your UE5 assets

Vibe code your Blueprints, materials, and Anim Blueprints. This plugin lets Claude Code (or any MCP client) read, modify, and create Unreal Engine 5 Blueprints — just describe what you want in plain English.

> "Add a health component to my player character" · "Find everywhere I use GetActorLocation and replace it" · "What does my damage system do?"

https://github.com/user-attachments/assets/11b86d62-982b-42b3-bddb-aeeddc3e675c

## Getting Started

Tell Claude Code:

```
Set up https://github.com/mirno-ehf/ue5-mcp in my project
```

## Prebuilt binaries (no C++ toolchain needed)

Using a **Blueprint-only** project, or don't want to compile? A precompiled, drop-in
build is available here:

**➡️ [hoodtronik/BlueprintMCP-prebuilt](https://github.com/hoodtronik/BlueprintMCP-prebuilt)** — UE 5.6, Win64

Copy that plugin into your project's `Plugins/` folder and the editor loads it directly,
no build step required. (Prebuilt binaries are engine-version-specific — for other engine
versions, build from source in this repo.)

## How It Works

A UE5 editor plugin exposes your project's Blueprints over a local HTTP server. An [MCP](https://modelcontextprotocol.io) wrapper connects that to AI tools like Claude Code. When the editor is open, it runs inside the editor process with zero overhead. When the editor is closed, it can spawn a headless process instead.

## Editor console commands

Driven via the `exec_command` tool. The Voxel Sandbox → StaticMesh baker adds:

- `Voxel.BakeChunks [all|single] [cellSize] [outFolderNameOrPath]` — during PIE, bakes runtime voxel
  chunks (ProceduralMeshComponents) into saved `UStaticMesh` assets (preserving vertex colors +
  per-section materials). Optional output folder lets variants coexist.
- `Voxel.AssembleLevel [/Game/Path/LevelName]` — after stopping PIE, assembles the baked meshes into a
  fresh level and saves the `.umap`.

## Tools

The MCP server exposes the following tools, grouped by area.

**Scripting / Python**
- `run_python` — execute Unreal Editor Python and return captured output. Full reflected editor API
  including the **PCG framework** (`unreal.PCGGraph`, `PCGComponent`, PCG node settings),
  `EditorAssetLibrary`, `EditorActorSubsystem`, etc. The escape hatch for anything without a dedicated tool.

**Blueprints — read**
- `list_blueprints` · `get_blueprint` · `get_blueprint_summary` · `get_blueprint_graph` · `describe_graph`
  · `search_blueprints` · `search_by_type` · `find_asset_references`

**Blueprints — mutate**
- `create_blueprint` · `reparent_blueprint` · `create_graph` · `delete_graph` · `rename_graph`
- `add_node` · `delete_node` · `move_node` · `duplicate_nodes` · `connect_pins` · `disconnect_pin`
  · `set_pin_default` · `refresh_all_nodes` · `replace_function_calls` · `change_struct_node_type`
  · `get_node_comment` · `set_node_comment`
- `add_variable` · `remove_variable` · `change_variable_type` · `set_variable_metadata` · `set_blueprint_default`
- `add_function_parameter` · `remove_function_parameter` · `change_function_parameter_type`
- `add_interface` · `remove_interface` · `list_interfaces`
- `add_event_dispatcher` · `list_event_dispatchers`
- `add_component` · `remove_component` · `list_components`
- `rename_asset` · `delete_asset`

**Discovery / reflection**
- `list_classes` · `list_functions` · `list_properties` · `get_pin_info` · `check_pin_compatibility`

**Structs / enums**
- `create_struct` · `add_struct_property` · `remove_struct_property` · `create_enum`

**Validation / graph snapshots**
- `validate_blueprint` · `validate_all_blueprints` · `diff_blueprints`
- `snapshot_graph` · `diff_graph` · `restore_graph` · `find_disconnected_pins` · `analyze_rebuild_impact`

**Materials**
- `list_materials` · `get_material` · `get_material_graph` · `describe_material` · `search_materials`
  · `find_material_references` · `list_material_functions` · `get_material_function`
- `create_material` · `set_material_property` · `add_material_expression` · `delete_material_expression`
  · `move_material_expression` · `connect_material_pins` · `disconnect_material_pin` · `set_expression_value`
  · `create_material_function` · `validate_material` · `snapshot_material_graph` · `diff_material_graph`
  · `restore_material_graph`
- `create_material_instance` · `set_material_instance_parameter` · `get_material_instance_parameters`
  · `reparent_material_instance`

**Animation Blueprints**
- `create_anim_blueprint` · `add_anim_state` · `remove_anim_state` · `add_anim_transition`
  · `set_transition_rule` · `add_anim_node` · `add_state_machine` · `set_state_animation`
  · `create_blend_space` · `set_blend_space_samples` · `set_state_blend_space` · `list_anim_slots`
  · `list_sync_groups`

**Niagara**
- `create_niagara_system` · `create_niagara_emitter` · `add_emitter_to_system` · `remove_emitter_from_system`
  · `list_niagara_systems` · `get_niagara_system_summary` · `get_niagara_emitter_summary`
  · `list_emitter_modules` · `list_module_inputs` · `list_module_library` · `set_emitter_sim_target`
  · `add_niagara_renderer` · `remove_niagara_renderer` · `set_renderer_property` · `add_niagara_module`
  · `set_module_input` · `set_system_module_input` · `add_user_parameter` · `remove_user_parameter`
  · `set_user_parameter_default`

**Widgets (UMG)**
- `create_widget_blueprint` · `list_widget_tree` · `get_widget_properties` · `add_widget` · `remove_widget`
  · `set_widget_property` · `move_widget`

**Groom** — `create_groom_binding` and related grooming tools.

**Levels / actors**
- `get_current_level` · `get_level_info` · `list_actors` · `get_selected_actors` · `get_actor_properties`
  · `spawn_actor` · `delete_actor` · `duplicate_actor` · `rename_actor` · `attach_actor` · `detach_actor`
  · `set_actor_transform` · `set_actor_property`
- `find_actors_by_class` · `find_actors_by_tag` · `find_actors_in_radius` · `get_actor_bounds` · `set_actor_tags`
- `set_actor_mobility` · `set_actor_visibility` · `set_actor_physics`
- `attach_actor` · `focus_actor` · `raycast`
- Sublevels: `list_sublevels` · `load_sublevel` · `unload_sublevel`
- Selection: `get_editor_selection` · `set_editor_selection` · `clear_selection`

**Editor / viewport**
- `server_status` · `rescan_assets` · `exec_command` · `shutdown_server` · `editor_notification`
- `save_all` · `get_dirty_packages` · `undo` · `redo` · `begin_transaction` · `end_transaction`
- `navigate_content_browser` · `open_asset_editor`
- Camera: `get_viewport_camera` · `set_viewport_camera`
- View: `set_view_mode` · `set_show_flags` · `set_viewport_type` · `set_realtime_rendering` · `set_game_view`
- Screenshots: `take_screenshot` · `take_high_res_screenshot`
- Output log: `get_output_log` · `clear_output_log`
- CVars: `get_cvar` · `set_cvar` · `list_cvars`

**Play In Editor (PIE)**
- `start_pie` · `stop_pie` · `pie_pause` · `is_pie_running`
- `pie_get_player_transform` · `pie_teleport_player` · `pie_query_actors`

## License

[MIT](LICENSE)
