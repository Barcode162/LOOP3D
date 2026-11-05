# Where Loop3D stands, and what's next

Saved on 5 November 2026, at the end of a long session. Read this first next time.

## Where everything is

| What | Where |
|---|---|
| The engine, editor and settings (merged) | branch `main` (5e2c345) |
| The Showcase project and the nature and ruins model library (**not merged yet**) | branch `project/showcase`. It is `main` plus the showcase commits, so it merges cleanly. |
| The public site (built from `project/showcase`) | branch `gh-pages`: https://barcode162.github.io/LOOP3D/ (editor: `editor.html`) |
| The Showcase write-up | `projects/showcase/REVIEW.md` on `project/showcase` |
| Every setting, by name | `docs/SETTINGS.md` |
| The language | `docs/LANGUAGE.md` |

## What was built (most recent last)

1. **The editor:** `site/editor.html`, source in `web/editor/`. It has docked panels, a script editor, the play controls and about 270 settings.
2. **Engine settings:** `engine/core/settings.*`. One registry with three layers (project.json, the scene's script, the editor). The same names are used everywhere, and scripts set them with `setting "…", v`.
3. **Settings wired:**
   - 226 of 271 work.
   - 39 are "Not built yet": features on the roadmap.
   - 6 "Need you": publishing targets and devices.
4. **The Showcase** (branch `project/showcase`):
   - the Glade: the hero forest ruin
   - the Hall of Features: ten rooms, each with a before/after plinth
   - a cinema still and a side-by-side with the reference
   - new library words: `grass`, `fern`, `flower(s)`, `column`, `log`, `butterfly`, `fireflies`, plus `leaf_size`/`detail` on trees

## Checks to run before changing anything

```sh
cmake -B build -DCMAKE_BUILD_TYPE=Release && cmake --build build -j
./build/loop_tests                         # 68 checks, all passing on project/showcase
node tools/check_settings.js               # the editor's and engine's settings agree
for f in projects/*/*.loop; do ./build/loop check "$f"; done
./build/loop play projects/echoes_of_ardane/ardane_01_home.loop   # 24 scenes, 0 problems
./tools/build_site.sh                      # needs Emscripten 6.0.13 (emsdk)
```

In a browser, on a virtual display: `xvfb-run -a node tools/settings_browser_check.js http://localhost:8123 out`.

## Recommended next steps, in order

1. **Merge `project/showcase` into `main`.** It holds engine work every project should have.
2. **Measure on real devices:** run the Benchmark (toolbar ⏱) on the iPhone and a computer, on `showcase_glade` and `showcase_hall`. Every number so far came from a software GPU.
3. **Close the live-vs-cinema gap:**
   - volumetric light shafts in the live fog
   - wind sway in the vertex shader
   - image textures
4. **Make heavy scenes run:** GPU culling, levels of detail, temporal upscaling.
5. **Let creators work without C++:** glTF model import, and moving objects in the editor and saving them back to `.loop`.
6. **Grow the Showcase:** one before/after room per new feature.

## Things to know

- **Pages serves `gh-pages`.** To publish:
  1. Build `site/` with `tools/build_site.sh`.
  2. Replace `gh-pages`' contents with it, keeping `.nojekyll`, and push.
  3. The editor's comparison images are copied to `site/showcase/` by hand.
- **The software GPU in the cloud sandbox** takes seconds per frame on heavy scenes. Judge visuals from `loop render` (the cinema renderer) there.
- **The Glade's fog is tuned for the cinema still** (god rays). Live it reads as flat haze; a live-only fog strength is an easy fix if wanted.
