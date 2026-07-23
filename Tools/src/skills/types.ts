// CLAUDE-NOTE: Shared shape for "Skills" — named, retrievable instruction fragments
// exposed as MCP Resources (skill://unreal/{name}). Skills tell the agent *how to think*
// about a class of task (spatial reasoning, safe Blueprint editing, etc.). This mirrors
// Epic 5.8's UAgentSkill concept at the MCP transport layer, so it works on UE 5.6.

export interface Skill {
  /** kebab/lowercase id used in the URI: skill://unreal/{name} */
  name: string;
  /** one-line summary shown by list_skills and resources/list */
  description: string;
  /** full markdown instructions returned by resources/read */
  content: string;
}
