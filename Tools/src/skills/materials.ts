import type { Skill } from "./types.js";

export const materialsSkill: Skill = {
  name: "materials",
  description: "Material graph editing workflow",
  content: `# Materials Skill

Instructions for working with Materials in Unreal Engine:
- Always call get_material before modifying — map the existing node graph first
- Use snapshot_material_graph before any structural changes
- Material expressions need explicit position — space nodes 200-300 units apart
- Always connect to the correct input slot — use describe_material to see available inputs
- Validate with validate_material after changes before saving
- For simple color/value changes, prefer material instances over editing the master material
`,
};
