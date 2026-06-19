import type { Skill } from "./types.js";

export const blueprintsSkill: Skill = {
  name: "blueprints",
  description: "Safe, incremental Blueprint editing workflow",
  content: `# Blueprints Skill

Instructions for working with Blueprints in Unreal Engine:
- Always call get_blueprint before modifying — understand the existing structure first
- Use validate_blueprint after any structural change before compiling
- Use snapshot_graph before any complex mutation — enables restore_graph if something breaks
- Connect pins by exact name — use get_pin_info to verify pin names before connecting
- Compile after adding variables or functions, not after every individual node addition
- Use begin_transaction / end_transaction around multi-step mutations for undo safety
`,
};
