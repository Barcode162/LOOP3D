# Log

One line per request, newest first: the date, what the user asked, what was done.

- **2026-11-08** · "Go ahead do occlusions": **occlusion culling** (phase 4): two passes, last frame's depth
  pyramid then this frame's, so hidden pieces aren't drawn and nothing coming into view goes missing
  (`render.occlude`, on). Pieces drawn: the Woods 38% fewer, the cabin 40%, the Glade 10%, the bar 2% (open
  room). The meter and recordings now count pieces drawn. Pictures 12 of 12; editor and recorder checks pass.

- **2026-11-08** · "Straight to phase 3": **the change tracker** (`engine/render/change_tracker.h`, 2 tests): the
  sun's shadow kept until something it is made from changes (the Glade standing still about a quarter less
  graphics work on the test machine; on the phone its shadow was 17 ms) (859a26e). **Each pixel's motion**:
  each copy's move since the last frame, read by frame blending through the visibility picture (a board moving
  at 6 m/s: 598 sharp edges with, 346 without, 244 reversed) (aa855af). Pictures 12 of 12; the editor check's
  fixed 2.5 s wait (failed one run in two) now waits for the box; the recording page updated (version 9).

- **2026-11-08** · "I did a new recording" / "Go put live for the fix": the phone (the Claude app) **has the graphics
  chip's timers**, so every step was timed on the iPhone. It also showed a fault: its chip starts a pass early and lets
  it wait, so adding each pass's own time counted bloom up to ten frames. Steps are now timed by how far each pass
  moves the frame's finish line (they add up to the frame; tests for overlap and frames in flight, b90cdb3); the
  recording page updated (version 8). The blur on your screen is the engine's own resolution (Phone 1 pixel a point,
  High dropping to 50% to keep up; Ultra is the live path tracer's look): phase 7's upscaling is its answer.

- **2026-11-08** · "Go ahead phase 2": **the frame graph.** `engine/render/frame_graph.h` (steps that say what
  they read and write, the order checked, times added up per step; tested natively, 5 tests); the renderer's
  frame rebuilt as its steps, the code moved as it was (cdcd3e1, 9fc90ff); **each step timed** by the graphics
  chip's own timers where it has them, in the meter and every recorded part, and **the iPhone's 8-buffer limit
  checked** for every pipeline (8cf3f11). Pictures 12 of 12 (the bar's test had failed one run in two since
  frame blending: photographs now from the unshifted view, references renewed); the same speed measured before
  and after; editor and recorder checks pass; the recording page updated (version 7).

- **2026-11-08** · GitHub's **Check** failed six times today on one thing outside our code: GitHub's release
  server timing out (504) on Emscripten's WebGPU port. The web build now brings its own copy
  (`tools/ports/`, the exact file, BSD-3 and MIT; checked with an empty cache and no network), Emscripten's
  cache is kept between runs, and the web build is tried up to three times. Check green again (b8d97dc).

- **2026-11-08** · "The whole bar doesn't work … skip my side, you know what to do, you have the plan" (the
  reins for phase 1): the bar's visit saved the Courtyard, then nothing, so the recording page now **says what
  went wrong** (problems, the graphics card lost, no WebGPU, page errors, a scene with no picture after 20 s
  with how far its loading got, leaving mid-load; saved at once) and loads the bar's models four at a time
  (memory peak 144-163 → 76 MB in the test browser); recordings list the graphics chip's features (its own
  timers, for phase 2). **`loop budget` counts the cluster way** (the bar 568 → 47 draw calls; 6 of 41 over
  the Phone budget). **`loop score`**: each picture test against cinema, ΔE (`perf/score.md`: average 23.4;
  closed rooms lit by the sky, rooms too bright, patterns missing on Phone, a haze outdoors). Tests: frame_cost
  (cluster way, the engine's own choice), score_test 6, recorder_test 36, recorder_check 19 and 23.

- **2026-11-08** · The first live recording from your phone (pasted, and saved by the page itself: both
  arrived; the page runs in the Claude app): 7 scenes, 3 minutes, kept in `perf/` (`live-20261108-094026-w7cs`).
  The Woods on High: 48 ms of graphics-chip time a frame, 18 fps at half resolution (Nov 5, on Phone, before
  clusters: 159 ms, 6 fps); the Glade on Phone 27 ms, 37 fps; the Courtyard and the Lantern Isle fit 60.
  Freezes at a scene's start (the Glade 7 s on Ultra, the Isle 2 s, the Woods 1.9 s). The bar played was the
  empty new room: on the recording page the whole bar now sits first in the bar's group as "The whole bar".

- **2026-11-08** · "A more direct way for you to record it while I'm playing live … as a feature for the future
  as well": **the live recorder**. The player times every scene played, in parts of 30 s, saved as each ends;
  **the recording page** (https://claude.ai/artifact/EPvB3EZadvnepQiTiTUXpT, private) is the player inside
  claude.ai, saving each visit in its database for Claude to read (`ArtifactData`, then `tools/perf_add.py`); the
  public player keeps it for **Copy my recording**. The page has the whole bar (models as text: claude.ai serves
  no .glb) and one core. Tests: `tests/recorder_test.js` (31, in the quick checks and Check),
  `tools/recorder_check.js` (the real player framed and not, and the published page itself: 20); editor 38 of
  38. Not yet known: whether Safari draws WebGPU inside a claude.ai page (09e03eb, 6e9431c).

- **2026-11-08** · "This, make it my plan and then we go over it" (your page, Loop3D Pipeline Plan: The
  Strongest Combination): it is now the plan. `PLAN.md` gets **the picture pipeline** (six foundations, ten
  phases, measure first) in the place of rebuild step 5; the whole plan, checked against the code, in
  `docs/PIPELINE.md`. Three lines corrected from the code: the live fog is height fog (fog cells are new), the
  bar's 1,894 → 443 was already won by skipping what's off-screen, and the phone was measured on 12 scenes on
  2026-11-05 (the plan had said never). Going over it with you next, one piece at a time.

- **2026-11-07** · Step 4 continued: **grass, leaves and flowers get simpler far away** (separate cards can't
  collapse edges: half of a group's cards are kept, each bigger by √2 so the same area stays covered, tested);
  at their coarsest the Glade keeps 15% of its triangles (was 68%), the Woods 18% (was 29%). Pictures (a new
  bar reference: thin outlines and bottle shelves past a pixel), editor 38 of 38.

- **2026-11-06** · "You know what you're doing go": step 4 continued. **The sun's shadow from clusters**
  (its own cull from the sun's view, detail chosen from the eye; ee3a944). **Scenes open at once**: the cook had
  made first loads slow in the browser (the Glade 4.6 → 19.5 s); now a scene is drawn the old way at once while
  one core cooks its clusters in the background, then switches over (the Glade's first frame 4.7 s, clusters 9 s
  later in the test browser); the meter says which; the cook itself faster (the Glade 5.8 → 3.5 s on one core).
  Pictures (2 new references: grass and leaf edges), editor 38 of 38 (676d727).

- **2026-11-06** · "Here's the problems I found" (your iPhone: a black screen, "storageBufferCount(9) >
  maxStorageBuffers(8)"): the new lighting pass read 9 data buffers and the iPhone allows 8 (Safari reported
  more than it honours). The eyes' exposure now rides in the frame's constants (copied on the GPU each frame),
  so every scene pass reads at most 8; the renderer no longer asks for more than the defaults, so the test
  browser has the iPhone's limit and the picture tests would catch it. Pictures 12 of 12, editor 38 of 38.

- **2026-11-06** · The rebuild, **step 4 continued**: **frame blending** (`render.taa`): the view shifts a fraction
  of a pixel each frame and every pixel is blended with its past (measured without the shifts, so a still view
  stays sharp; kept within its neighbours, so nothing smears), smoothing edges in multisampling's place; the
  **cluster drawing is on by default** (the old drawing stays where a GPU can't give it 9 storage buffers, and
  as `render.clusters` off). New picture references (smoother edges); editor 38 of 38 (7114486). Still to come
  in step 4: the sun's shadow through clusters, cooked trees kept between visits (the cook adds load time:
  about 1.6 s for the Glade on this machine), leaf cards and hard edges simplifying, `loop budget` counting the
  new way. Published (gh-pages a9e7bca) and checked live: the bar draws through clusters with no errors, on
  4 cores.

- **2026-11-06** · The rebuild, **step 4 (drawing rebuilt), first half**: drawing in clusters, Nanite's way,
  behind `render.clusters` (`?clusters=1`; off until step 4 is done): the cook's trees in one pool on the GPU,
  a GPU cull that picks each cluster's level (`render.clusterError`, pixels) in a fixed order (no flicker), one
  draw for everything into a visibility picture, each pixel lit once from its exactly rebuilt triangle. Against
  the old drawing 11 of 12 pictures within 1.4/255 (the bar 4.2: a light strip flush with a beam shows). On
  this machine's software GPU: the bar 5.1 → 1.4 s a frame, the Glade 4.6 → 1.7, the Woods 4.6 → 2.7. Next:
  frame blending (edges), shadows through clusters, then on by default (c20ce35).

- **2026-11-06** · The rebuild, **step 3 (the library and the cook)**: **the library** (`library/`, every
  project's): each entry a text recipe with its card (what, how to place it, credit, licence, from where);
  `use "lantern"` finds it; the first five come out of the projects (the isle's lantern and sailing boat, the
  Showcase's room shell, plinth and stone beam), which now use them (same pictures, the isle plays through);
  `loop library`; a test refuses an entry without a licence; the editor's Build space places them with a tap
  (editor 38 of 38); the rulebook's new rule: every goal grows the library (6745537). **The cook**: a simplifier
  that never moves a vertex, cluster trees the Nanite way (groups simplified with their edges kept: any view
  meets without cracks, tested; a sphere draws 19,064 triangles at 2 m, 80 at 200 m), each mesh cooked once by
  its fingerprint on every core; `loop cook`: the bar in 0.46 s, the Glade in 1.6 s (1f3cf84). Left for step
  4: leaf cards and hard-edged models don't simplify yet (two thirds stay at the coarsest). Published (gh-pages
  d43f5af): GitHub's Pages service timed out deploying it, so it went out again from the next commit.

- **2026-11-06** · The rebuild, **step 2 (more than one core)**: a scene's model pictures wait packed while its
  script runs, then are decoded together on every core (the bar's load was nearly all JPEG decoding: 4.2 →
  1.05 s natively); the web gets a many-core player beside the one-core one, chosen by the page (our own small
  service worker, `web/isolate.js`, makes the page isolated so cores can share memory; one reload on a first
  visit; `?cores=1` asks for one core), both with SIMD; test browser: the bar's first frame 7.1 → 4.7 s; the
  meter says how many cores work; pictures and the editor unchanged (2a2688e). Steps 0 and 1 are on the live
  player (gh-pages 4f42392, built here with the Check's own toolchain: this session can't start the publish
  workflow or fetch the Check's artifact).

- **2026-11-06** · The rebuild, **step 1 (the one clock)**: the day, the film's time and the world all step on
  the fixed clock (not the frame rate); each thing keeps where it was and where it is, and the drawing blends
  between the two steps (smooth at any screen rate) and is sent only what moved: the Glade's sending 1.71 →
  0.26 ms a frame, its CPU frame 4.1 → 2.6 ms (this machine); the walking body moves in fixed steps, the eye
  drawn between them. The things stay one list (`Stage::placed`) the drawing is made from; handles that
  survive removal wait until things can be removed. Pictures, sameness and the editor unchanged (f7b4852).

- **2026-11-06** · The rebuild, **step 0 (the safety net)**, done: **the meter** (tap the stats: each frame's
  CPU time by part (scripts, physics, people, sending, drawing), the GPU, memory, against the 16.7 ms line;
  5dafe42); **the sameness test** (a scene run twice gives the same world, bit for bit; it found that every
  script shared one stream of random numbers, so a scene changed with whatever ran before it: fixed, 79a74b0);
  **picture tests** (`tools/picture_check.js`: 12 scenes held still and compared with `tests/pictures/`; two runs
  match exactly; 71f9616). First reading on this machine's software GPU: the bar's CPU needs 4.4 ms a frame, the
  graphics chip is the slow side.

- **2026-11-06** · "Lighting, a full skeleton, physics, universal animations, performance, a library that grows
  … Nanite, water, world generation … play chess … a ragdoll as real as possible that tries to stand up and
  grabs hold … new ways to calculate": **the rebuild**, planned with you and written into `PLAN.md`: ten moves
  ahead decided first (one clock, a flat world, clusters, recipes cooked once, smart light, one skeleton, water
  in two layers, people always physical with human data, worlds calculated like nature, what things offer),
  then 14 steps from the safety net to memory; Phase A folded in.

- **2026-11-06** · "Try making a full game of your choosing to try the capabilities of our engine": **The Lantern
  Isle** (`projects/lantern_isle/`, made with `loop new`): an opening film, the island you play (light five
  lanterns before the sun goes down; each one lit wakes another voice of the music; the keeper guides you), and
  an ending film (the boat home, your time and best time). It found and fixed five engine gaps: island land
  (`terrain … island: yes`), standing on docks, bridges and stairs (`World::floorUnder`, tested), an upright
  phone keeping the lens's width (`cam.portraitWide`, tested), shared scripts in a project (`*_common`), and water
  that isn't a floor; two left open (blurry stars, overlapping lines). Every scene fits the Phone budget.
  Published (ad5db47 → gh-pages 2fc7633); played on the live site from the opening to the island.
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
