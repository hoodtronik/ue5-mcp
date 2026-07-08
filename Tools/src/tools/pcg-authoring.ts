import type { McpServer } from "@modelcontextprotocol/sdk/server/mcp.js";
import { z } from "zod";
import { ensureUE, uePost } from "../ue-bridge.js";

// CLAUDE-NOTE: PCG graph-authoring tools. Pair with the C++ endpoints in BlueprintMCPHandlers_PCG.cpp.
// These do what the reflected Python API can't: add/list/remove graph user-parameters and bind a node's
// property override pin to a parameter (via a UserParameterGet node). Enables native parameter-driven PCG
// graphs that a Blueprint can drive at runtime (e.g. per-mesh Count/Size on a scatter graph).

const vec3 = z.object({ x: z.number(), y: z.number(), z: z.number() });
const scalarOrVec = z.union([z.number(), z.boolean(), z.string(), vec3]);

export function registerPcgAuthoringTools(server: McpServer): void {
  server.tool(
    "pcg_add_user_param",
    "Add a user-parameter to a PCG graph (the graph's Parameters panel). Types: bool, int, float, double, name, string, vector. Optional 'default'. These params can be bound to node properties (pcg_bind_override) and set at runtime from a Blueprint.",
    {
      graph: z.string().describe("PCG graph asset path, e.g. /Game/PCG/PCG_KekosBeachScatter"),
      name: z.string().describe("Parameter name (e.g. 'CellSize_Shell')"),
      type: z.enum(["bool", "int", "int32", "float", "double", "name", "string", "vector"]),
      default: scalarOrVec.optional().describe("Optional default value (vector as {x,y,z})"),
    },
    async ({ graph, name, type, default: def }) => {
      const err = await ensureUE();
      if (err) return { content: [{ type: "text" as const, text: err }] };
      const body: Record<string, unknown> = { graph, name, type };
      if (def !== undefined) body.default = def;
      const data = await uePost("/api/pcg-add-user-param", body);
      if (data.error) return { content: [{ type: "text" as const, text: `Error: ${data.error}` }] };
      return { content: [{ type: "text" as const, text: `Added user-parameter '${data.parameter}' (${data.type}) to ${graph}.` }] };
    }
  );

  server.tool(
    "pcg_list_user_params",
    "List the user-parameters defined on a PCG graph (name + type).",
    { graph: z.string().describe("PCG graph asset path") },
    async ({ graph }) => {
      const err = await ensureUE();
      if (err) return { content: [{ type: "text" as const, text: err }] };
      const data = await uePost("/api/pcg-list-user-params", { graph });
      if (data.error) return { content: [{ type: "text" as const, text: `Error: ${data.error}` }] };
      const params = (data.parameters ?? []) as Array<{ name: string; type: string }>;
      const lines = [`User-parameters on ${graph} (${params.length}):`, ...params.map((p) => `  ${p.name}  [${p.type}]`)];
      return { content: [{ type: "text" as const, text: lines.join("\n") }] };
    }
  );

  server.tool(
    "pcg_remove_user_param",
    "Remove a user-parameter from a PCG graph by name.",
    { graph: z.string(), name: z.string() },
    async ({ graph, name }) => {
      const err = await ensureUE();
      if (err) return { content: [{ type: "text" as const, text: err }] };
      const data = await uePost("/api/pcg-remove-user-param", { graph, name });
      if (data.error) return { content: [{ type: "text" as const, text: `Error: ${data.error}` }] };
      return { content: [{ type: "text" as const, text: `Removed user-parameter '${data.removed}' from ${graph}.` }] };
    }
  );

  server.tool(
    "pcg_set_user_param",
    "Set the (default) value of an existing PCG graph user-parameter. Value type must match the parameter (vector as {x,y,z}). Note: a Blueprint can override the value per-PCGComponent at runtime via PCGGraphParametersHelpers.",
    { graph: z.string(), name: z.string(), value: scalarOrVec.describe("Value (vector as {x,y,z})") },
    async ({ graph, name, value }) => {
      const err = await ensureUE();
      if (err) return { content: [{ type: "text" as const, text: err }] };
      const data = await uePost("/api/pcg-set-user-param", { graph, name, value });
      if (data.error) return { content: [{ type: "text" as const, text: `Error: ${data.error}` }] };
      return { content: [{ type: "text" as const, text: `Set '${data.parameter}' on ${graph}.` }] };
    }
  );

  server.tool(
    "pcg_bind_override",
    "Bind a PCG node's property override pin to a graph user-parameter. Creates a UserParameterGet node for 'parameter' and connects it to the target node's '<property>' override pin (label = property name, e.g. 'CellSize', 'ScaleMin'). 'node' is the UPCGNode object name (from list_pcg_nodes / Python node.get_name()).",
    {
      graph: z.string(),
      node: z.string().describe("Target UPCGNode object name"),
      property: z.string().describe("Override pin label = property name, e.g. 'CellSize', 'ScaleMin', 'ScaleMax'"),
      parameter: z.string().describe("Graph user-parameter name to read from"),
    },
    async ({ graph, node, property, parameter }) => {
      const err = await ensureUE();
      if (err) return { content: [{ type: "text" as const, text: err }] };
      const data = await uePost("/api/pcg-bind-override", { graph, node, property, parameter });
      if (data.error) return { content: [{ type: "text" as const, text: `Error: ${data.error}` }] };
      return { content: [{ type: "text" as const, text: `Bound ${data.boundTo} <- parameter '${parameter}' (getter ${data.getterNode}, out pin '${data.outputPin}').` }] };
    }
  );
}
