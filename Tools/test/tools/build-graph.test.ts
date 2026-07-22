import { describe, it, expect, beforeAll, afterAll } from "vitest";
import { uePost, createTestBlueprint, deleteTestBlueprint, uniqueName } from "../helpers.js";

describe("build_graph", () => {
  const bpName = uniqueName("BP_BuildGraphTest");
  const packagePath = "/Game/Test";

  beforeAll(async () => {
    const res = await createTestBlueprint({ name: bpName });
    expect(res.error).toBeUndefined();
  });

  afterAll(async () => {
    await deleteTestBlueprint(`${packagePath}/${bpName}`);
  });

  it("creates nodes, wires them, and sets a pin default in one call", async () => {
    const data = await uePost("/api/build-graph", {
      blueprint: bpName,
      graph: "EventGraph",
      nodes: [
        { ref: "Begin", nodeType: "OverrideEvent", functionName: "ReceiveBeginPlay" },
        { ref: "Print", nodeType: "CallFunction", functionName: "PrintString" },
      ],
      connections: [{ from: "Begin.then", to: "Print.execute" }],
      pinDefaults: [{ nodeRef: "Print", pinName: "InString", value: "hello from build_graph" }],
    });

    expect(data.error).toBeUndefined();
    expect(data.nodesCreated).toBe(2);
    expect(data.nodesFailed).toBe(0);
    expect(data.connectionsMade).toBe(1);
    expect(data.connectionsFailed).toBe(0);
    expect(data.pinDefaultsSet).toBe(1);
    expect(data.success).toBe(true);
    expect(data.saved).toBe(true);

    // Every created node reports a usable GUID.
    for (const n of data.nodes) {
      expect(n.success).toBe(true);
      expect(n.nodeId).toBeTruthy();
    }
    expect(data.pinDefaults[0].newValue).toBe("hello from build_graph");
  });

  it("resolves pin aliases (then/execute) to real pin names", async () => {
    const data = await uePost("/api/build-graph", {
      blueprint: bpName,
      graph: "EventGraph",
      nodes: [
        { ref: "A", nodeType: "CallFunction", functionName: "PrintString", posX: 0, posY: 400 },
        { ref: "B", nodeType: "CallFunction", functionName: "PrintString", posX: 400, posY: 400 },
      ],
      connections: [{ from: "A.then", to: "B.execute" }],
    });
    expect(data.error).toBeUndefined();
    expect(data.connectionsMade).toBe(1);
    // The alias must have resolved to the schema's real pin names.
    expect(data.connections[0].fromPin).toBeTruthy();
    expect(data.connections[0].toPin).toBeTruthy();
  });

  it("wires a new node onto an existing node by GUID", async () => {
    const first = await uePost("/api/add-node", {
      blueprint: bpName,
      graph: "EventGraph",
      nodeType: "CallFunction",
      functionName: "PrintString",
      posX: 0,
      posY: 800,
    });
    expect(first.error).toBeUndefined();
    const existingId = first.nodeId;

    const data = await uePost("/api/build-graph", {
      blueprint: bpName,
      graph: "EventGraph",
      nodes: [{ ref: "New", nodeType: "CallFunction", functionName: "PrintString", posX: 400, posY: 800 }],
      connections: [{ from: `${existingId}.then`, to: "New.execute" }],
    });
    expect(data.error).toBeUndefined();
    expect(data.connectionsMade).toBe(1);
    expect(data.connectionsFailed).toBe(0);
  });

  it("reports per-item failures without discarding the successful items", async () => {
    const data = await uePost("/api/build-graph", {
      blueprint: bpName,
      graph: "EventGraph",
      nodes: [
        { ref: "Good", nodeType: "CallFunction", functionName: "PrintString", posX: 0, posY: 1200 },
        { ref: "Bad", nodeType: "NoSuchNodeType" },
      ],
      connections: [{ from: "Good.then", to: "Nonexistent.execute" }],
    });

    expect(data.error).toBeUndefined();
    // The good node still applied; the bad one is reported individually.
    expect(data.nodesCreated).toBe(1);
    expect(data.nodesFailed).toBe(1);
    expect(data.connectionsFailed).toBe(1);
    expect(data.success).toBe(false);
    expect(data.partial).toBe(true);

    const bad = data.nodes.find((n: any) => n.ref === "Bad");
    expect(bad.success).toBe(false);
    expect(bad.error).toBeTruthy();
    expect(data.connections[0].error).toContain("Nonexistent");
  });

  it("dryRun validates without modifying the Blueprint", async () => {
    const data = await uePost("/api/build-graph", {
      blueprint: bpName,
      graph: "EventGraph",
      nodes: [{ ref: "X", nodeType: "CallFunction", functionName: "PrintString" }],
      connections: [{ from: "X.then", to: "X.execute" }],
      dryRun: true,
    });
    expect(data.error).toBeUndefined();
    expect(data.dryRun).toBe(true);
    expect(data.success).toBe(true);
    expect(data.nodeCount).toBe(1);
    expect(data.problems).toEqual([]);
    // Nothing was created.
    expect(data.nodesCreated).toBeUndefined();
  });

  it("dryRun reports duplicate refs and malformed endpoints", async () => {
    const data = await uePost("/api/build-graph", {
      blueprint: bpName,
      graph: "EventGraph",
      nodes: [
        { ref: "Dup", nodeType: "CallFunction", functionName: "PrintString" },
        { ref: "Dup", nodeType: "CallFunction", functionName: "PrintString" },
        { nodeType: "CallFunction", functionName: "PrintString" },
      ],
      connections: [{ from: "NoDotHere", to: "Dup.execute" }],
      dryRun: true,
    });
    expect(data.success).toBe(false);
    expect(data.problems.length).toBeGreaterThanOrEqual(3);
    expect(data.problems.join(" ")).toContain("duplicate ref");
  });

  it("returns an error for a non-existent blueprint", async () => {
    const data = await uePost("/api/build-graph", {
      blueprint: "BP_Nonexistent_XYZ_999",
      graph: "EventGraph",
      nodes: [{ ref: "A", nodeType: "CallFunction", functionName: "PrintString" }],
    });
    expect(data.error).toBeDefined();
  });

  it("returns an error for a non-existent graph", async () => {
    const data = await uePost("/api/build-graph", {
      blueprint: bpName,
      graph: "NoSuchGraph",
      nodes: [{ ref: "A", nodeType: "CallFunction", functionName: "PrintString" }],
    });
    expect(data.error).toBeDefined();
  });

  it("rejects missing required fields", async () => {
    const data = await uePost("/api/build-graph", {});
    expect(data.error).toBeDefined();
  });

  it("rejects a request with nothing to do", async () => {
    const data = await uePost("/api/build-graph", { blueprint: bpName, graph: "EventGraph" });
    expect(data.error).toBeDefined();
  });
});
