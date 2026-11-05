# Engine gaps

Fundamental components Loop3D is missing, by domain. Sources: README "Honest
status", the editor's 39 `NB` (not built yet) settings, TUNING.md §7, and a read of
every engine folder (2026-11-05, commit a56c14c). Priority: **P0** blocks real
projects · **P1** expected of an engine · **P2** nice to have.
Remove a row when it is built; add rows as they are found.
**3D:** marks a gap the original repo (Barcode162/3D) already solved; see
`.claude/knowledge/origin-3d/port-map.md` before building it again.

## Content pipeline
| Gap | Priority | Notes |
|---|---|---|
| **3D:** Model import, the rest: normal maps, emissive pictures, skins and clips, separate-file and base64 buffers | P0 | **.glb meshes, materials, JPEG/PNG pictures, glass: built 2026-11-05** (`engine/geometry/gltf.*`, `engine/core/jpeg.*`, word `model`); cinema renders only |
| Image textures in the language (a picture on a box) | P2 | models carry pictures now (`Scene::textures`); a word for a loose picture file is still missing |
| Asset/resource system (load once, refer by name, hot reload) | P1 | |
| Scene serialization (editor object edits saved back to `.loop`) | P1 | editor edits scripts only |

## Rendering (live)
| Gap | Priority | Setting |
|---|---|---|
| **3D:** Temporal upscaling (TAAU) replacing MSAA | P1 | `render.upscaler` |
| TAA | P1 | `render.taa` |
| Clustered lights | P1 | `render.clustered` |
| **3D:** GPU culling, LOD, impostors | P1 | `world.gpuCulling`, `world.lodBias`, `world.impostors` |
| Cascaded / cached sun shadows | P1 | `render.cascades`, `render.cachedShadows` |
| **3D:** Pipelines compiled async | P1 | `perf.asyncPipelines` |
| Half-res lamp shadows, contact shadows | P2 | `render.lampHalfRes`, `render.contactShadows` |
| SSAO, reflections, probe GI | P2 | `render.ao`, `render.reflections`, `render.gi` |
| Live motion blur, wireframe | P2 | `cam.motionBlur`, `render.wireframe` |
| Ultra: SVGF reprojection, ReSTIR, blue noise | P2 | `ultra.motionVectors`, `ultra.restir`, `ultra.blueNoise` |

## World and performance
| Gap | Priority | Setting |
|---|---|---|
| Streaming big worlds | P1 | `world.streaming` |
| Scene building off the main thread | P1 | `perf.loadWorker` |
| WASM threads and SIMD | P2 | `perf.wasmThreads` |

## Characters and AI
| Gap | Priority | Setting |
|---|---|---|
| Pathfinding / navmesh, steering around obstacles | P1 | `anim.steering` |
| **3D:** Skinned meshes | P1 | `anim.skinned` |
| Animation state machine and clip playback for scripts | P1 | |
| Faces, hair | P2 | `anim.faces`, `anim.hair` |
| Crowd LOD | P2 | `anim.crowdLod` |

## Physics
| Gap | Priority | Setting |
|---|---|---|
| Continuous collision (no tunnelling) | P1 | `physics.ccd` |
| Collision / trigger events to scripts | P1 | |
| Convex hulls, compound shapes | P2 | |
| Ragdolls, vehicles | P2 | |

## Audio
| Gap | Priority | Setting |
|---|---|---|
| Mixer on its own thread (AudioWorklet) | P1 | `audio.worklet` |
| **3D:** Sample playback (imported sounds, music, voice) | P1 | |
| Occlusion, HRTF | P2 | |

## From the user's wishes in 3D (not yet in LOOP3D)
| Gap | Priority | Notes |
|---|---|---|
| **3D:** WebGL2 backend (every browser plays) | P1 | user asked 2026-10-27 #7 and 11-01 #1; 3D's `looped/draw/` |
| A shared world clock + loops on things (the user's own idea) | P1 | 3D's world model; LOOP3D has `every`/fibers per scene, not a shared clock |
| **3D:** Light baking / probes / relight (settle and repair) | P2 | 3D's `looped/bake/`; the user later found pure baking "fake" |
| Bloom as a clear adjustable setting (already: `look bloom:`; maybe a `render.bloom` engine setting) | P2 | user, 2026-11-05 |

## Engine core and tooling
| Gap | Priority | Setting |
|---|---|---|
| CI on every push (build + tests) | P1 | only the manual Pages workflow runs tests |
| Render regression tests (image diffs) | P1 | |
| **3D:** Input abstraction (gamepad, rebinding beyond keys) | P1 | |
| Save games (beyond story memory) | P1 | |
| **3D:** Engine profiler markers (CPU scopes) | P2 | |
| Script warnings (unused names), debugger | P2 | `script.strict` |
| Editor gizmos, snapping, free camera | P1 | `snap.*`, `gizmo.*`, `vp.flySpeed` |
| **3D:** Networking / multiplayer | P2 | |
| Parser fuzzing, sanitizer builds | P2 | |
