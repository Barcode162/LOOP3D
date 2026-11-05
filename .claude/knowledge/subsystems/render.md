# Cinema renderer and scene model (engine/render/)

**Files:** `scene.*` (Scene, Material, Light, Sky, Fog, Camera, Look; `kelvin()`;
`lookByName`, `sensorByName`), `materials.*` (59 names), `bsdf.*`, `pattern.*`,
`sky.*`, `film.*`, `shots.*`, `scene_bvh.*`, `pathtracer.*` (749), `demo_scenes.*`.

## Model (scene.h)
- Units: metres; lamps lumens + kelvin; sun lux; emission nits; camera mm, f-stop,
  shutter s, ISO. `Light::Kind` point/spot/rect/sun; `radius` = bulb size, or the
  sun's angular radius.
- `Material`: principled-style (baseColor, metallic, roughness, specular, ior,
  transmission, dispersion, absorption, subsurface, clearcoat, sheen, translucency,
  emission, opacity, leafShape) + procedural `Pattern` (checker, tiles, bricks,
  wood, marble, noise, ground, planks, image) computed at the hit, no textures.
- `Look`: tonemap AgX (default) / ACES / filmic / linear, grade, bloom, halation,
  vignette, grain, CA, distortion, letterbox, monochrome. 12 named looks.
- `Camera`: physical; `autoExposure` meters to middle grey; `ev100()`.

## Model pictures (2026-11-05)
`Material::baseTexture/metalRoughTexture/normalTexture/emissiveTexture` index
`Scene::textures`; `shadeSurface(..., &scene.textures)` multiplies colour (sRGB
→ linear, alpha into `Surface::alpha`), roughness (G) and metal (B);
`alphaCutoff` cuts with the picture's alpha. Normal and emissive pictures are
read but not used yet (the path tracer has no tangents per hit).

## Path tracer
- `setScene` builds the `SceneBvh` (BLAS per mesh, TLAS over instances), light lists
  (emissive triangles, lamps, panels, sun) and the `SkyModel`.
- `trace()`: next-event estimation with MIS (`sampleLights`, `lampHit`), BSDF
  sampling, spectral dispersion via wavelength sampling (`wavelengthRaw`), fog with
  Henyey-Greenstein phase and `transmittance`, firefly `clamp` (auto from scene mean).
- Output: color, noisy, albedo, normal → `denoise()` (à-trous, Dammertz 2010,
  on light ÷ albedo so textures stay sharp) → `develop()` (film.h) → PNG.
- Motion blur via `Camera::positionEnd`; aperture blades in the lens sample.
- Threads: `JobSystem::shared().parallelFor` over tiles (`cinema.threads`).

## Quality presets (`loop render --quality`)
draft | good | cinema | ultra; `cinema.*` settings override width, samples,
bounces, denoise, dispersion, motion blur, fps, folder.

## Tests (`tests/render_test.cpp`, 8)
BSDF energy conservation, white furnace (diffuse and glass), inverse-square point
light, sun matches lux, sky blue at noon and red at sunset, film middle grey, small
render + denoise. **Physical correctness tests are the guard rail: keep them passing.**

## Gaps
Caustics flag exists but off (slow). No image textures from files in the language
(Pattern kImage exists), no glTF import, no volumetric media beyond fog, no
light-path guiding, no adaptive sampling.
