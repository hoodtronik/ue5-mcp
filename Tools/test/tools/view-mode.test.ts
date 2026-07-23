import { describe, it, expect } from "vitest";
import { uePost, describeEditorOnly} from "../helpers.js";

describeEditorOnly("view mode tools", () => {
  describe("set_view_mode", () => {
    it("sets view mode to Lit", async () => {
      const data = await uePost("/api/set-view-mode", { mode: "Lit" });
      expect(data.error).toBeUndefined();
      expect(data.success).toBe(true);
      expect(data.mode).toBe("Lit");
    });

    it("sets view mode to Wireframe", async () => {
      const data = await uePost("/api/set-view-mode", { mode: "Wireframe" });
      expect(data.error).toBeUndefined();
      expect(data.success).toBe(true);
    });

    it("rejects unknown view mode", async () => {
      const data = await uePost("/api/set-view-mode", { mode: "InvalidMode" });
      expect(data.error).toBeDefined();
    });

    it("rejects missing mode", async () => {
      const data = await uePost("/api/set-view-mode", {});
      expect(data.error).toBeDefined();
    });

    // Reset to Lit after tests
    it("resets to Lit", async () => {
      await uePost("/api/set-view-mode", { mode: "Lit" });
    });
  });

  describe("set_show_flags", () => {
    it("disables Grid flag", async () => {
      const data = await uePost("/api/set-show-flags", { flag: "Grid", enabled: false });
      expect(data.error).toBeUndefined();
      expect(data.success).toBe(true);
      expect(data.enabled).toBe(false);
    });

    it("enables Grid flag", async () => {
      const data = await uePost("/api/set-show-flags", { flag: "Grid", enabled: true });
      expect(data.error).toBeUndefined();
      expect(data.success).toBe(true);
      expect(data.enabled).toBe(true);
    });

    it("rejects unknown flag", async () => {
      const data = await uePost("/api/set-show-flags", { flag: "NonExistentFlag_XYZ" });
      expect(data.error).toBeDefined();
    });
  });

  describe("set_viewport_type", () => {
    it("switches to Top view", async () => {
      const data = await uePost("/api/set-viewport-type", { type: "Top" });
      expect(data.error).toBeUndefined();
      expect(data.success).toBe(true);
    });

    it("switches back to Perspective", async () => {
      const data = await uePost("/api/set-viewport-type", { type: "Perspective" });
      expect(data.error).toBeUndefined();
      expect(data.success).toBe(true);
    });

    it("rejects unknown type", async () => {
      const data = await uePost("/api/set-viewport-type", { type: "Isometric" });
      expect(data.error).toBeDefined();
    });
  });

  describe("set_realtime_rendering", () => {
    it("disables realtime rendering", async () => {
      const data = await uePost("/api/set-realtime-rendering", { enabled: false });
      expect(data.error).toBeUndefined();
      expect(data.success).toBe(true);
    });

    it("enables realtime rendering", async () => {
      const data = await uePost("/api/set-realtime-rendering", { enabled: true });
      expect(data.error).toBeUndefined();
      expect(data.success).toBe(true);
      expect(data.realtime).toBe(true);
    });
  });

  describe("set_game_view", () => {
    it("enables game view", async () => {
      const data = await uePost("/api/set-game-view", { enabled: true });
      expect(data.error).toBeUndefined();
      expect(data.success).toBe(true);
    });

    it("disables game view", async () => {
      const data = await uePost("/api/set-game-view", { enabled: false });
      expect(data.error).toBeUndefined();
      expect(data.success).toBe(true);
    });
  });
});
