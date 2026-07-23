import { describe, it, expect } from "vitest";
import { uePost } from "../helpers.js";

// CLAUDE-NOTE: mirror-table tools operate on UMirrorDataTable. There is no headless-safe way to
// fixture one: UMirrorDataTableFactory.Struct/Skeleton are UPROPERTY(protected) (can't be set from
// Python), and UDataTable::RowStruct is VisibleAnywhere, not editable (set_editor_property refuses
// it too) — confirmed against a live editor, not just the headless commandlet. Asset creation is
// only reachable through the factory's interactive modal window. These tests cover the endpoint
// contract (missing fields, non-existent table) rather than a real row edit.
describe("mirror table tools", () => {
  describe("list_mirror_table_rows", () => {
    it("rejects a missing 'table' field", async () => {
      const data = await uePost("/api/list-mirror-table-rows", {});
      expect(data.error).toBeDefined();
    });

    it("returns error for a non-existent table", async () => {
      const data = await uePost("/api/list-mirror-table-rows", { table: "MDT_Nonexistent_XYZ_999" });
      expect(data.error).toBeDefined();
    });
  });

  describe("set_mirror_table_rows", () => {
    it("rejects a missing 'rows' field", async () => {
      const data = await uePost("/api/set-mirror-table-rows", { table: "MDT_Nonexistent_XYZ_999" });
      expect(data.error).toBeDefined();
    });

    it("returns error for a non-existent table", async () => {
      const data = await uePost("/api/set-mirror-table-rows", { table: "MDT_Nonexistent_XYZ_999", rows: [] });
      expect(data.error).toBeDefined();
    });
  });

  describe("remove_mirror_table_rows", () => {
    it("rejects a missing 'rowNames' field", async () => {
      const data = await uePost("/api/remove-mirror-table-rows", { table: "MDT_Nonexistent_XYZ_999" });
      expect(data.error).toBeDefined();
    });

    it("returns error for a non-existent table", async () => {
      const data = await uePost("/api/remove-mirror-table-rows", { table: "MDT_Nonexistent_XYZ_999", rowNames: [] });
      expect(data.error).toBeDefined();
    });
  });
});
