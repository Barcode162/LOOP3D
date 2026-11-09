#pragma once
// The live renderer (WebGPU, in the browser). It draws a Scene every frame:
// physical materials and patterns, the sun with soft shadows, lamps in real
// units, the physical sky lighting everything, fog, and the film (auto
// exposure like eyes, bloom, the look) — the cinema renderer's rules, fast.
#include <array>
#include <functional>
#include <memory>
#include <string>
#include <utility>
#include <vector>

#include "engine/render/scene.h"

namespace loop::gpu {

enum class Quality { kPhone, kHigh, kUltra };

// Testing only: 256 draws off-screen (browsers that can't show WebGPU).
// Everything else the renderer can be tuned by lives in the engine
// settings (engine/core/settings.h: config().render, .ultra, .camera, .world).
extern int g_skipPasses;

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
  // Placements moved: the whole list again (the copies, their order).
  virtual void syncTransforms(const Scene& scene) = 0;
  // Only these copies moved (scene instance indices), to these transforms
  // (which may be between two steps of the clock): cheap, sends only them.
  virtual void moveInstances(const Scene& scene, const std::vector<int>& ids, const std::vector<Mat4>& xfs) = 0;
  // Only lamps changed (brightness, colour, place): cheap.
  virtual void syncLights(const Scene& scene) = 0;
  // Draws one frame with this camera and look; fade 0..1 (black..clear).
  // False when the frame was dropped (the GPU is still busy with older ones).
  virtual bool draw(const Camera& cam, const Look& look, float dt, float time, float fade) = 0;
  // Frames that reached the screen, and the share of full resolution drawn.
  virtual uint64_t presentedFrames() const = 0;
  // How many times the sun's shadow has been drawn (it is kept while nothing it is made from changes).
  virtual uint64_t shadowDraws() const { return 0; }
  // How many times the main lamps' shadow maps have been drawn (Phone).
  virtual uint64_t lampShadowDraws() const { return 0; }
  // The light cache: probes still to bake (0: done or off).
  virtual int lightCacheWaiting() const { return 0; }
  virtual float renderScale() const = 0;
  // GPU milliseconds per frame (estimated from the GPU's "done" signals):
  // smoothed, and the latest frame's.
  virtual float gpuMs() const = 0;
  virtual float lastGpuMs() const = 0;
  virtual void setQuality(Quality q) = 0;
  // Reads the render settings again (lamp shadows, pixel density, shadow
  // maps, multisampling); call after any render.* setting changes.
  virtual void applySettings() = 0;
  // The browser took the GPU away (a driver reset, too little memory, a
  // frame that ran too long): this renderer can't draw again. Make a new one
  // and sync the scene into it.
  virtual bool lost() const = 0;
  // Frames sent to the GPU that it hasn't finished yet.
  virtual int framesInFlight() const = 0;
  virtual float pixelRatio() const = 0;
  virtual size_t drawCalls() const = 0;
  // Bytes the GPU holds for this renderer now (buffers and pictures; the
  // screen's own pictures not counted), for the meter.
  virtual size_t gpuBytes() const = 0;
  // Drawing in clusters now (false while a new scene's clusters are cooked,
  // or with render.clusters off).
  virtual bool clustered() const = 0;
  virtual float currentExposure() const = 0;
  // Each step of a frame on the graphics chip (engine/render/frame_graph.h), in
  // milliseconds, from the chip's own timers: smoothed for the meter, and each
  // step's average over the frames since the last call for the recordings.
  // Empty when the chip has no timers (timers() false).
  virtual bool timers() const = 0;
  virtual std::vector<std::pair<std::string, float>> stepMs() const = 0;
  virtual std::vector<std::pair<std::string, float>> takeStepAverages() = 0;
  // Drawing in clusters: how many pieces the view drew a moment ago, in its
  // first pass and in the second (what last frame's depth had hidden but this
  // frame's showed), and in the sun's shadow (its last redraw); all 0
  // otherwise. Read back a frame or two late.
  virtual std::array<uint32_t, 3> piecesDrawn() const { return {0, 0, 0}; }
  // Reads the next frame back (for tests and screenshots): RGBA8, top row first.
  virtual void capture(std::function<void(int w, int h, const uint8_t* rgba)> done) = 0;
};

}  // namespace loop::gpu
