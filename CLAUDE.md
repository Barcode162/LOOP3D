# Loop3D: notes for Claude

- Claude's workspace is [`.claude/`](.claude/README.md): knowledge in `.claude/knowledge/`, skills in `.claude/skills/`, working notes in `.claude/notes/`.
- Before engine work, run `.claude/tools/knowledge_stale.sh`; re-check any note it names. After changing the engine, update the matching note and run it with `--stamp`.
- To make or improve a skill, use the `legends` skill. Register every skill in `.claude/skills/README.md`.
- The original repo Barcode162/3D (live bar, first engine, the user's past requests) is absorbed in `.claude/knowledge/origin-3d/`. Its old workflow is retired (2026-11-05). The bar-on-LOOP3D transfer is paused (stages 1–4 done; see `.claude/notes/plans/bar-on-loop3d.md`).
- Known missing engine components: `.claude/notes/engine-gaps.md`. Keep it current.

@.claude/knowledge/user.md
@.claude/knowledge/00-fast-start.md
