import { describe, it, expect } from "vitest";
import { ueGet, uePost } from "../helpers.js";

// CLAUDE-NOTE: rebuild_groom_bindings operates on UGroomBindingAsset, which needs real Groom +
// Skeletal Mesh source data to build meaningfully — not something a headless test fixture can
// synthesize cheaply, and no other tool in this project creates one. These tests cover the
// endpoint's contract (selector validation, not-found handling, dryRun) rather than an actual
// rebuild. See memory: blueprint-mcp-test-harness-gotchas for why engine-facing work like an actual
// build still needs live-editor verification beyond what this file covers.
describe("rebuild_groom_bindings", () => {
  it("rejects when no selector (assetPath/assetPaths/query) is given", async () => {
    const data = await uePost("/api/rebuild-groom-bindings", {});
    expect(data.error).toBeDefined();
  });

  it("reports notFound for a non-existent asset path without erroring", async () => {
    const data = await uePost("/api/rebuild-groom-bindings", {
      assetPath: "/Game/Test/GB_Nonexistent_XYZ_999",
    });
    expect(data.error).toBeUndefined();
    expect(data.matched).toBe(0);
    expect(data.notFound).toContain("/Game/Test/GB_Nonexistent_XYZ_999");
  });

  it("reports notFound for each path in an assetPaths array", async () => {
    const data = await uePost("/api/rebuild-groom-bindings", {
      assetPaths: ["/Game/Test/GB_Nonexistent_A", "/Game/Test/GB_Nonexistent_B"],
    });
    expect(data.error).toBeUndefined();
    expect(data.matched).toBe(0);
    expect(data.notFound).toHaveLength(2);
  });

  it("errors when a query substring matches nothing (mirrors backend's no-selector check)", async () => {
    // CLAUDE-NOTE: the backend can't distinguish "empty query result" from "nothing specified" —
    // both hit the same "Specify 'assetPath'..." error. Pre-existing behavior, not introduced here.
    const data = await uePost("/api/rebuild-groom-bindings", {
      query: "NoSuchGroomBindingXYZ999",
    });
    expect(data.error).toBeDefined();
  });

  it("dryRun with a non-existent path reports notFound without building or saving", async () => {
    const data = await uePost("/api/rebuild-groom-bindings", {
      assetPath: "/Game/Test/GB_Nonexistent_XYZ_999",
      dryRun: true,
    });
    expect(data.error).toBeUndefined();
    expect(data.dryRun).toBe(true);
    expect(data.matched).toBe(0);
    expect(data.notFound).toContain("/Game/Test/GB_Nonexistent_XYZ_999");
    expect(data.saved).toEqual([]);
  });
});

// CLAUDE-NOTE: list_groom_bindings, duplicate_groom_binding, and set_groom_binding_target_mesh
// had zero test coverage before this — none of the three check bIsEditor, so unlike the tools
// above they're fully exercisable headless. Only the fixture-requiring success paths (an actual
// duplicate, an actual mesh swap) are out of reach for the same reason noted above.
describe("list_groom_bindings", () => {
  it("returns a well-formed response with no bindings in a fresh project", async () => {
    const data = await ueGet("/api/list-groom-bindings", {});
    expect(data.error).toBeUndefined();
    expect(data.count).toBe(0);
    expect(data.bindings).toEqual([]);
  });

  it("respects a query filter that matches nothing", async () => {
    const data = await ueGet("/api/list-groom-bindings", { query: "NoSuchGroomBindingXYZ999" });
    expect(data.error).toBeUndefined();
    expect(data.count).toBe(0);
  });
});

describe("duplicate_groom_binding", () => {
  it("rejects missing assetPath/newName", async () => {
    const data = await uePost("/api/duplicate-groom-binding", {});
    expect(data.error).toBeDefined();
  });

  it("returns error for a non-existent source asset", async () => {
    const data = await uePost("/api/duplicate-groom-binding", {
      assetPath: "/Game/Test/GB_Nonexistent_XYZ_999",
      newName: "GB_Copy",
    });
    expect(data.error).toBeDefined();
  });
});

describe("set_groom_binding_target_mesh", () => {
  it("rejects missing assetPath/targetMeshPath", async () => {
    const data = await uePost("/api/set-groom-binding-target-mesh", {});
    expect(data.error).toBeDefined();
  });

  it("returns error for a non-existent binding asset", async () => {
    const data = await uePost("/api/set-groom-binding-target-mesh", {
      assetPath: "/Game/Test/GB_Nonexistent_XYZ_999",
      targetMeshPath: "/Game/Test/SKM_Nonexistent",
    });
    expect(data.error).toBeDefined();
  });
});
