import type { McpServer } from "@modelcontextprotocol/sdk/server/mcp.js";
import type { Example } from "./types.js";
import { spawnStaticMeshExample } from "./spawn-static-mesh.js";
import { createBlueprintComponentExample } from "./create-blueprint-component.js";
import { createPcgScatterExample } from "./create-pcg-scatter.js";
import { createMaterialInstanceExample } from "./create-material-instance.js";

// CLAUDE-NOTE: Static Examples — canonical workflow recipes exposed as MCP Resources at
// example://unreal/{name}. Same transport as Skills (server.resource auto-wires
// resources/list + resources/read), different URI prefix. UE 5.6 safe.

export const EXAMPLES: Example[] = [
  spawnStaticMeshExample,
  createBlueprintComponentExample,
  createPcgScatterExample,
  createMaterialInstanceExample,
];

const EXAMPLE_URI = (name: string) => `example://unreal/${name}`;

export function registerExamples(server: McpServer): void {
  for (const example of EXAMPLES) {
    server.resource(
      `example-${example.name}`,
      EXAMPLE_URI(example.name),
      { description: example.description, mimeType: "text/markdown" },
      async (uri) => ({
        contents: [{ uri: uri.href, text: example.content, mimeType: "text/markdown" }],
      })
    );
  }

  server.tool(
    "list_examples",
    "List canonical step-by-step examples — concrete recipes (WHAT to do) for common Unreal tasks like spawning a static mesh, adding a Blueprint component, scattering meshes with PCG, or making a material instance. Retrieve a recipe's full steps via resources/read on example://unreal/{name}.",
    {},
    async () => {
      const lines = ["Available examples (retrieve via resources/read on the URI):", ""];
      for (const e of EXAMPLES) {
        lines.push(`  ${e.name}  —  ${e.description}`);
        lines.push(`      ${EXAMPLE_URI(e.name)}`);
      }
      lines.push("", "Tip: examples are fixed templates; adapt the asset names/paths to your project.");
      return { content: [{ type: "text" as const, text: lines.join("\n") }] };
    }
  );
}
