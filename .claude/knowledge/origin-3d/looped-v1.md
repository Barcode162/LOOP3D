# The first Looped (3D's `looped/`, ~28k lines) and its J116 rebuild

## Conventions there
C++20, `namespace engine`, 2-space indent, Dawn C++ WebGPU API with async
callbacks (never Asyncify), `LOG_INFO/WARN/ERROR/FATAL` (300-line ring, sent in
reports), pixel ratio capped at 2, right-handed +Y up, camera looks down -Z,
column-major matrices like WGSL. Engine never includes `bar/` (lint enforced);
talks to its page only through `window.loopedHost` (net, audio, voice, bridge,
log, account, settings). A second app, the place viewer (`looped/apps/viewer`).

## Systems (live in the bar)
| System | Files | Essence |
|---|---|---|
| Frame & core | `gpu.*`, `frame_time.*`, `bridge.*`, `log.*`, `math.h`, `json.*`, `file.*` | one `frameTime()` per frame; phases input → interaction → physics → net in → games → claims → net out → render; a **mailbox** batches all JS↔WASM calls (pull at start, flush at end) because crossings are slow on phones |
| Loading & jobs | `jobs.*`, `fetch.*`, `pipeline_cache.*`, `gpu_upload.*` | frame-sliced jobs with deadlines and lanes, learning each kind's cost; images decoded by the browser, mipmaps on GPU; pipelines compiled in the background; "Ready" = files in, jobs done, pipelines compiled, 30 frames under 20 ms |
| Rendering | `scene_renderer.*` (1.9k), `render_scene.h`, `culling.h`, `quality.h`, `shadows.*`, `billboard.*`, `text.*`, `font.*` | forward, light grid (up to 255 lamps, 8 per pixel), 40 cube shadow maps, mirrors as extra passes over their screen area, rooms and doorways cut drawing, instancing (up to 4096 models), ASTC textures, own SDF font from a 5×7 pixel font |
| Light | `light_units.h`, `post.*`, `radiosity.*` (CPU), `gi.*` + `gi.wgsl` (GPU), `beams.*`, `ssao.*` | lumens/Kelvin, inverse square, candela unit; eyes metered 64→8→1 with AgX and `eyeKeep`; bounce = CPU visibility once per layout + GPU bounce every frame; beams ray-marched; no bloom |
| Materials | `material.h`, `procedural.*`, `assets/world/materials.json` | one record per stuff for look, feel and sound; procedural detail from noise |
| Physics | `physics.*` | fixed boxes + bodies of glued spheres (a glass = rings of spheres), 60 Hz × 4 substeps; same C++ runs in the room |
| Animation | `skeleton.*`, `skinned_mesh.*` | 18-bone avatar, skinned (2 bones a vertex), walk driven by distance |
| Models | `gltf.*`, `model.*` | **glTF 2.0 binary loader** (PBR metal/rough, emissive; no anims/skins from files) |
| Audio | `audio.*`, `synth.*`, `midi.*` | Web Audio bridge; synth piano (inharmonic partials, 3 strings, hammer), pluck, bell, clinks, pour; MIDI reader (formats 0/1, tempo, sustain) |
| Input | `input.*`, `gamepad.*`, `steering.*` | one InputFrame a frame; taps; Gamepad API (pads work on iPhone); steering with caster return |
| Net | `netproto.*`, `net.*` | the room protocol codec (never crashes on garbage) |
| Driving | `vehicle.*`, `drive/` (`car_spec`, `tire`, `vehicle_sim`) | raycast wheels, Pacejka combined slip, engine/clutch/gearbox/LSD; 480 Hz sim |
| World model | `world/` (`kind`, `loop`, `world`, `place_layout`, `catalog`), `place_player.*` | kinds (parts, states, rules, loops), places, loops = channels × shapes (wave, keys, noise) over a clock or the beat; **everything follows from the clock + one fact per thing** (state and since when), so phones and the room agree without talking |
| UI kit | `ui_theme.*` + `ui/` | the engine reads the same theme files as the pages |
| Profiler | `profiler.*` | phase scopes, draw counts, `window.loopedPerf` |

## J116/J118: Looped rebuilt (labs, behind ?engine=next)
- **Words** (`looped/words/`: `syntax`, `program`, `words`, `rig`, `place*`): a
  dictionary where every word is lines of other words down to engine roots
  (`assets/words/first.looped`, 31 words); phrases like "a warm candle that
  flickers" become things with a trail of why; place words (`on`, `by`, `in the
  middle of`); cause words (`when the lights go down, …`); a light rig language
  (`light <channel>: <lamps>`, `state`, `when … fade to … over …`, `shape breathe
  (by 6%, every 8 s): 1 + by * noise(...)`); 8,000-step budget; errors with line,
  column and "did you mean". The whole bar is one line per thing in
  `assets/world/places/bar.looped`, converted and proven equal to the JSON.
- **One light formula**: light is linear, so bake each light channel's full
  transport (direct, shadows, bounces, haze) once; compress 12 channels to ~4
  basis maps; relight each frame with a 4×12 matrix from the scripts. No compute
  → **same frame on WebGL2 and WebGPU**. Measured 5.5–6.3% off its own path
  tracer in the lab. The user later said it **"feels fake"** (static, flat, no
  view-dependent light): see lighting-story.md.
- **Reference path tracer** (`looped/bake/light_paths.*`): all 63 lamps at size,
  every bounce; found a real room has much more fill light than the old renderer.
- **Draw** (`looped/draw/`): one small interface, WebGPU and WebGL2 backends, a
  WGSL and a GLSL ES 3.0 copy of each shader, a same-picture test (0.05/255).
- **Clusters (own Nanite)** (`draw/clusters.*`): ~128-tri clusters, groups of 4
  simplified by quadric edge collapse with locked borders, a tree of simpler
  versions, picked per frame on the CPU by screen error (0.5 px Ultra … 2 px hot
  phones). Bar from the door: 269k of 606k tris drawn.
- **Model spec** (`draw/model_spec.*`, `node tools/clusters.mjs spec`): big models
  simplify to a quarter, ≤20% seam corners, pieces ≥48 tris avg, ≤5% open edges,
  ≤8 materials, ≤20k tris/m². All 165 models remade to pass.
- **Settle and repair** (physics lab): a knocked-over stool is lit live while
  awake; when it sleeps only the texels near where it was and is are re-lit
  (4 ms a frame), the rest of the bake stays.
- **Unwrap** (`bake/unwrap.*`): light texture 2048 × 878 at 5 cm; probes
  (`bake/probes.*`) for small and moving things.

## How LOOP3D differs
LOOP3D started over (2026-11-03): native C++ CLI + WASM player, Looped Script 2
as a real programming language (bytecode VM, fibers) rather than the word web,
a CPU cinema path tracer, a WebGPU live renderer with ray-traced lamp shadows and
an Ultra path tracer, procedural shapes and materials instead of glTF models, no
multiplayer, no WebGL2. See [port-map.md](port-map.md).
