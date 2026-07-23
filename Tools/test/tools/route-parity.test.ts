import { describe, it, expect } from "vitest";
import * as fs from "node:fs";
import * as path from "node:path";

const PLUGIN_ROOT = path.resolve(import.meta.dirname, "..", "..", "..");
const SRC_DIR = path.resolve(PLUGIN_ROOT, "Tools", "src");
const SERVER_CPP = path.resolve(
  PLUGIN_ROOT,
  "Source",
  "BlueprintMCP",
  "Private",
  "BlueprintMCPServer.cpp",
);

const ROUTE_RE = /\/api\/[a-zA-Z0-9-]+/g;

// CLAUDE-NOTE: routes with real, implemented C++ handlers but no TS caller yet. /api/test-save is
// an intentional diagnostic-only endpoint (see BlueprintMCPHandlers_Read.cpp: "load a Blueprint and
// save it unmodified"). The other four are a genuine coverage gap — tracked as open-work item in
// memory (blueprint-mcp-open-work) rather than silently left unflagged.
const KNOWN_ORPHAN_CPP_ROUTES = new Set([
  "/api/test-save",
  "/api/rebuild-groom-bindings",
  "/api/list-mirror-table-rows",
  "/api/set-mirror-table-rows",
  "/api/remove-mirror-table-rows",
]);

function walkTsFiles(dir: string): string[] {
  return fs.readdirSync(dir, { withFileTypes: true }).flatMap((entry) => {
    const full = path.join(dir, entry.name);
    if (entry.isDirectory()) return walkTsFiles(full);
    return entry.name.endsWith(".ts") ? [full] : [];
  });
}

// CLAUDE-NOTE: catches the failure mode from the 2026-05-27 merge that silently dropped 58 route
// registrations from BlueprintMCPServer.cpp (see memory: blueprint-mcp-route-loss-incident) — every
// route the TS layer calls must have a matching FHttpPath registration in the C++ backend, and vice
// versa, or a tool silently 404s / a registration goes uncalled.
describe("TS/C++ route parity", () => {
  const tsRoutes = new Set(
    walkTsFiles(SRC_DIR).flatMap((file) =>
      Array.from(fs.readFileSync(file, "utf-8").matchAll(ROUTE_RE), (m) => m[0]),
    ),
  );

  const cppSource = fs.readFileSync(SERVER_CPP, "utf-8");
  const cppRoutes = new Set(
    Array.from(
      cppSource.matchAll(/FHttpPath\(TEXT\("(\/api\/[a-zA-Z0-9-]+)"\)\)/g),
      (m) => m[1],
    ),
  );

  it("has a C++ route registration for every route the TS layer calls", () => {
    const missingInCpp = [...tsRoutes].filter((r) => !cppRoutes.has(r)).sort();
    expect(missingInCpp).toEqual([]);
  });

  it("has a TS caller for every C++ route registration", () => {
    const missingInTs = [...cppRoutes]
      .filter((r) => !tsRoutes.has(r) && !KNOWN_ORPHAN_CPP_ROUTES.has(r))
      .sort();
    expect(missingInTs).toEqual([]);
  });

  it("does not carry stale entries in the known-orphan allowlist", () => {
    // If a route gets wired up or removed, its allowlist entry must be deleted too.
    const staleEntries = [...KNOWN_ORPHAN_CPP_ROUTES]
      .filter((r) => !cppRoutes.has(r) || tsRoutes.has(r))
      .sort();
    expect(staleEntries).toEqual([]);
  });
});
