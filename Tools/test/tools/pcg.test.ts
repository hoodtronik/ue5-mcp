import { describe, it, expect, afterAll } from "vitest";
import { uePost, uniqueName } from "../helpers.js";

// CLAUDE-NOTE: PCG tools execute via the run_python bridge (POST /api/run-python),
// so these integration tests drive that endpoint with the same Python the MCP tools
// generate. They require an EDITOR-mode server (run_python is editor-only); against a
// headless commandlet, run_python returns an error, which the tests tolerate by
// skipping the happy-path assertions. Error/missing-field paths are always asserted.

const PCG_GRAPH = uniqueName("PCG_ToolTest");
const PCG_PKG = "/Game/Test";
const FULL = `${PCG_PKG}/${PCG_GRAPH}`;

/** Wrap a python body the way pcg.ts does: param injection + __PCG__ sentinel emit. */
function pcgScript(body: string, params: Record<string, unknown>): string {
  const b64 = Buffer.from(JSON.stringify(params), "utf-8").toString("base64");
  const indented = body.trim().split("\n").map((l) => "    " + l).join("\n");
  return `import unreal, json, base64
P = json.loads(base64.b64decode("${b64}").decode("utf-8"))
def _emit(d):
    print("__PCG__" + json.dumps(d))
try:
${indented}
except Exception as _e:
    _emit({"ok": False, "error": "%s: %s" % (type(_e).__name__, _e)})
`;
}

async function runPcg(body: string, params: Record<string, unknown>): Promise<any> {
  const data = await uePost("/api/run-python", { code: pcgScript(body, params) });
  return data;
}

function parseSentinel(output: string): any | null {
  const line = (output || "").split(/\r?\n/).find((l) => l.startsWith("__PCG__"));
  return line ? JSON.parse(line.slice("__PCG__".length)) : null;
}

/** True when the backend is editor-mode (run_python works). */
async function editorAvailable(): Promise<boolean> {
  const data = await uePost("/api/run-python", { code: "print('ping')" });
  return !data.error; // commandlet mode returns an error for run_python
}

describe("pcg tools (via run_python bridge)", () => {
  afterAll(async () => {
    if (await editorAvailable()) {
      await uePost("/api/delete-asset", { assetPath: FULL, force: true });
    }
  });

  describe("run_python endpoint contract", () => {
    it("rejects a missing 'code' field", async () => {
      const data = await uePost("/api/run-python", {});
      expect(data.error).toBeDefined();
    });
  });

  describe("create_pcg_graph", () => {
    it("creates a graph in editor mode, or reports editor-only otherwise", async () => {
      const data = await runPcg(
        `
name = P["name"]; pkg = P["pkg"].rstrip("/"); full = pkg + "/" + name
if unreal.EditorAssetLibrary.does_asset_exist(full):
    raise Exception("exists")
asset = unreal.AssetToolsHelpers.get_asset_tools().create_asset(name, pkg, unreal.PCGGraph, unreal.PCGGraphFactory())
unreal.EditorAssetLibrary.save_loaded_asset(asset)
_emit({"ok": True, "path": asset.get_path_name()})
`,
        { name: PCG_GRAPH, pkg: PCG_PKG }
      );
      if (data.error) {
        // commandlet/headless mode: run_python unavailable — acceptable
        expect(typeof data.error).toBe("string");
        return;
      }
      const res = parseSentinel(data.output);
      expect(res).not.toBeNull();
      expect(res.ok).toBe(true);
      expect(res.path).toContain(PCG_GRAPH);
    });
  });

  describe("add_pcg_node", () => {
    it("adds a surface sampler in editor mode", async () => {
      if (!(await editorAvailable())) return;
      const data = await runPcg(
        `
g = unreal.EditorAssetLibrary.load_asset(P["full"])
if not isinstance(g, unreal.PCGGraph):
    raise Exception("not a graph")
res = g.add_node_of_type(unreal.PCGSurfaceSamplerSettings)
node = res[0] if isinstance(res, (list, tuple)) else res
unreal.EditorAssetLibrary.save_loaded_asset(g)
nodes = list(g.get_editor_property("nodes"))
_emit({"ok": True, "nodeCount": len(nodes)})
`,
        { full: FULL }
      );
      const res = parseSentinel(data.output);
      expect(res?.ok).toBe(true);
      expect(res.nodeCount).toBeGreaterThanOrEqual(1);
    });

    it("reports a clear error for an unknown node type", async () => {
      if (!(await editorAvailable())) return;
      const data = await runPcg(
        `
tn = P["nodeType"]
cls = getattr(unreal, tn, None)
if cls is None:
    raise Exception("Unknown PCG settings class: '%s'" % tn)
_emit({"ok": True})
`,
        { nodeType: "PCGNotARealNodeXYZ" }
      );
      const res = parseSentinel(data.output);
      expect(res?.ok).toBe(false);
      expect(res.error).toContain("Unknown PCG settings class");
    });
  });

  describe("list_pcg_graphs", () => {
    it("returns a graph list structure in editor mode", async () => {
      if (!(await editorAvailable())) return;
      const data = await runPcg(
        `
ar = unreal.AssetRegistryHelpers.get_asset_registry()
out = []
for a in ar.get_assets_by_class(unreal.TopLevelAssetPath("/Script/PCG", "PCGGraph"), True):
    out.append({"name": str(a.asset_name), "path": str(a.package_name)})
_emit({"ok": True, "count": len(out), "graphs": out})
`,
        {}
      );
      const res = parseSentinel(data.output);
      expect(res?.ok).toBe(true);
      expect(typeof res.count).toBe("number");
      expect(Array.isArray(res.graphs)).toBe(true);
    });
  });
});
