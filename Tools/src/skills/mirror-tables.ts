import type { Skill } from "./types.js";

// CLAUDE-NOTE: Added 2026-07-23 while writing tests for list/set/remove_mirror_table_rows (3 of
// the 4 orphan routes found by that session's TS/C++ route-parity work). The "can't be created
// headlessly" finding below isn't a guess — it's the exact reason those tests only cover the
// endpoint contract and not a real row edit: creating a fixture table failed twice against a live
// editor with the specific Python errors quoted here.

export const mirrorTablesSkill: Skill = {
  name: "mirror-tables",
  description: "Mirror Data Table (bone/curve/notify name-swap) row editing — and why you can't create one headlessly",
  content: `# Mirror Data Table Skill

A Mirror Data Table (UMirrorDataTable) maps a bone/curve/notify name to its mirrored counterpart,
used to mirror animations left-to-right at runtime. list_mirror_table_rows / set_mirror_table_rows /
remove_mirror_table_rows edit rows on an EXISTING table — there is no create_mirror_data_table tool.

## WARNING: you cannot create a new Mirror Data Table via Python or any MCP tool
This was tested directly, not assumed:
- unreal.MirrorDataTableFactory.Struct and .Skeleton are UPROPERTY(protected) in C++ — Python's
  set_editor_property refuses them: "Property 'Struct' for attribute 'struct' on
  'MirrorDataTableFactory' is protected and cannot be set."
- Even bypassing the factory, UDataTable::RowStruct (the base class field every DataTable needs)
  is UPROPERTY(VisibleAnywhere) — visible but not editable. set_editor_property refuses it too:
  "Property 'RowStruct' ... is read-only and cannot be set."
- The factory's ConfigureProperties() only ever gets its Struct/Skeleton values from an interactive
  modal window (skeleton picker + struct picker) triggered by the Content Browser's "New Asset"
  flow. There is no scripted equivalent.
- Practical consequence: a Mirror Data Table must be created once, by a human, in the editor UI
  (right-click in Content Browser -> Animation -> Mirror Data Table, pick a Skeleton). After that,
  every row edit (add/update/remove) is fully scriptable via the three MCP tools below — no editor
  interaction needed for those.

## Row editing (works headlessly once the table exists)
- list_mirror_table_rows(table) — table accepts a bare asset name or a full /Game/... path; returns
  rowName/name/mirroredName/entryType per row plus the table's mirrorAxis.
- set_mirror_table_rows(table, rows=[{name, mirroredName, entryType?}]) — passing a 'name' that
  already exists on the table UPDATES that row in place rather than duplicating it (added vs
  updated counts in the response reflect this). entryType defaults to 'Bone' if omitted; valid
  values are Bone / AnimationNotify / Curve / SyncMarker / Custom.
- A CENTERLINE entry (a bone with no left/right counterpart, e.g. a pelvis or spine root that
  should map to itself) is declared by passing the SAME value for 'name' and 'mirroredName'.
- remove_mirror_table_rows(table, rowNames=[...]) — removes by row name; names not present are
  silently skipped (removed count reflects only what actually existed).
`,
};
