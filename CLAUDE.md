# Loop3D: notes for Claude

- Claude's workspace is [`.claude/`](.claude/README.md): skills in `.claude/skills/`, working notes in `.claude/notes/`.
- To make or improve a skill, use the `legends` skill. Register every skill in `.claude/skills/README.md`.
- Known missing engine components: `.claude/notes/engine-gaps.md`. Keep it current.
- Engine: from-scratch C++20, no outside libraries. Build and test: `cmake -B build -DCMAKE_BUILD_TYPE=Release && cmake --build build -j && ./build/loop_tests`.
- Docs stay honest: update `README.md` "Honest status" and `docs/` when behaviour changes.
