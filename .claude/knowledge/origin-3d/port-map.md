# What LOOP3D can take from 3D

Everything below is the user's own work (or licensed and credited), so reuse is
allowed under their from-scratch rule, but **ask the user before porting**:
the user chose to start LOOP3D fresh, and 3D's code uses a different style
(Dawn C++ API, `namespace engine`) from LOOP3D's (`namespace loop`).

## Already solved in 3D, missing in LOOP3D (see .claude/notes/engine-gaps.md)
| LOOP3D gap | 3D has | Files in 3D |
|---|---|---|
| glTF import (P0) | glTF 2.0 binary loader, PBR, emissive, background upload | `looped/gltf.*`, `model.*`, `gpu_upload.*` |
| Skinned meshes | 2-bone skinning, tube/ellipsoid builders, 18-bone avatar | `looped/skinned_mesh.*`, `skeleton.*`, `bar/avatar.*` |
| WebGL2 (every browser) | draw interface with WebGPU + WebGL2 backends, same-picture test | `looped/draw/` |
| LOD / clusters / impostors | own Nanite: cluster tree, QEM simplification, screen-error pick; model spec + checker | `looped/draw/clusters.*`, `model_spec.*`, `model_measure.*` |
| Multiplayer | Cloudflare room (DO), protocol v1 + golden vectors, ledger | `cloud/`, `looped/netproto.*`, `proto/`, `docs/handbook/protocol.md` |
| Input beyond keys | touch joystick, gamepads, steering feel | `looped/input.*`, `gamepad.*`, `steering.*` |
| Async pipelines, frame-sliced loading | pipeline cache, job lanes with deadlines | `looped/pipeline_cache.*`, `jobs.*`, `fetch.*` |
| Phone reports / profiler | phase profiler, report codes to the server | `looped/profiler.*`, `server/report.php` |
| Sample playback, music, MIDI | MIDI reader, synth piano, Salamander samples, jukebox sync | `looped/midi.*`, `synth.*`, `assets/piano/` |
| Vehicles | Pacejka tires, drivetrain, 480 Hz sim, car specs | `looped/vehicle.*`, `drive/` |
| Text in the world | SDF font, one draw a frame | `looped/font.*`, `text.*` |
| Mirrors, culling by rooms/doorways | per-mirror passes, room portals | `looped/scene_renderer.*`, `culling.h` |
| Light baking / probes | reference path tracer, unwrapper, probes, relight formula, settle-and-repair | `looped/bake/` |
| Accounts, age bands | PHP accounts, gate, voice by age group | `server/`, `cloud/room.js` |

## Assets (all credited in 3D)
- **172 models** (`assets/models/*.glb`, 30 MB): geometry all ours from Blender
  scripts in `tools/assets/*.py` (stools, counter, back bar, bottles, piano,
  chess, jukebox, arcade, lounge furniture, credit coin…), Poly Haven CC0 photo
  textures inside some. All meet 3D's model spec. Needs a glTF loader in LOOP3D.
- **Music** (105 MB): Kevin MacLeod CC-BY 4.0 (jukebox), TeknoAXE CC-BY 4.0
  (30 Training-pack songs with charts). Credits in `assets/music/CREDITS.md`.
- **Piano**: Salamander Grand Piano samples (CC-BY), MIDI songs (`assets/midi/`).
- **World data**: the bar in words (`assets/world/places/bar.looped`, 603 things),
  kinds, materials, the light rig, the first word dictionary.
- **Fonts** (SIL OFL): Archivo, Martian Mono, Limelight, Righteous, Unbounded.
- **Ember** theme and UI kit (`ui/`).

## Ideas worth carrying (design, not code)
The shared clock + one fact per thing (deterministic world, cheap multiplayer);
one material for every sense; loops as the world's heartbeat; the word web as a
layer above Looped Script 2; settle-and-repair caching; quality levels and power
modes; the preflight checklist; phone report codes.
