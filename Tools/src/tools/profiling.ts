import type { McpServer } from "@modelcontextprotocol/sdk/server/mcp.js";
import { ensureUE, ueGet } from "../ue-bridge.js";

// CLAUDE-NOTE: minimal frame-timing snapshot (scoped-down "performance/profiling service"
// backlog item). Deliberately NOT a full profiling service — single-frame read of the engine's
// existing thread timers, no sampling/aggregation/history. See BlueprintMCPHandlers_Profiling.cpp.

export function registerProfilingTools(server: McpServer): void {
  server.tool(
    "get_frame_timing",
    "Get a single-frame snapshot of engine thread timings (game thread, render thread, RHI thread, swap buffer) in milliseconds. Useful as a quick sanity check for obvious perf problems, not a substitute for a real profiler (Unreal Insights). In commandlet/headless mode most values will read as 0 — that's expected, not an error.",
    {},
    async () => {
      const err = await ensureUE();
      if (err) return { content: [{ type: "text" as const, text: err }] };

      const data = await ueGet("/api/get-frame-timing", {});
      if (data.error) return { content: [{ type: "text" as const, text: `Error: ${data.error}` }] };

      const lines: string[] = [];
      lines.push(`Frame timing snapshot (ms):`);
      lines.push(`  Game thread: ${data.gameThreadMs} (wait: ${data.gameThreadWaitMs})`);
      lines.push(`  Render thread: ${data.renderThreadMs} (wait: ${data.renderThreadWaitMs})`);
      lines.push(`  RHI thread: ${data.rhiThreadMs}`);
      lines.push(`  Swap buffer: ${data.swapBufferMs}`);
      lines.push(`  Game thread critical path: ${data.gameThreadCriticalPathMs}`);
      lines.push(`  Render thread critical path: ${data.renderThreadCriticalPathMs}`);
      if (data.note) lines.push(`\nNote: ${data.note}`);

      return { content: [{ type: "text" as const, text: lines.join("\n") }] };
    }
  );
}
