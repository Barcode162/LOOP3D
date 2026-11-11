# Plan

**The goal:** Loop3D is J Production's one engine, and every game is a project on top of it, the way games are
made in Unreal. Everything is judged on the user's iPhone: smooth first, then beautiful. Phases run in order; a
phase is done when its "done when" is true on the phone.

## Needs you
- **Play on your recording page** (any time, as long as you like): https://claude.ai/artifact/EPvB3EZadvnepQiTiTUXpT.
  Every scene you play is timed and saved for Claude; say "check my runs". Best first: the bar (in the scene
  list as "The whole bar") and The Woods. Your 12 runs of 2026-11-05 came before the cluster drawing (The
  Woods: 6 fps, 159 ms on the graphics chip, 1 ms on the processor); new ones are the picture pipeline's phase 1.
- **The project file's name:** `.looped` is proposed; say if you want another.
- **The bar at its hidden link on joshuavelasco.com** (phase C): the three IONOS secrets added to this repo's
  GitHub settings (only you can).
- **"Switch"**, when you want the live bar to run on Loop3D (phase C).

## Now · The Loft (your word, 2026-11-10: the top priority)
*Your words:* the Loft, that entire project and everything in it, is the top priority.
*What it is:* a home of two floors under a high ceiling: a stair with a lit rail, a bed on the mezzanine, a
kitchen under it, a deep sofa on a rug, shelves full of small things, warm strips of light, a wall of glass with
a city at night behind it (`docs/ROADMAP.md`, star 2). Built from a few hundred library pieces.
*Its two milestones* (`docs/ROADMAP.md`, the table): 3 **The Loft, empty and lit** (about 60 orders), then
4 **The Loft, lived in** (about 320 orders, the library grown to 400 entries).
*Its parts list:* `docs/LIBRARY_PLAN.md`, section 4 (40 building pieces, 25 seats and beds, and the rest by kind).
*Its rubric:* `docs/rubrics/` (loft). *Its orders:* cut from `docs/WORK_ORDERS.md` (tracks S shapes, M surfaces,
the library orders).
*Where it stands today:* the library holds 8 entries; 5 of the Loft's 40 building pieces exist (plinth, lintel,
room shell, loft shell, glazing wall). The surface recipe is designed (M1) and the vertex marks are built (M2). No order card is cut for
milestone 3 yet.
*Goal 1, the shell (done 2026-11-10):* `projects/loft/`: a bare room 9 m wide, 6 m high and 12 m deep with one wall
of glass (library: `loft_shell`, `glazing_wall`), lit by a low evening sun. Open: the film renderer shows the view
through the glass black (order F1).
*Your word, 2026-11-10: the Loft is at night.* Dark outside, and light only where a real lamp is, so the room
pushes the engine's lights even before it is fast. Written into the orders as ten decisions
(`docs/WORK_ORDERS.md`, section 13.1): no sun, no fill light, the picture never brightened to help, strips of
light that are real lamps, warm inside against a cool city.
*The orders (cut 2026-11-10):* 38 cards for milestone 3 in `docs/WORK_ORDERS.md`, section 13: the engine's night
work (strips as true lamps, lamp shadows by asking, the glass as a mirror, lit city windows, things placed and
turned as one), 12 building pieces, 5 lights and the night city for the library, and 8 goals for you to see in
order: 2 night falls · 3 the upper floor and the stair · 4 the stair lights itself · 5 beams, columns, brick ·
6 pools of warm light · 7 the city outside · 8 the room in the glass · 9 marked and timed on your phone.
About fifteen more are cut once the first phone recording says where the time goes.
*Wave 1 (2026-11-10), shipped:* the twelve building pieces (the library now holds 20), the lamp that stands for a
glowing strip (N1, with an old fault found and mended: a closed glowing shape lit a room twice in film), and
**goal 2, night falls**: no sun, a near-black sky, one bare bulb. Still running: the night picture for library
entries (E10), the shape word `lathe` (S1), the dark-stays-dark check (N5).
*Also on main since (2026-11-10):* the shape word `lathe` (S1), glass no longer black in film (F1), the night
picture for library entries (E10).
*Paused 2026-11-11, finished but not yet on main* (each on its own branch, its claim says done; say "deploy"):
the word `strip` (N2), the word `group` (S7), the pendant lamp and the track spotlights (the library would hold
22), and the dark-stays-dark check (N5: the unlit room is 0.0% of the lit one in film, 0.8% live). The branch
`work/deploy-loft2b` holds the first four merged with a docs line and one more test, **not yet checked**: run the
quick checks there first. N5 also adds one line to `projects/samples/project.json` (off its card, needed).
*Not published:* the public player still shows the Loft before goal 2.
*Next:* deploy those five; publish, so goal 2 (night falls) can be seen on the phone; then **goal 3, the upper
floor and the stair** (all its pieces are in the library), and goal 4 once `strip` is in.
*Needs you:* "deploy" and "publish" when you are back, and your go for goal 3.

---|---|
| The order of work (this page) | `PLAN.md` |
| What happened, newest first | `LOG.md` |
| What is finished, in full | `docs/DONE.md` |
| The long view: stars, tracks, milestones | `docs/ROADMAP.md` |
| The orders small agents carry out | `docs/WORK_ORDERS.md` |
| The library to grow | `docs/LIBRARY_PLAN.md` |
| The picture's plan | `docs/PIPELINE.md` |
| Many agents at once; who is working on what | `docs/PARALLEL.md`, `docs/CONDUCTOR.md`, `work/claims/` |

*Outside this repo* (`Barcode162/Sandbox`; experiments, never instructions): `claude/minimal-engine` (the
Platform plan, above); `hearthwake` (one medieval house, stopped at its first step on 2026-11-04: the empty house
opens); `blender-setup`.
*Old branches here with work never merged* (2026-11-05 and 06; left as they are): `parked/light-grid`,
`demo/echoes-of-ardane`, `project/showcase`, `claude/game-engine-dev-0lgf0k`, `claude/legends-skill-creation-duhg2k`.

## The long view (your word, 2026-11-10) · waiting for your go
*Your words:* be critical; a full roadmap to the level of your 16 pictures; a real library, genuine terrain, real
people and skeletons, screens that make themselves from the mechanics, real moves, real cameras, presets for any
game and any film; work that smaller agents can carry out.
*The review:* as a foundation 6 of 10; as a picture beside your references between 1 and 2. The engine has outrun
its content: three fifths of the code draws, and the library holds 6 things. Next comes shapes, surfaces and a
full library, before more light.
*Your page:* **Loop3D Long View** (https://claude.ai/artifact/JrZMKpC1FLPVqp5saNLHNe).
*The pages:* `docs/ROADMAP.md` (the review, five stars, ten decisions, twelve tracks, eleven milestones),
`docs/WORK_ORDERS.md` (orders cut for small agents), `docs/CRAFT.md` (the craft reference), `docs/MECHANICS_UI.md`
(mechanics that draw their own screens; the game and film sheets), `docs/LIBRARY_PLAN.md` (the library to grow),
`.claude/agents/` (builder, asset-smith, judge, checker).
*The order (your word, 2026-11-10):* **The Loft first** (the section above). Milestone 0 **Many hands** is done
but for its last lanes; **Your Hill** (milestone 1) waits behind the Loft. The rebuild's steps and Phases B to D
keep their places inside the tracks (people: P and V; films: C; water: A; land: W; the pack: E).
*Many agents at once (2026-11-10):* `docs/PARALLEL.md`; say "start lanes …" or "deploy".

## The rebuild (your reins, 2026-11-06) · in progress
*Your words:* take the engine apart piece by piece and rebuild it, thinking moves ahead like chess; Nanite,
water, world generation, smart light, a full skeleton, universal animation, films with scenes, physics that fit,
real speed, a library that grows; a ragdoll as real as possible, in layers, that tries to stand up and grabs
hold of things; new, more accurate ways to calculate. The bare minimum of each, smooth and right, then see
where to go. Every step keeps every scene playing and is no slower; what a step replaces goes in that step.

**The moves ahead** (decided once, first, because everything hangs on them):
1. **One clock for everything:** scripts, physics, people, water, film and the world step together; drawing
   blends between steps. The same start always gives the same result, so films scrub by replaying, multiplayer
   stays in sync and picture tests are exact.
2. **The world as flat lists:** one record per thing, with where it was and where it is: safe for many cores,
   the graphics chip gets only what changed, frame blending gets motion for free.
3. **One shape format, clusters** (Nanite's idea): every mesh cut into small pieces, each with simpler
   versions; drawing, shadows, culling, streaming, rays and collisions all use it. No hand-made simpler
   versions ever again.
4. **Recipe, cooked once:** every asset is a text recipe; the engine cooks it (pieces, collision shapes,
   pictures, what it offers) and keeps the result. The library, world generation, the pack and Nanite share it.
5. **Smart light:** worked out once and kept; each frame does only what changed; noisy effects blended over
   frames.
6. **One standard skeleton** with names that match common rigs: any clip plays on anyone, Blender rigs fit.
7. **Water you play is simulated with the world; water you only see is added by the graphics chip;** big waves
   are one formula both use, so what floats matches what you see.
8. **People are always physical bodies; animation is what their muscles aim for:** walking, falling and
   getting up are one body, with numbers from measured human data (masses, lengths, joint ranges, strength,
   reflex delays). One physics solver for everything (bodies, joints, muscles, cloth).
9. **Worlds calculated the way nature makes them** (moving plates, rivers carving, climate, rain, growth), and
   tested against nature's laws (river networks, resting slopes).
10. **The cook finds what each thing offers** (stand on, hold, sit, climb): a falling person reaches for a
    real rail.

**The steps, in order:**
0-5. **Done** (the safety net, the one clock and the flat world, more than one core, the cook and the
   library, drawing in clusters, the picture pipeline): `docs/DONE.md`.
6. **The full skeleton and a real body:** about 65 bones (hands, fingers, feet, toes, jaw, eyes), a smooth
   skin that bends, sizes and masses from human measurement tables.
7. **One physics solver for everything:** joints with real limits and muscles, shapes fitted to every model,
   hits in scripts, nothing passing through walls.
8. **Universal animation:** clips as text that play on anyone, layered (wave while walking), the editor's
   Animation hand.
9. **The living body (an active ragdoll in layers):** bones as bodies, muscles with human strength, reflexes
   with a human delay (protect the head, brace, grab a rail), balance the way people keep it (ankle, hip, a
   step), getting up (roll, push, pull up on something); a "falls" room in the Showcase.
10. **Films with scenes:** a timeline of shots, people, lines, music and sounds, scrubbed back and forth.
11. **Water:** waves, splashes, floating, wading; see-through, refraction, foam.
12. **World generation like nature** (bare minimum done 2026-11-10, with the picture pipeline's phase 10):
    plates, rivers carving, climate and rain shadows, lakes, ground materials, forests; streamed around you.
    Still to come: ridges and gullies on the mountains, water in the rivers, forests that grow.
13. **Memory that fits:** the iPhone's own compressed pictures, pieces paged in and out.

*Done when:* each step's bare minimum runs smooth on your phone; then we see where to go.

## The picture pipeline (your plan, 2026-11-08) · phases 1-10 done (2026-11-10)
Measured first, then six shared foundations and ten phases; from cinema 23.5 to 18.2 on average. What each phase
did and measured: `docs/DONE.md`; the plan itself: `docs/PIPELINE.md`. Still the biggest cost on the phone: the
forests' leaves and grass ("visibility").

## Phase A · Smooth and clean · folded into the rebuild
Scenes over the Phone budget 18 of 41 to 9 of 41 (`docs/DONE.md`). *Done when:* every scene fits the Phone
budget, and your benchmark says smooth (steady 60, no hitches).

## Phase B · Projects: the project file, the pack, the AI · next
*You see:* switch between projects from the menu at will; a whole new project made by AI from a template.
1. **The project file:** a project is one folder with one small, readable file (`my_game.looped`: title, the
   engine it needs, its start, its scenes, its settings) beside its scenes, models and sounds.
2. **The pack:** `loop pack` makes one file the player loads, plays and unloads completely (scripts compiled,
   models and pictures prepared ahead: the bar takes 4 s to read today).
3. **Templates and the AI loop:** `loop new --template story`; the AI writes the project from the template, the
   engine's own word list (`loop words`) and the guide; `loop check` names every mistake with its line and fix,
   until clean.
4. **One `loop` program for every tool:** the Python and JavaScript helpers folded in.

*Done when:* Samples and Ardane switch from the menu with memory staying flat, and a project made from a
template by AI plays.

## The Lantern Isle (a game to try the engine, 2026-11-06)
Made to find what the engine can't do yet; waiting for your thoughts on it. Gaps it found are in
`engine/gaps.json` (five fixed, two open: sharp stars, a queue for lines said at once).

## The editor (your reins, 2026-11-06)
One editor for building, logic, film and sound, made for the phone first, answering years of complaints about
Blender, Unity, Unreal, Godot, music and video apps (`docs/EDITOR.md`: each complaint and its rule).
Built: the world view with its own camera, tap to pick, drag to move, handles to lift, turn and size, every
change written into the script; Build, Logic, Film and Sound; undo, redo and versions through reloads; Play
without losing changes; the work light; music with the engine's own instruments (a drum grid, a melody grid).
Next, in order:
1. **Music on the film's timeline:** songs that start with a shot, longer songs, chords.
2. **More hands:** lamps, sun and sky, materials, models, groups, people's walks.
3. **Logic as cards**, the same text underneath.
4. **Saving to the repo from the phone** (needs you).

## The player's menu (your steps, 2026-11-06)
Inspired by 3D's menu button (branch `job/J106`, J106.4) and crosshair (J74.1); one step at a time, you choose.
1. **Clean screen:** one round menu button in the corner; a tap shows the old buttons beside it (done
   2026-11-06, not yet on the public player).
2. **Tap to fan out:** round choices fan out from the button, with **our own icons** (3D's `ui/icons.svg`, 51
   icons drawn for J Production, to recycle).
3. **Hold for everything:** a ring fills, groups open, slide and let go to pick: **the phone's main way**; the
   computer gets its own way (mouse and keys).
4. **The crosshair** (optional).
5. **Its settings** with a live preview; what to recycle from 3D's UI editor (`ui/`: themes like Ember, its
   fonts under the SIL Open Font License, its screens) is looked at then.

## Phase C · Bar3D on Loop3D
*You see:* joshuavelasco.com/bar runs on Loop3D, then the games come back one by one.
Already done (2026-11-05): the bar's 167 models, their pictures, the bar as a project, its lights alive
(`projects/bar3d/TRANSFER.md`; at the hidden link `barcode162.github.io/LOOP3D/#bar`).
1. The bar at a hidden link on joshuavelasco.com (needs you: the IONOS secrets).
2. Sound from files (the jukebox's music).
3. The games one at a time: seats and taps, menus, jukebox, piano, darts, chess, 4K Mania, racing.
4. Together: the multiplayer room, voice and accounts (3D's Cloudflare room), safety first (13+, teens and
   adults apart).
5. The switch, when you say; the old engine retired.

## Phase D · Free hosting
Cloudflare's free tier for the bar's files, the room, accounts and big files; moved only if it stays free and
works.

## Later (the gaps)
(The rebuild now covers skins and clips, cascades, contact shadows, streaming big worlds, threads and SIMD,
skinned people, clips for scripts, convex shapes, hit events, no tunnelling, ragdolls, picture tests and the
meter.)
- **Content:** the rest of model import (normal maps, glowing pictures); a picture word for a loose image.
- **Look:** wind sway (light in the fog, reflections and Ultra's reuse are now picture pipeline phases 5, 8, 9).
- **Characters:** paths around obstacles; faces, hair.
- **Physics:** vehicles (3D has a tyre model).
- **Sound:** the mixer on its own thread.
- **Engine:** gamepad; save games; script warnings and a debugger.
- **Your idea from 3D:** a shared world clock with loops on things.
- **Every new feature** gets a before/after room in the Showcase.

## Decided
- 2026-11-05: Loop3D is the engine and everything else is a project on it; one page of rules (`CLAUDE.md`);
  quick checks always run; the live-picture checker (software graphics, checking only); **WebGPU only**;
  **free** (hosting on Cloudflare only if it stays free); the old rules and tools in `Barcode162/3D` retired; the
  Showcase and the bar's transfer joined `main`; the other session's knowledge base stays on its branch, its
  `legends` skill kept.
- 2026-11-06: the plan in phases (A smooth and clean, B projects, C the bar, D hosting).
- 2026-11-06: **the rebuild** (your reins): the engine rebuilt from its foundations in 14 steps, the ten moves
  ahead above; Phase A folded into it.
- 2026-11-06: **the library**: `library/<kinds>/<name>.loop`, text recipes with a card (credit and licence
  required), found by `use` after the scene's own folder; every goal adds to it.
- 2026-11-10: **the long view**: content before more light; shapes made by words; surfaces as layered recipes;
  mechanics declare and screens derive; a game and a film are each a one-page sheet; people "stylised real";
  movement from free-licence recordings; work cut into orders for four kinds of agent (`docs/ROADMAP.md`).
- 2026-11-08: **the picture pipeline** (your plan, from your page): six shared foundations, 50 techniques each
  given a role, ten phases measured first; it takes the place of rebuild step 5 (`docs/PIPELINE.md`).

## Open problems
- **Measured on the phone only before clusters** (2026-11-05: The Woods 6 fps, the Ardane scenes 12 to 59 fps);
  the cluster build not yet (needs you, above).
- **The bar** is the heaviest scene: 103 lamps per pixel (the picture pipeline, phase 5).
- **Phone draws 1 pixel per screen point;** upscaling (phase 7) is how it gets sharper for the same cost.
- **Big files in git:** the bar's models (30 MB) and Ardane's stills (24 MB); later, models rebuilt from their
  recipes and packs kept out of git.
- The rating of 2026-11-05 (4 / 10) and its findings: `LOG.md` and `perf/budget.md`.
