import type { Skill } from "./types.js";

// CLAUDE-NOTE: Added 2026-07-23 while wiring rebuild_groom_bindings (the 4th orphan route from
// that session's TS/C++ route-parity work). The rebuild_groom_bindings selector gotcha below was
// found by reading the C++ handler source and confirmed via curl against a live editor, not guessed.

export const groomSkill: Skill = {
  name: "groom",
  description: "Groom Binding asset workflow: retargeting hair to a new skeletal mesh, rebuilding stale bindings",
  content: `# Groom Skill

## Workflow: retarget a hair groom to a new character
1. list_groom_bindings(query=<substring>) to find the source binding asset.
2. duplicate_groom_binding(assetPath, newName) — copy it so the original is untouched.
3. set_groom_binding_target_mesh(assetPath=<the copy>, targetMeshPath=<new Skeletal Mesh>) — this
   only rewrites the TargetSkeletalMesh (and optionally SourceSkeletalMesh) reference; the response
   includes a 'note' field saying the geometry data is now stale.
4. rebuild_groom_bindings(assetPath=<the copy>) to actually regenerate the binding data for the new
   mesh. Skipping this step leaves a binding pointing at the right mesh but with geometry baked for
   the OLD one.

## Gotchas
- WARNING: rebuild_groom_bindings can't tell "an empty query result" apart from "you didn't specify
  a selector at all" — both hit the exact same error: "Specify 'assetPath', 'assetPaths' (array),
  or 'query' (substring)." If you pass query='SomeSubstring' and it matches zero bindings, you get
  that generic error instead of a clean "0 matched" result. Prefer assetPath/assetPaths when you
  already know the target path — those report per-path 'notFound' entries in the response instead
  of erroring outright, which is more informative when checking whether something exists.
- dryRun=true on rebuild_groom_bindings reports which bindings would be touched (respecting
  notFound/matched-count reporting) without building or saving anything — safe to use first when
  operating on a query/array selector you're not 100% sure of.
- There is no MCP tool (or scriptable Python path) to CREATE a new Groom Binding asset from
  nothing — it requires real Groom + Skeletal Mesh source data and goes through the editor's own
  interactive binding-creation flow. These tools only operate on bindings that already exist.
`,
};
