# How we work (the only rulebook)

**Loop3D** is J Production's engine, made from scratch: C++20 compiled to WebAssembly, drawn with WebGPU, with its
own language, **Looped Script** (`.loop`). Everything else (Bar3D, Echoes of Ardane, The Last Ember, future games) is
a **project** in `projects/<name>/`, like games in Unreal. The engine never knows about a project.

**The user** owns J Production, works only from an iPhone (Safari) through this chat, and is new to code and
graphics. They ask in any words; you plan, build, check and explain in plain words, saying what to look at on the
phone. Never make them learn a file, tool or command.

## North: read `docs/NORTH.md` first, every chat
Where Loop3D is going (a world the user owns; creators own land and build cities), the measure (the best inside the
compute budget, reaching for what nobody has done), their eye, and how to work (structure first, polish last; stay
inside the circle; one work template; slow checks on a schedule; fix the cause, not the symptom; framed, working,
finished). **Where North and anything below disagree, North wins.**

## The pace (the user's word, 2026-11-06)
**Super slow, one tiny goal at a time; the user grows with the project.** Each goal is one sentence the user can see
on the phone. Before building: the one idea it teaches, in a few plain sentences, then wait for the user's go. Build
only that goal, show it, say in two or three lines what changed and what they learned, and add it to the dashboard
(Loop3D Workshop). Never run ahead. When the user hands over the reins for a bigger goal (e.g. the editor), build all
of it in solid, checked steps and report at the end.

## Working
0. **Many chats at once** (`docs/PARALLEL.md`): before changing anything, `git pull origin main`, run
   `python3 tools/claims.py --remote` (the board), and claim your job with
   `python3 tools/claims.py new <code> <title> --touch "<files>"` (refused if another claim holds a file). Push the
   claim to `main` at once (if refused, pull and check again), and check in with `claims.py beat` as you go
   (`docs/CONDUCTOR.md`). Work only on your own branch, only in your claim's files. **`LOG.md` and `PLAN.md` are the
   deployer's:** put your log line in the claim's `note:`; the deployer copies it when it ships. Only the deployer
   merges into `main`. A chat that is the only one working may say so and follow the steps below directly.
1. **Start:** read `docs/NORTH.md`, `PLAN.md` and the top of `LOG.md`. That's all.
2. **The user asks, you do it.** Ask only when the choice is really theirs (money, going public, deleting their
   things, a change of direction): one question, your recommendation first.
3. **Quick checks before every push** (seconds): `cmake -B build -G Ninja -DCMAKE_BUILD_TYPE=Release && cmake --build
   build && ./build/loop_tests && node tests/editor_source_test.js && node tests/recorder_test.js`. A bug you fix
   gets a test. Slow checks only when they cover what changed: editor `tools/editor_check.js`; live picture
   `xvfb-run -a node tools/picture_check.js` (12 scenes against `tests/pictures/`, ~4 min); web player
   `source /opt/emsdk/emsdk_env.sh && ./tools/build_site.sh` (~30 s; the cloud setup **J Production (Loop3D)** has
   Emscripten 6.0.13 and builds the engine at each start).
4. **Commit small, push `main`.** GitHub's **Check** tests and builds every push. The public player changes only when
   published: the **Publish the player** workflow (by hand), or, where Claude can't start workflows, the `site`
   artifact of a green Check put on `gh-pages` as one new commit. Then open the live page to check it. A player change
   also goes to the user's **recording page** (`docs/TUNING.md`, section 5).
5. **Grow the library:** whatever another project could use (model, room, piece, light rig, clip, sound) goes into
   `library/<kinds>/<name>.loop` with its card (what it is, how to place it, credit and licence, origin), and the
   project uses it from there (`loop library check`; the library test refuses an entry without a licence).
6. **Log:** one line at the top of `LOG.md` per request: date, what was asked (short), what was done, the commit.
   Change `PLAN.md` when the order changes.
7. **Reply short:** what's done, what to look at on the phone, what's next. Never say done or live without checking.

## Which model (the user switches it; docs/MODELS.md)
Before work that needs a different tier than the chat is on, say so in one short block and wait: **Switch
suggested:** `/model <id>`, why, how long, and which to switch back to. Also say when the chat is on a higher tier
than the work needs. Never switch silently; never do top-tier design on a lower tier without saying the risk.
Top tier: design that sets a format, judging pictures, hard algorithms. Most orders: the workhorse tier.

## The engine's rules
- **One of everything:** one engine, one language, one way to do each thing. Look for what exists before adding.
- **Phone first:** judged on an iPhone 15 Pro Max in Safari. Smooth beats pretty; every frame fits its budget.
- **WebGPU only** (no WebGL).
- **From scratch:** no outside engines or runtime libraries. Build and test tools are fine (compilers, Blender,
  texture compressors, the test browser).
- **Free:** no paid services or plans.
- **Text is the source:** scripts, worlds and models as recipes; the engine makes everything else from them.

## Never
- A secret, key or password in the repo, a commit, a log or a reply.
- An asset without a licence that allows it (credit it); no copyrighted music.
- Weaker safety: 13+ only, teens and adults apart, the 18+ casino with earned credits only (Bar3D has accounts and
  voice).
- Pretending to be a real person or company.
- Force-pushing or rewriting `main`.
- Deleting or overwriting the user's things outside this repo (server data, accounts, the old repos) without asking.

## Where things are
`engine/` (core, project, geometry, render, gpu, script, physics, anim, audio; sides: `engine/modules.json`) ·
`web/` (player, editor) · `tools/loop.cpp` (the `loop` command: check, run, play, render, budget, score, project,
library, words, settings) · `library/` (shared by every project) · `projects/` (one folder each; a cover page
`name.looped` names it and its first scene) · `tests/` · `docs/` (LANGUAGE, SETTINGS, STORY_GUIDE, TUNING, PIPELINE,
DONE, ROADMAP, WORK_ORDERS, CRAFT, MECHANICS_UI, LIBRARY_PLAN) · `.claude/agents/` (builder, asset-smith, judge,
checker, deployer; see `docs/PARALLEL.md`, `work/claims/`) · `perf/` (phone benchmark runs).
- Public player: https://barcode162.github.io/LOOP3D/ (editor: `editor.html`), from `gh-pages`.
- Recording page (private, the player inside claude.ai): https://claude.ai/artifact/EPvB3EZadvnepQiTiTUXpT; every
  scene played there is timed and saved in its database (`runs`), read with `ArtifactData`.
- Pictures of the live renderer: download the `site` artifact of GitHub's **Check**, serve it, run
  `xvfb-run -a node tools/web_check.js <url> out.png` (never headless: WebGPU loses its device there).
- New skills are made with the `legends` skill (`.claude/skills/legends/`).

## The old repos
`Barcode162/3D` keeps the live bar (joshuavelasco.com/bar) running until Bar3D is rebuilt here as a project; its docs,
plan, playbook and tools are retired: history, never instructions. `Barcode162/ENG` (The Last Ember) is the same.
