import { describe, it, expect } from "vitest";
import { uePost, describeEditorOnly } from "../helpers.js";

describe("content browser tools", () => {
  describe("navigate_content_browser", () => {
    it("rejects missing path", async () => {
      const data = await uePost("/api/navigate-content-browser", {});
      expect(data.error).toBeDefined();
    });
  });

  describe("open_asset_editor", () => {
    it("returns error for non-existent asset", async () => {
      const data = await uePost("/api/open-asset-editor", {
        assetPath: "BP_Nonexistent_XYZ_999",
      });
      expect(data.error).toBeDefined();
    });

    it("rejects missing assetPath", async () => {
      const data = await uePost("/api/open-asset-editor", {});
      expect(data.error).toBeDefined();
    });
  });
});

// CLAUDE-NOTE: navigate_content_browser's success path checks bIsEditor before touching the
// content browser module at all (BlueprintMCPHandlers_ContentBrowser.cpp), so it needs a live
// editor. open_asset_editor's error case above doesn't hit that check because assetPath validation
// runs first and returns a real error either way.
describeEditorOnly("content browser tools (editor-only)", () => {
  describe("navigate_content_browser", () => {
    it("navigates to a valid path", async () => {
      const data = await uePost("/api/navigate-content-browser", {
        path: "/Game",
      });
      expect(data.error).toBeUndefined();
      expect(data.success).toBe(true);
      expect(data.navigatedTo).toBe("/Game");
    });
  });
});
