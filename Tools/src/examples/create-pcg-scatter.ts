import type { Example } from "./types.js";

export const createPcgScatterExample: Example = {
  name: "create-pcg-scatter",
  description: "Create a PCG graph that scatters meshes across a surface",
  content: `# Example: Create a PCG graph that scatters meshes across a surface

Task: Create a PCG graph that scatters meshes across a surface

Steps:
1. create_pcg_graph at /Game/PCG/{name}
2. add_pcg_node type="PCGSurfaceSamplerSettings" — generates points on surface
3. add_pcg_node type="PCGStaticMeshSpawnerSettings" — spawns meshes at points
4. connect_pcg_nodes from SurfaceSampler output to MeshSpawner input
5. set_pcg_node_property on MeshSpawner to assign your mesh asset
6. execute_pcg_graph on a target actor
7. save_all
`,
};
