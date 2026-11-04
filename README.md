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
| Phone | live, Safari | playable on the iPhone |
| High | live, computer | sharper, still playable |
| Ultra | live, high-end GPU | path traced, slow but close to the cinema look |
| Cinema | `loop render` | the best stills and film frames (the "trailer") |

The player's **Trailer view** button shows the cinema still of a scene next to the live picture: the target beside where we are.

## Using it

```sh
# build and test (Linux or macOS, a C++20 compiler and CMake)
cmake -B build -DCMAKE_BUILD_TYPE=Release && cmake --build build -j
./build/loop_tests

# check a script, run it, render what it asks for
./build/loop check  examples/cabin.loop
./build/loop run    examples/courtyard.loop --seconds 10
./build/loop render examples/courtyard.loop --quality good     # draft | good | cinema | ultra

# the web player (needs Emscripten), then open site/ in a browser
./tools/build_site.sh
cd site && python3 -m http.server 8000      # http://localhost:8000/?scene=courtyard
```

A scene's script asks for renders itself (`render file: "x.png", quality: "cinema"`), and for a whole film with `film "frames", fps: 24` (numbered PNGs).

## Writing for it

- [`docs/LANGUAGE.md`](docs/LANGUAGE.md): the language and every word.
- [`docs/STORY_GUIDE.md`](docs/STORY_GUIDE.md): how to turn a written story into Loop3D scenes, step by step. **Give this to the AI writing your story.**
- `examples/`: `cabin.loop` (a candlelit room), `courtyard.loop` (a living courtyard: a person, physics, sound, shots).

## Honest status

- **Size:** about 15,700 lines of engine, tools, player and tests; 58 automated checks, all passing. A strong foundation, not yet Unreal-sized.
- **Real phones untested:** the live renderer was tested in a headless browser with a software GPU. Frame rate on the iPhone needs checking on the phone.
- **Live lamps have no shadows yet** (the sun does). Light spills a bit evenly indoors.
- **People are simple** (capsule bodies) and walk straight to where they're sent, without steering around things.
- **Not yet:** an editor app, model import (glTF), character faces and hair, streaming big worlds, multiplayer.
