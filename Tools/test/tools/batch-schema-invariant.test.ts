import { describe, it, expect } from "vitest";
import { z } from "zod";
import type { McpServer } from "@modelcontextprotocol/sdk/server/mcp.js";
import { TOOL_REGISTRATIONS } from "../../src/tool-registry.js";

// CLAUDE-NOTE: catches a bug class found 2026-07-23 while manually testing delete_asset's batch
// mode: 4 batch-capable tools (delete_asset, connect_pins, change_function_parameter_type,
// change_variable_type) had their single-mode fields as REQUIRED z.string() in the Zod schema,
// even though the handler logic correctly branched on batch vs single. Since the MCP SDK validates
// tool arguments against the schema before the handler runs, a batch-only call ({batch: [...]})
// was rejected at the protocol layer with "expected string, received undefined" — the handler's
// own branching logic never even ran. set_pin_default already had the correct pattern (every
// single-mode field .optional()); this test generalizes that as an enforced invariant across every
// tool in the repo, the same way route-parity.test.ts generalizes route registration.
describe("batch-mode schema invariant", () => {
  const captured: { name: string; shape: Record<string, any> }[] = [];
  const fakeServer = {
    tool(name: string, _description: string, shape: Record<string, any>, _handler: (...args: any[]) => any) {
      captured.push({ name, shape });
    },
    resource() {},
  } as unknown as McpServer;

  for (const { register } of TOOL_REGISTRATIONS) register(fakeServer);

  const batchTools = captured.filter((t) => "batch" in t.shape);

  it("found batch-capable tools to check (sanity check the test itself isn't a no-op)", () => {
    expect(batchTools.length).toBeGreaterThanOrEqual(4);
  });

  it.each(batchTools.map((t) => [t.name, t] as const))(
    "%s: a batch-only call passes schema validation",
    (_name, tool) => {
      const schema = z.object(tool.shape);
      const result = schema.safeParse({ batch: [] });
      expect(result.success).toBe(true);
    }
  );
});
