# .claude: Claude's workspace for Loop3D

Everything Claude keeps between sessions lives here, in the repo, so it travels with
the engine and every session starts with it.

| Folder | What's in it |
|---|---|
| `skills/` | Project skills Claude Code loads automatically. Each is a folder with a `SKILL.md`. The registry and lineage are in [`skills/README.md`](skills/README.md). |
| `notes/` | Claude's working memory: engine gaps found, lessons learned, plans in progress. |

Rules:
- New skills are forged with the `legends` skill (`.claude/skills/legends/`), never
  written ad hoc, so every skill meets the same bar.
- Notes are short and kept true. Stale notes are deleted, not left to mislead.
