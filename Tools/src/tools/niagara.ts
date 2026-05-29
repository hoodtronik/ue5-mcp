import type { McpServer } from "@modelcontextprotocol/sdk/server/mcp.js";
import { z } from "zod";
import { ensureUE, uePost, ueGet } from "../ue-bridge.js";

// CLAUDE-NOTE: Niagara MCP tools (Tier 1: asset creation + introspection,
// Tier 2: stack authoring). Mirrors the structure of material-mutation.ts /
// material-read.ts. Tier 3 (custom node-graph authoring) is deferred per the
// niagara_extension_plan.

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
    "Create a UNiagaraEmitter asset. By DEFAULT the factory seeds a full working emitter (EmitterState, SpawnRate, sprite size/lifetime, color + a Sprite renderer) — it already emits. Pass bare=true for a truly empty emitter to build up from scratch with the stack-authoring tools. Standalone emitters attach to systems via add_emitter_to_system.",
    {
      name: z.string().describe("Emitter asset name (convention: 'NE_MyEmitter')"),
      packagePath: z.string().default("/Game/Niagara").describe("Package path (e.g. '/Game/Exhibit/Niagara')"),
      simTarget: z.enum(["CPU", "GPU"]).optional().describe("Simulation target: CPU (default) or GPU compute"),
      bare: z.boolean().optional().describe("If true, create an EMPTY emitter (no default modules/renderers) to author entirely via add_niagara_module / add_niagara_renderer. Default false = full factory emitter that already emits."),
    },
    async ({ name, packagePath, simTarget, bare }) => {
      const err = await ensureUE();
      if (err) return { content: [{ type: "text" as const, text: err }] };

      const body: Record<string, any> = { name, packagePath };
      if (simTarget) body.simTarget = simTarget;
      if (bare !== undefined) body.bare = bare;

      const data = await uePost("/api/create-niagara-emitter", body);
      if (data.error) return { content: [{ type: "text" as const, text: `Error: ${data.error}` }] };

      const lines: string[] = [];
      lines.push(`Created NiagaraEmitter ${data.name || name} at ${data.assetPath || `${packagePath}/${name}`}`);
      if (data.bare !== undefined) lines.push(`Bare (empty): ${data.bare}`);
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

  server.tool(
    "list_emitter_modules",
    "List the stack modules on an emitter, grouped by stage and in execution order, with each module's node GUID. Use the returned GUID with set_module_input to edit an EXISTING module's inputs (not just ones you just added).",
    {
      emitter: z.string().describe("Emitter name or /Game/... path"),
      stage: z.enum(["EmitterSpawn", "EmitterUpdate", "ParticleSpawn", "ParticleUpdate"]).optional().describe("Only modules on this stage (default: all stages)"),
    },
    async ({ emitter, stage }) => {
      const err = await ensureUE();
      if (err) return { content: [{ type: "text" as const, text: err }] };

      const body: Record<string, any> = { emitter };
      if (stage) body.stage = stage;

      const data = await uePost("/api/list-emitter-modules", body);
      if (data.error) return { content: [{ type: "text" as const, text: `Error: ${data.error}` }] };

      const lines: string[] = [];
      lines.push(`${data.count} module(s) on ${data.emitter || emitter}${data.stageFilter ? ` (stage: ${data.stageFilter})` : ""}`);
      for (const m of data.modules || []) {
        const flag = m.enabled === false ? " [disabled]" : "";
        lines.push(`  [${m.stage} #${m.order}] ${m.name}${flag}  guid=${m.nodeGuid}`);
      }
      return { content: [{ type: "text" as const, text: lines.join("\n") }] };
    }
  );

  server.tool(
    "list_module_inputs",
    "List a module's top-level inputs (name + Niagara type) so you can call set_module_input safely. 'input' is the name to pass to set_module_input; 'type' is the matching enum value ('' / settable:false means set_module_input can't write that type yet). Get the module's nodeGuid from list_emitter_modules.",
    {
      emitter: z.string().describe("Emitter name or /Game/... path"),
      moduleNodeGuid: z.string().describe("Module node GUID from list_emitter_modules or add_niagara_module"),
    },
    async ({ emitter, moduleNodeGuid }) => {
      const err = await ensureUE();
      if (err) return { content: [{ type: "text" as const, text: err }] };

      const data = await uePost("/api/list-module-inputs", { emitter, moduleNodeGuid });
      if (data.error) return { content: [{ type: "text" as const, text: `Error: ${data.error}` }] };

      const lines: string[] = [];
      lines.push(`${data.count} input(s) on module '${data.moduleName}' (${data.emitter || emitter})`);
      for (const i of data.inputs || []) {
        const settable = i.settable ? i.type : `${i.niagaraType} [not settable via set_module_input]`;
        lines.push(`  ${i.input}  : ${settable}`);
      }
      return { content: [{ type: "text" as const, text: lines.join("\n") }] };
    }
  );

  // ---------------------------------------------------------------------------
  // Stack authoring (Tier 2)
  // ---------------------------------------------------------------------------

  const STAGES = ["EmitterSpawn", "EmitterUpdate", "ParticleSpawn", "ParticleUpdate"] as const;
  // CLAUDE-NOTE: value-bearing tools accept a scalar, bool, or numeric array; the C++
  // side maps it to the Niagara type (float/int/bool/vec2-4/color) from the 'type' field.
  const valueSchema = z.union([z.number(), z.boolean(), z.array(z.number())]);

  server.tool(
    "set_emitter_sim_target",
    "Set a UNiagaraEmitter's simulation target to CPU or GPU compute. Triggers an emitter recompile.",
    {
      emitter: z.string().describe("Emitter name or /Game/... path"),
      simTarget: z.enum(["CPU", "GPU"]).describe("CPU or GPU compute"),
    },
    async ({ emitter, simTarget }) => {
      const err = await ensureUE();
      if (err) return { content: [{ type: "text" as const, text: err }] };

      const data = await uePost("/api/set-emitter-sim-target", { emitter, simTarget });
      if (data.error) return { content: [{ type: "text" as const, text: `Error: ${data.error}` }] };

      return { content: [{ type: "text" as const, text: `Set ${data.emitter || emitter} sim target to ${data.simTarget || simTarget} (saved: ${data.saved})` }] };
    }
  );

  server.tool(
    "add_niagara_renderer",
    "Add a renderer (Sprite/Mesh/Ribbon/Light) to a UNiagaraEmitter. A Sprite renderer is the minimum needed to see particles.",
    {
      emitter: z.string().describe("Emitter name or /Game/... path"),
      rendererType: z.enum(["Sprite", "Mesh", "Ribbon", "Light"]).describe("Renderer class to add"),
    },
    async ({ emitter, rendererType }) => {
      const err = await ensureUE();
      if (err) return { content: [{ type: "text" as const, text: err }] };

      const data = await uePost("/api/add-niagara-renderer", { emitter, rendererType });
      if (data.error) return { content: [{ type: "text" as const, text: `Error: ${data.error}` }] };

      const lines: string[] = [];
      lines.push(`Added ${data.rendererClass || rendererType} to ${data.emitter || emitter}`);
      if (data.rendererCount !== undefined) lines.push(`Emitter now has ${data.rendererCount} renderer(s)`);
      if (data.saved !== undefined) lines.push(`Saved: ${data.saved}`);
      return { content: [{ type: "text" as const, text: lines.join("\n") }] };
    }
  );

  server.tool(
    "add_niagara_module",
    "Add a module script to a stack stage on an emitter (e.g. a Spawn Rate module on ParticleSpawn). Returns the module node GUID needed by set_module_input. Use list_module_library to find module script paths.",
    {
      emitter: z.string().describe("Emitter name or /Game/... path"),
      stage: z.enum(STAGES).describe("Stack stage to add the module to"),
      moduleScript: z.string().describe("Full /Game or /Niagara/... asset path of the module UNiagaraScript"),
      index: z.number().int().optional().describe("Insertion index within the stage (default: append)"),
    },
    async ({ emitter, stage, moduleScript, index }) => {
      const err = await ensureUE();
      if (err) return { content: [{ type: "text" as const, text: err }] };

      const body: Record<string, any> = { emitter, stage, moduleScript };
      if (index !== undefined) body.index = index;

      const data = await uePost("/api/add-niagara-module", body);
      if (data.error) return { content: [{ type: "text" as const, text: `Error: ${data.error}` }] };

      const lines: string[] = [];
      lines.push(`Added module '${data.moduleName}' to ${data.stage} on ${data.emitter || emitter}`);
      if (data.moduleNodeGuid) lines.push(`Module node GUID: ${data.moduleNodeGuid}  (pass to set_module_input)`);
      if (data.saved !== undefined) lines.push(`Saved: ${data.saved}`);
      return { content: [{ type: "text" as const, text: lines.join("\n") }] };
    }
  );

  // CLAUDE-NOTE: a single curve key. value is an array of components matching `type`:
  // float=[v], vec2=[x,y], vec3=[x,y,z], vec4=[x,y,z,w], color=[r,g,b,a]. `time` is the
  // curve key time (for over-life curves this is Normalized Age, 0..1).
  const curveKeySchema = z.object({
    time: z.number(),
    value: z.array(z.number()),
  });

  server.tool(
    "set_module_input",
    "Set a module input to either an inline CONSTANT (valueMode='constant', default) or a CURVE over life (valueMode='curve'). Target the module by its node GUID from add_niagara_module/list_emitter_modules. Constant supports float/int/bool/vec2/vec3/vec4/color; curve supports float/vec2/vec3/vec4/color (grafts a *FromCurve dynamic input — size/color/velocity over life). Re-calling with the same curve input updates its keys in place (idempotent for the iterate loop).",
    {
      emitter: z.string().describe("Emitter name or /Game/... path"),
      stage: z.enum(STAGES).describe("Stack stage the module lives on"),
      moduleNodeGuid: z.string().describe("Module node GUID returned by add_niagara_module"),
      input: z.string().describe("Module input name, e.g. 'SpawnRate'"),
      type: z.enum(["float", "int", "bool", "vec2", "vec3", "vec4", "color"]).describe("Niagara type of the input"),
      valueMode: z.enum(["constant", "curve"]).default("constant").describe("'constant' (inline literal) or 'curve' (value over life via a *FromCurve dynamic input)"),
      value: valueSchema.optional().describe("constant mode: number for scalars, boolean for bool, [x,y,...] array for vectors/color"),
      curveKeys: z.array(curveKeySchema).optional().describe("curve mode: keys [{time, value:[...]}] sorted by time. value length must match type (float=1, vec2=2, vec3=3, vec4=4, color=4 as r,g,b,a). For over-life curves, time is Normalized Age 0..1."),
      curveInterp: z.enum(["cubic", "linear", "constant"]).default("cubic").describe("curve mode: key interpolation. cubic = smooth auto-tangents (organic default)."),
    },
    async ({ emitter, stage, moduleNodeGuid, input, type, valueMode, value, curveKeys, curveInterp }) => {
      const err = await ensureUE();
      if (err) return { content: [{ type: "text" as const, text: err }] };

      if (valueMode === "curve" && (!curveKeys || curveKeys.length === 0)) {
        return { content: [{ type: "text" as const, text: "Error: valueMode='curve' requires a non-empty curveKeys array." }] };
      }
      if (valueMode === "constant" && value === undefined) {
        return { content: [{ type: "text" as const, text: "Error: valueMode='constant' requires a value." }] };
      }

      const data = await uePost("/api/set-module-input", { emitter, stage, moduleNodeGuid, input, type, valueMode, value, curveKeys, curveInterp });
      if (data.error) return { content: [{ type: "text" as const, text: `Error: ${data.error}` }] };

      const lines: string[] = [];
      if (data.valueMode === "curve") {
        lines.push(`Set ${data.moduleName}.${data.input} = curve (${data.type}), ${data.keyCount} key(s), interp=${curveInterp}`);
        if (data.reusedExistingCurve) lines.push(`Updated existing curve in place (no new node).`);
      } else {
        lines.push(`Set ${data.moduleName}.${data.input} = ${JSON.stringify(value)} (${data.type})`);
        if (data.pinDefaultValue) lines.push(`Pin default: ${data.pinDefaultValue}`);
      }
      if (data.saved !== undefined) lines.push(`Saved: ${data.saved}`);
      return { content: [{ type: "text" as const, text: lines.join("\n") }] };
    }
  );

  server.tool(
    "add_user_parameter",
    "Add a User Parameter to a UNiagaraSystem (the uds_* values exposed to RenderStream/Blueprint). Name is auto-prefixed with 'User.' if omitted.",
    {
      system: z.string().describe("System name or /Game/... path"),
      name: z.string().describe("Parameter name, e.g. 'uds_Intensity' (User. prefix added automatically)"),
      type: z.enum(["float", "int", "bool", "vec2", "vec3", "vec4", "color"]).describe("Niagara type"),
    },
    async ({ system, name, type }) => {
      const err = await ensureUE();
      if (err) return { content: [{ type: "text" as const, text: err }] };

      const data = await uePost("/api/add-user-parameter", { system, name, type });
      if (data.error) return { content: [{ type: "text" as const, text: `Error: ${data.error}` }] };

      const lines: string[] = [];
      lines.push(`Added user parameter ${data.name} : ${data.type} to ${data.system || system}`);
      if (data.saved !== undefined) lines.push(`Saved: ${data.saved}`);
      return { content: [{ type: "text" as const, text: lines.join("\n") }] };
    }
  );

  server.tool(
    "set_user_parameter_default",
    "Set the default value of an existing User Parameter on a UNiagaraSystem. Add it first with add_user_parameter.",
    {
      system: z.string().describe("System name or /Game/... path"),
      name: z.string().describe("Parameter name (User. prefix added automatically)"),
      value: valueSchema.describe("Value: number for scalars, [x,y,...] array for vectors/color"),
    },
    async ({ system, name, value }) => {
      const err = await ensureUE();
      if (err) return { content: [{ type: "text" as const, text: err }] };

      const data = await uePost("/api/set-user-parameter-default", { system, name, value });
      if (data.error) return { content: [{ type: "text" as const, text: `Error: ${data.error}` }] };

      const lines: string[] = [];
      lines.push(`Set ${data.name} = ${JSON.stringify(value)} (${data.type}) on ${data.system || system}`);
      if (data.saved !== undefined) lines.push(`Saved: ${data.saved}`);
      return { content: [{ type: "text" as const, text: lines.join("\n") }] };
    }
  );

  server.tool(
    "list_module_library",
    "List Niagara module scripts available in the project, optionally filtered to those valid for a given stack stage. Use the returned paths with add_niagara_module.",
    {
      stage: z.enum(STAGES).optional().describe("Only modules valid for this stage"),
      path: z.string().optional().describe("Package-path prefix filter (e.g. '/Niagara/Modules')"),
    },
    async ({ stage, path }) => {
      const err = await ensureUE();
      if (err) return { content: [{ type: "text" as const, text: err }] };

      const data = await ueGet("/api/list-module-library", { stage: stage || "", path: path || "" });
      if (data.error) return { content: [{ type: "text" as const, text: `Error: ${data.error}` }] };

      const lines: string[] = [];
      lines.push(`Found ${data.count} module(s)${data.stageFilter ? ` valid for ${data.stageFilter}` : ""}`);
      for (const m of data.modules || []) {
        lines.push(`  ${m.name}  (${m.path})`);
      }
      return { content: [{ type: "text" as const, text: lines.join("\n") }] };
    }
  );

  server.tool(
    "set_renderer_property",
    "Set a property on a renderer by index (from get_niagara_emitter_summary) via reflection. Most common use: set 'Material' on a Sprite/Ribbon renderer to an asset path so particles get the right look (glow/additive/etc). Value is UE import-text: an asset path like '/Game/FX/M_Glow.M_Glow' for object props, '1.0'/'true'/'(R=1,G=0,B=0,A=1)' for scalars/structs.",
    {
      emitter: z.string().describe("Emitter name or /Game/... path"),
      index: z.number().int().describe("Renderer index (0-based) from get_niagara_emitter_summary"),
      property: z.string().describe("Property name on the renderer class, e.g. 'Material'"),
      value: z.string().describe("Value in UE import-text format (asset path for Material; '1.0'/'true'/struct text for others)"),
    },
    async ({ emitter, index, property, value }) => {
      const err = await ensureUE();
      if (err) return { content: [{ type: "text" as const, text: err }] };

      const data = await uePost("/api/set-renderer-property", { emitter, index, property, value });
      if (data.error) return { content: [{ type: "text" as const, text: `Error: ${data.error}` }] };

      const lines: string[] = [];
      lines.push(`Set ${data.rendererClass}[${data.index}].${data.property} = ${data.value} on ${data.emitter || emitter}`);
      if (data.saved !== undefined) lines.push(`Saved: ${data.saved}`);
      return { content: [{ type: "text" as const, text: lines.join("\n") }] };
    }
  );

  // ---------------------------------------------------------------------------
  // Removal (Tier 2 CRUD counterparts)
  // ---------------------------------------------------------------------------

  server.tool(
    "remove_niagara_renderer",
    "Remove a renderer from a UNiagaraEmitter by its index (as listed by get_niagara_emitter_summary).",
    {
      emitter: z.string().describe("Emitter name or /Game/... path"),
      index: z.number().int().describe("Renderer index (0-based) from get_niagara_emitter_summary"),
    },
    async ({ emitter, index }) => {
      const err = await ensureUE();
      if (err) return { content: [{ type: "text" as const, text: err }] };

      const data = await uePost("/api/remove-niagara-renderer", { emitter, index });
      if (data.error) return { content: [{ type: "text" as const, text: `Error: ${data.error}` }] };

      const lines: string[] = [];
      lines.push(`Removed ${data.removedClass} from ${data.emitter || emitter}`);
      if (data.rendererCount !== undefined) lines.push(`Emitter now has ${data.rendererCount} renderer(s)`);
      if (data.saved !== undefined) lines.push(`Saved: ${data.saved}`);
      return { content: [{ type: "text" as const, text: lines.join("\n") }] };
    }
  );

  server.tool(
    "remove_user_parameter",
    "Remove a User Parameter from a UNiagaraSystem. Name is auto-prefixed with 'User.' if omitted.",
    {
      system: z.string().describe("System name or /Game/... path"),
      name: z.string().describe("Parameter name (User. prefix added automatically)"),
    },
    async ({ system, name }) => {
      const err = await ensureUE();
      if (err) return { content: [{ type: "text" as const, text: err }] };

      const data = await uePost("/api/remove-user-parameter", { system, name });
      if (data.error) return { content: [{ type: "text" as const, text: `Error: ${data.error}` }] };

      const lines: string[] = [];
      lines.push(`Removed user parameter ${data.name} from ${data.system || system} (removed: ${data.removed})`);
      if (data.saved !== undefined) lines.push(`Saved: ${data.saved}`);
      return { content: [{ type: "text" as const, text: lines.join("\n") }] };
    }
  );

  server.tool(
    "remove_emitter_from_system",
    "Detach an emitter handle from a UNiagaraSystem by its handle name (as listed by get_niagara_system_summary). Triggers a system recompile.",
    {
      system: z.string().describe("System name or /Game/... path"),
      handleName: z.string().describe("Emitter handle name inside the system (from get_niagara_system_summary)"),
    },
    async ({ system, handleName }) => {
      const err = await ensureUE();
      if (err) return { content: [{ type: "text" as const, text: err }] };

      const data = await uePost("/api/remove-emitter-from-system", { system, handleName });
      if (data.error) return { content: [{ type: "text" as const, text: `Error: ${data.error}` }] };

      const lines: string[] = [];
      lines.push(`Removed emitter handle '${data.removedHandle || handleName}' from ${data.system || system}`);
      if (data.emitterCount !== undefined) lines.push(`System now has ${data.emitterCount} emitter(s)`);
      if (data.saved !== undefined) lines.push(`Saved: ${data.saved}`);
      return { content: [{ type: "text" as const, text: lines.join("\n") }] };
    }
  );
}
