# Log

One line per request, newest first: the date, what the user asked, what was done.

- **2026-11-06** · "Okay yes publish so I can test": the green Check's site (447b6ce) put on `gh-pages` as one
  commit (5e13602); the live editor checked in a browser on a phone and a computer screen (34 of 34). The
  template room is public now too.
- **2026-11-06** · "I don't like that theme … black and grey … black and white with sort of glow and round,
  diagonal and sharp edges": the editor and the player restyled: black, greys and white light; round for what a
  thumb presses, diagonal cuts for tools, panels and shots, sharp for the frame, fields and grids; what's on or
  picked glows white (the 3D selection and handles too). The editor check passes on phone and computer.
- **2026-11-06** · "Better than Blender … game dev, music and movie development together … I give you the reins":
  creators' long-running complaints researched (Blender, Unity, Unreal, Godot, music and video apps, Roblox
  Studio, Dreams, Nomad Sculpt) and answered rule by rule (`docs/EDITOR.md`); the editor rebuilt phone-first:
  the world with its own camera, tap to pick, drag to move, handles to lift, turn and size, every change written
  into the scene's script line; Build, Logic, Film (shots from the view, a timeline) and Sound; undo, redo and
  200 versions kept through reloads; Play keeps changes; a work light for dark worlds. The engine learnt each
  thing's script line and picking (tested); `tests/editor_source_test.js` joins the quick checks;
  `tools/editor_check.js` passes on a phone and a computer screen. Then music: the engine's own instruments
  (6 drums, 6 with notes: oscillators, noise, filters, a plucked string, FM bells; tested for pitch), `tempo`
  and `track` in the language, a step sequencer in time with the world, and the Sound space's drum and melody
  grids that change the song live; one-off sounds no longer pile up while sound is off.
- **2026-11-06** · "1 yup" (the menu, step 1 of 5): the player's row of buttons waits behind one round menu
  button in the bottom-right corner (after 3D's J106.4); a tap shows the buttons beside it and the thumb sticks
  step aside, ✕ hides them; `story.hud` and `?ui=0` hide it too. Your next steps (own icons, hold on the phone
  and another way on a computer, the crosshair, recycling 3D's UI editor) are in `PLAN.md`.
- **2026-11-06** · "I want the floor to be transparent and darker": the template's first room is a dark space with
  a see-through grid floor: a grid whose material has opacity 0 keeps only its lines (live and film), which glow
  softly and fade at their edges and into the distance; the camera's exposure is fixed so the dark stays dark.
- **2026-11-06** · `loop new "My Game"` makes a new project from the template (`projects/my_game/`: its cover page
  named, its first room renamed `my_game_room` and titled; checked; nothing left behind on a problem). 85 tests.
- **2026-11-06** · Perfecting the template (the user's 1, 2, 3, 4): a Sound group and plainer notes (16 settings);
  the first room opens on a grid floor (1-metre squares, soft even light) through a new `grid` pattern whose lines
  stay sharp at any distance (live and film renderers).
- **2026-11-06** · Step 2 of 3, the template (kept to the minimum, the user's word): `projects/template/` is the
  folder every new project is copied from: `template.looped` (a name, its first room, 13 starting settings for the
  camera, things and you, each with its range) and `places/first_room.loop` (empty). Rooms are found in `places/`,
  else beside the cover page. A test keeps the template reading cleanly. 84 tests.
- **2026-11-06** · "Let's get a base template with the settings first", step 1 of 3: cover pages take
  `setting "name", value` lines, each checked as it's read (name, kind, safe range; the problem names its line);
  settings anywhere now refuse a number outside their safe range instead of quietly capping it (the old test of
  the capping changed to the refusal); a project's settings take effect when it opens. 83 tests.
- **2026-11-06** · Tiny goal 1 (the bar, piece 1): the bar's cover page. `projects/bar/bar.looped` names it
  ("The Bar") and its first scene (`bar_room.loop`, empty and dark); the engine reads cover pages
  (`engine/project/`, 2 tests, 82 in all); `loop check|run|play|render|budget` take a cover page; `loop project`
  prints one as JSON for the site; the site picks up projects with a cover page. Found: scene names must differ
  across projects; the other projects still use project.json.
- **2026-11-06** · "For work I want to go super slow … start with a super simple tiny goal … progress myself at the
  same time … it's about growth and progressiveness": the pace is in `CLAUDE.md` (one tiny goal at a time, its idea
  explained first, the user's go, then only that goal).
- **2026-11-06** · A fresh approach (the user guides; the skeleton before the visuals): the bar built from empty,
  one piece at a time; the engine kept whole but split in two sides, core (behind the scenes: fast, small) and
  visual (what you see: extras that cost nothing unless used); `.looped` names a project, scripts stay `.loop`;
  one constant dashboard, **Loop3D Workshop** (https://claude.ai/artifact/AQiT7gWc7teeNhNLt71T5s), fed by
  `tools/dashboard.py` from `engine/modules.json` (the map of sides), `projects/bar/pieces.json`,
  `engine/gaps.json`, git, `perf/`. The light-grid work parked on `parked/light-grid`. The 12 phone runs of
  2026-11-05 kept in `perf/`.
- **2026-11-06** · "Restructure a plan": `PLAN.md` in phases (A smooth and clean, in progress; B projects: the
  project file, the pack, the AI; C Bar3D on Loop3D; D free hosting), each with what you'll see and when it's
  done; a "Needs you" list; the open problems brought up to date.
- **2026-11-06** · Published `cb88328` (GitHub's build, pictured first): simpler distant trees.
- **2026-11-06** · "Resume go": plan step 4, third part: simpler distant trees (levels of detail; the same
  trees, thinned; the shadow pass one version simpler): The Woods 10.7 M → 3.7 M triangles, 9 of 41 scenes over
  budget. `loop budget --set name=value`, a "heaviest shapes" list and copies per level; the picture tool waits
  for the scene's first frame before `AT` (it had paused one at its black fade-in); `?lod=` on links. 80 tests.
- **2026-11-06** · Published `935db67` (GitHub's build, pictured first): skipping what can't be seen, each pixel
  lit once from 6 lamps. To compare on the phone: `?cull=0`, `?prepass=0`.
- **2026-11-06** · Plan step 4, second part: the depth pre-pass is safe (`@invariant`, holes cut in both passes)
  and automatic from 6 lamps: each pixel lit once (the bar 213 → 103 lamp work); 10 of 41 scenes over budget.
- **2026-11-06** · Plan step 4, first part: each pass draws only what its view can see (`render.cull`, on;
  `?cull=0` to compare); the same picture (0.00% of pixels differ, held at the same moment with the picture
  tool's new `AT="filmtime:10,pause:1"`), The Woods 28% less work on the test GPU; 17 of 41 scenes over the
  Phone budget (`perf/budget.md`).
- **2026-11-05** · "Didn't you have more stuff to do before optimization? And go": yes, step 2 first. Built the
  work budget (`loop budget`, `engine/render/frame_cost.*`, 4 tests, 77 in all) and measured every scene:
  `perf/budget.md` (18 of 41 over the Phone budget: forests by triangles, the bar by lamps).
- **2026-11-05** · "I have perms": Emscripten now installs here (the web player builds in 27 s); a cloud setup,
  **J Production (Loop3D)**, does it at every session start. Starting workflows from Claude is still refused by
  GitHub (the app's own permission), so publishing stays on the `gh-pages` route.
- **2026-11-05** · Published `4096756` to https://barcode162.github.io/LOOP3D/ (GitHub's own build, pictured here
  first): no live grain; the bar at its hidden link `#bar` (draws at Phone quality; High is too heavy for the
  software GPU: the next steps). GitHub refuses workflow starts from Claude, so the build went on `gh-pages` directly.
- **2026-11-05** · One of everything: the Showcase (`project/showcase`) and the bar's transfer to Loop3D (stages 1–4,
  `claude/legends-skill-creation-duhg2k`: glTF and JPEG readers, textures live, `projects/bar3d`) joined `main`;
  the hand-off notes and the engine-gaps list folded into `PLAN.md`; the bar's plan is
  `projects/bar3d/TRANSFER.md`; the knowledge base stays on its branch; "Publish the player" now works from
  GitHub. 73 tests, 43 scripts clean.
- **2026-11-05** · Plan step 1, a clean picture: no film grain in the live picture unless `render.filmGrain` is
  on (stills keep a scene's grain); the anti-banding dither no longer changes every frame; a test for it.
  GitHub's **Check** now builds the web player on every push (nothing published).
- **2026-11-05** · "Forget everything I had before, fix this nightmare of rules … FIX HELP"; yes to all four
  (one page of rules, quick checks always, the live-picture checker, WebGPU only; hosting free); Loop3D is the
  engine and the bar and everything else are projects on it. Done: this rulebook (`CLAUDE.md`), `PLAN.md`, this
  log; the old repo's rules, plan tools and session hooks switched off (`Barcode162/3D`).
- **2026-11-05** · "Rate this … /10": 4 / 10; why it's grainy and why it lags (`PLAN.md` → Known problems).
- **2026-11-03 to 05** · Loop3D built: core, geometry, path tracer, Looped Script 2, live WebGPU player,
  physics, animation, sound, editor, settings, Echoes of Ardane (see the git history).
