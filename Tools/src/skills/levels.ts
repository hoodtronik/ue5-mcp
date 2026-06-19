import type { Skill } from "./types.js";

export const levelsSkill: Skill = {
  name: "levels",
  description: "Level/spatial placement workflow for actors and environments",
  content: `# Levels Skill

Instructions for working with levels and spatial placement in Unreal Engine:
- Always call get_level_info first to understand current scene state
- Use find_actors_in_radius to understand what's already in a space before adding to it
- Spawn actors with explicit transforms — never rely on default placement
- Use begin_transaction before spawning multiple actors so they can be undone together
- After placing actors, use get_actor_bounds to verify placement is correct
- For large placements, use PCG graphs instead of spawning individual actors
`,
};
