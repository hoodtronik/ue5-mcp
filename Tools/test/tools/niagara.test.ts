import { describe, it, expect, beforeAll, afterAll } from "vitest";
import { uePost, ueGet, deleteTestBlueprint, uniqueName } from "../helpers.js";

// CLAUDE-NOTE: niagara.ts shipped 20 MCP tools with no test coverage at all — the C++ backend
// (BlueprintMCPHandlers_Niagara.cpp) lived unmerged on a feature branch for months while the
// TypeScript called endpoints that 404'd. These tests were written from a live UE 5.6 editor
// session that walked the whole authoring chain, so every assertion here reflects verified
// real behaviour rather than assumed API shape.
describe("niagara", () => {
  const sysName = uniqueName("NS_Test");
  const emiName = uniqueName("NE_Test");
  const packagePath = "/Game/Test";
  const sysPath = `${packagePath}/${sysName}`;
  const emiPath = `${packagePath}/${emiName}`;

  // Discovered at runtime — module GUIDs are assigned on insert.
  let colorModuleGuid = "";

  beforeAll(async () => {
    const sys = await uePost("/api/create-niagara-system", { name: sysName, packagePath });
    expect(sys.error).toBeUndefined();
    const emi = await uePost("/api/create-niagara-emitter", { name: emiName, packagePath });
    expect(emi.error).toBeUndefined();
  });

  afterAll(async () => {
    await deleteTestBlueprint(sysPath);
    await deleteTestBlueprint(emiPath);
  });

  it("creates a system with a resolvable asset path", async () => {
    const data = await uePost("/api/create-niagara-system", {
      name: uniqueName("NS_Throwaway"), packagePath,
    });
    expect(data.error).toBeUndefined();
    expect(data.assetPath).toContain(packagePath);
    expect(data.saved).toBe(true);
    await deleteTestBlueprint(data.assetPath);
  });

  it("lists systems including the one just created", async () => {
    const data = await ueGet("/api/list-niagara-systems", {});
    expect(data.error).toBeUndefined();
    expect(data.count).toBeGreaterThan(0);
    expect(data.systems.some((s: any) => s.name === sysName)).toBe(true);
  });

  it("creates an emitter defaulting to CPU sim", async () => {
    const name = uniqueName("NE_Throwaway");
    const data = await uePost("/api/create-niagara-emitter", { name, packagePath });
    expect(data.error).toBeUndefined();
    expect(data.simTarget).toBe("CPU");
    expect(data.saved).toBe(true);
    await deleteTestBlueprint(data.assetPath);
  });

  it("attaches an emitter to a system and returns a handle", async () => {
    const data = await uePost("/api/add-emitter-to-system", {
      system: sysPath, emitter: emiPath, handleName: "MainBurst",
    });
    expect(data.error).toBeUndefined();
    expect(data.handleId).toBeTruthy();
    expect(data.handleName).toBe("MainBurst");
    expect(data.emitterCount).toBe(1);
  });

  it("summarises the system with its emitter handle", async () => {
    const data = await uePost("/api/get-niagara-system-summary", { system: sysPath });
    expect(data.error).toBeUndefined();
    expect(data.emitterCount).toBe(1);
    expect(data.emitters[0].handleName).toBe("MainBurst");
    expect(data.emitters[0].enabled).toBe(true);
    expect(data.fixedBounds).toBeDefined();
  });

  it("lists the engine module library for a stage", async () => {
    const data = await ueGet("/api/list-module-library", { stage: "ParticleSpawn" });
    expect(data.error).toBeUndefined();
    expect(data.count).toBeGreaterThan(0);
    // The stock Spawn location module ships with the engine.
    expect(data.modules.some((m: any) => m.name === "SystemLocation")).toBe(true);
  });

  it("lists the modules already on a new emitter's spawn stage", async () => {
    const data = await uePost("/api/list-emitter-modules", { emitter: emiPath, stage: "ParticleSpawn" });
    expect(data.error).toBeUndefined();
    expect(data.count).toBeGreaterThan(0);
    for (const m of data.modules) {
      expect(m.nodeGuid).toBeTruthy();
      expect(m.stage).toBe("ParticleSpawn");
    }
  });

  it("adds a module to a stage and returns its node GUID", async () => {
    const data = await uePost("/api/add-niagara-module", {
      emitter: emiPath, stage: "ParticleUpdate",
      moduleScript: "/Niagara/Modules/Update/Color/Color.Color",
    });
    expect(data.error).toBeUndefined();
    expect(data.moduleNodeGuid).toBeTruthy();
    expect(data.saved).toBe(true);
    colorModuleGuid = data.moduleNodeGuid;

    // ...and it shows up in the stack.
    const stack = await uePost("/api/list-emitter-modules", { emitter: emiPath, stage: "ParticleUpdate" });
    expect(stack.modules.some((m: any) => m.nodeGuid === colorModuleGuid)).toBe(true);
  });

  it("lists the inputs of an added module", async () => {
    expect(colorModuleGuid).toBeTruthy();
    const data = await uePost("/api/list-module-inputs", {
      emitter: emiPath, stage: "ParticleUpdate", moduleNodeGuid: colorModuleGuid,
    });
    expect(data.error).toBeUndefined();
    const color = data.inputs.find((i: any) => i.input === "Color");
    expect(color).toBeDefined();
    expect(color.niagaraType).toBe("LinearColor");
    expect(color.settable).toBe(true);
  });

  it("sets a local module input value", async () => {
    expect(colorModuleGuid).toBeTruthy();
    const data = await uePost("/api/set-module-input", {
      emitter: emiPath, stage: "ParticleUpdate", moduleNodeGuid: colorModuleGuid,
      input: "Color", type: "LinearColor", valueMode: "local", value: [1, 0, 0, 1],
    });
    expect(data.error).toBeUndefined();
    expect(data.pinDefaultValue).toContain("R=1.000000");
    expect(data.saved).toBe(true);
  });

  // CLAUDE-NOTE: vector/color inputs require `value` as a numeric ARRAY. Passing "1,0,0,1" as a
  // string fails with a clear message — asserted here so the contract stays enforced.
  it("rejects a comma-string where a numeric array is required", async () => {
    expect(colorModuleGuid).toBeTruthy();
    const data = await uePost("/api/set-module-input", {
      emitter: emiPath, stage: "ParticleUpdate", moduleNodeGuid: colorModuleGuid,
      input: "Color", type: "LinearColor", valueMode: "local", value: "1,0,0,1",
    });
    expect(data.error).toBeDefined();
    expect(data.error).toContain("numeric array");
  });

  it("adds a user parameter, namespaced under User.", async () => {
    const data = await uePost("/api/add-user-parameter", {
      system: sysPath, name: "BurstColor", type: "LinearColor",
    });
    expect(data.error).toBeUndefined();
    expect(data.name).toBe("User.BurstColor");
    expect(data.added).toBe(true);

    const summary = await uePost("/api/get-niagara-system-summary", { system: sysPath });
    expect(summary.userParameters.some((p: any) => p.name === "User.BurstColor")).toBe(true);
  });

  it("links a module input to a user parameter", async () => {
    expect(colorModuleGuid).toBeTruthy();
    const data = await uePost("/api/set-module-input", {
      emitter: emiPath, stage: "ParticleUpdate", moduleNodeGuid: colorModuleGuid,
      input: "Color", type: "LinearColor", valueMode: "link",
      linkedParameter: "User.BurstColor",
    });
    expect(data.error).toBeUndefined();
    expect(data.valueMode).toBe("link");
    expect(data.linkedParameter).toBe("User.BurstColor");
  });

  it("returns an error for a non-existent system", async () => {
    const data = await uePost("/api/get-niagara-system-summary", { system: "/Game/Test/NS_Nope_XYZ_999" });
    expect(data.error).toBeDefined();
  });

  it("rejects missing required fields", async () => {
    const data = await uePost("/api/create-niagara-system", {});
    expect(data.error).toBeDefined();
  });
});
