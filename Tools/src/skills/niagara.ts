import type { Skill } from "./types.js";

// CLAUDE-NOTE: Added 2026-07-23. Every gotcha below was reproduced live against a UE 5.6 editor
// while filling this skill-pack gap (create_niagara_emitter -> add_niagara_module -> set_module_input
// -> attach to a system), not guessed from the tool schemas alone.

export const niagaraSkill: Skill = {
  name: "niagara",
  description: "Niagara system/emitter authoring workflow via MCP tools — module library noise, path guessing traps, input-name traps",
  content: `# Niagara Skill

## Basics
- create_niagara_emitter defaults to a FULLY SEEDED working emitter (EmitterState, SpawnRate,
  sprite size/lifetime/color, a Sprite renderer — it already emits on its own). Pass bare=true for
  a truly empty emitter when building the stack up yourself with add_niagara_module.
- Workflow: create_niagara_emitter (bare=true) -> add_niagara_module per stage -> set_module_input
  per input -> add_niagara_renderer -> create_niagara_system -> add_emitter_to_system.
- After add_emitter_to_system, keep editing the emitter via its STANDALONE asset name/path
  (e.g. 'NE_MyEmitter'), never the system-qualified sub-object path shown by
  get_niagara_system_summary ('NS_Foo.NS_Foo:HandleName') — that string is not a valid 'emitter'
  argument to add_niagara_module / set_module_input / list_emitter_modules and returns a flat
  "not found" error. Edits to the standalone asset are what the system picks up.

## Gotchas
- WARNING: list_module_library is NOT curated — a stage-only query can return 300+ results,
  including irrelevant third-party plugin modules mixed in with the real ones (seen: NiagaraFluids
  Grid2D/Grid3D fluid-sim modules, ChaosNiagara, HairStrands groom-physics modules, WaterAdvanced,
  and asset-marketplace plugins like UltraDynamicSky's weather particles). Narrow with the 'path'
  filter to a known-good root ('/Niagara/Modules/...') once you know roughly where a module lives,
  or you'll be reading past irrelevant hits.
- WARNING: some core module names exist at MULTIPLE paths (V1/V2/V3 variants), e.g.
  'SphereLocation' at both /Niagara/Modules/Spawn/Location/SphereLocation.SphereLocation and
  .../Location/V2/SphereLocation.SphereLocation — same for OrientMeshToVector, MeshRotationForce,
  InitialMeshOrientation, PointAttractionForce, CurlNoiseForce. Prefer the highest version-numbered
  path when more than one match turns up; the plain (unversioned) path is often the legacy one.
- WARNING: do not guess a module's category folder from its name. A plausible-looking path like
  '/Niagara/Modules/Spawn/Rate/SpawnRate.SpawnRate' can fail to load even though the module is
  real — the actual path is '/Niagara/Modules/Emitter/SpawnRate.SpawnRate' (category 'Emitter',
  not 'Spawn/Rate'). Always call list_module_library with a stage filter FIRST and copy the exact
  path from the result; add_niagara_module fails with "Could not load module script" on a wrong guess.
- WARNING: module input names are not always compact camelCase. SpawnRate's third input is
  literally 'Spawn Probability' (WITH a space), not 'SpawnProbability'. Always call
  list_module_inputs on the module's node GUID before set_module_input — the error on a wrong
  name ("Input 'X' not found on module 'Y'") is at least clear and tells you to do this, so
  recovery is cheap, but don't burn a call guessing first.
- Verified working as documented (no surprises found): set_module_input's constant and curve
  value modes both apply cleanly on the first try, including multi-key curves with the default
  cubic interpolation.

## Recipe: build a simple emitter from scratch
1. create_niagara_emitter(name, bare=true)
2. list_module_library(stage='EmitterUpdate') -> find 'SpawnRate' at /Niagara/Modules/Emitter/SpawnRate.SpawnRate
3. add_niagara_module(emitter, stage='EmitterUpdate', moduleScript=<that path>) -> note the returned nodeGuid
4. list_module_inputs(emitter, moduleNodeGuid) -> confirm exact input names ('SpawnRate', 'SpawnGroup', 'Spawn Probability')
5. set_module_input(emitter, stage='EmitterUpdate', moduleNodeGuid, input='SpawnRate', type='float', value=50)
6. Repeat add_niagara_module for a ParticleSpawn init module (e.g. InitializeParticle) and a renderer
   via add_niagara_renderer.
7. create_niagara_system -> add_emitter_to_system(system, emitter) — continue editing the STANDALONE
   emitter asset for any further changes.
`,
};
