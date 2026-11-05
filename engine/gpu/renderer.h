#pragma once
// The live renderer (WebGPU, in the browser). It draws a Scene every frame:
// physical materials and patterns, the sun with soft shadows, lamps in real
// units, the physical sky lighting everything, fog, and the film (auto
// exposure like eyes, bloom, the look) — the cinema renderer's rules, fast.
#include <functional>
#include <memory>

#include "engine/render/scene.h"

namespace loop::gpu {

enum class Quality { kPhone, kHigh, kUltra };

// Debugging: skip passes (1 shadow, 2 scene, 4 exposure, 8 bloom, 16 film).
extern int g_skipPasses;
extern int g_debugView;  // 1 raw light, 2 exposure, 3 bloom
// Testing: draw at this share of full resolution (0 = automatic).
extern float g_fixedScale;
// Testing / the editor: lamp shadows (lamps that cast them, rays per lamp;
// -1 = the quality's own choice) and the depth pre-pass. Changes to the lamp
// settings take effect on the next setQuality.
extern int g_shadowLamps;
extern int g_shadowRays;
extern bool g_prepass;

class Renderer {
 public:
  // Starts WebGPU on a canvas (CSS selector); `ready` gets the renderer, or
  // null if the browser can't do WebGPU.
  static void create(const char* canvas, std::function<void(std::unique_ptr<Renderer>)> ready);
  virtual ~Renderer() = default;
  // The scene's shapes, materials, lights, sky changed: read them again.
  virtual void sync(const Scene& scene, bool geometryChanged, bool skyChanged) = 0;
  // A new scene: forget the old one's shapes.
  virtual void reset() = 0;
  // Only placements moved (cheap).
  virtual void syncTransforms(const Scene& scene) = 0;
  // Only lamps changed (brightness, colour, place): cheap.
  virtual void syncLights(const Scene& scene) = 0;
  // Draws one frame with this camera and look; fade 0..1 (black..clear).
  // False when the frame was dropped (the GPU is still busy with older ones).
  virtual bool draw(const Camera& cam, const Look& look, float dt, float time, float fade) = 0;
  // Frames that reached the screen, and the share of full resolution drawn.
  virtual uint64_t presentedFrames() const = 0;
  virtual float renderScale() const = 0;
  // GPU milliseconds per frame (estimated from the GPU's "done" signals):
  // smoothed, and the latest frame's.
  virtual float gpuMs() const = 0;
  virtual float lastGpuMs() const = 0;
  virtual void setQuality(Quality q) = 0;
  // The browser took the GPU away (a driver reset, too little memory, a
  // frame that ran too long): this renderer can't draw again. Make a new one
  // and sync the scene into it.
  virtual bool lost() const = 0;
  // Frames sent to the GPU that it hasn't finished yet.
  virtual int framesInFlight() const = 0;
  virtual float pixelRatio() const = 0;
  virtual size_t drawCalls() const = 0;
  virtual float currentExposure() const = 0;
  // Reads the next frame back (for tests and screenshots): RGBA8, top row first.
  virtual void capture(std::function<void(int w, int h, const uint8_t* rgba)> done) = 0;
};

}  // namespace loop::gpu
