# Knowledge: everything Claude needs to know about Loop3D

The user-facing docs (`README.md`, `docs/`) are written for people. These files are
the same knowledge re-cut for Claude: where things are, how they connect, the
rules, the commands, the traps, and the gaps, verified against the code.

**Reading order:** `00-fast-start.md` is loaded into every session through
`CLAUDE.md`. Then open only what the task needs:

| File | When |
|---|---|
| [00-fast-start.md](00-fast-start.md) | always (auto-loaded) |
| [architecture.md](architecture.md) | anything crossing two subsystems; frame loop; settings; JS bridge |
| [where-is.md](where-is.md) | "where do I change X?" |
| [recipes.md](recipes.md) | adding a word, setting, material, look, sound, export, test, project |
| [language-quickref.md](language-quickref.md) | writing or reading `.loop` files |
| [history.md](history.md) | before performance or GPU work |
| [subsystems/](subsystems/) | deep work in one engine folder |

## Keeping it true
- The canonical docs stay in `docs/` and `README.md`; these notes point to them
  rather than copying long tables (copies go stale). `docs/SETTINGS.md` is generated.
- `VERIFIED_AT` holds the commit these notes were last checked against.
  `.claude/tools/knowledge_stale.sh` lists changed files and the note to re-check.
- After changing the engine, update the matching note in the same commit, then
  `.claude/tools/knowledge_stale.sh --stamp`.
