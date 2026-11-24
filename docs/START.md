# Start here: every chat reads this, then `docs/NORTH.md`, then only what the goal needs

This page replaces reading `PLAN.md` and `LOG.md` at the start. Together they are about 90 KB, and this page is
about 5 KB. They are still the record: open them only when the goal touches them, and search them for the lines
you need (`grep -n "<word>" PLAN.md`) rather than reading them whole.

## 1. The start, in four steps (the user types `/start`, or just says what they want)
1. `git pull origin main` · `python3 tools/claims.py --remote` (the board). If this chat is the only one working, say so.
2. Read `docs/NORTH.md` (the user's rule: every chat). Then **section 3 below** (where we are) and **section 4**
   (the words), and nothing else yet.
3. Find the goal's row in **section 5** (where to look) and read only those lines.
4. Say the goal in one sentence and the one idea it teaches, then wait for the user's "go" (`CLAUDE.md`, the pace).

## 2. The template, the same every job (`CLAUDE.md`, Working)
claim → branch → build only the goal → quick checks (`cmake -B build -G Ninja -DCMAKE_BUILD_TYPE=Release &&
cmake --build build && ./build/loop_tests && node tests/editor_source_test.js && node tests/recorder_test.js`) →
slow checks only if the picture or editor changed → commit, push → the log line goes in the claim's `note:` →
reply short: what changed, what to look at on the phone, what's next, and "a new chat is cheaper for the next goal".

## 3. Where we are (the deployer rewrites this section each time it ships; keep it under 15 lines)
*Last written 2026-11-18.*
- **Top priority: the Loft** (`projects/loft/`, PLAN "Now"). Goals 1 to 8 are in: the shell, night, the upper
  floor and stair, stair strips, the bones, pools of warm light, the city through the glass, furnished from 62
  library entries, and the room mirrored in the glass (M60). Lamp work is 25 against the Phone budget's 24 (the
  mirror adds 2). **Next: the lamp grid (it adds about twice the lamps a pixel needs), then goal 9, marked and
  timed on the phone (LOFT9).** Open: the bed pillows don't show; cushions look stepped; the public player still
  shows the Loft before goal 2.
- **Paused: the Studio** (track T, `docs/STUDIO.md`, PLAN "Beside it"). The skeleton, skin and live bending,
  pull a corner, and extrude a face are done. Next when it resumes: goal 19b (inset, then bevel, loop cut), or keyed bones.
- **Needs the user** (PLAN "Needs you"): play on the recording page; the three IONOS secrets for the bar (phase C).
- **The library's Scripts shelf** (2026-11-23): camera, controls and a scene starter any project drops in; the editor drags library entries onto the view. Next there: a follow camera, a third-person walk.
- **Light on the phone** (2026-11-24): soft lamp shadows, panels cast them, moving lamps keep them; the Dark Room (samples). Still short of film: light bounced off things, and 2 shadowed lamps at a time.
- **Not built yet:** the Loop3D Workshop dashboard page.
- **The board:** empty. main is green (297 tests).

## 4. The words (what things mean here)
| Word | Means |
|---|---|
| **engine / project** | `engine/` is Loop3D itself and never knows a project. A project is a game or world in `projects/<name>/`; its cover page `<name>.looped` names it and its first scene. |
| **scene / place** | One `.loop` file you can play (`projects/<p>/places/*.loop`, samples in `projects/samples/`). |
| **Looped Script** | The language (`.loop`). Words like `box`, `light`, `strip`, `group`, `pull`, `extrude`, `skin`. The reference is `docs/LANGUAGE.md`; search it, don't read it whole. |
| **recipe** | Text that the engine turns into a thing. Text is the source; nothing is stored baked. |
| **library / entry / card / piece** | `library/<kind>/<name>.loop`, shared by every project. Each entry has a card (what it is, how to place it, credit, licence). A *piece* is a building part (beam, column, joist). `loop library check` checks them. |
| **DRAFT** | A library entry below its rubric bar (`docs/LIBRARY_PLAN.md`). |
| **film / live** | Two renderers. **Film** is the slow, perfect picture made with rays (`loop render`). **Live** is the phone's WebGPU picture, every frame. The same scene feeds both. |
| **the player / the editor** | The web page that plays scenes (public: barcode162.github.io/LOOP3D, from `gh-pages`) and `editor.html`. |
| **bench** | A tool panel in the editor: the Shape bench (Points, Faces), the Move bench (keys, timeline). |
| **recording page** | The private player inside claude.ai. Every run the user plays there is timed into its database (`runs`). "Check my runs" means read it with `ArtifactData`. |
| **Phone budget / lamp work** | What fits one frame on an iPhone 15 Pro Max (`loop budget`). Lamp work is the lamp cost, out of 24. |
| **picture tests** | The 12 live pictures in `tests/pictures/` (`tools/picture_check.js`, about 4 min, only when the picture changed). |
| **goal** | One step the user sees on the phone, numbered inside its project (Loft goal 8). |
| **order / card / track** | A work order in `docs/WORK_ORDERS.md` with a code: the letter is its track (S shapes, M surfaces, N night lights, W land, A water and air, U screens, E many hands and tools, F film, T the Studio, L-name library entry, J the bar's jobs, LOFTn a Loft goal). |
| **claim / board / wave** | A claim (`work/claims/<code>.md`) says which files a job holds. The board is all claims (`tools/claims.py`). A wave is jobs run side by side (`docs/PARALLEL.md`, `docs/CONDUCTOR.md`). |
| **deployer** | The only one who merges into `main`, writes `LOG.md`/`PLAN.md` and section 3 here. |
| **framed / working / finished** | The three honest states (NORTH 4). Never call *working* finished. |
| **the reins** | The user hands over a bigger goal: build all of it in checked steps, then report at the end. |
| **"go"** | The user approves the goal you just proposed. |
| **phases** | PLAN's long order: A smooth, B projects, C Bar3D on Loop3D, D free hosting. |

## 5. Where to look (read only the row the goal is in)
| The goal touches | Read |
|---|---|
| the Loft | PLAN "Now · The Loft"; `docs/WORK_ORDERS.md` section 13 (search the LOFT code); `projects/loft/` |
| the Studio, bones, the Shape or Move bench | PLAN "Beside it"; `docs/STUDIO.md`; `docs/EDITOR.md` |
| a language word | `docs/LANGUAGE.md` (search the word); `engine/script/` |
| a library entry | `docs/LIBRARY_PLAN.md` (its kind's section); a neighbour entry in `library/` |
| how the picture looks (light, shadow, surfaces) | `docs/PIPELINE.md`, `docs/CRAFT.md`, `docs/SURFACES.md`, `docs/rubrics/` |
| settings or the cover page | `docs/SETTINGS.md` |
| the player, publishing, the recording page | `docs/TUNING.md` (section 5); `web/` |
| many chats or shipping | `docs/PARALLEL.md`, `docs/CONDUCTOR.md`, `.claude/agents/deployer.md` |
| which model to use | `docs/MODELS.md` |
| what is already finished | `docs/DONE.md` (search) |
| the long view or the next milestone | `docs/ROADMAP.md` |
| what happened recently | the first 3 lines of `LOG.md` only (`head -3 LOG.md`) |
