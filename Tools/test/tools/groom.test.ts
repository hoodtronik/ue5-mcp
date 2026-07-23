import { describe, it, expect } from "vitest";
import { uePost } from "../helpers.js";

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
