# UE5 MCP — Give AI agents full access to your UE5 assets

Vibe code your Blueprints, materials, and Anim Blueprints. This plugin lets Claude Code (or any MCP client) read, modify, and create Unreal Engine 5 Blueprints — just describe what you want in plain English.

> "Add a health component to my player character" · "Find everywhere I use GetActorLocation and replace it" · "What does my damage system do?"

https://github.com/user-attachments/assets/11b86d62-982b-42b3-bddb-aeeddc3e675c

## Getting Started

Tell Claude Code:

```
Set up https://github.com/mirno-ehf/ue5-mcp in my project
```

## Prebuilt binaries (no C++ toolchain needed)

Using a **Blueprint-only** project, or don't want to compile? A precompiled, drop-in
build is available here:

**➡️ [hoodtronik/BlueprintMCP-prebuilt](https://github.com/hoodtronik/BlueprintMCP-prebuilt)** — UE 5.6, Win64

Copy that plugin into your project's `Plugins/` folder and the editor loads it directly,
no build step required. (Prebuilt binaries are engine-version-specific — for other engine
versions, build from source in this repo.)

## How It Works

A UE5 editor plugin exposes your project's Blueprints over a local HTTP server. An [MCP](https://modelcontextprotocol.io) wrapper connects that to AI tools like Claude Code. When the editor is open, it runs inside the editor process with zero overhead. When the editor is closed, it can spawn a headless process instead.

## License

[MIT](LICENSE)
