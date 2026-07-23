import { describe, it, expect } from "vitest";
import { uePost, describeEditorOnly} from "../helpers.js";

describeEditorOnly("PIE runtime tools", () => {
  // Note: These tests require an active PIE session.
  // They may fail in commandlet mode where PIE is not available.

  describe("pie_get_player_transform", () => {
    it("returns error when PIE is not running", async () => {
      const data = await uePost("/api/pie-get-player-transform", {});
      // Expect an error since PIE is unlikely to be running in test
      expect(data.error).toBeDefined();
      expect(data.error).toContain("PIE");
    });
  });

  describe("pie_teleport_player", () => {
    it("returns error when PIE is not running", async () => {
      const data = await uePost("/api/pie-teleport-player", {
        location: { x: 0, y: 0, z: 100 },
      });
      expect(data.error).toBeDefined();
      expect(data.error).toContain("PIE");
    });

    it("rejects missing location", async () => {
      const data = await uePost("/api/pie-teleport-player", {});
      expect(data.error).toBeDefined();
    });
  });

  describe("pie_query_actors", () => {
    it("returns error when PIE is not running", async () => {
      const data = await uePost("/api/pie-query-actors", {});
      expect(data.error).toBeDefined();
      expect(data.error).toContain("PIE");
    });

    it("accepts filters when PIE is not running (still errors)", async () => {
      const data = await uePost("/api/pie-query-actors", {
        classFilter: "Character",
        maxResults: 10,
      });
      expect(data.error).toBeDefined();
    });
  });
});
