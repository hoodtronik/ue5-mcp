import { describe, it, expect, beforeAll, afterAll } from "vitest";
import { uePost, ueGet, createTestBlueprint, deleteTestBlueprint, uniqueName } from "../helpers.js";

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

  // CLAUDE-NOTE: regression — found live in the editor, not by the original tests. A Sequence
  // node's exec outputs are really named "then_0"/"then_1", but the Blueprint editor DISPLAYS
  // them as "Then 0"/"Then 1", so that is the spelling an agent writes. The first version only
  // handled single-output nodes and Branch, so "Seq.then 0" failed against a real Sequence.
  it("resolves displayed pin names that use spaces (then 0 -> then_0)", async () => {
    const data = await uePost("/api/build-graph", {
      blueprint: bpName,
      graph: "EventGraph",
      nodes: [
        { ref: "S", nodeType: "Sequence", posX: 0, posY: 1600 },
        { ref: "A", nodeType: "CallFunction", functionName: "PrintString", posX: 400, posY: 1600 },
        { ref: "B", nodeType: "CallFunction", functionName: "PrintString", posX: 400, posY: 1780 },
      ],
      connections: [
        { from: "S.then 0", to: "A.execute" },
        { from: "S.then_1", to: "B.execute" },
      ],
    });
    expect(data.error).toBeUndefined();
    expect(data.connectionsFailed).toBe(0);
    expect(data.connectionsMade).toBe(2);
    // Both spellings must land on the engine's real underscore names.
    expect(data.connections[0].fromPin).toBe("then_0");
    expect(data.connections[1].fromPin).toBe("then_1");
  });

  // CLAUDE-NOTE: regression — the original auto-layout put every node on one row 320px apart,
  // so branch arms overlapped each other's bodies and wires crossed through nodes. Layout is now
  // depth-layered: column by exec depth, row by sibling index.
  it("auto-layouts branch arms into the same column on separate rows", async () => {
    const layoutBp = uniqueName("BP_BuildGraphLayout");
    const created = await createTestBlueprint({ name: layoutBp });
    expect(created.error).toBeUndefined();

    try {
      const data = await uePost("/api/build-graph", {
        blueprint: layoutBp,
        graph: "EventGraph",
        nodes: [
          { ref: "Br", nodeType: "Branch" },
          { ref: "T", nodeType: "CallFunction", functionName: "PrintString" },
          { ref: "F", nodeType: "CallFunction", functionName: "PrintString" },
        ],
        connections: [
          { from: "Br.true", to: "T.execute" },
          { from: "Br.false", to: "F.execute" },
        ],
      });
      expect(data.error).toBeUndefined();
      expect(data.connectionsFailed).toBe(0);

      const ids = Object.fromEntries(data.nodes.map((n: any) => [n.ref, n.nodeId]));
      const graph = await ueGet("/api/graph", { name: layoutBp, graph: "EventGraph" });
      const at = (id: string) => graph.nodes.find((n: any) => n.id === id);
      const br = at(ids.Br), t = at(ids.T), f = at(ids.F);

      // Branch feeds both arms, so both arms share the next column...
      expect(t.posX).toBe(f.posX);
      expect(t.posX).toBeGreaterThan(br.posX);
      // ...and must not sit on top of each other.
      expect(t.posY).not.toBe(f.posY);
      expect(Math.abs(t.posY - f.posY)).toBeGreaterThanOrEqual(150);
    } finally {
      await deleteTestBlueprint(`${packagePath}/${layoutBp}`);
    }
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
