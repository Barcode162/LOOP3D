# Architecture

## Layers (who may include whom)
```
core  ←  geometry  ←  render  ←  gpu (web only)
  ↑         ↑           ↑
physics   anim        audio
  ↑─────────┴───────────┴────── script/scene_api  ←  tools/loop.cpp, web/player.cpp
script (lexer/parser/compiler/vm/stdlib) depends only on core
```
Namespaces: `loop`, `loop::physics`, `loop::anim`, `loop::audio`, `loop::gpu`,
`loop::script`. All includes are from the repo root (`#include "engine/core/math.h"`).
CMake globs `engine/*.cpp` into one static lib `loop3d`; `engine/gpu/` and
`engine/web/` only under Emscripten.

## A scene's life
1. **Load.** Host (`loop` or the player) makes a `Vm`, calls
   `installStandardLibrary(vm)` and `installSceneApi(vm, stage)`, expands
   `use "x"` lines (native: `tools/loop.cpp:expandUses`; web: `web/loop_ui.js` before
   `loop_run_script`), then
   `vm.load(src, file)`. The top level runs immediately; world words append to
   `stage.scene` (meshes cached by shape key in `stage.meshCache`, so 5,000 trees
   share a few meshes) and to `stage.placed` (the script-visible handle per instance).
2. **Every frame.** `vm.update(dt)` on a fixed clock (`script.tick` = 120 Hz, at most
   `script.maxSteps` catch-up steps and `script.catchUp` ms), then `stage.update(dt)`:
   physics `World::step` (fixed sub-steps at `physics.rate`), people's `Animator`s
   (footsteps call the mixer), sound positions. Physics bodies write back into
   `placed` → `updateTransform` → bumps `transformsVersion`.
3. **Sync to the GPU** (`web/player.cpp:syncScene`): if `version` or any count
   changed → `renderer->sync` (full); else if `lightsVersion` → `syncLights`; else if
   `transformsVersion` → `syncTransforms`. Keep this split cheap: it is why moving
   and flickering things are almost free.
4. **Camera.** Movie mode: `stage.film.at(t, base)` gives the shot camera and fade.
   Game mode: `walkCamera(dt)` in the player (eased look, character controller via
   `World::moveCharacter`).
5. **UI.** `stage.ui` (line / ask / clear) is drained by the player each frame;
   `stage.nextScene` (from `go_to`) triggers `window.loopGoTo`. Choices come back via
   `loop_choose` → `vm.emit("choice", n)`. Use key → `emit("use")`.
6. **Renders.** `render`/`film` words push `RenderRequest`s; `loop render` runs the
   script to `at`, builds `PathTracer::setScene`, `render()`, `denoise`, `develop()`
   (film.h) → PNG.

## Script VM essentials
- `Value` is a fat struct: number (double), bool, vec (3 floats, inline), or `Obj*`.
  Heap kinds: Str, List, Table (ordered keys), Proto, Closure, Cell, Native, Thing, Bound.
- Mark-and-sweep GC (`nextCollect_` threshold); C++ must `vm.keep(v)` what it holds.
- Fibers: one per `every`/`after`/`when`/`task` and the top level. `OP_WAIT` parks
  a fiber; timers and handlers live in the Vm. A fiber over `budget` steps
  (2,000,000) without waiting is stopped with its line.
- Built-ins: `vm.native(name, fn, help)`; methods per kind: `vm.method("list", …)`.
  `Args` gives positional-or-named access: `a.num(0, "radius", 1)` (named wins).
  Index `-1` means "named only".
- Engine objects reach scripts as `Thing` with a `ThingClass` (get/set field
  functions and a field list for "did you mean"). Classes: material, shape, light,
  person, sound, terrain (`scene_api.cpp` ~line 157).
- Errors: `vm.fail(what, hint)` from a built-in; compile problems via `Problem`
  (file, line, column, what, hint) with `closest()` for suggestions.

## Settings (engine/core/settings.*)
- `Config` struct of plain fields grouped by area; engine code reads `config().x.y`.
- `Settings::get()` registers each field once with id, type, range, help.
- Three layers: project (project.json) < scene (`setting`/`settings` words) <
  editor (live). Higher wins; `clear(kScene)` on each new scene.
- `watch(prefix, fn)`: the player re-applies renderer settings on `render.`/`ultra.`.
- The editor mirrors it in `settings_schema.js` (`bind: 'set'`); 39 entries there
  are `NB` (not built yet) and are the roadmap.

## The two renderers
| | Cinema (`engine/render/`) | Live (`engine/gpu/`) |
|---|---|---|
| Method | CPU path tracer, MIS, all cores (JobSystem) | WebGPU raster + ray-traced lamp shadows (High), GPU path tracer (Ultra) |
| Acceleration | `SceneBvh`: per-mesh BLAS + top-level over instances | BVH uploaded to GPU buffers, built only when needed |
| Lighting | sun, lamps, panels, emissive, physical sky, fog volumes | same lights; sky as SH9 (`SkyModel::shCoefficients`); sun shadow map |
| Post | `develop()` in film.cpp | the same steps in the film shader |
| Cleanup | à-trous denoiser (albedo/normal guided) | Ultra: 4-pass à-trous + history |

## Web player ↔ JS
`web/player.cpp` exports `loop_*` C functions (list in `CMakeLists.txt`
`EXPORTED_FUNCTIONS`). Adding an export means adding it there too. JS calls them
through `web/loop_ui.js` (player) and `web/editor/engine.js` (`LE.engine`, which
wraps strings with `_malloc`/`stringToUTF8`). The engine calls back via
`EM_ASM` into `window.loopReady`, `loopGoTo`, `loopUi`, `loopNoGpu`.
