import { describe, it, expect } from "vitest";
import { z } from "zod";
import type { McpServer } from "@modelcontextprotocol/sdk/server/mcp.js";

import { registerReadTools } from "../../src/tools/read.js";
import { registerMutationTools } from "../../src/tools/mutation.js";
import { registerVariableTools } from "../../src/tools/variables.js";
import { registerParamTools } from "../../src/tools/params.js";
import { registerGraphTools } from "../../src/tools/graphs.js";
import { registerBuildGraphTools } from "../../src/tools/build-graph.js";
import { registerInterfaceTools } from "../../src/tools/interfaces.js";
import { registerDispatcherTools } from "../../src/tools/dispatchers.js";
import { registerComponentTools } from "../../src/tools/components.js";
import { registerSnapshotTools } from "../../src/tools/snapshot.js";
import { registerValidationTools } from "../../src/tools/validation.js";
import { registerUtilityTools } from "../../src/tools/utility.js";
import { registerDiscoveryTools } from "../../src/tools/discovery.js";
import { registerDiffBlueprintsTools } from "../../src/tools/diff-blueprints.js";
import { registerUserTypeTools } from "../../src/tools/user-types.js";
import { registerMaterialReadTools } from "../../src/tools/material-read.js";
import { registerMaterialMutationTools } from "../../src/tools/material-mutation.js";
import { registerAnimationTools } from "../../src/tools/animation-mutation.js";
import { registerLevelActorTools } from "../../src/tools/level-actors.js";
import { registerActorQueryTools } from "../../src/tools/actor-query.js";
import { registerSpatialTools } from "../../src/tools/spatial.js";
import { registerCameraTools } from "../../src/tools/camera.js";
import { registerViewModeTools } from "../../src/tools/view-mode.js";
import { registerPIERuntimeTools } from "../../src/tools/pie-runtime.js";
import { registerSublevelTools } from "../../src/tools/sublevels.js";
import { registerEditorUtilityTools } from "../../src/tools/editor-utils.js";
import { registerSelectionTools } from "../../src/tools/selection.js";
import { registerCVarTools } from "../../src/tools/cvars.js";
import { registerOutputLogTools } from "../../src/tools/output-log.js";
import { registerScreenshotTools } from "../../src/tools/screenshot.js";
import { registerPIELifecycleTools } from "../../src/tools/pie-lifecycle.js";
import { registerContentBrowserTools } from "../../src/tools/content-browser.js";
import { registerUndoRedoTools } from "../../src/tools/undo-redo.js";
import { registerWidgetTools } from "../../src/tools/widgets.js";
import { registerLevelTools } from "../../src/tools/level.js";
import { registerNiagaraTools } from "../../src/tools/niagara.js";
import { registerRunPythonTools } from "../../src/tools/run-python.js";
import { registerDiscoverPythonTools } from "../../src/tools/discover-python.js";
import { registerGroomTools } from "../../src/tools/groom.js";
import { registerMirrorTableTools } from "../../src/tools/mirror-table.js";
import { registerPcgTools } from "../../src/tools/pcg.js";
import { registerPcgAuthoringTools } from "../../src/tools/pcg-authoring.js";

// CLAUDE-NOTE: catches a bug class found 2026-07-23 while manually testing delete_asset's batch
// mode: 3 of the 4 batch-capable tools (delete_asset, connect_pins, change_function_parameter_type)
// had their single-mode fields as REQUIRED z.string() in the Zod schema, even though the handler
// logic correctly branched on batch vs single. Since the MCP SDK validates tool arguments against
// the schema before the handler runs, a batch-only call ({batch: [...]}) was rejected at the
// protocol layer with "expected string, received undefined" — the handler's own branching logic
// never even ran. set_pin_default already had the correct pattern (every single-mode field
// .optional()); this test generalizes that as an enforced invariant across every tool in the repo,
// the same way route-parity.test.ts generalizes route registration — so this can't silently
// regress in a newly added batch-capable tool either.
describe("batch-mode schema invariant", () => {
  const captured: { name: string; shape: Record<string, any> }[] = [];
  const fakeServer = {
    tool(name: string, _description: string, shape: Record<string, any>, _handler: (...args: any[]) => any) {
      captured.push({ name, shape });
    },
    resource() {},
  } as unknown as McpServer;

  registerReadTools(fakeServer);
  registerMutationTools(fakeServer);
  registerVariableTools(fakeServer);
  registerParamTools(fakeServer);
  registerGraphTools(fakeServer);
  registerBuildGraphTools(fakeServer);
  registerInterfaceTools(fakeServer);
  registerDispatcherTools(fakeServer);
  registerComponentTools(fakeServer);
  registerSnapshotTools(fakeServer);
  registerValidationTools(fakeServer);
  registerUtilityTools(fakeServer);
  registerDiscoveryTools(fakeServer);
  registerDiffBlueprintsTools(fakeServer);
  registerUserTypeTools(fakeServer);
  registerMaterialReadTools(fakeServer);
  registerMaterialMutationTools(fakeServer);
  registerAnimationTools(fakeServer);
  registerLevelActorTools(fakeServer);
  registerActorQueryTools(fakeServer);
  registerSpatialTools(fakeServer);
  registerCameraTools(fakeServer);
  registerViewModeTools(fakeServer);
  registerPIERuntimeTools(fakeServer);
  registerSublevelTools(fakeServer);
  registerEditorUtilityTools(fakeServer);
  registerSelectionTools(fakeServer);
  registerCVarTools(fakeServer);
  registerOutputLogTools(fakeServer);
  registerScreenshotTools(fakeServer);
  registerPIELifecycleTools(fakeServer);
  registerContentBrowserTools(fakeServer);
  registerUndoRedoTools(fakeServer);
  registerWidgetTools(fakeServer);
  registerLevelTools(fakeServer);
  registerNiagaraTools(fakeServer);
  registerRunPythonTools(fakeServer);
  registerDiscoverPythonTools(fakeServer);
  registerGroomTools(fakeServer);
  registerMirrorTableTools(fakeServer);
  registerPcgTools(fakeServer);
  registerPcgAuthoringTools(fakeServer);

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
