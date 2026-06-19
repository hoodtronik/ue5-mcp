// CLAUDE-NOTE: Shared shape for "Static Examples" — canonical, step-by-step workflow
// recipes exposed as MCP Resources (example://unreal/{name}). Where Skills say *how to
// think*, Examples say *what to do*: a fixed, deterministic sequence of tool calls for a
// common task. Cheap and template-like. Transport-layer only — UE 5.6 safe.

export interface Example {
  /** kebab/lowercase id used in the URI: example://unreal/{name} */
  name: string;
  /** one-line summary shown by list_examples and resources/list */
  description: string;
  /** full markdown recipe (Task + numbered Steps) returned by resources/read */
  content: string;
}
