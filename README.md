# Loop3D

J Production's engine, written from scratch in C++ with no outside engines or libraries. Its language is **Looped Script**: a story or game is written in plain, readable words, and Loop3D turns it into a world you can walk through, or a film that plays itself.

- **Live:** in the browser (WebGPU), on an iPhone or a computer.
- **Cinema:** a physically based movie renderer for stills and frames.

## What's in it

| Part | What it does | Where |
|---|---|---|
| **Looped Script 2** | The language: variables, functions, closures, lists, tables, loops, text with `{name}` inside, real units (`30 cm`, `2700 K`, `45 deg`, `2 s`), events (`when`), timers (`every`, `after`), `wait`, many things running at once, errors with the line and a "did you mean…?" | `engine/script/` |
| **World words** | Shapes, 59 named real materials, land carved by rain, forests, rocks, lamps in lumens, the sun in lux, a physical sky, fog, a physical camera, 12 film looks, cinematic shots, people, physics, sound | `engine/script/scene_api.*` |
| **Cinema renderer** | Path tracing: soft sun and lamp shadows, bounced light, glass with rainbow dispersion, metals, clear-coat paint, skin and wax glow, cloth sheen, see-through leaves, fog with god rays, lens blur with aperture blades, noise removal | `engine/render/pathtracer.*` |
| **Live renderer** | WebGPU: the same materials and patterns, sun shadows, sky lighting, fog, eye-like exposure, bloom, the film looks, instancing | `engine/gpu/` |
| **Ultra** | Path tracing on the graphics card in the browser; it sharpens while the camera holds still (high-end GPUs) | `engine/gpu/shaders.h` (`kUltraWgsl`) |
| **Film** | Exposure (auto or real camera settings), bloom, halation, lens fringes and distortion, vignette, white balance, lift/gamma/gain, split toning, AgX / ACES / filmic curves, grain, cinema bars | `engine/render/film.*` |
| **Sky** | Rayleigh + Mie + ozone scattering through a round atmosphere: blue noon, red sunsets, by physics | `engine/render/sky.*` |
| **Physics** | Spheres, boxes, capsules; friction, bounce, stacking, sleeping; land and meshes; ropes, ball joints, hinges; cloth; a walking body that climbs steps | `engine/physics/` |
| **Animation** | Skeletons, blending, IK, springs; people who breathe, walk and run with planted feet, swing their arms, look at things and sit | `engine/anim/` |
| **Sound** | Its own synth: footsteps by surface, knocks by material, fire, wind, rain, a stream, birds, crickets; 3D placement, distance, room echo | `engine/audio/` |
| **Worlds** | Meshes and shapes, fast ray hits (BVH, instanced), noise, eroded terrain, scattering, trees, rocks, grass | `engine/geometry/` |
| **Core** | Maths, our own PNG and zlib, JSON, threads | `engine/core/` |

## Quality levels

| Level | Where | What for |
|---|---|---|
| Phone | live, Safari | smooth on the iPhone: 1x pixels, sun shadows, lamps without shadows |
| High | live, computer | sharper (1.5x pixels), crisp shadows from the two lamps that matter most |
| Ultra | live, high-end GPU | path traced at 1x, sharpens while the camera holds still; close to the cinema look |
| Cinema | `loop render` | the best stills and film frames (the "trailer") |

The player's **Trailer view** button shows the cinema still of a scene next to the live picture: the target beside where we are.

## Using it

```sh
# build and test (Linux or macOS, a C++20 compiler and CMake)
cmake -B build -DCMAKE_BUILD_TYPE=Release && cmake --build build -j
./build/loop_tests

# check a script, run it, render what it asks for
./build/loop check  projects/samples/cabin.loop
./build/loop run    projects/samples/courtyard.loop --seconds 10
./build/loop render projects/samples/courtyard.loop --quality good     # draft | good | cinema | ultra

# the web player and the editor (needs Emscripten), then open site/ in a browser
./tools/build_site.sh
cd site && python3 -m http.server 8000      # player: http://localhost:8000/#courtyard
                                            # editor: http://localhost:8000/editor.html
```

A scene's script asks for renders itself (`render file: "x.png", quality: "cinema"`), and for a whole film with `film "frames", fps: 24` (numbered PNGs).

## Writing for it

- [`docs/LANGUAGE.md`](docs/LANGUAGE.md): the language and every word.
- [`docs/TUNING.md`](docs/TUNING.md): every quality and look setting, what it costs, how they trade, and the performance tricks we have and are adding.
- [`docs/STORY_GUIDE.md`](docs/STORY_GUIDE.md): how to turn a written story into Loop3D scenes, step by step. **Give this to the AI writing your story.**
- `projects/`: games and films made with the engine, each a folder with a `project.json`. The engine needs none of them.
  - `projects/samples/`: `cabin` (a candlelit room), `courtyard` (a living courtyard: a person, physics, sound, shots), and a two-scene story (`the_gate`, `the_woods`).
  - `projects/echoes_of_ardane/`: a 24-scene story.

## The editor

`site/editor.html` (source in `web/editor/`, plain HTML, CSS and JS like the rest of the engine):

- **Docked panels** you can drag between docks, resize and close: Viewport, Script, Project, Outliner, Inspector, Look, Console, Content, Timeline, Profiler. Layout presets for scripting, look and lighting, and performance. On a phone: the view on top, the panels in tabs below.
- **Play controls:** walk (Play), Movie, pause, step a frame, restart, world speed, quality, debug views, trailer compare (the cinema still over the live view with a split handle), screenshot, sound, benchmark.
- **Script editor:** Looped Script highlighting, line numbers, problems marked on their lines, Ctrl+Enter to run, edits kept in the browser.
- **Project Settings and Editor Preferences:** about 270 settings (`web/editor/settings_schema.js`), searchable, with reset, import and export. About 230 work, live: 131 of them are engine settings with one name everywhere (the editor, `project.json`, and scripts: `setting "physics.iterations", 24`). The rest are marked **Not built yet** (features on the roadmap) or **Needs you**. Every one is listed in [`docs/SETTINGS.md`](docs/SETTINGS.md).
- **Play in the player** (Ctrl+Shift+P): opens the player on exactly what's open in the editor, edits and all. **File → Save to project** writes the project's `project.json` with its settings.
- **Command palette** (Ctrl+P) for every command, scene and setting, and a console that takes commands (`help`, `set physics.iterations 24`).
- **Script editor extras:** word suggestions and help from the engine's own word list (`loop words js > web/editor/words.js`), a minimap.

Checks: `node tools/check_settings.js` (the editor's and engine's settings agree), `node tools/settings_doc.js > docs/SETTINGS.md`, and in a browser on a virtual display `xvfb-run -a node tools/settings_browser_check.js` (every setting set, read back and drawn with).

## Honest status

- **Size:** about 18,000 lines of engine, tools, player and tests, plus the editor; 65 automated checks, all passing. A strong foundation, not yet Unreal-sized.
- **Real GPUs untested by us:** the live renderer is tested in Chromium with a software GPU (SwiftShader) on a virtual display. It tells us what costs what, not real frame rates: the frame rate on your phone or computer is the truth (run the benchmark).
- **Lamp shadows are ray-traced** live on High, for the two brightest lamps near the camera, one ray each (crisp edges); Ultra traces every lamp. Phone lights lamps without shadows: each shadow ray per pixel costs about as much as all the other lighting. `?lamps=N&rays=N` tries more on a strong GPU.
- **Resolution adapts by itself:** each quality starts a little below full size, climbs when the GPU keeps up and drops when it doesn't (down to half, or a third on Ultra). Changing it costs nothing: the scene is drawn into part of full-size targets and stretched to the screen.
- **The GPU can be taken away** by the browser (a driver reset, memory running low, a frame that took too long). The player then gets a new one and carries on; twice in a minute and it steps down a quality.
- **People are simple** (capsule bodies) and walk straight to where they're sent, without steering around things.
- **The editor edits scripts, not yet objects:** moving things with the mouse and saving that back to `.loop` comes next, along with the settings marked Not built yet.
- **Models from files:** `.glb` models (our own glTF and JPEG readers) with their pictures, metal, roughness, glow and glass, in cinema renders; the live player draws them without their pictures yet.
- **Not yet:** pictures on models in the live player, normal maps, skinned models, character faces and hair, streaming big worlds, multiplayer.
