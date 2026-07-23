import type { Example } from "./types.js";

export const spawnStaticMeshExample: Example = {
  name: "spawn-static-mesh",
  description: "Spawn a static mesh actor in the level",
  content: `# Example: Spawn a static mesh actor in the level

Task: Spawn a static mesh actor in the level

Steps:
1. get_level_info — understand the current scene
2. list_classes with query "StaticMeshActor" — confirm the class name
3. spawn_actor with class="StaticMeshActor", transform at desired location
4. set_actor_property to assign the StaticMesh asset
5. save_all
`,
};
