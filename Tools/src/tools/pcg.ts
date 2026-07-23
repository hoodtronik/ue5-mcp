import type { McpServer } from "@modelcontextprotocol/sdk/server/mcp.js";
import { z } from "zod";
import { ensureUE, uePost } from "../ue-bridge.js";

// CLAUDE-NOTE: PCG (Procedural Content Generation) tools.
//
// These are purpose-built MCP tools with proper schemas/validation/descriptions,
// but they execute via the EXISTING run_python bridge (POST /api/run-python) rather
// than dedicated C++ endpoints. Rationale: the PCG scripting API is BlueprintCallable
// (reflected to Python) and has shipped since UE 5.0, so driving it through Python is
// fully UE 5.6-safe and adds ZERO new C++, ZERO new module dependencies, and ZERO
// plugin load-order / RHI impact (critical for the RenderStream broadcast pipeline).
//
// Every API name used below was verified against actual UE 5.6 source
// (Engine/Plugins/PCG/Source/PCG/Public/PCGGraph.h, PCGNode.h, PCGComponent.h):
//   UPCGGraph::AddNodeOfType / AddEdge / RemoveNode / GetInputNode / GetOutputNode  (BlueprintCallable)
//   UPCGGraph::Nodes / InputNode / OutputNode                                       (BlueprintReadOnly UPROPERTY)
//   UPCGNode::GetSettings                                                           (BlueprintCallable)
//   UPCGComponent::SetGraph / Generate / GenerateLocal                             (BlueprintCallable)
//   UPCGGraphFactory                                                                (UFactory)

// ---------------------------------------------------------------------------
// Python bridge plumbing
// ---------------------------------------------------------------------------

// Shared Python helpers prepended to every script that needs graph/node lookup.
const PCG_PRELUDE = `
def _node_title(n):
    try:
        s = n.get_settings()
        if s is not None:
            return s.get_class().get_name()
    except Exception:
        pass
    try:
        return n.get_name()
    except Exception:
        return "<node>"

def _load_graph(ref):
    path = ref
    if not ref.startswith("/"):
        ar = unreal.AssetRegistryHelpers.get_asset_registry()
        found = None
        for a in ar.get_assets_by_class(unreal.TopLevelAssetPath("/Script/PCG", "PCGGraph"), True):
            if str(a.asset_name) == ref:
                found = a
                break
        if found is None:
            raise Exception("PCG graph not found by name: " + ref + " (pass a full /Game/... path if the name is ambiguous)")
        path = str(found.package_name)
    g = unreal.EditorAssetLibrary.load_asset(path)
    if g is None:
        raise Exception("Failed to load asset: " + path)
    if not isinstance(g, unreal.PCGGraph):
        raise Exception("Asset is not a PCGGraph: " + path)
    return g

def _all_nodes(g):
    return list(g.get_editor_property("nodes"))

def _node_index_listing(g):
    return ", ".join("%d:%s" % (i, _node_title(n)) for i, n in enumerate(_all_nodes(g)))

def _find_node(g, ref):
    nodes = _all_nodes(g)
    try:
        idx = int(ref)
        if idx < 0 or idx >= len(nodes):
            raise Exception("Node index %d out of range (graph has %d nodes: %s)" % (idx, len(nodes), _node_index_listing(g)))
        return nodes[idx], idx
    except (ValueError, TypeError):
        pass
    matches = [(i, n) for i, n in enumerate(nodes) if str(ref).lower() in _node_title(n).lower()]
    if not matches:
        raise Exception("No node matching '%s'. Available: %s" % (ref, _node_index_listing(g)))
    if len(matches) > 1:
        raise Exception("Ambiguous node '%s' matches: %s" % (ref, ", ".join("%d:%s" % (i, _node_title(n)) for i, n in matches)))
    return matches[0][1], matches[0][0]
`;

/** Wrap a Python body (which must call _emit once) with param injection + error capture. */
function pcgScript(body: string, params: Record<string, unknown>, withPrelude = true): string {
  const b64 = Buffer.from(JSON.stringify(params), "utf-8").toString("base64");
  const indented = body
    .trim()
    .split("\n")
    .map((l) => "    " + l)
    .join("\n");
  return `import unreal, json, base64
P = json.loads(base64.b64decode("${b64}").decode("utf-8"))
def _emit(d):
    print("__PCG__" + json.dumps(d))
${withPrelude ? PCG_PRELUDE : ""}
try:
${indented}
except Exception as _e:
    _emit({"ok": False, "error": "%s: %s" % (type(_e).__name__, _e)})
`;
}

type PcgResult = { ok: boolean; error?: string; [k: string]: unknown };

/** Execute a PCG python script via the run_python bridge and parse the __PCG__ sentinel line. */
async function execPcg(code: string): Promise<PcgResult> {
  const data = await uePost("/api/run-python", { code });
  if (data.error) return { ok: false, error: data.error };
  const out: string = typeof data.output === "string" ? data.output : "";
  const line = out.split(/\r?\n/).find((l) => l.startsWith("__PCG__"));
  if (!line) {
    return { ok: false, error: `No result emitted by PCG script. Raw output: ${out || "(empty)"}` };
  }
  try {
    return JSON.parse(line.slice("__PCG__".length)) as PcgResult;
  } catch {
    return { ok: false, error: `Could not parse PCG result payload: ${line}` };
  }
}

function text(t: string) {
  return { content: [{ type: "text" as const, text: t }] };
}

// ---------------------------------------------------------------------------
// Curated node catalog (names verified against UE 5.6 PCG source)
// ---------------------------------------------------------------------------

const PCG_NODE_CATALOG: { category: string; nodes: { type: string; desc: string }[] }[] = [
  {
    category: "Samplers (generate points)",
    nodes: [
      { type: "PCGSurfaceSamplerSettings", desc: "Scatter points across a surface (landscape/mesh)" },
      { type: "PCGVolumeSamplerSettings", desc: "Fill a volume with a 3D grid of points" },
      { type: "PCGCreatePointsSettings", desc: "Create explicit points from a list" },
      { type: "PCGCreatePointsGridSettings", desc: "Create a regular grid of points" },
      { type: "PCGCreatePointsSphereSettings", desc: "Create points on a sphere" },
    ],
  },
  {
    category: "Spawners (produce content)",
    nodes: [
      { type: "PCGStaticMeshSpawnerSettings", desc: "Spawn static mesh instances at each point" },
      { type: "PCGSpawnActorSettings", desc: "Spawn actors at each point" },
    ],
  },
  {
    category: "Point operations",
    nodes: [
      { type: "PCGTransformPointsSettings", desc: "Offset/rotate/scale points (incl. randomized)" },
      { type: "PCGDensityFilterSettings", desc: "Keep/cull points by density threshold" },
      { type: "PCGDensityRemapSettings", desc: "Remap point density values" },
      { type: "PCGSelfPruningSettings", desc: "Prune overlapping points (distance-based thinning)" },
      { type: "PCGCopyPointsSettings", desc: "Copy source points onto target points" },
      { type: "PCGDuplicatePointSettings", desc: "Duplicate each point N times" },
    ],
  },
  {
    category: "Spatial / boolean",
    nodes: [
      { type: "PCGDifferenceSettings", desc: "Subtract one spatial data set from another" },
      { type: "PCGDataFromActorSettings", desc: "Read spatial data from an actor in the level" },
      { type: "PCGCullPointsOutsideActorBoundsSettings", desc: "Remove points outside the owning actor's bounds" },
    ],
  },
  {
    category: "Attributes",
    nodes: [
      { type: "PCGAddAttributeSettings", desc: "Add/set an attribute on the data" },
      { type: "PCGAttributeNoiseSettings", desc: "Apply noise to an attribute" },
      { type: "PCGAttributeFilteringSettings", desc: "Filter points by attribute value" },
      { type: "PCGCopyAttributeSettings", desc: "Copy an attribute from one set to another" },
    ],
  },
];

// ---------------------------------------------------------------------------
// Tools
// ---------------------------------------------------------------------------

export function registerPcgTools(server: McpServer): void {
  // ----- Reads --------------------------------------------------------------

  server.tool(
    "list_pcg_graphs",
    "List all PCG graph assets in the project. Optionally filter by package-path prefix. Requires editor mode (uses the run_python bridge).",
    {
      path: z.string().optional().describe("Package-path prefix to filter by, e.g. '/Game/PCG'"),
    },
    async ({ path }) => {
      const err = await ensureUE();
      if (err) return text(err);

      const code = pcgScript(
        `
ar = unreal.AssetRegistryHelpers.get_asset_registry()
pf = P.get("path") or ""
out = []
for a in ar.get_assets_by_class(unreal.TopLevelAssetPath("/Script/PCG", "PCGGraph"), True):
    pkg = str(a.package_name)
    if pf and not pkg.startswith(pf):
        continue
    out.append({"name": str(a.asset_name), "path": pkg})
out.sort(key=lambda x: x["path"])
_emit({"ok": True, "count": len(out), "graphs": out})
`,
        { path: path || "" }
      );

      const res = await execPcg(code);
      if (!res.ok) return text(`Error: ${res.error}`);

      const graphs = (res.graphs as { name: string; path: string }[]) || [];
      const lines = [`Found ${res.count} PCG graph(s)${path ? ` under '${path}'` : ""}`];
      for (const g of graphs) lines.push(`  ${g.name}  (${g.path})`);
      return text(lines.join("\n"));
    }
  );

  server.tool(
    "get_pcg_graph",
    "Get the node structure of a PCG graph: each node's index, title (settings class), and the input/output node titles. Node index is the stable id used by connect_pcg_nodes / set_pcg_node_property / delete_pcg_node. Requires editor mode.",
    {
      graph: z.string().describe("PCG graph name (e.g. 'PCG_Scatter') or full /Game/... path"),
    },
    async ({ graph }) => {
      const err = await ensureUE();
      if (err) return text(err);

      const code = pcgScript(
        `
g = _load_graph(P["graph"])
nodes = _all_nodes(g)
node_list = [{"index": i, "title": _node_title(n)} for i, n in enumerate(nodes)]
inp = None
outp = None
try:
    inp = _node_title(g.get_input_node())
except Exception:
    pass
try:
    outp = _node_title(g.get_output_node())
except Exception:
    pass
_emit({"ok": True, "path": g.get_path_name(), "nodeCount": len(nodes), "nodes": node_list, "inputNode": inp, "outputNode": outp})
`,
        { graph }
      );

      const res = await execPcg(code);
      if (!res.ok) return text(`Error: ${res.error}`);

      const nodes = (res.nodes as { index: number; title: string }[]) || [];
      const lines = [
        `PCG graph: ${res.path}`,
        `Input node:  ${res.inputNode ?? "(none)"}`,
        `Output node: ${res.outputNode ?? "(none)"}`,
        `Nodes (${res.nodeCount}):`,
      ];
      if (nodes.length === 0) lines.push("  (no editable nodes — only input/output)");
      for (const n of nodes) lines.push(`  [${n.index}] ${n.title}`);
      lines.push("", "Note: edges/pins are not enumerated here; use run_python for deep edge inspection.");
      return text(lines.join("\n"));
    }
  );

  server.tool(
    "list_pcg_nodes",
    "List a curated catalog of common PCG node types (the building blocks) with their exact settings-class names to pass to add_pcg_node. Names are verified against UE 5.6. This is a core subset — any UPCGSettings subclass name also works with add_pcg_node. Static reference; does not require the editor.",
    {
      filter: z.string().optional().describe("Case-insensitive substring to filter node types/descriptions"),
    },
    async ({ filter }) => {
      const f = (filter || "").toLowerCase();
      const lines: string[] = ["PCG node catalog (pass `type` to add_pcg_node):", ""];
      let count = 0;
      for (const group of PCG_NODE_CATALOG) {
        const matched = group.nodes.filter(
          (n) => !f || n.type.toLowerCase().includes(f) || n.desc.toLowerCase().includes(f)
        );
        if (matched.length === 0) continue;
        lines.push(`${group.category}:`);
        for (const n of matched) {
          lines.push(`  ${n.type}  —  ${n.desc}`);
          count++;
        }
        lines.push("");
      }
      lines.push(
        `${count} node type(s)${f ? ` matching '${filter}'` : ""}. The full PCG library has 80+ node types — any UPCGSettings subclass name (e.g. from run_python introspection) is valid for add_pcg_node.`
      );
      return text(lines.join("\n"));
    }
  );

  // ----- Mutations ----------------------------------------------------------

  server.tool(
    "create_pcg_graph",
    "Create a new, empty PCG graph asset at a content path. The graph starts with only its Input and Output nodes — add building blocks with add_pcg_node. Requires editor mode.",
    {
      name: z.string().describe("Graph asset name (convention: 'PCG_MyGraph')"),
      packagePath: z.string().default("/Game/PCG").describe("Package path, e.g. '/Game/PCG'"),
      dryRun: z.boolean().optional().describe("If true, report the intended action without creating anything"),
    },
    async ({ name, packagePath, dryRun }) => {
      const err = await ensureUE();
      if (err) return text(err);

      const fullPath = `${packagePath.replace(/\/$/, "")}/${name}`;
      if (dryRun) {
        return text(
          [`[dryRun] Would create PCG graph '${name}' at ${fullPath}`, "No changes made."].join("\n")
        );
      }

      const code = pcgScript(
        `
name = P["name"]
pkg = P["packagePath"].rstrip("/")
full = pkg + "/" + name
if unreal.EditorAssetLibrary.does_asset_exist(full):
    raise Exception("Asset already exists: " + full)
tools = unreal.AssetToolsHelpers.get_asset_tools()
asset = tools.create_asset(name, pkg, unreal.PCGGraph, unreal.PCGGraphFactory())
if asset is None:
    raise Exception("create_asset returned None for " + full)
unreal.EditorAssetLibrary.save_loaded_asset(asset)
_emit({"ok": True, "path": asset.get_path_name()})
`,
        { name, packagePath },
        false
      );

      const res = await execPcg(code);
      if (!res.ok) return text(`Error: ${res.error}`);

      return text(
        [
          `Created PCG graph at ${res.path}`,
          "",
          "Next steps:",
          "  1. list_pcg_nodes to see available node types",
          "  2. add_pcg_node to add a sampler, then a spawner",
          "  3. connect_pcg_nodes to wire them, then execute_pcg_graph on a target actor",
        ].join("\n")
      );
    }
  );

  server.tool(
    "add_pcg_node",
    "Add a node to a PCG graph by its settings-class type name (e.g. 'PCGSurfaceSamplerSettings'). Call list_pcg_nodes first to get valid type names — never guess. Returns the new node's index. Requires editor mode.",
    {
      graph: z.string().describe("PCG graph name or full /Game/... path"),
      nodeType: z
        .string()
        .describe("UPCGSettings subclass name, e.g. 'PCGSurfaceSamplerSettings' (the 'U' prefix is optional)"),
      dryRun: z.boolean().optional().describe("If true, report the intended action without modifying the graph"),
    },
    async ({ graph, nodeType, dryRun }) => {
      const err = await ensureUE();
      if (err) return text(err);

      if (dryRun) {
        return text([`[dryRun] Would add node '${nodeType}' to graph '${graph}'`, "No changes made."].join("\n"));
      }

      const code = pcgScript(
        `
g = _load_graph(P["graph"])
tn = P["nodeType"]
cls = getattr(unreal, tn, None)
if cls is None and tn.startswith("U"):
    cls = getattr(unreal, tn[1:], None)
if cls is None:
    raise Exception("Unknown PCG settings class: '%s'. Use list_pcg_nodes for valid names." % tn)
res = g.add_node_of_type(cls)
node = res[0] if isinstance(res, (list, tuple)) else res
if node is None:
    raise Exception("add_node_of_type returned no node for " + tn)
unreal.EditorAssetLibrary.save_loaded_asset(g)
nodes = _all_nodes(g)
idx = nodes.index(node) if node in nodes else len(nodes) - 1
_emit({"ok": True, "nodeIndex": idx, "nodeTitle": _node_title(node), "nodeCount": len(nodes)})
`,
        { graph, nodeType }
      );

      const res = await execPcg(code);
      if (!res.ok) return text(`Error: ${res.error}`);

      return text(
        [
          `Added node [${res.nodeIndex}] ${res.nodeTitle} to '${graph}' (graph now has ${res.nodeCount} node(s))`,
          "",
          "Next steps:",
          `  1. set_pcg_node_property to configure node [${res.nodeIndex}]`,
          "  2. connect_pcg_nodes to wire it to another node",
        ].join("\n")
      );
    }
  );

  server.tool(
    "connect_pcg_nodes",
    "Connect an output pin of one node to an input pin of another in a PCG graph. Nodes are identified by index (from get_pcg_graph) or by a unique title substring. Pin labels default to 'Out' (source) and 'In' (target). Requires editor mode.",
    {
      graph: z.string().describe("PCG graph name or full /Game/... path"),
      fromNode: z.string().describe("Source node: index (e.g. '0') or unique title substring"),
      toNode: z.string().describe("Target node: index or unique title substring"),
      fromPin: z.string().optional().describe("Source output pin label (default 'Out')"),
      toPin: z.string().optional().describe("Target input pin label (default 'In')"),
      dryRun: z.boolean().optional().describe("If true, report the intended action without modifying the graph"),
    },
    async ({ graph, fromNode, toNode, fromPin, toPin, dryRun }) => {
      const err = await ensureUE();
      if (err) return text(err);

      if (dryRun) {
        return text(
          [
            `[dryRun] Would connect ${fromNode}.${fromPin || "Out"} -> ${toNode}.${toPin || "In"} in graph '${graph}'`,
            "No changes made.",
          ].join("\n")
        );
      }

      const code = pcgScript(
        `
g = _load_graph(P["graph"])
fn, fi = _find_node(g, P["fromNode"])
tn, ti = _find_node(g, P["toNode"])
fp = P.get("fromPin") or "Out"
tp = P.get("toPin") or "In"
g.add_edge(fn, fp, tn, tp)
unreal.EditorAssetLibrary.save_loaded_asset(g)
_emit({"ok": True, "fromLabel": "%d:%s" % (fi, _node_title(fn)), "toLabel": "%d:%s" % (ti, _node_title(tn)), "fromPin": fp, "toPin": tp})
`,
        { graph, fromNode, toNode, fromPin, toPin }
      );

      const res = await execPcg(code);
      if (!res.ok) return text(`Error: ${res.error}`);

      return text(
        [
          `Connected ${res.fromLabel} (${res.fromPin}) -> ${res.toLabel} (${res.toPin})`,
          "",
          "Next step: execute_pcg_graph on a target actor to see the result.",
        ].join("\n")
      );
    }
  );

  server.tool(
    "set_pcg_node_property",
    "Set a property on a PCG node's settings object. Identify the node by index or unique title. String values that look like an asset path ('/Game/...') are auto-loaded into objects. Complex struct-valued properties (e.g. mesh selectors) may need run_python. Requires editor mode.",
    {
      graph: z.string().describe("PCG graph name or full /Game/... path"),
      node: z.string().describe("Node: index (e.g. '1') or unique title substring"),
      property: z.string().describe("Settings property name in snake_case (e.g. 'points_per_squared_meter')"),
      value: z
        .union([z.string(), z.number(), z.boolean()])
        .describe("Value to set. '/Game/...' strings are auto-loaded as assets."),
      dryRun: z.boolean().optional().describe("If true, report the intended action without modifying the graph"),
    },
    async ({ graph, node, property, value, dryRun }) => {
      const err = await ensureUE();
      if (err) return text(err);

      if (dryRun) {
        return text(
          [
            `[dryRun] Would set ${property} = ${JSON.stringify(value)} on node '${node}' in graph '${graph}'`,
            "No changes made.",
          ].join("\n")
        );
      }

      const code = pcgScript(
        `
g = _load_graph(P["graph"])
n, i = _find_node(g, P["node"])
s = n.get_settings()
if s is None:
    raise Exception("Node [%d] %s has no settings object" % (i, _node_title(n)))
prop = P["property"]
val = P["value"]
if isinstance(val, str) and val.startswith("/"):
    loaded = unreal.EditorAssetLibrary.load_asset(val)
    if loaded is not None:
        val = loaded
s.set_editor_property(prop, val)
unreal.EditorAssetLibrary.save_loaded_asset(g)
_emit({"ok": True, "node": "%d:%s" % (i, _node_title(n)), "property": prop})
`,
        { graph, node, property, value }
      );

      const res = await execPcg(code);
      if (!res.ok) return text(`Error: ${res.error}`);

      return text(`Set ${res.property} on node ${res.node} in '${graph}'.`);
    }
  );

  server.tool(
    "delete_pcg_node",
    "Remove a node from a PCG graph. Identify the node by index or unique title substring. Note: removing a node shifts the indices of later nodes — re-run get_pcg_graph afterwards. Requires editor mode.",
    {
      graph: z.string().describe("PCG graph name or full /Game/... path"),
      node: z.string().describe("Node: index (e.g. '2') or unique title substring"),
      dryRun: z.boolean().optional().describe("If true, report the intended action without modifying the graph"),
    },
    async ({ graph, node, dryRun }) => {
      const err = await ensureUE();
      if (err) return text(err);

      if (dryRun) {
        return text([`[dryRun] Would delete node '${node}' from graph '${graph}'`, "No changes made."].join("\n"));
      }

      const code = pcgScript(
        `
g = _load_graph(P["graph"])
n, i = _find_node(g, P["node"])
title = _node_title(n)
g.remove_node(n)
unreal.EditorAssetLibrary.save_loaded_asset(g)
_emit({"ok": True, "removed": "%d:%s" % (i, title), "nodeCount": len(_all_nodes(g))})
`,
        { graph, node }
      );

      const res = await execPcg(code);
      if (!res.ok) return text(`Error: ${res.error}`);

      return text(
        `Removed node ${res.removed} from '${graph}' (graph now has ${res.nodeCount} node(s)). Indices may have shifted — re-run get_pcg_graph.`
      );
    }
  );

  server.tool(
    "execute_pcg_graph",
    "Trigger a PCG graph to (re)generate on a target actor. Finds the actor by label, ensures it has a PCGComponent, optionally assigns the given graph, and regenerates. Requires editor mode.",
    {
      actor: z.string().describe("Target actor's label in the level (e.g. 'PCGVolume_1')"),
      graph: z
        .string()
        .optional()
        .describe("Optional PCG graph name/path to assign before generating. Omit to regenerate the actor's existing graph."),
      dryRun: z.boolean().optional().describe("If true, report the intended action without generating"),
    },
    async ({ actor, graph, dryRun }) => {
      const err = await ensureUE();
      if (err) return text(err);

      if (dryRun) {
        return text(
          [
            `[dryRun] Would ${graph ? `assign graph '${graph}' and ` : ""}regenerate PCG on actor '${actor}'`,
            "No changes made.",
          ].join("\n")
        );
      }

      const code = pcgScript(
        `
label = P["actor"]
graph_ref = P.get("graph")
eas = unreal.get_editor_subsystem(unreal.EditorActorSubsystem)
actor = None
for a in eas.get_all_level_actors():
    if a.get_actor_label() == label:
        actor = a
        break
if actor is None:
    raise Exception("Actor not found by label: " + label)
comp = actor.get_component_by_class(unreal.PCGComponent)
created = False
if comp is None:
    comp = actor.add_component_by_class(unreal.PCGComponent, False, unreal.Transform(), False)
    created = True
if comp is None:
    raise Exception("Could not get or add a PCGComponent on '" + label + "'")
if graph_ref:
    comp.set_graph(_load_graph(graph_ref))
try:
    comp.generate_local(True)
except Exception:
    comp.generate(True)
_emit({"ok": True, "actor": label, "componentCreated": created, "graph": graph_ref or "(existing)"})
`,
        { actor, graph }
      );

      const res = await execPcg(code);
      if (!res.ok) return text(`Error: ${res.error}`);

      return text(
        [
          `Triggered PCG generation on '${res.actor}' (graph: ${res.graph}${res.componentCreated ? ", PCGComponent added" : ""}).`,
          "",
          "Next step: use take_screenshot or get_actor_bounds to verify the result.",
        ].join("\n")
      );
    }
  );
}
