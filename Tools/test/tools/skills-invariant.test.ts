import { describe, it, expect } from "vitest";
import { SKILLS } from "../../src/skills/index.js";

// CLAUDE-NOTE: the skills system had zero test coverage before this. A duplicate skill 'name'
// would silently overwrite an earlier resource registration in server.resource() (last one wins) —
// this catches that mechanically instead of relying on someone noticing a skill went missing from
// list_skills.
describe("skills registry invariants", () => {
  it("has at least the skills known to exist as of 2026-07-23", () => {
    expect(SKILLS.length).toBeGreaterThanOrEqual(9);
  });

  it("has no duplicate skill names", () => {
    const names = SKILLS.map((s) => s.name);
    const unique = new Set(names);
    expect(unique.size).toBe(names.length);
  });

  it("every skill has a non-empty name, description, and content", () => {
    for (const s of SKILLS) {
      expect(s.name.length).toBeGreaterThan(0);
      expect(s.description.length).toBeGreaterThan(0);
      expect(s.content.length).toBeGreaterThan(0);
    }
  });

  it("skill names are URI-safe (lowercase, digits, hyphens only)", () => {
    for (const s of SKILLS) {
      expect(s.name).toMatch(/^[a-z0-9-]+$/);
    }
  });
});
