# Where is…? (task → place)

| I need to… | Go to |
|---|---|
| Add or change a script world word | `engine/script/scene_api.cpp` (`vm.native("box", …)` blocks); then `loop words js > web/editor/words.js`, `docs/LANGUAGE.md` |
| Add a stdlib word (maths, text, list method) | `engine/script/stdlib.cpp` (`vm.native`, `vm.method("list", …)`) |
| Change syntax | `lexer.cpp` (tokens, units, indentation) → `parser.cpp` (AST in `ast.h`) → `compiler.cpp` (emit ops in `bytecode.h`) → `vm.cpp` (execute) |
| Add a unit (`kmh`, `lux`…) | `engine/script/lexer.cpp` `unitScale` |
| Change a script error message | where it is raised: lexer/parser/compiler `Problem`, or `vm.fail` in built-ins |
| A field on a shape / light / person / sound | `scene_api.cpp` `getShape/setShape`… and the `ThingClass` field list (~line 157) |
| Add a named material | `engine/render/materials.cpp` table (`{"oak", …}`), `materialByName` |
| Add a film look | `engine/render/scene.cpp` `lookByName` (+ `lookNames`) |
| Change tone mapping / grading | `engine/render/film.cpp` and the matching film shader in `engine/gpu/shaders.h` |
| Change surface shading | cinema `engine/render/bsdf.cpp`; live `engine/gpu/shaders.h` (keep them matching) |
| Procedural patterns | `engine/render/pattern.cpp` (+ GPU version in shaders.h) |
| Sky | `engine/render/sky.cpp` (`SkyModel::build`, SH9 for the GPU) |
| Cinema render loop, sampling, denoiser | `engine/render/pathtracer.cpp` |
| Shots / camera moves | `engine/render/shots.cpp`, easing in `engine/core/ease.h` |
| Live renderer passes, quality, auto resolution, lost GPU | `engine/gpu/renderer.cpp` |
| WGSL shaders (scene, shadow, film, bloom, Ultra) | `engine/gpu/shaders.h` |
| Player frame loop, walk camera, scene sync, JS exports | `web/player.cpp` (`frame`, `walkCamera`, `syncScene`, `extern "C"` block) |
| Player page UI (menus, lines, choices, benchmark) | `web/index.html`, `web/loop_ui.js` |
| Editor panels / layout / commands | `web/editor/panels.js`, `ui.js`, `editor.js`; bridge `engine.js` |
| An engine setting | `engine/core/settings.h` (`Config`) + `settings.cpp` (registration) + `web/editor/settings_schema.js` |
| Physics behaviour | `engine/physics/physics.cpp` (`substep`, `collide`, `solveJoints`, `stepCloth`, `moveCharacter`) |
| How people move | `engine/anim/anim.cpp` (`Animator::update`, `humanSkeleton`); people glue in `scene_api.cpp` (`Stage::update`) |
| A sound or ambience | `engine/audio/audio.cpp` (`makeSound`, `makeAmbience`, `surfaceOf`) |
| Model files (.glb), JPEG | `engine/geometry/gltf.*`, `engine/core/jpeg.*`; word `model` and `materialFromGltf` in `scene_api.cpp` |
| Mesh shapes | `engine/geometry/mesh.cpp` (`shapes::`) |
| Terrain, forests, scattering | `engine/geometry/terrain.cpp`, `plants.cpp`; words `terrain`/`forest` in scene_api |
| Ray casts (CPU) | `engine/geometry/bvh.*` (per mesh), `engine/render/scene_bvh.*` (instanced scene) |
| CLI commands | `tools/loop.cpp` (`main`, `renderScript`, `playStory`, `demo`) |
| Site build | `tools/build_site.sh`, `tools/build_projects.py` |
| Benchmarks | player `?bench=`; `tools/perf_add.py`; `perf/` |
| Tests | `tests/<area>_test.cpp`; runner `tests/test.h`, `tests/main.cpp` |
| A story / scenes | `projects/<name>/project.json` + `.loop` files; guide `docs/STORY_GUIDE.md` |
