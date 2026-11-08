#pragma once
// What one live frame costs, counted on the processor: the things and
// triangles each pass draws, the draw calls, how many surfaces stack up behind
// each pixel, and how many lamps each pixel works out against how many really
// light it. `loop budget` prints it against each quality's budget, so a speed
// change is measured here before it reaches a phone. It follows the live
// renderer (engine/gpu/renderer.cpp) pass by pass: change one, change both.
#include <cmath>
#include <cstdint>
#include <string>
#include <vector>

#include "engine/core/math.h"
#include "engine/render/scene.h"

namespace loop {

// The depth pre-pass (render.prepass: 0 off, 1 on, 2 automatic): it pays when
// lighting each pixel is heavy (many lamps), and costs an extra pass over the
// triangles, so automatic turns it on from 6 lamps. The renderer and
// `loop budget` both ask this.
inline bool prepassOn(int mode, int lamps) { return mode == 1 || (mode == 2 && lamps >= 6); }

// Which version of a mesh to draw (Scene::lods): 0 full, 1 simpler, 2
// simplest, by how big a copy's bounding sphere looks on screen (its radius in
// pixels), shifted by world.lodBias (+1: full detail twice as far; -1: simpler
// twice as soon). The renderer and `loop budget` both ask this.
inline int lodLevel(float radiusPixels, float bias) {
  float px = radiusPixels * std::exp2(bias);
  return px >= 220 ? 0 : px >= 70 ? 1 : 2;
}
// The sun's shadow picture can't show detail finer than its texels (5 cm on
// Phone), so the shadow pass draws one version simpler than the camera would.
inline int shadowLodLevel(int cameraLevel) { return cameraLevel < 2 ? cameraLevel + 1 : 2; }
// A copy's bounding sphere radius in pixels, from its world box.
inline float radiusPixels(const AABB& box, Vec3 eye, float pixelsPerRadian) {
  float r = length(box.extent()) * 0.5f;
  float d = std::max(length(box.center() - eye) - r, 0.05f);
  return r / d * pixelsPerRadian;
}

// The view a frame is drawn from, set up as the live renderer sets it up.
struct FrameView {
  Camera camera;
  int width = 374, height = 811;   // pixels drawn (the screen x pixel density x resolution share)
  float drawDistance = 4000;       // world.drawDistance: the far plane
  float shadowRadius = 25;         // half-width of the sun's shadow box (Phone 25 m, High 40 m)
  int maxLights = 256;             // render.maxLights
  bool cullOffscreen = false;      // things outside a pass's view are skipped
  bool cullBackFaces = false;      // the backs of closed shapes are skipped
  bool prepass = false;            // depth first: each pixel is lit once
  bool lightGrid = false;          // render.lightGrid: each pixel works out its cell's lamps and far groups
  float lampGrouping = 7;          // render.lampGrouping
  float lodBias = 0;               // world.lodBias
  bool portraitWide = true;        // cam.portraitWide: an upright screen keeps the lens's width
  // render.clusters: drawn from the cook's cluster trees, as the graphics chip
  // does it (each copy in view, then each cluster in view at the detail whose
  // error looks under render.clusterError pixels; one draw a pass; each pixel
  // lit once). Glass is still drawn the old way.
  bool clusters = false;
  float clusterError = 1;
};

struct FrameCost {
  size_t things = 0;                          // copies in the scene
  size_t hidden = 0;                          // of them scaled to nothing (drawn anyway, unless culled)
  size_t sceneThings = 0, shadowThings = 0;   // copies each pass draws
  uint64_t sceneTriangles = 0, shadowTriangles = 0;
  size_t sceneDraws = 0, shadowDraws = 0;     // draw calls (one per shape and kind of surface)
  size_t lodCopies[3] = {0, 0, 0};            // copies with simpler versions drawn full, simpler, simplest (scene pass)
  uint64_t lodTriangles[3] = {0, 0, 0};       // and their triangles
  uint64_t prepassTriangles = 0;              // the depth pre-pass draws the opaque ones again
  size_t prepassDraws = 0;
  bool sun = false;                           // a sun, so a shadow pass
  bool clustered = false;                     // counted the cluster way (FrameView::clusters)
  size_t sceneClusters = 0, shadowClusters = 0;  // clusters each pass draws
  uint64_t fullTriangles = 0;                 // what the copies drawn hold at full detail (clusters: how much the detail saves)
  // what skipping off-screen copies would leave
  size_t inViewThings = 0, inShadowThings = 0;
  uint64_t inViewTriangles = 0, inShadowTriangles = 0;
  size_t inViewDraws = 0, inShadowDraws = 0;
  // per pixel, from rays through a grid of the frame's pixels
  int samples = 0;
  float coverage = 0;            // share of pixels that show a surface (the rest is sky)
  float layers = 0;              // surfaces behind a covered pixel, fronts and backs
  float frontLayers = 0;         // of them facing the camera
  float shadedPerPixel = 0;      // fragments shaded per covered pixel (expected, with no depth pre-pass)
  float shadedFrontsOnly = 0;    // the same with the backs of shapes skipped
  float lampsComputed = 0;       // lamps the shader works out per shaded fragment (with the light grid: its cell's, on average)
  float lampsThatMatter = 0;     // lamps giving 95% of a visible point's direct light (average)
  int lampsThatMatterMost = 0;   // and at the worst point
  float lampsFor99 = 0;          // lamps giving 99% of it (average)
  float patternShare = 0;        // share of covered pixels with a computed pattern (wood, stone...)
  uint64_t triangles() const { return sceneTriangles + shadowTriangles + prepassTriangles; }
  size_t draws() const { return sceneDraws + shadowDraws + prepassDraws; }
  // per-pixel work today: fragments shaded x lamps each works out
  float lampWorkPerPixel() const { return shadedPerPixel * lampsComputed; }
};

// The view's camera matrices, exactly as the renderer builds them.
Mat4 frameViewProj(const FrameView& view);
Mat4 frameShadowViewProj(const FrameView& view, Vec3 towardSun, int shadowMapSize);

FrameCost measureFrame(const Scene& scene, const FrameView& view, int maxSamples = 40000);

// The graphics chip's choice of a copy's clusters (kClusterCullWgsl's
// `chosen`): each whose own error looks within `limit` pixels from `eye` while
// its parent's doesn't, and whose sphere is inside `view`. Adds the chosen
// clusters and their triangles.
struct ClusterTree;
void chooseClusters(const ClusterTree& tree, const Mat4& model, const Frustum& view, Vec3 eye, float perPixel, float limit, size_t& clusters,
                    uint64_t& triangles);

// One quality's budget for a frame on the user's phone. First guesses from
// what phones of 2023 sustain at 60 frames a second; the phone's benchmark
// runs set them for real.
struct FrameBudget {
  std::string quality;
  int width = 0, height = 0;   // pixels drawn on an iPhone 15 Pro Max held upright (430 x 932 points)
  uint64_t triangles = 0;      // all passes
  size_t draws = 0;            // all passes
  float shadedPerPixel = 0;
  float lampWorkPerPixel = 0;  // fragments x lamps each works out
};
FrameBudget budgetFor(const std::string& quality);  // "phone" or "high"
// The budget's lines a frame goes over, in plain words (empty: it fits).
std::vector<std::string> overBudget(const FrameCost& c, const FrameBudget& b);

}  // namespace loop
