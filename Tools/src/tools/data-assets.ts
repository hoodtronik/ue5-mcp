import type { McpServer } from "@modelcontextprotocol/sdk/server/mcp.js";
import { z } from "zod";
import { ensureUE, uePost } from "../ue-bridge.js";

// CLAUDE-NOTE: asset creation beyond Blueprints (github.com/mirno-ehf/ue5-mcp#26). Material
// creation already shipped in PRs #49/#50; this covers DataTable/CurveTable/generic DataAsset.

export function registerDataAssetTools(server: McpServer): void {
  server.tool(
    "create_data_table",
    "Create a new DataTable asset with a given row struct. The row struct can be a native C++ USTRUCT (e.g. 'FMyRow') or a Blueprint UserDefinedStruct made with create_struct. Use list_mirror_table_rows-style row tools via run_python for row data, or edit rows in the editor after creation.",
    {
      assetPath: z.string().describe("Full asset path (e.g. '/Game/Data/DT_Items')"),
      rowStruct: z.string().describe("Row struct name (e.g. 'FMyRow', or the name of a UserDefinedStruct asset)"),
    },
    async ({ assetPath, rowStruct }) => {
      const err = await ensureUE();
      if (err) return { content: [{ type: "text" as const, text: err }] };

      const data = await uePost("/api/create-data-table", { assetPath, rowStruct });
      if (data.error) return { content: [{ type: "text" as const, text: `Error: ${data.error}` }] };

      const lines: string[] = [];
      lines.push(`DataTable created successfully.`);
      lines.push(`Path: ${data.assetPath}`);
      lines.push(`Row struct: ${data.rowStruct}`);
      lines.push(`Saved: ${data.saved}`);

      return { content: [{ type: "text" as const, text: lines.join("\n") }] };
    }
  );

  server.tool(
    "create_curve_table",
    "Create a new CurveTable asset — a table of named runtime curves, useful for damage falloff, difficulty scaling, etc.",
    {
      assetPath: z.string().describe("Full asset path (e.g. '/Game/Data/CT_DamageFalloff')"),
    },
    async ({ assetPath }) => {
      const err = await ensureUE();
      if (err) return { content: [{ type: "text" as const, text: err }] };

      const data = await uePost("/api/create-curve-table", { assetPath });
      if (data.error) return { content: [{ type: "text" as const, text: `Error: ${data.error}` }] };

      const lines: string[] = [];
      lines.push(`CurveTable created successfully.`);
      lines.push(`Path: ${data.assetPath}`);
      lines.push(`Saved: ${data.saved}`);

      return { content: [{ type: "text" as const, text: lines.join("\n") }] };
    }
  );

  server.tool(
    "create_data_asset",
    "Create a new instance of a UDataAsset subclass (e.g. PrimaryDataAsset or a project-specific DataAsset class). Useful for shared configuration/reference data. Note: the class must be constructible with no extra arguments — some custom DataAsset subclasses with required constructor parameters won't work via this generic path.",
    {
      assetPath: z.string().describe("Full asset path (e.g. '/Game/Data/DA_ItemConfig')"),
      dataAssetClass: z.string().describe("UDataAsset subclass name — native (e.g. 'PrimaryDataAsset') or a Blueprint class"),
    },
    async ({ assetPath, dataAssetClass }) => {
      const err = await ensureUE();
      if (err) return { content: [{ type: "text" as const, text: err }] };

      const data = await uePost("/api/create-data-asset", { assetPath, dataAssetClass });
      if (data.error) return { content: [{ type: "text" as const, text: `Error: ${data.error}` }] };

      const lines: string[] = [];
      lines.push(`DataAsset created successfully.`);
      lines.push(`Path: ${data.assetPath}`);
      lines.push(`Class: ${data.dataAssetClass}`);
      lines.push(`Saved: ${data.saved}`);

      return { content: [{ type: "text" as const, text: lines.join("\n") }] };
    }
  );
}
