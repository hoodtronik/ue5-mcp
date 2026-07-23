import { describe, it, expect, beforeAll, afterAll } from "vitest";
import * as fs from "node:fs";
import * as path from "node:path";
import * as os from "node:os";

// CLAUDE-NOTE: agent-config.ts reads UE_PROJECT_DIR at module-load time (ue-bridge.ts:
// `process.env.UE_PROJECT_DIR || process.cwd()`, evaluated once as a top-level const), so the env
// var must be set BEFORE the module is first imported. Static imports are hoisted ahead of any
// top-level code in this file, so the env var is set in beforeAll and the module is loaded via a
// dynamic import afterwards — the same approach used to manually verify this tool before writing
// this file (a scratch tmpdir, never the real project directory).
let scratchDir: string;
let handlers: Record<string, (args: any) => Promise<any>>;

beforeAll(async () => {
  scratchDir = fs.mkdtempSync(path.join(os.tmpdir(), "bpmcp-agentconfig-test-"));
  process.env.UE_PROJECT_DIR = scratchDir;

  const { registerAgentConfigTools } = await import("../../src/tools/agent-config.js");
  handlers = {};
  const fakeServer = {
    tool: (name: string, _d: string, _s: any, handler: (args: any) => Promise<any>) => {
      handlers[name] = handler;
    },
  } as any;
  registerAgentConfigTools(fakeServer);
});

afterAll(() => {
  fs.rmSync(scratchDir, { recursive: true, force: true });
});

function write(name: string, content: string) {
  fs.writeFileSync(path.join(scratchDir, name), content, "utf-8");
}
function read(name: string): string {
  return fs.readFileSync(path.join(scratchDir, name), "utf-8");
}
function exists(name: string): boolean {
  return fs.existsSync(path.join(scratchDir, name));
}

describe("refresh_agent_config", () => {
  it("creates a missing file with the managed block", async () => {
    const res = await handlers["refresh_agent_config"]({ files: ["GEMINI.md"] });
    expect(res.content[0].text).toContain("GEMINI.md: created");
    expect(exists("GEMINI.md")).toBe(true);
    const content = read("GEMINI.md");
    expect(content).toContain("<!-- BLUEPRINTMCP:BEGIN -->");
    expect(content).toContain("<!-- BLUEPRINTMCP:END -->");
    expect(content).toContain("BlueprintMCP capabilities");
  });

  it("appends to an existing file with no prior managed block, preserving its content", async () => {
    write("AGENTS.md", "# Agents\n\nHand-written notes.\n");
    const res = await handlers["refresh_agent_config"]({ files: ["AGENTS.md"] });
    expect(res.content[0].text).toContain("AGENTS.md: updated");
    const content = read("AGENTS.md");
    expect(content).toContain("Hand-written notes.");
    expect(content).toContain("<!-- BLUEPRINTMCP:BEGIN -->");
  });

  it("replaces an existing managed block in place, preserving surrounding user content", async () => {
    write(
      "CLAUDE.md",
      "# My Project\n\nBefore notes.\n\n<!-- BLUEPRINTMCP:BEGIN -->\nSTALE\n<!-- BLUEPRINTMCP:END -->\n\nAfter notes.\n"
    );
    const res = await handlers["refresh_agent_config"]({ files: ["CLAUDE.md"] });
    expect(res.content[0].text).toContain("CLAUDE.md: updated");
    const content = read("CLAUDE.md");
    expect(content).toContain("Before notes.");
    expect(content).toContain("After notes.");
    expect(content).not.toContain("STALE");
    expect(content).toContain("BlueprintMCP capabilities");
  });

  it("is idempotent: a second run on unchanged content reports unchanged and does not rewrite", async () => {
    write("CLAUDE.md", "# X\n\n<!-- BLUEPRINTMCP:BEGIN -->\nplaceholder\n<!-- BLUEPRINTMCP:END -->\n");
    await handlers["refresh_agent_config"]({ files: ["CLAUDE.md"] });
    const afterFirst = read("CLAUDE.md");
    const mtimeBefore = fs.statSync(path.join(scratchDir, "CLAUDE.md")).mtimeMs;

    const res2 = await handlers["refresh_agent_config"]({ files: ["CLAUDE.md"] });
    expect(res2.content[0].text).toContain("CLAUDE.md: already up to date");
    expect(read("CLAUDE.md")).toBe(afterFirst);
    expect(fs.statSync(path.join(scratchDir, "CLAUDE.md")).mtimeMs).toBe(mtimeBefore);
  });

  it("respects createIfMissing: false by skipping absent files", async () => {
    const res = await handlers["refresh_agent_config"]({ files: ["GEMINI.md"], createIfMissing: false });
    fs.rmSync(path.join(scratchDir, "GEMINI.md"), { force: true });
    const res2 = await handlers["refresh_agent_config"]({ files: ["GEMINI.md"], createIfMissing: false });
    expect(res2.content[0].text).toContain("skipped");
    expect(exists("GEMINI.md")).toBe(false);
    void res;
  });

  it("dryRun reports intended action without writing anything", async () => {
    fs.rmSync(path.join(scratchDir, "GEMINI.md"), { force: true });
    const res = await handlers["refresh_agent_config"]({ files: ["GEMINI.md"], dryRun: true });
    expect(res.content[0].text).toContain("[dryRun]");
    expect(exists("GEMINI.md")).toBe(false);
  });

  it("only touches the files listed in the 'files' filter", async () => {
    write("AGENTS.md", "# Agents\n");
    write("GEMINI.md", "# Gemini\n");
    const before = read("GEMINI.md");
    await handlers["refresh_agent_config"]({ files: ["AGENTS.md"] });
    expect(read("GEMINI.md")).toBe(before); // untouched
    expect(read("AGENTS.md")).toContain("BlueprintMCP capabilities");
  });

  it("generated block reports a nonzero tool count across multiple categories", async () => {
    write("CLAUDE.md", "# X\n");
    await handlers["refresh_agent_config"]({ files: ["CLAUDE.md"] });
    const content = read("CLAUDE.md");
    const match = content.match(/exposing \*\*(\d+) MCP tools\*\* across (\d+) categories/);
    expect(match).not.toBeNull();
    expect(Number(match![1])).toBeGreaterThan(100);
    expect(Number(match![2])).toBeGreaterThan(10);
  });
});
