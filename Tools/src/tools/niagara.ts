import type { McpServer } from "@modelcontextprotocol/sdk/server/mcp.js";
import { z } from "zod";
import { ensureUE, uePost, ueGet } from "../ue-bridge.js";

// CLAUDE-NOTE: Niagara MCP tools (Tier 1: asset creation + introspection).
// Mirrors the structure of material-mutation.ts / material-read.ts. Tier 2
// (stack authoring) will land in a second pass; Tier 3 (custom node-graph)
// is deferred per the niagara_extension_plan.

export function registerNiagaraTools(server: McpServer): void {
  // ---------------------------------------------------------------------------
  // Asset creation
  // ---------------------------------------------------------------------------

  server.tool(
    "create_niagara_system",
    "Create an empty UNiagaraSystem asset. The system starts with no emitters — use add_emitter_to_system to attach one.",
    {
      name: z.string().describe("System asset name (convention: 'NS_MyEffect')"),
      packagePath: z.string().default("/Game/Niagara").describe("Package path (e.g. '/Game/Exhibit/Niagara')"),
    },
    async ({ name, packagePath }) => {
      const err = await ensureUE();
      if (err) return { content: [{ type: "text" as const, text: err }] };

      const data = await uePost("/api/create-niagara-system", { name, packagePath });
      if (data.error) return { content: [{ type: "text" as const, text: `Error: ${data.error}` }] };

      const lines: string[] = [];
      lines.push(`Created NiagaraSystem ${data.name || name} at ${data.assetPath || `${packagePath}/${name}`}`);
      if (data.saved !== undefined) lines.push(`Saved: ${data.saved}`);
      lines.push("");
      lines.push("Next steps:");
      lines.push("  1. create_niagara_emitter to make a standalone emitter asset");
      lines.push("  2. add_emitter_to_system to attach it to this system");
      return { content: [{ type: "text" as const, text: lines.join("\n") }] };
    }
  );

  server.tool(
    "create_niagara_emitter",
    "Create an empty UNiagaraEmitter asset. Standalone emitters can be attached to one or more systems via add_emitter_to_system.",
    {
      name: z.string().describe("Emitter asset name (convention: 'NE_MyEmitter')"),
      packagePath: z.string().default("/Game/Niagara").describe("Package path (e.g. '/Game/Exhibit/Niagara')"),
      simTarget: z.enum(["CPU", "GPU"]).optional().describe("Simulation target: CPU (default) or GPU compute"),
    },
    async ({ name, packagePath, simTarget }) => {
      const err = await ensureUE();
      if (err) return { content: [{ type: "text" as const, text: err }] };

      const body: Record<string, any> = { name, packagePath };
      if (simTarget) body.simTarget = simTarget;

      const data = await uePost("/api/create-niagara-emitter", body);
      if (data.error) return { content: [{ type: "text" as const, text: `Error: ${data.error}` }] };

      const lines: string[] = [];
      lines.push(`Created NiagaraEmitter ${data.name || name} at ${data.assetPath || `${packagePath}/${name}`}`);
      if (data.simTarget) lines.push(`Sim target: ${data.simTarget}`);
      if (data.saved !== undefined) lines.push(`Saved: ${data.saved}`);
      return { content: [{ type: "text" as const, text: lines.join("\n") }] };
    }
  );

  server.tool(
    "add_emitter_to_system",
    "Attach an existing UNiagaraEmitter asset to a UNiagaraSystem as a new emitter handle. Triggers a system recompile.",
    {
      system: z.string().describe("Target system name or /Game/... path (e.g. 'NS_MyEffect')"),
      emitter: z.string().describe("Source emitter name or /Game/... path (e.g. 'NE_MyEmitter')"),
      handleName: z.string().optional().describe("Optional display name for the handle inside the system. Defaults to the emitter asset name minus 'NE_' prefix."),
    },
    async ({ system, emitter, handleName }) => {
      const err = await ensureUE();
      if (err) return { content: [{ type: "text" as const, text: err }] };

      const body: Record<string, any> = { system, emitter };
      if (handleName) body.handleName = handleName;

      const data = await uePost("/api/add-emitter-to-system", body);
      if (data.error) return { content: [{ type: "text" as const, text: `Error: ${data.error}` }] };

      const lines: string[] = [];
      lines.push(`Attached emitter '${data.emitter || emitter}' to system '${data.system || system}'`);
      if (data.handleName) lines.push(`Handle name: ${data.handleName}`);
      if (data.handleId) lines.push(`Handle ID: ${data.handleId}`);
      if (data.emitterCount !== undefined) lines.push(`System now has ${data.emitterCount} emitter(s)`);
      if (data.saved !== undefined) lines.push(`Saved: ${data.saved}`);
      return { content: [{ type: "text" as const, text: lines.join("\n") }] };
    }
  );

  // ---------------------------------------------------------------------------
  // Introspection
  // ---------------------------------------------------------------------------

  server.tool(
    "list_niagara_systems",
    "List all UNiagaraSystem assets in the project. Optionally filter by package path prefix.",
    {
      path: z.string().optional().describe("Package-path prefix to filter by (e.g. '/Game/Exhibit')"),
    },
    async ({ path }) => {
      const err = await ensureUE();
      if (err) return { content: [{ type: "text" as const, text: err }] };

      const data = await ueGet("/api/list-niagara-systems", { path: path || "" });
      if (data.error) return { content: [{ type: "text" as const, text: `Error: ${data.error}` }] };

      const lines: string[] = [];
      lines.push(`Found ${data.count} NiagaraSystem(s)${data.pathFilter ? ` under '${data.pathFilter}'` : ""}`);
      for (const sys of data.systems || []) {
        lines.push(`  ${sys.name}  (${sys.path})`);
      }
      return { content: [{ type: "text" as const, text: lines.join("\n") }] };
    }
  );

  server.tool(
    "get_niagara_system_summary",
    "Return structured info about a UNiagaraSystem: emitter handles, user parameters (the uds_* RenderStream values), and fixed bounds.",
    {
      system: z.string().describe("System name or /Game/... path"),
    },
    async ({ system }) => {
      const err = await ensureUE();
      if (err) return { content: [{ type: "text" as const, text: err }] };

      const data = await uePost("/api/get-niagara-system-summary", { system });
      if (data.error) return { content: [{ type: "text" as const, text: `Error: ${data.error}` }] };

      const lines: string[] = [];
      lines.push(`System: ${data.name} (${data.assetPath})`);
      lines.push(`Emitters: ${data.emitterCount}`);
      for (const e of data.emitters || []) {
        const flag = e.enabled ? "" : " [disabled]";
        lines.push(`  - ${e.handleName}${flag}  (asset: ${e.emitterAsset || "<inline>"})`);
      }
      lines.push(`User parameters: ${data.userParameterCount}`);
      for (const p of data.userParameters || []) {
        lines.push(`  - ${p.name}  : ${p.type}`);
      }
      if (data.fixedBounds) {
        const b = data.fixedBounds;
        lines.push(`Fixed bounds: (${b.minX}, ${b.minY}, ${b.minZ}) to (${b.maxX}, ${b.maxY}, ${b.maxZ})`);
      }
      return { content: [{ type: "text" as const, text: lines.join("\n") }] };
    }
  );

  server.tool(
    "get_niagara_emitter_summary",
    "Return structured info about a UNiagaraEmitter: sim target, renderers, and which script stages are present.",
    {
      emitter: z.string().describe("Emitter name or /Game/... path"),
    },
    async ({ emitter }) => {
      const err = await ensureUE();
      if (err) return { content: [{ type: "text" as const, text: err }] };

      const data = await uePost("/api/get-niagara-emitter-summary", { emitter });
      if (data.error) return { content: [{ type: "text" as const, text: `Error: ${data.error}` }] };

      const lines: string[] = [];
      lines.push(`Emitter: ${data.name} (${data.assetPath})`);
      if (data.simTarget) lines.push(`Sim target: ${data.simTarget}`);
      if (data.exposedVersion) lines.push(`Exposed version: ${data.exposedVersion}`);
      if (data.warning) lines.push(`Warning: ${data.warning}`);
      if (data.stages) {
        lines.push("Stages:");
        lines.push(`  emitter spawn:  ${data.stages.emitterSpawn ? "yes" : "no"}`);
        lines.push(`  emitter update: ${data.stages.emitterUpdate ? "yes" : "no"}`);
        lines.push(`  particle spawn: ${data.stages.particleSpawn ? "yes" : "no"}`);
        lines.push(`  particle update: ${data.stages.particleUpdate ? "yes" : "no"}`);
      }
      lines.push(`Renderers: ${data.rendererCount || 0}`);
      for (const r of data.renderers || []) {
        const flag = r.enabled ? "" : " [disabled]";
        lines.push(`  - ${r.class}${flag}`);
      }
      return { content: [{ type: "text" as const, text: lines.join("\n") }] };
    }
  );
}
