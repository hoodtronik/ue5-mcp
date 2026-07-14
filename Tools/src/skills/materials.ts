import type { Skill } from "./types.js";

// CLAUDE-NOTE: Extended 2026-07-14 with run_python/MaterialEditingLibrary (MEL) gotchas learned while
// adding a UV-scroll "flow" feature to a lava decal master, converting a decal to a surface material, and
// repairing plugin-nulled function calls. These document where the MCP graph tools silently misbehave.

export const materialsSkill: Skill = {
  name: "materials",
  description: "Material graph editing workflow (MCP tools + run_python/MEL deep edits & gotchas)",
  content: `# Materials Skill

## Basics (MCP tools)
- Call get_material / get_material_graph before modifying — map the existing graph first.
- snapshot_material_graph before structural changes; validate_material after; save when clean.
- New expressions need explicit positions (space nodes 200-300 units apart).
- For simple color/value tweaks, prefer a material instance over editing the master.

## Deep edits via run_python + MaterialEditingLibrary (mel = unreal.MaterialEditingLibrary)
For anything structural, run_python + MEL is more reliable than the graph tools, which have real bugs:

- WARNING: connect_material_pins does NOT bind MaterialAttributes function-call inputs. The visual pin shows
  connected but FunctionInputs[].Input stays null -> compile error "(Function X) Missing function input 'Y'".
  VERIFY with mel.get_inputs_for_material_expression(mat, expr) (returns [None] if unbound). FIX:
  mel.connect_material_expressions(fromExpr, "", toExpr, "Attributes")  (output name usually ""; input name is
  the pin: "UVs"/"Attributes"/"True"/"False"). To a material output:
  mel.connect_material_property(fromExpr, "", unreal.MaterialProperty.MP_BASE_COLOR).
- WARNING: you cannot enumerate a material's expressions from Python (UE5.6). get_expressions / data_model /
  editor_only_data all fail. To get an expression OBJECT: read its internal expression.name from
  get_material_graph (e.g. MaterialExpressionStaticSwitchParameter_15), then unreal.find_object(material, name).
  Duplicated materials keep the same internal names.
- WARNING: enabling a plugin does NOT re-resolve already-loaded materials ("Unspecified Function" /
  "Missing Material Function" persist). FIX: unreal.EditorLoadingAndSavingUtils.reload_packages([mat.get_outermost()]).
  A node still null after reload while siblings re-link = baked-null on disk; rewire it with MEL.
- WARNING: validate_material can be stale. Run mel.recompile_material(mat) first. New assets need rescan_assets.
- Enums are prefixed: unreal.MaterialDomain.MD_SURFACE / MD_DEFERRED_DECAL; unreal.BlendMode.BLEND_OPAQUE.

## Recipes
- Add a gated feature to a SHARED master (non-destructive): wrap it in a StaticSwitchParameter with
  default_value=False so existing instances are unchanged; only the target instance flips it on. Build nodes with
  mel.create_material_expression(mat, cls, x, y) (returns the object), set parameter_name/default_value/group, wire
  with connect_material_expressions, then recompile_material.
- Decal -> Surface material: duplicate the decal master; set material_domain=MD_SURFACE + blend_mode=BLEND_OPAQUE;
  delete decal-only nodes (MaterialExpressionDecalColor_*) with mel.delete_material_expression (they error in
  surface domain); wire an albedo into Base Color.
- Match a material to how an INSTANCE looks: the instance overrides ARE the look. Read them with
  get_material_instance_{scalar,vector,texture,static_switch}_parameter_value and bake into the material's
  expression defaults. Trap: emission is often gated by a "Base Use Emission" switch the master defaults FALSE - a
  surface variant looks like plain rock until you flip that default true and copy Emission Strength/Color+textures.
- Set values: master defaults -> find_object(...).set_editor_property('default_value', v) (textures:
  set_editor_property('texture', asset)). Instances -> mel.set_material_instance_*_parameter_value(mi, name, v).
- Verify visually: spawn a mesh, set_material(0, mat), position the viewport camera, take_high_res_screenshot, read
  the PNG, delete the test actor. Time-based flow does not show in a still - confirm the look, ask about motion.
`,
};
