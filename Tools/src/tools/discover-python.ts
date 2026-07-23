import type { McpServer } from "@modelcontextprotocol/sdk/server/mcp.js";
import { z } from "zod";
import { ensureUE, uePost } from "../ue-bridge.js";

// CLAUDE-NOTE: structured introspection over the run_python bridge, so an agent doesn't have to
// guess unreal.* method/property names before writing a run_python script. Executes via the
// EXISTING /api/run-python endpoint (same rationale as pcg.ts: zero new C++, zero new module
// dependencies). Verified against a live UE 5.6 editor: every unreal.* class the Python plugin
// exposes already carries a fully structured __doc__ (description, C++ source location, and an
// "Editor Properties:" table with type + Read-Only/Read-Write access) because that's how UE
// generates its own Python API reference docs — so this doesn't need inspect.signature gymnastics,
// just regex over cls.__doc__ plus a docstring-first-line pull for the remaining callables.

/** Wrap a Python body (which must call _emit once) with param injection + error capture. */
function discoverScript(body: string, params: Record<string, unknown>): string {
  const b64 = Buffer.from(JSON.stringify(params), "utf-8").toString("base64");
  const indented = body
    .trim()
    .split("\n")
    .map((l) => "    " + l)
    .join("\n");
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

type DiscoverResult = { ok: boolean; error?: string; [k: string]: unknown };

async function execDiscover(code: string): Promise<DiscoverResult> {
  const data = await uePost("/api/run-python", { code });
  if (data.error) return { ok: false, error: data.error };
  const out: string = typeof data.output === "string" ? data.output : "";
  const line = out.split(/\r?\n/).find((l) => l.startsWith("__DISCOVER__"));
  if (!line) {
    return { ok: false, error: `No result emitted by discovery script. Raw output: ${out || "(empty)"}` };
  }
  try {
    return JSON.parse(line.slice("__DISCOVER__".length)) as DiscoverResult;
  } catch {
    return { ok: false, error: `Could not parse discovery result payload: ${line}` };
  }
}

function text(t: string) {
  return { content: [{ type: "text" as const, text: t }] };
}

// Universal UObject/reflection noise present on nearly every class — not worth listing every time.
const NOISE_METHODS = [
  "acquire_editor_element_handle", "call_method", "cast", "get_class",
  "get_default_object", "get_fname", "get_full_name", "get_name",
  "get_outer", "get_outermost", "get_package", "get_path_name",
  "get_typed_outer", "get_world", "is_editor_property_overridden",
  "is_package_external", "modify", "rename", "reset_editor_property",
  "set_editor_properties", "set_editor_property", "get_editor_property",
  "static_class", "static_struct",
];

export function registerDiscoverPythonTools(server: McpServer): void {
  server.tool(
    "discover_python_class",
    "Introspect a unreal.* Python class: its description, C++ source location, editor properties " +
    "(name/type/Read-Only-or-Read-Write), and callable methods with their signatures. Use this " +
    "before writing a run_python script instead of guessing property or method names — UE's Python " +
    "bindings carry this metadata on every reflected class. Requires editor mode.",
    {
      className: z.string().describe(
        "Class name, with or without the 'unreal.' prefix (e.g. 'GroomBindingAsset' or 'unreal.GroomBindingAsset')"
      ),
    },
    async ({ className }) => {
      const err = await ensureUE();
      if (err) return text(err);

      const code = discoverScript(
        `
name = P["className"]
if name.startswith("unreal."):
    name = name[len("unreal."):]
cls = getattr(unreal, name, None)
if cls is None:
    raise Exception("No such unreal.%s. Use discover_python_search to find the right name." % name)

doc = cls.__doc__ or ""
summary = doc.split("**C++ Source:**")[0].strip().replace("\\r", "")

plugin = re.search(r"\\*\\*Plugin\\*\\*: (\\S+)", doc)
module = re.search(r"\\*\\*Module\\*\\*: (\\S+)", doc)
srcfile = re.search(r"\\*\\*File\\*\\*: (\\S+)", doc)

prop_names = set(re.findall(r"- \`\`([a-zA-Z0-9_]+)\`\`", doc))
props = []
for line in doc.split("\\n"):
    mm = re.match(r"- \`\`([a-zA-Z0-9_]+)\`\` \\(([^)]*)\\):\\s*\\[([^\\]]*)\\]\\s*(.*)", line.strip())
    if mm:
        props.append({"name": mm.group(1), "type": mm.group(2), "access": mm.group(3), "desc": mm.group(4).replace("\\r", "")})

noise = set(P["noise"])
methods = []
for m in sorted(dir(cls)):
    if m.startswith("_") or m in prop_names or m in noise:
        continue
    attr = getattr(cls, m, None)
    if not callable(attr):
        continue
    mdoc = (attr.__doc__ or "").strip().split("\\n")[0].replace("\\r", "")
    methods.append({"name": m, "signature": mdoc})

_emit({
    "ok": True, "class": name, "summary": summary,
    "plugin": plugin.group(1) if plugin else None,
    "module": module.group(1) if module else None,
    "sourceFile": srcfile.group(1) if srcfile else None,
    "properties": props, "methods": methods,
})
`,
        { className, noise: NOISE_METHODS }
      );

      const res = await execDiscover(code);
      if (!res.ok) return text(`Error: ${res.error}`);

      const props = (res.properties as { name: string; type: string; access: string; desc: string }[]) || [];
      const methods = (res.methods as { name: string; signature: string }[]) || [];

      const lines: string[] = [`unreal.${res.class}`, ""];
      if (res.summary) lines.push(String(res.summary), "");
      if (res.module) lines.push(`Module: ${res.module}${res.plugin ? ` (plugin: ${res.plugin})` : ""}`);
      if (res.sourceFile) lines.push(`Source: ${res.sourceFile}`);
      lines.push("");

      lines.push(`Editor properties (${props.length}) — read/set via get_editor_property/set_editor_property:`);
      if (props.length === 0) lines.push("  (none)");
      for (const p of props) {
        lines.push(`  ${p.name}  (${p.type})  [${p.access}]${p.desc ? `  — ${p.desc}` : ""}`);
      }
      lines.push("");

      lines.push(`Methods (${methods.length}):`);
      if (methods.length === 0) lines.push("  (none beyond standard UObject reflection methods)");
      for (const m of methods) lines.push(`  ${m.signature || m.name}`);

      return text(lines.join("\n"));
    }
  );

  server.tool(
    "discover_python_search",
    "Search unreal.* Python module names (classes, functions, enums) by substring — use this when " +
    "you don't know the exact class name to pass to discover_python_class. Case-insensitive. " +
    "Requires editor mode.",
    {
      query: z.string().describe("Substring to search for in unreal.* names (e.g. 'groom', 'niagara', 'mirror')"),
      limit: z.number().optional().describe("Max results to return details for (default 40)"),
    },
    async ({ query, limit }) => {
      const err = await ensureUE();
      if (err) return text(err);

      const code = discoverScript(
        `
q = P["query"].lower()
lim = int(P.get("limit") or 40)
matches = sorted([n for n in dir(unreal) if q in n.lower()])
total = len(matches)
out = []
for n in matches[:lim]:
    obj = getattr(unreal, n, None)
    kind = "class" if isinstance(obj, type) else ("function" if callable(obj) else "other")
    d = (obj.__doc__ or "").strip().split("\\n")[0].replace("\\r", "") if obj is not None else ""
    out.append({"name": n, "kind": kind, "summary": d})
_emit({"ok": True, "query": P["query"], "total": total, "shown": len(out), "matches": out})
`,
        { query, limit }
      );

      const res = await execDiscover(code);
      if (!res.ok) return text(`Error: ${res.error}`);

      const matches = (res.matches as { name: string; kind: string; summary: string }[]) || [];
      const lines: string[] = [`${res.total} match(es) for '${res.query}' in unreal.*${res.total !== res.shown ? ` (showing ${res.shown})` : ""}:`];
      for (const m of matches) {
        lines.push(`  unreal.${m.name}  [${m.kind}]${m.summary ? `  — ${m.summary}` : ""}`);
      }
      if (Number(res.total) === 0) {
        lines.push("", "No matches. Try a shorter or different substring.");
      } else {
        lines.push("", "Next step: discover_python_class on a class name above to see its properties/methods.");
      }

      return text(lines.join("\n"));
    }
  );
}
