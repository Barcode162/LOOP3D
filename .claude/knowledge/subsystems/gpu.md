# Live renderer (engine/gpu/, WebGPU, Emscripten only)

**Files:** `renderer.h` (interface), `renderer.cpp` (1622), `shaders.h` (1288 lines
of WGSL in C++ raw strings: `kCommonWgsl`, `kSceneWgsl`, `kPostWgsl`, `kRayWgsl`,
`kUltraWgsl`, `kDenoiseWgsl`, `kExposureWgsl`).

## Frame (`draw`)
Frame gate (≤ `render.framesInFlight` in flight; watchdog after 2 s) → lights
sorted brightest-near-camera first (`uploadLights`, re-sorted per 0.5 m camera
move) → sun shadow map pass → scene pass (MSAA 4, opaque then glass;
`drawBatches` instanced) with ray-traced lamp shadows on High (`kRayWgsl`, BVH in
storage buffers, built only when used) → exposure (compute, eye adaptation) →
bloom (6 half-size passes) → film (tone curve, grade, grain, bars) → screen.
Ultra replaces the scene pass with `kUltraWgsl` path tracing + `kDenoiseWgsl` and
an accumulating history.

## Key mechanisms
- **Auto resolution** (`adaptResolution`): real-time clock + GPU done-times; steps
  100/87/75/65/56/50/42/35 %; draws into the top-left of full-size targets so a
  change costs nothing (`makeTargets` only on real resize).
- **Lost device:** `lost()` → player makes a new Renderer and resyncs; twice a minute
  → quality down (`web/player.cpp:recoverGpu`).
- **Sync split:** `sync` (full), `syncTransforms`, `syncLights`, `reset` (new scene).
- **Settings:** `applySettings()` re-reads `config().render/.ultra`; the player calls
  it from settings watchers.
- `g_skipPasses` / `?skip=N` bitmask for cost isolation.

## Keep in step with the cinema renderer
Materials, patterns, sky (SH9 from `SkyModel::shCoefficients`), film grading:
change `engine/render/*` and the WGSL together.

## Testing
Only in Chromium on SwiftShader under `xvfb-run` (`tools/web_check.js`). Use
`&msaa=1` to judge shader cost. Numbers rank costs; real devices give real fps.

## Gaps (NB in the editor; TUNING §7)
Temporal upscaling (TAAU, the biggest planned win), TAA, cascaded and cached sun
shadows, half-res lamp shadows, contact shadows, clustered lights, SSAO, reflections,
probe GI, wireframe, GPU culling, impostors, LOD, async pipelines, RG11B10, timestamp
queries; Ultra: SVGF reprojection, ReSTIR, blue noise; live motion blur.
