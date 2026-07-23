import type { Example } from "./types.js";

export const createBlueprintComponentExample: Example = {
  name: "create-blueprint-component",
  description: "Add a new component to an existing Blueprint",
  content: `# Example: Add a new component to an existing Blueprint

Task: Add a new component to an existing Blueprint

Steps:
1. get_blueprint {name} — read current component list
2. snapshot_graph — safety backup
3. begin_transaction
4. add_component {blueprint, component_class, component_name}
5. validate_blueprint
6. end_transaction
7. save_all
`,
};
