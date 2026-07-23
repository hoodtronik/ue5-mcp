import type { McpServer } from "@modelcontextprotocol/sdk/server/mcp.js";
import { ListToolsRequestSchema } from "@modelcontextprotocol/sdk/types.js";
import { z } from "zod";

// CLAUDE-NOTE: Opt-in tool discovery / search mode (Epic 5.8 meta-tool concept,
// reimplemented purely at the MCP transport layer — no C++, no UE API, UE 5.6 safe).
//
// With ~196 tools, returning the full list on every tools/list bloats the LLM context.
// When MCP_DISCOVERY_MODE=true, tools/list returns ONLY three meta-tools
// (search_tools, list_tool_categories, describe_category). The real tools remain
// registered and fully CALLABLE by name (tools/call looks them up directly) — they are
// merely hidden from the listing. The agent uses the meta-tools to find the tool + its
// schema, then calls it directly.
//
// When the env var is unset/false (DEFAULT), this module does nothing and behavior is
// exactly as before — all tools listed normally. Purely additive and opt-in.

const META_NAMES = ["search_tools", "list_tool_categories", "describe_category"] as const;
const META_SET = new Set<string>(META_NAMES);

// Canonical category order (matches the Phase 2 spec).
const CATEGORY_ORDER = [
  "Blueprints",
  "Materials",
  "Levels",
  "PCG",
  "Animation",
  "Niagara",
  "Widgets",
  "Editor",
  "PIE",
] as const;

export function isDiscoveryMode(): boolean {
  return (process.env.MCP_DISCOVERY_MODE || "").toLowerCase() === "true";
}

/** Heuristic category assignment from a tool name (most specific rules first). */
function categorize(name: string): string {
  const n = name.toLowerCase();
  const has = (...keys: string[]) => keys.some((k) => n.includes(k));

  if (has("pcg")) return "PCG";
  if (has("niagara", "emitter")) return "Niagara";
  if (has("material")) return "Materials";
  if (has("anim", "blend_space", "state_machine", "transition", "mirror_table", "sync_group")) return "Animation";
  if (has("widget")) return "Widgets";
  if (has("pie")) return "PIE";
  if (has("actor", "level", "spawn", "sublevel", "raycast", "selection", "camera", "viewport", "view_mode", "focus"))
    return "Levels";
  if (
    has(
      "cvar",
      "screenshot",
      "output_log",
      "undo",
      "redo",
      "transaction",
      "content_browser",
      "save_all",
      "exec",
      "python",
      "server",
      "rescan",
      "notification",
      "dirty",
      "open_asset"
    )
  )
    return "Editor";
  return "Blueprints";
}

interface CatalogEntry {
  name: string;
  description: string;
  category: string;
  schema: unknown;
}

let catalogCache: CatalogEntry[] | null = null;

function toJsonSchema(zodSchema: unknown): unknown {
  if (!zodSchema) return { type: "object" };
  try {
    // zod v4 native JSON Schema conversion.
    return z.toJSONSchema(zodSchema as z.ZodType);
  } catch {
    return { type: "object" };
  }
}

/** Build (once) a catalog of all real (non-meta) registered tools. */
function getCatalog(server: McpServer): CatalogEntry[] {
  if (catalogCache) return catalogCache;
  const registered = (server as unknown as { _registeredTools?: Record<string, { description?: string; inputSchema?: unknown; enabled?: boolean }> })
    ._registeredTools || {};
  const out: CatalogEntry[] = [];
  for (const [name, tool] of Object.entries(registered)) {
    if (META_SET.has(name)) continue;
    if (tool.enabled === false) continue;
    out.push({
      name,
      description: tool.description || "",
      category: categorize(name),
      schema: toJsonSchema(tool.inputSchema),
    });
  }
  out.sort((a, b) => a.name.localeCompare(b.name));
  catalogCache = out;
  return out;
}

function formatEntry(e: CatalogEntry, includeSchema: boolean): string {
  const head = `  ${e.name}  [${e.category}]\n      ${e.description}`;
  if (!includeSchema) return head;
  return `${head}\n      schema: ${JSON.stringify(e.schema)}`;
}

function textResult(t: string) {
  return { content: [{ type: "text" as const, text: t }] };
}

/**
 * Enable discovery mode if MCP_DISCOVERY_MODE=true. Must be called AFTER all real tools
 * are registered. Returns true if discovery mode was enabled.
 */
export function registerDiscoveryMode(server: McpServer): boolean {
  if (!isDiscoveryMode()) return false;

  // --- Meta-tool: search_tools ---------------------------------------------
  server.tool(
    "search_tools",
    "Search the full Unreal tool library by keyword. Discovery mode is ON, so tools/list shows only these meta-tools — use this to find the specific tool you need, then CALL IT DIRECTLY BY NAME (hidden tools are still callable). Returns matching tool names, categories, descriptions, and full input schemas.",
    {
      query: z.string().describe("Keyword(s) to match against tool name and description, e.g. 'PCG graph' or 'material instance'"),
      limit: z.number().int().positive().optional().describe("Max results to return (default 40)"),
    },
    async ({ query, limit }) => {
      const cap = limit ?? 40;
      const terms = query.toLowerCase().split(/\s+/).filter(Boolean);
      const catalog = getCatalog(server);
      const scored = catalog
        .map((e) => {
          const hay = `${e.name} ${e.description}`.toLowerCase();
          const score = terms.reduce((s, t) => s + (hay.includes(t) ? 1 : 0), 0);
          return { e, score };
        })
        .filter((x) => x.score > 0)
        .sort((a, b) => b.score - a.score || a.e.name.localeCompare(b.e.name));

      if (scored.length === 0) {
        return textResult(`No tools matched "${query}". Try list_tool_categories to browse, or broaden the query.`);
      }
      const shown = scored.slice(0, cap);
      const lines = [`${shown.length} of ${scored.length} match(es) for "${query}":`, ""];
      for (const { e } of shown) lines.push(formatEntry(e, true), "");
      if (scored.length > shown.length) lines.push(`… ${scored.length - shown.length} more (raise 'limit' to see).`);
      return textResult(lines.join("\n"));
    }
  );

  // --- Meta-tool: list_tool_categories -------------------------------------
  server.tool(
    "list_tool_categories",
    "List the tool categories in the Unreal MCP server with a tool count for each. Use describe_category to see all tools in a category.",
    {},
    async () => {
      const catalog = getCatalog(server);
      const counts = new Map<string, number>();
      for (const e of catalog) counts.set(e.category, (counts.get(e.category) || 0) + 1);
      // canonical order first, then any extras
      const ordered = [
        ...CATEGORY_ORDER.filter((c) => counts.has(c)),
        ...[...counts.keys()].filter((c) => !CATEGORY_ORDER.includes(c as (typeof CATEGORY_ORDER)[number])).sort(),
      ];
      const lines = [`Tool categories (${catalog.length} tools total):`, ""];
      for (const c of ordered) lines.push(`  ${c}  (${counts.get(c)})`);
      lines.push("", "Use describe_category {category} to list a category's tools and schemas.");
      return textResult(lines.join("\n"));
    }
  );

  // --- Meta-tool: describe_category ----------------------------------------
  server.tool(
    "describe_category",
    "List all tools in a given category with their descriptions and full input schemas. Get valid category names from list_tool_categories.",
    {
      category: z.string().describe("Category name, e.g. 'PCG', 'Materials', 'Blueprints'"),
    },
    async ({ category }) => {
      const catalog = getCatalog(server);
      const want = category.trim().toLowerCase();
      const matches = catalog.filter((e) => e.category.toLowerCase() === want);
      if (matches.length === 0) {
        const cats = [...new Set(catalog.map((e) => e.category))].sort().join(", ");
        return textResult(`Unknown category "${category}". Available: ${cats}`);
      }
      const lines = [`${matches.length} tool(s) in category '${matches[0].category}':`, ""];
      for (const e of matches) lines.push(formatEntry(e, true), "");
      return textResult(lines.join("\n"));
    }
  );

  // --- Override tools/list to expose ONLY the meta-tools --------------------
  // Real tools stay registered + enabled, so tools/call still reaches them by name.
  const metaListing = {
    tools: [
      {
        name: "search_tools",
        description:
          "Search the full Unreal tool library by keyword; returns matching tools with their full schemas. Call the found tool directly by name.",
        inputSchema: {
          type: "object",
          properties: {
            query: { type: "string", description: "Keyword(s) to match against tool name and description" },
            limit: { type: "number", description: "Max results (default 40)" },
          },
          required: ["query"],
        },
      },
      {
        name: "list_tool_categories",
        description: "List tool categories with counts.",
        inputSchema: { type: "object", properties: {} },
      },
      {
        name: "describe_category",
        description: "List all tools in a category with descriptions and full schemas.",
        inputSchema: {
          type: "object",
          properties: { category: { type: "string", description: "Category name from list_tool_categories" } },
          required: ["category"],
        },
      },
    ],
  };

  server.server.setRequestHandler(ListToolsRequestSchema, async () => metaListing);

  console.error(
    "[BlueprintMCP] MCP_DISCOVERY_MODE=true — tools/list exposes only search_tools / list_tool_categories / describe_category. All other tools remain callable by name."
  );
  return true;
}
