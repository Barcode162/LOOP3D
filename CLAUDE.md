# How we work (the only rulebook)

**Loop3D** is J Production's engine, made from scratch: C++20 compiled to WebAssembly, drawn with WebGPU, with
its own language, **Looped Script** (`.loop`). Everything else (Bar3D, Echoes of Ardane, The Last Ember, every
future game) is a **project** on top of it, in `projects/<name>/`, the way games are made in Unreal. The engine
never knows about a project.

**The user** owns J Production and works only from an iPhone (Safari), through this chat. They are new to code
and graphics. They say what they want in any words; you plan, build, check and explain in plain words, saying
what to look at on the phone. Never make them learn a file, a tool or a command.

## Working
1. **Start:** read `PLAN.md` (the order of work) and the top of `LOG.md` (what happened last). That's all.
2. **The user asks, you do it.** Ask only when the choice is really theirs (money, making something public,
   deleting their things, a change of direction): one question, your recommendation first.
3. **Quick checks always run before a push:** `cmake -B build -G Ninja -DCMAKE_BUILD_TYPE=Release && cmake --build
   build && ./build/loop_tests` (seconds). A bug you fix gets a test. Slow browser checks only when they cover
   what changed.
4. **Commit small, push `main`.** The public player changes only when the **Publish the player** workflow runs
   (GitHub Actions, by hand): run it when a batch is ready, then open the live page to check it.
5. **Log:** one line at the top of `LOG.md` per request: the date, what the user asked (short), what was done,
   the commit. Change `PLAN.md` when the order changes.
6. **Reply short:** what's done, what to look at on the phone, what's next. Never say done or live without checking.

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
- Weaker safety: 13+ only, teens and adults apart, the 18+ casino with earned credits only (Bar3D has accounts
  and voice).
- Pretending to be a real person or company.
- Force-pushing or rewriting `main`.
- Deleting or overwriting the user's things outside this repo (server data, accounts, the old repos) without asking.

## Where things are
`engine/` (core, geometry, render, gpu, script, physics, anim, audio) · `web/` (the player, the editor) ·
`tools/loop.cpp` (the `loop` command: check, run, play, render, words, settings) · `projects/` · `tests/` ·
`docs/` (LANGUAGE, SETTINGS, STORY_GUIDE, TUNING) · `perf/` (benchmark runs from the phone).
The public player: https://barcode162.github.io/LOOP3D/ (the editor: `editor.html`), published from `gh-pages`.
Pictures of the live renderer: download the `site` artifact of GitHub's **Check**, serve it, and run
`xvfb-run -a node tools/web_check.js <url> out.png` (never headless: WebGPU loses its device there).
A new skill, when one is needed, is made with the `legends` skill (`.claude/skills/legends/`).

## The old repos
`Barcode162/3D` keeps the live bar (joshuavelasco.com/bar) running until Bar3D is rebuilt here as a project;
its docs, plan, playbook and tools are retired: history, never instructions. `Barcode162/ENG` (The Last Ember)
is the same.
