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

// The pane that mirrors the room (order M60): a flat glass thing under 5 cm
// thick and over 1 m^2 the other two ways. At most one mirrors in a frame: of
// the panes in view, gathered by plane (a wall of panes is one mirror), the
// largest. The renderer and `loop budget` both ask these.
struct Pane {
  Vec4 plane;      // normal xyz and d (n.p + d = 0)
  float area = 0;  // m^2
};
inline bool flatPane(const AABB& box, const Mat4& xf, Pane& out) {
  if (!box.valid()) return false;
  const Mat3 L = xf.linear();
  const Vec3 e = box.extent();
  float size[3];
  for (int k = 0; k < 3; ++k) size[k] = e[k] * length(L.c[k]);
  int thin = 0;
  for (int k = 1; k < 3; ++k)
    if (size[k] < size[thin]) thin = k;
  const float area = size[(thin + 1) % 3] * size[(thin + 2) % 3];
  if (!(size[thin] < 0.05f) || !(area > 1.0f)) return false;
  const Vec3 axis = thin == 0 ? Vec3(1, 0, 0) : thin == 1 ? Vec3(0, 1, 0) : Vec3(0, 0, 1);
  const Vec3 n = normalize(L.inverse().transposed() * axis);  // the thin way, turned as normals turn
  out = {Vec4(n, -dot(n, xf.point(box.center()))), area};
  return true;
}
// Of the panes in view, the plane that mirrors, its normal towards the eye (false: none; the eye in the glass doesn't count).
inline bool largestPane(const std::vector<Pane>& inView, Vec3 eye, Vec4& plane) {
  std::vector<Pane> planes;
  for (Pane p : inView) {
    const float side = dot(Vec3(p.plane.x, p.plane.y, p.plane.z), eye) + p.plane.w;
    if (std::fabs(side) < 0.05f) continue;
    if (side < 0) p.plane = p.plane * -1.0f;
    bool joined = false;
    for (Pane& q : planes)
      if (dot(Vec3(q.plane.x, q.plane.y, q.plane.z), Vec3(p.plane.x, p.plane.y, p.plane.z)) > 0.999f && std::fabs(q.plane.w - p.plane.w) < 0.02f) {
        q.area += p.area, joined = true;
        break;
      }
    if (!joined) planes.push_back(p);
  }
  const Pane* best = nullptr;
  for (const Pane& p : planes)
    if (!best || p.area > best->area) best = &p;
  if (best) plane = best->plane;
  return best != nullptr;
}
// Is a pane on this plane (either way round)?
inline bool onPlane(const Pane& p, Vec4 plane) {
  const float c = dot(Vec3(p.plane.x, p.plane.y, p.plane.z), Vec3(plane.x, plane.y, plane.z));
  return std::fabs(c) > 0.999f && std::fabs(p.plane.w - (c > 0 ? plane.w : -plane.w)) < 0.02f;
}
// The mirror leaves out what it would show smaller than `smallest` pixels across
// its radius (render.mirrorSmallest, counted in the screen's pixels), seen from
// the eye mirrored in the pane: a mug reflected in a window is a dot at a
// quarter of the pixels, but every different shape is one more draw call.
inline bool mirrorSkips(const AABB& box, Vec4 plane, Vec3 eye, float pixelsPerRadian, float smallest) {
  if (smallest <= 0) return false;
  const Vec3 n(plane.x, plane.y, plane.z);
  const Vec3 mirrored = eye - n * (2 * (dot(n, eye) + plane.w));
  return radiusPixels(box, mirrored, pixelsPerRadian) < smallest;
}
// Where a box lands on screen: its corners' low and high x, y in -1..1 (the
// whole screen when a corner is behind the eye). The mirror draws only there.
inline Vec4 screenRect(const AABB& box, const Mat4& viewProj) {
  Vec4 r(1, 1, -1, -1);
  for (int k = 0; k < 8; ++k) {
    const Vec4 c = viewProj * Vec4((k & 1) ? box.hi.x : box.lo.x, (k & 2) ? box.hi.y : box.lo.y, (k & 4) ? box.hi.z : box.lo.z, 1);
    if (c.w <= 1e-4f) return {-1, -1, 1, 1};
    r = {std::min(r.x, c.x / c.w), std::min(r.y, c.y / c.w), std::max(r.z, c.x / c.w), std::max(r.w, c.y / c.w)};
  }
  return {std::clamp(r.x, -1.0f, 1.0f), std::clamp(r.y, -1.0f, 1.0f), std::clamp(r.z, -1.0f, 1.0f), std::clamp(r.w, -1.0f, 1.0f)};
}

// The mirroring in a plane: p - 2 (n.p + d) n.
inline Mat4 mirrorIn(Vec4 plane) {
  Mat4 R;
  const float n[3] = {plane.x, plane.y, plane.z};
  for (int c = 0; c < 3; ++c) {
    for (int r = 0; r < 3; ++r) R.m[c][r] = (c == r ? 1.0f : 0.0f) - 2 * n[r] * n[c];
    R.m[3][c] = -2 * plane.w * n[c];
  }
  return R;
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
  float lampError = 0;             // render.lampError
  float lampShare = 1;             // render.lampShare
  float lodBias = 0;               // world.lodBias
  bool portraitWide = true;        // cam.portraitWide: an upright screen keeps the lens's width
  // render.clusters: drawn from the cook's cluster trees, as the graphics chip
  // does it (each copy in view, then each cluster in view at the detail whose
  // error looks under render.clusterError pixels; one draw a pass; each pixel
  // lit once). Glass is still drawn the old way.
  bool clusters = false;
  float mirrorSmallest = 0;        // render.mirrorSmallest: the mirror leaves out what it shows smaller than this (pixels)
  bool mirrorPane = false;         // render.mirrorPane: the largest pane in view mirrors the room (a second drawing, a quarter of the pixels)
  float clusterError = 1;
  int textureSize = 256;           // render.textureSize: the side of every picture layer on the graphics chip
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
  // the mirror (order M60): the room drawn again from the eye mirrored in the pane, at a quarter of the pixels,
  // the old way (no clusters, no depth first); per covered pixel of the frame
  bool mirror = false;
  size_t mirrorThings = 0, mirrorDraws = 0;
  uint64_t mirrorTriangles = 0;
  float mirrorShaded = 0;        // its fragments shaded, per covered pixel of the frame
  float mirrorLampWork = 0;      // and their lamps worked out
  // the pictures on the graphics chip, as the renderer lays them out: each model picture one layer, each
  // surface two (its colour and shape tiles), the wander picture one if a surface's slow change reads it;
  // every layer `textureSize` square in 4 bytes a pixel, with its smaller copies (a third more)
  size_t pictureLayers = 0, surfaceLayers = 0;
  uint64_t pictureBytes = 0;
  uint64_t triangles() const { return sceneTriangles + shadowTriangles + prepassTriangles + mirrorTriangles; }
  size_t draws() const { return sceneDraws + shadowDraws + prepassDraws + mirrorDraws; }
  // per-pixel work today: fragments shaded x lamps each works out (the mirror's too)
  float lampWorkPerPixel() const { return shadedPerPixel * lampsComputed + mirrorLampWork; }
};

// The view's camera matrices, exactly as the renderer builds them.
Mat4 frameViewProj(const FrameView& view);
Mat4 frameShadowViewProj(const FrameView& view, Vec3 towardSun, int shadowMapSize);

FrameCost measureFrame(const Scene& scene, const FrameView& view, int maxSamples = 40000);
// The layers and bytes of a scene's pictures on the graphics chip at `textureSize` (as above).
void countPictures(const Scene& scene, int textureSize, size_t& layers, size_t& surfaceLayers, uint64_t& bytes);

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
