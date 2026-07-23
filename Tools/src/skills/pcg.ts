import type { Skill } from "./types.js";

export const pcgSkill: Skill = {
  name: "pcg",
  description: "Spatial reasoning workflow for building PCG (procedural) graphs",
  content: `# PCG Skill

Instructions for working with PCG graphs in Unreal Engine:
- Always call list_pcg_nodes before adding any node — never guess node type names
- Build graphs incrementally: create graph → add nodes one at a time → connect → execute
- Spatial operations need a target actor with a PCGComponent — check it exists first with list_actors
- After connecting nodes, always call execute_pcg_graph to see the result
- When building environments: start with a surface/volume sampler, then add mesh spawners
- PCG graphs are assets — save with save_all after any structural change
`,
};
