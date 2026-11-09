#pragma once
// The cinematic renderer: a physically based path tracer. Light is followed
// backwards from a real camera (lens, aperture blades, focus, shutter)
// through bounces off every surface, through glass (with rainbow
// dispersion), through fog (god rays), to lamps, glowing surfaces, the sun
// and the sky. Multiple importance sampling keeps it clean; a denoiser
// guided by the surfaces' colour and direction cleans what's left.
// Runs on every CPU core; the Ultra GPU tracer (gpu/) mirrors it.
#include <functional>
#include <memory>
#include <string>

#include "engine/core/image.h"
#include "engine/geometry/bvh.h"
#include "engine/render/scene.h"
#include "engine/render/sky.h"

namespace loop {

struct RenderSettings {
  int width = 1280, height = 720;
  int samples = 64;       // per pixel
  int maxBounces = 10;
  bool denoise = true;
  bool caustics = false;
  bool dispersion = true; // glass splits light into rainbows (materials with dispersion)  // light focused through glass onto diffuse surfaces (slow to converge)
  float clamp = 0;        // max brightness of a bounce, in multiples of the scene's average; 0 = auto
  uint32_t seed = 1;
  // Called now and then with progress 0..1; return false to stop early.
  std::function<bool(float)> progress;
};

struct RenderOutput {
  Image color;    // scene light (nits), denoised if asked
  Image noisy;    // before denoising
  Image albedo;   // surface colour at the first hit
  Image normal;   // surface direction at the first hit (as colour)
  double seconds = 0;
  uint64_t rays = 0;
};

class PathTracer {
 public:
  PathTracer();
  ~PathTracer();
  // Builds the ray structures, light lists and sky. Call again after changes.
  void setScene(const Scene& scene);
  RenderOutput render(const RenderSettings& s);
  // One path's light through pixel (px, py) (for tests).
  Vec3 tracePixel(float px, float py, int w, int h, uint32_t sampleIndex);
  // The light cache's (engine/render/light_cache.h): the light arriving at `p`
  // from direction `d`, all but the lamps and the sun seen straight on (the
  // live renderer works those out itself), over at most `bounces` bounces.
  // `back`: the ray's first hit was the back of a surface (p is inside it).
  Vec3 probeLight(Vec3 p, Vec3 d, uint32_t seed, int bounces, bool* back);
  const SkyModel& sky() const;

  struct Impl;

 private:
  std::unique_ptr<Impl> impl_;
};

// Edge-aware à-trous wavelet filter guided by albedo and normals (Dammertz
// et al. 2010), on the light divided by the surface colour so textures stay
// sharp.
Image denoise(const Image& color, const Image& albedo, const Image& normal, const Image& variance, int iterations = 5);

}  // namespace loop
