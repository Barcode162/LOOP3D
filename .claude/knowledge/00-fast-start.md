# Loop3D in one page (read this first)

Tailored for Claude. Dense on purpose. Deeper files are linked at the bottom.
Verified against the code at commit `a56c14c` (2026-11-05); run
`.claude/tools/knowledge_stale.sh` to see what changed since.

## What it is
J Production's engine. **From-scratch C++20, no outside libraries** (own PNG/zlib,
JSON, maths, threads, synth). About 18k lines. Its language is **Looped Script**
(`.loop` files): plain words → a world you walk through (game) or a film that plays
itself (movie). Two renderers draw the same `Scene`:
- **Live**: WebGPU in the browser (`engine/gpu/`, `web/player.cpp`), qualities
  Phone / High / Ultra (Ultra = GPU path tracer).
- **Cinema**: CPU path tracer (`engine/render/pathtracer.*`) via `loop render`.

## The bigger picture
LOOP3D is the fresh rewrite of the engine from the user's original repo
**Barcode162/3D**, which runs the live site and bar (joshuavelasco.com/bar) on the
first Looped. The user, their taste and their rules: [user.md](user.md). The
original's systems, assets, requests and lessons: [origin-3d/](origin-3d/README.md).
Several LOOP3D gaps (glTF, skinning, WebGL2, LOD, multiplayer) are solved there
([origin-3d/port-map.md](origin-3d/port-map.md)).

## The pipeline (one line each)
```
.loop text ─lexer─▶ tokens (units → metres/seconds/radians, indent → blocks)
           ─parser─▶ AST (ast.h) ─compiler─▶ bytecode Proto (bytecode.h)
           ─Vm─▶ runs top level; every/after/when/task become fibers that can `wait`
scene words (scene_api.cpp) fill a Stage { Scene, Film(shots), physics::World,
           audio::Mixer, people (anim::Animator), ui messages, nextScene }
Stage.update(dt) moves physics, people, sounds  ── version counters tell the
live renderer what to re-sync (version / transformsVersion / lightsVersion)
Scene ─▶ gpu::Renderer::draw (live)   or   PathTracer::render → develop() (cinema)
```

## Directory map
| Path | What |
|---|---|
| `engine/core/` | math (Vec3, Quat, Mat4, AABB, Ray), random, ease curves, image+PNG+zlib, JSON, jobs (thread pool, inline on web), log, file, **settings** (the one registry) |
| `engine/geometry/` | Mesh + shapes, BVH (binned SAH), noise, terrain (hydraulic+thermal erosion, Poisson disk), plants (trees, rocks, grass) |
| `engine/render/` | Scene description, materials by name (59), BSDF, patterns, sky (Rayleigh/Mie/ozone), film (exposure, grade, AgX/ACES), shots, instanced scene BVH, path tracer + denoiser, demo scenes |
| `engine/gpu/` | WebGPU live renderer (`renderer.cpp`, 1.6k lines) and WGSL shaders as C++ strings (`shaders.h`, incl. `kUltraWgsl`). Built only under Emscripten |
| `engine/physics/` | rigid bodies (sphere/box/capsule), sequential impulse + warm start, sleeping, joints, XPBD cloth, character controller, raycast |
| `engine/anim/` | skeleton, pose, clips, blend, two-bone IK, FABRIK, springs, procedural person Animator |
| `engine/audio/` | synth voices (one-shots + ambiences), 3D Mixer (ear delay, air, reverb, limiter), WAV out |
| `engine/script/` | lexer, parser, ast, compiler, bytecode, vm (fibers, GC), stdlib, **scene_api** (all world words, 1.1k lines) |
| `tools/loop.cpp` | the `loop` CLI: check, run, play, render, demo, settings list, words |
| `web/player.cpp` | WASM entry: frame loop, walk camera, scene sync, `loop_*` exports to JS |
| `web/index.html`, `web/loop_ui.js` | the player page |
| `web/editor/` | the editor (plain JS): editor.js, panels.js, ui.js, engine.js (bridge), settings_schema.js (~270 settings), words.js (generated) |
| `tests/` | own tiny runner (`TEST`, `CHECK`, `CHECK_NEAR`), 69 tests in 10 files |
| `projects/` | samples (cabin, courtyard, the_gate, the_woods) and echoes_of_ardane (24 scenes + common). Engine never depends on them |
| `docs/` | LANGUAGE, STORY_GUIDE, TUNING, SETTINGS (generated) |
| `perf/` | benchmark runs (`perf/raw/`, `runs.json`, `segments.json`) |

## Commands
```sh
cmake -B build -DCMAKE_BUILD_TYPE=Release && cmake --build build -j   # native: loop + loop_tests
./build/loop_tests [filter]                                          # all must pass
./build/loop check|run|play|render <file.loop>   # run --seconds N, play --choices 0,1, render --quality draft
./build/loop settings list  |  ./build/loop words json|js
node tools/check_settings.js                     # editor and engine settings agree
node tools/settings_doc.js > docs/SETTINGS.md    # regenerate, never hand-edit
./build/loop words js > web/editor/words.js      # regenerate after adding words
./tools/build_site.sh                            # needs Emscripten → site/
xvfb-run -a node tools/web_check.js <url> out.png  # browser check; NEVER headless (loses WebGPU)
```
Compiler flags are `-Wall -Wextra -Werror`: a warning breaks the build.

## House rules (break none)
1. **No outside libraries** in the engine. Write it ourselves.
2. **Real units everywhere**: metres, seconds, lumens, kelvin, lux, nits, mm, f-stops.
3. **One name per setting** everywhere: add to `Config` + `Settings::Settings()` in
   `engine/core/settings.*` *and* `web/editor/settings_schema.js`, then run
   `check_settings.js` and regenerate `docs/SETTINGS.md`.
4. **Plain words** in the language, errors with line + "did you mean…?".
5. **Live and cinema must agree**: a material/look/light change goes in both
   renderers (`render/` and `gpu/shaders.h`). `film.h` says the GPU matches its numbers.
6. **Docs stay honest**: README "Honest status", TUNING tables (Done/Next/Later).
7. **Smoothness first**: steady frame time > low delay > fps > picture.
8. Comments and docs use plain, short English (the house voice).

## Traps already paid for (see history.md)
- Headless Chromium loses the WebGPU device: every GPU number measured that way is fake.
- SwiftShader runs pixel shaders once per MSAA sample: test shader cost with `&msaa=1`.
- A single shadow ray per pixel ≈ all other lighting. Phone traces no rays.
- Auto resolution must read **real time and GPU done-times**, not skipped frames.
- GPU objects must be destroyed explicitly (the browser frees late), and buffers
  sized to the adapter's limits (128 MB default).
- Changing a shape's material or adding shapes while playing re-reads the whole world.

## Go deeper
- [architecture.md](architecture.md): data flow, frame loop, change tracking, settings layers
- [where-is.md](where-is.md): "I need to change X" → the file and function
- [subsystems/](subsystems/): one file per engine folder (API, internals, tests, gaps)
- [language-quickref.md](language-quickref.md): every Looped Script word on one page
- [recipes.md](recipes.md): step-by-step for the common changes
- [history.md](history.md): how the engine was built and what went wrong
- Missing components: [../notes/engine-gaps.md](../notes/engine-gaps.md)
