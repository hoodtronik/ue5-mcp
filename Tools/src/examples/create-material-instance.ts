import type { Example } from "./types.js";

export const createMaterialInstanceExample: Example = {
  name: "create-material-instance",
  description: "Create a material instance from a master material",
  content: `# Example: Create a material instance from a master material

Task: Create a material instance from a master material

Steps:
1. list_materials — find the master material asset path
2. create_material_instance at /Game/Materials/{name}, parent={master_material_path}
3. get_material_instance_parameters — see what parameters are exposed
4. set_material_instance_parameter for any values you want to override
5. save_all
`,
};
