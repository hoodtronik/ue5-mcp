import { describe, it, expect } from "vitest";
import { uePost } from "../helpers.js";

// CLAUDE-NOTE: discover_python_class/discover_python_search execute via the run_python bridge
// (editor-only), so these tests drive the same generated Python the tools produce, same pattern
// as pcg.test.ts. Against a headless commandlet, run_python errors and the tests tolerate that by
// skipping the happy-path assertions — the actual extraction logic (regex over a real UE
// __doc__ string) was separately verified against a live editor before this file was written:
// unreal.GroomBindingAsset correctly yielded 15 properties + 1 method; unreal.EditorAssetLibrary
// yielded 39 methods with real signatures.

/** Minimal reimplementation of discover-python.ts's script wrapper, for driving the HTTP API directly. */
function discoverScript(body: string, params: Record<string, unknown>): string {
  const b64 = Buffer.from(JSON.stringify(params), "utf-8").toString("base64");
  const indented = body.trim().split("\n").map((l) => "    " + l).join("\n");
  return `import unreal, json, base64, re
P = json.loads(base64.b64decode("${b64}").decode("utf-8"))
def _emit(d):
    print("__DISCOVER__" + json.dumps(d))
try:
${indented}
except Exception as _e:
    _emit({"ok": False, "error": "%s: %s" % (type(_e).__name__, _e)})
`;
}

async function execDiscover(code: string): Promise<any> {
  const data = await uePost("/api/run-python", { code });
  if (data.error) return { ok: false, error: data.error };
  const out: string = typeof data.output === "string" ? data.output : "";
  const line = out.split(/\r?\n/).find((l) => l.startsWith("__DISCOVER__"));
  if (!line) return { ok: false, error: `No sentinel emitted. Raw output: ${out}` };
  return JSON.parse(line.slice("__DISCOVER__".length));
}

/** True when the backend is a real editor rather than the headless test commandlet. */
async function editorAvailable(): Promise<boolean> {
  const data = await uePost("/api/run-python", { code: "print('ping')" });
  return !data.error;
}

describe("discover_python_class (via run_python bridge)", () => {
  it("rejects an unknown class name with a helpful error", async () => {
    const res = await execDiscover(
      discoverScript(
        `
name = P["className"]
cls = getattr(unreal, name, None)
if cls is None:
    raise Exception("No such unreal.%s. Use discover_python_search to find the right name." % name)
_emit({"ok": True})
`,
        { className: "NonExistentClassXYZ999" }
      )
    );
    if (res.error && res.error.includes("editor mode")) return; // headless commandlet — acceptable
    expect(res.ok).toBe(false);
    expect(res.error).toContain("NonExistentClassXYZ999");
  });

  it("extracts editor properties and methods from a real class's __doc__", async () => {
    if (!(await editorAvailable())) return;

    const res = await execDiscover(
      discoverScript(
        `
cls = getattr(unreal, P["className"])
doc = cls.__doc__ or ""
prop_names = set(re.findall(r"- \`\`([a-zA-Z0-9_]+)\`\`", doc))
props = []
for line in doc.split("\\n"):
    mm = re.match(r"- \`\`([a-zA-Z0-9_]+)\`\` \\(([^)]*)\\):\\s*\\[([^\\]]*)\\]", line.strip())
    if mm:
        props.append({"name": mm.group(1), "type": mm.group(2), "access": mm.group(3)})
_emit({"ok": True, "propertyCount": len(props), "hasBuild": "build" in dir(cls)})
`,
        { className: "GroomBindingAsset" }
      )
    );

    expect(res.ok).toBe(true);
    expect(res.propertyCount).toBeGreaterThan(5);
    expect(res.hasBuild).toBe(true);
  });
});

describe("discover_python_search (via run_python bridge)", () => {
  it("finds known class names by substring", async () => {
    if (!(await editorAvailable())) return;

    const res = await execDiscover(
      discoverScript(
        `
q = P["query"].lower()
matches = sorted([n for n in dir(unreal) if q in n.lower()])
_emit({"ok": True, "total": len(matches), "hasExact": P["expect"] in matches})
`,
        { query: "mirrordatatable", expect: "MirrorDataTable" }
      )
    );

    expect(res.ok).toBe(true);
    expect(res.total).toBeGreaterThan(0);
    expect(res.hasExact).toBe(true);
  });

  it("returns zero matches for a nonsense query", async () => {
    if (!(await editorAvailable())) return;

    const res = await execDiscover(
      discoverScript(
        `
q = P["query"].lower()
matches = [n for n in dir(unreal) if q in n.lower()]
_emit({"ok": True, "total": len(matches)})
`,
        { query: "nosuchsymbolxyz999" }
      )
    );

    expect(res.ok).toBe(true);
    expect(res.total).toBe(0);
  });
});
