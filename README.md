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

# play a whole story with no picture: every scene, every line, the choices made for you
./build/loop play   stories/echoes_of_ardane/ardane_01_home.loop --choices 0,1,0

# the web player (needs Emscripten), then open site/ in a browser
./tools/build_site.sh
cd site && python3 -m http.server 8000      # http://localhost:8000/?scene=courtyard
```

A scene's script asks for renders itself (`render file: "x.png", quality: "cinema"`), and for a whole film with `film "frames", fps: 24` (numbered PNGs).

## The demo: *Echoes of Ardane*

A full story made in Loop3D, to show what it can do: [`stories/echoes_of_ardane/`](stories/echoes_of_ardane/).

- **The story:** Milo Ardane, fourteen, poor, in his father's jacket, runs from the rich family that owns his street, through a ring of light in Thirteenth Alley. He crosses four worlds and a thousand years to meet his ancestors, gathers their five relics, nearly loses himself in a city that claps forever, and faces the first boy who ever went through the portal.
- **The script:** [`STORY.md`](stories/echoes_of_ardane/STORY.md) is the director's script: the world bible, the cast, the relics, every scene's light, sound and beats, and the final speech in full.
- **The game:** 24 scene files, about 30 minutes, played in order. Eleven are PLAYED (you walk, press Use, and choose) and thirteen are WATCHED (shot like a film).
  - The choices are remembered and come back: the coins in scene 1 decide a line in the epilogue.
  - Every scene shares its people, lamps, portals and places through `ardane_common.loop`.
- **Play it:** open the player with `#ardane_01_home` (`site/index.html#ardane_01_home`, or pick it from the scene list). Walk with WASD or the left thumb, look with the mouse or the right thumb, and press E or the Use button.

| Act | Scenes | Where |
|---|---|---|
| One | 1–3 | Calder Row: the room above the laundry, the Varen Arcade, Thirteenth Alley |
| Two | 4–6 | Velasse, the drowned city: the Low Water, the Great Sluice in a storm |
| Three | 7–10 | Qadesh, the red city: the Sunborn's palace, the cisterns, the Deep Well |
| Four | 11–13 | Aelor, islands in the sky: the garden party, the Root Warrens |
| Five | 14–16 | Nexus 9: neon rain, the Undercity, the archive core |
| Six | 17–19 | Home as a stranger, the Hollow, the Scar |
| Seven | 20–23 | The Convergence, the last battle, inside Veyl, morning |
| Epilogue | 24 | The room above the laundry, in spring |

## Writing for it

- [`docs/LANGUAGE.md`](docs/LANGUAGE.md): the language and every word.
- [`docs/STORY_GUIDE.md`](docs/STORY_GUIDE.md): how to turn a written story into Loop3D scenes, step by step. **Give this to the AI writing your story.**
- `examples/`: `cabin.loop` (a candlelit room), `courtyard.loop` (a living courtyard: a person, physics, sound, shots).

## Honest status

- **Size:** about 16,500 lines of engine, tools, player and tests, plus 2,450 lines of Looped Script in the demo story; 58 automated checks, all passing. A strong foundation, not yet Unreal-sized.
- **Real phones untested:** the live renderer was tested in a headless browser with a software GPU. Frame rate on the iPhone needs checking on the phone.
- **Lamp shadows are ray-traced** live, for the brightest lamps near the camera (3 on Phone, 6 on High; Ultra traces them all).
- **Resolution adapts by itself:** when the GPU falls behind, the player draws fewer pixels (down to half, or a third on Ultra) and climbs back when it can. The stats line shows real frames drawn, the CPU time per frame and the resolution in use.
- **People are simple** (capsule bodies, no faces) and walk straight to where they're sent, without steering around things.
- **Not yet:** an editor app, model import (glTF), character faces and hair, streaming big worlds, multiplayer.
