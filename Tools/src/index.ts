import { McpServer } from "@modelcontextprotocol/sdk/server/mcp.js";
import { StdioServerTransport } from "@modelcontextprotocol/sdk/server/stdio.js";
import { getUEHealth, gracefulShutdown, state } from "./ue-bridge.js";

import { registerReadTools } from "./tools/read.js";
import { registerMutationTools } from "./tools/mutation.js";
import { registerVariableTools } from "./tools/variables.js";
import { registerParamTools } from "./tools/params.js";
import { registerGraphTools } from "./tools/graphs.js";
import { registerInterfaceTools } from "./tools/interfaces.js";
import { registerDispatcherTools } from "./tools/dispatchers.js";
import { registerComponentTools } from "./tools/components.js";
import { registerSnapshotTools } from "./tools/snapshot.js";
import { registerValidationTools } from "./tools/validation.js";
import { registerUtilityTools } from "./tools/utility.js";
import { registerDiscoveryTools } from "./tools/discovery.js";
import { registerDiffBlueprintsTools } from "./tools/diff-blueprints.js";
import { registerUserTypeTools } from "./tools/user-types.js";
import { registerMaterialReadTools } from "./tools/material-read.js";
import { registerMaterialMutationTools } from "./tools/material-mutation.js";
import { registerAnimationTools } from "./tools/animation-mutation.js";
import { registerLevelActorTools } from "./tools/level-actors.js";
import { registerActorQueryTools } from "./tools/actor-query.js";
import { registerSpatialTools } from "./tools/spatial.js";
import { registerCameraTools } from "./tools/camera.js";
import { registerViewModeTools } from "./tools/view-mode.js";
import { registerPIERuntimeTools } from "./tools/pie-runtime.js";
import { registerSublevelTools } from "./tools/sublevels.js";
import { registerEditorUtilityTools } from "./tools/editor-utils.js";
import { registerSelectionTools } from "./tools/selection.js";
import { registerCVarTools } from "./tools/cvars.js";
import { registerOutputLogTools } from "./tools/output-log.js";
import { registerScreenshotTools } from "./tools/screenshot.js";
import { registerPIELifecycleTools } from "./tools/pie-lifecycle.js";
import { registerContentBrowserTools } from "./tools/content-browser.js";
import { registerUndoRedoTools } from "./tools/undo-redo.js";
import { registerWidgetTools } from "./tools/widgets.js";
import { registerLevelTools } from "./tools/level.js";
import { registerNiagaraTools } from "./tools/niagara.js";
import { registerRunPythonTools } from "./tools/run-python.js";
// CLAUDE-NOTE: merged back — Groom is this repo's own toolset (has C++ backing); the global server
// sync didn't include it. Keep it alongside Niagara so no tools are lost.
import { registerGroomTools } from "./tools/groom.js";
import { registerPcgTools } from "./tools/pcg.js";

import { registerBlueprintListResource } from "./resources/blueprint-list.js";
import { registerWorkflowRecipesResource } from "./resources/workflow-recipes.js";
import { registerSkills } from "./skills/index.js";
import { registerExamples } from "./examples/index.js";
import { registerDiscoveryMode } from "./discovery/index.js";

const server = new McpServer({ name: "blueprint-mcp", version: "1.0.0" });

registerReadTools(server);
registerMutationTools(server);
registerVariableTools(server);
registerParamTools(server);
registerGraphTools(server);
registerInterfaceTools(server);
registerDispatcherTools(server);
registerComponentTools(server);
registerSnapshotTools(server);
registerValidationTools(server);
registerUtilityTools(server);
registerDiscoveryTools(server);
registerDiffBlueprintsTools(server);
registerUserTypeTools(server);
registerMaterialReadTools(server);
registerMaterialMutationTools(server);
registerAnimationTools(server);
registerLevelActorTools(server);
registerActorQueryTools(server);
registerSpatialTools(server);
registerCameraTools(server);
registerViewModeTools(server);
registerPIERuntimeTools(server);
registerSublevelTools(server);
registerEditorUtilityTools(server);
registerSelectionTools(server);
registerCVarTools(server);
registerOutputLogTools(server);
registerScreenshotTools(server);
registerPIELifecycleTools(server);
registerContentBrowserTools(server);
registerUndoRedoTools(server);
registerWidgetTools(server);
registerLevelTools(server);
registerNiagaraTools(server);
registerRunPythonTools(server);
registerGroomTools(server);
registerPcgTools(server);

registerBlueprintListResource(server);
registerWorkflowRecipesResource(server);
registerSkills(server);
registerExamples(server);

// Opt-in (MCP_DISCOVERY_MODE=true). Must run LAST — after every tool is registered —
// so the catalog sees them. No-op when the env var is unset (default behavior).
registerDiscoveryMode(server);

process.on("exit", () => { if (!state.editorMode) state.ueProcess?.kill(); });
for (const sig of ["SIGINT", "SIGTERM"] as const) {
  process.on(sig, async () => {
    if (!state.editorMode && state.ueProcess) await gracefulShutdown();
    process.exit();
  });
}

async function main() {
  const health = await getUEHealth();
  if (health) {
    state.editorMode = health.mode === "editor";
    console.error(`Connected to UE5 ${health.mode} \u2014 MCP server already running.`);
  } else {
    state.editorMode = false;
    console.error("UE5 server not detected. Commandlet will be spawned on first tool call.");
  }
  const transport = new StdioServerTransport();
  await server.connect(transport);
}

main().catch((err) => { console.error("Fatal error:", err); process.exit(1); });
