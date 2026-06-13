import type { McpServer } from "@modelcontextprotocol/sdk/server/mcp.js";
import { z } from "zod";
import { ensureUE, uePost } from "../ue-bridge.js";

// CLAUDE-NOTE: run_python bridge tool. Pairs with the C++ /api/run-python endpoint
// (IPythonScriptPlugin). Gives the full reflected UE editor API, including the entire
// PCG framework, so features without a dedicated MCP tool can still be driven.
export function registerRunPythonTools(server: McpServer): void {
  server.tool(
    "run_python",
    "Execute Unreal Editor Python and return captured output. Exposes the full reflected editor API including the PCG framework (unreal.PCGGraph, PCGComponent, PCG node settings), EditorAssetLibrary, EditorActorSubsystem, etc. Requires editor mode. Use mode='eval' to return the value of a single expression.",
    {
      code: z.string().describe("Python source to run. Multi-line scripts OK (e.g. 'import unreal; print(unreal.SystemLibrary.get_engine_version())')."),
      mode: z.enum(["file", "eval"]).optional().describe("'file' (default) runs a full/multi-line script; 'eval' returns the value of a single expression."),
    },
    async ({ code, mode }) => {
      const err = await ensureUE();
      if (err) return { content: [{ type: "text" as const, text: err }] };

      const data = await uePost("/api/run-python", mode ? { code, mode } : { code });
      if (data.error) {
        return { content: [{ type: "text" as const, text: `Error: ${data.error}` }] };
      }

      const lines = [`Success: ${data.success}`];
      if (data.result) lines.push(`Result: ${data.result}`);
      lines.push(data.output ? `Output:\n${data.output}` : "(no output)");
      return { content: [{ type: "text" as const, text: lines.join("\n") }] };
    }
  );
}
