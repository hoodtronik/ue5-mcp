import { describe, it, expect } from "vitest";
import { uePost, describeEditorOnly} from "../helpers.js";

describeEditorOnly("camera tools", () => {
  describe("get_viewport_camera", () => {
    it("returns camera info without errors", async () => {
      const data = await uePost("/api/get-viewport-camera", {});
      expect(data.error).toBeUndefined();
      expect(data.location).toBeDefined();
      expect(typeof data.location.x).toBe("number");
      expect(typeof data.location.y).toBe("number");
      expect(typeof data.location.z).toBe("number");
      expect(data.rotation).toBeDefined();
      expect(typeof data.rotation.pitch).toBe("number");
      expect(typeof data.rotation.yaw).toBe("number");
      expect(typeof data.fov).toBe("number");
    });
  });

  describe("set_viewport_camera", () => {
    it("sets camera location", async () => {
      const data = await uePost("/api/set-viewport-camera", {
        location: { x: 100, y: 200, z: 300 },
      });
      expect(data.error).toBeUndefined();
      expect(data.success).toBe(true);
      expect(data.location).toBeDefined();
    });

    it("sets camera rotation", async () => {
      const data = await uePost("/api/set-viewport-camera", {
        rotation: { pitch: -30, yaw: 45, roll: 0 },
      });
      expect(data.error).toBeUndefined();
      expect(data.success).toBe(true);
      expect(data.rotation).toBeDefined();
    });

    it("accepts empty body (no changes)", async () => {
      const data = await uePost("/api/set-viewport-camera", {});
      expect(data.error).toBeUndefined();
      expect(data.success).toBe(true);
    });
  });
});
