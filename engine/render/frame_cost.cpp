#include "engine/render/frame_cost.h"

#include <algorithm>
#include <cmath>
#include <set>
#include <utility>

#include "engine/geometry/cook.h"
#include "engine/render/light_grid.h"
#include "engine/render/scene_bvh.h"

namespace loop {

namespace {

constexpr int kMaxGpuLights = 256;  // the renderer's kMaxLights

bool isGlass(const Scene& s, int material) {
  return material >= 0 && material < static_cast<int>(s.materials.size()) && s.materials[material].transmission > 0;
}

// Light arriving at a point facing `n` from one lamp (lux), with no shadows,
// as the live shader adds it.
float lampLux(const Light& l, Vec3 p, Vec3 n) {
  Vec3 tl = l.position - p;
  float d2 = std::max(dot(tl, tl), std::max(l.radius, 0.01f) * std::max(l.radius, 0.01f));
  Vec3 dir = tl / std::sqrt(std::max(dot(tl, tl), 1e-12f));
  float nl = dot(n, dir);
  if (nl <= 0) return 0;
  switch (l.kind) {
    case Light::Kind::kSpot: {
      float ci = std::cos(l.innerAngle), co = std::cos(std::max(l.outerAngle, l.innerAngle + 1e-3f));
      float candela = l.lumens / (kTwoPi * (1 - 0.5f * (ci + co)));
      return candela * smoothstep(co, ci, dot(-dir, normalize(l.direction))) * nl / d2;
    }
    case Light::Kind::kRect:
      return l.lumens / kPi * std::max(dot(-dir, normalize(l.direction)), 0.0f) * nl / d2;
    default:
      return l.lumens / (4 * kPi) * nl / d2;
  }
}

// Expected fragments shaded at a pixel with k surfaces drawn in no particular
// order and no depth pre-pass: each one passes the depth test only if it is
// nearer than all drawn before it (on average 1 + 1/2 + ... + 1/k).
float shadedOf(int k) {
  float s = 0;
  for (int i = 1; i <= k; ++i) s += 1.0f / static_cast<float>(i);
  return s;
}

// How big an error looks, in pixels (the cull shader's `looks`): from inside
// its sphere any error is too big.
float looks(float err, Vec3 centre, float radius, Vec3 eye, float perPixel) {
  float d = length(centre - eye) - radius;
  if (d <= 1e-4f) return err > 0 ? 3.0e38f : 0.0f;
  return err / d * perPixel;
}

}  // namespace

void chooseClusters(const ClusterTree& tree, const Mat4& m, const Frustum& view, Vec3 eye, float perPixel, float limit, size_t& clusters,
                    uint64_t& triangles) {
  auto col = [&](int i) { return length(Vec3(m.m[i][0], m.m[i][1], m.m[i][2])); };
  const float scale = std::max(col(0), std::max(col(1), col(2)));
  for (const Cluster& cl : tree.clusters) {
    if (looks(cl.error * scale, m.point(cl.lodSphere.xyz()), cl.lodSphere.w * scale, eye, perPixel) > limit) continue;
    if (cl.parentError < kInf && looks(cl.parentError * scale, m.point(cl.parentSphere.xyz()), cl.parentSphere.w * scale, eye, perPixel) <= limit) continue;
    Vec3 bc = m.point(cl.bounds.xyz());
    float br = cl.bounds.w * scale;
    bool inside = true;
    for (const Plane& p : view.p)
      if (dot(p.n, bc) + p.d < -br) { inside = false; break; }
    if (!inside) continue;
    ++clusters, triangles += cl.count;
  }
}

// the camera as the screen sees it (an upright screen keeps the lens's width)
Camera viewCamera(const FrameView& v) {
  Camera c = v.camera;
  if (v.portraitWide) c.widenForPortrait(static_cast<float>(std::max(v.width, 1)) / static_cast<float>(std::max(v.height, 1)));
  return c;
}

Mat4 frameViewProj(const FrameView& v) {
  float aspect = static_cast<float>(std::max(v.width, 1)) / static_cast<float>(std::max(v.height, 1));
  Camera c = viewCamera(v);
  Mat4 view = Mat4::lookAt(c.position, c.target, c.up);
  return Mat4::perspective(c.fovY(), aspect, 0.05f, std::max(v.drawDistance, 1.0f)) * view;
}

Mat4 frameShadowViewProj(const FrameView& v, Vec3 towardSun, int shadowMapSize) {
  const Camera& cam = v.camera;
  float R = v.shadowRadius;
  Vec3 fwd = normalize(cam.target - cam.position);
  Vec3 center = cam.position + fwd * (R * 0.6f);
  Vec3 up = std::fabs(towardSun.y) > 0.99f ? Vec3(1, 0, 0) : Vec3(0, 1, 0);
  Mat4 lv = Mat4::lookAt(Vec3(0), -towardSun, up);
  Vec3 lc = lv.point(center);
  float texel = 2 * R / static_cast<float>(std::max(shadowMapSize, 16));
  lc.x = std::floor(lc.x / texel) * texel, lc.y = std::floor(lc.y / texel) * texel;
  return Mat4::ortho(lc.x - R, lc.x + R, lc.y - R, lc.y + R, -lc.z - R * 4, -lc.z + R * 4) * lv;
}

void countPictures(const Scene& scene, int textureSize, size_t& layers, size_t& surfaceLayers, uint64_t& bytes) {
  surfaceLayers = scene.surfaces.size() * 2;
  bool wander = false;
  for (const SurfaceRecipe& r : scene.surfaces) wander = wander || r.varyLarge > 0;
  if (wander) ++surfaceLayers;
  layers = scene.textures.size() + surfaceLayers;
  // one layer with its chain of smaller copies, down to a pixel
  uint64_t one = 0;
  for (int s = std::clamp(textureSize, 16, 2048); s >= 1; s /= 2) one += static_cast<uint64_t>(s) * s * 4;
  bytes = std::max<size_t>(layers, 1) * one;
}

FrameCost measureFrame(const Scene& scene, const FrameView& view, int maxSamples) {
  FrameCost c;
  countPictures(scene, view.textureSize, c.pictureLayers, c.surfaceLayers, c.pictureBytes);
  const Light* sun = nullptr;
  int lamps = 0;
  for (const Light& l : scene.lights) {
    if (l.kind == Light::Kind::kSun) {
      if (!sun) sun = &l;
    } else {
      ++lamps;
    }
  }
  c.sun = sun != nullptr;
  Vec3 towardSun = sun ? normalize(-sun->direction) : Vec3(0, 1, 0);
  c.lampsComputed = static_cast<float>(std::min({lamps, kMaxGpuLights, std::max(view.maxLights, 1)}));
  // the light grid: a pixel works out only its cell's entries (as the renderer builds it)
  const LightGrid grid = view.lightGrid ? buildLightGrid(gridLamps(scene, kMaxGpuLights), view.lampGrouping, 8, 4096) : LightGrid();
  double gridEntries = 0;

  // ---- the passes: what each draws, and what skipping off-screen copies leaves
  Frustum eye = Frustum::fromViewProj(frameViewProj(view));
  Frustum shadow = Frustum::fromViewProj(frameShadowViewProj(view, towardSun, 1024));
  const float pixelsPerRadian = static_cast<float>(std::max(view.height, 1)) / (2 * std::tan(viewCamera(view).fovY() * 0.5f));
  std::vector<AABB> meshBox(scene.meshes.size());
  for (size_t i = 0; i < scene.meshes.size(); ++i) meshBox[i] = scene.meshes[i].bounds();
  std::set<std::pair<int, bool>> sceneBatches, shadowBatches, viewBatches, shadowViewBatches;
  // the cluster way: every mesh's tree, from the cook (made once, then kept)
  std::vector<std::shared_ptr<const ClusterTree>> trees;
  if (view.clusters) {
    std::vector<const Mesh*> ms;
    for (const Mesh& m : scene.meshes) ms.push_back(&m);
    Cook::shared().cookAll(ms);
    for (const Mesh& m : scene.meshes) trees.push_back(Cook::shared().clusters(m));
    c.clustered = true;
  }
  for (const MeshInstance& in : scene.instances) {
    if (in.mesh < 0 || in.mesh >= static_cast<int>(scene.meshes.size())) continue;
    if (!scene.meshes[in.mesh].triangleCount()) continue;
    ++c.things;
    bool hidden = std::fabs(in.transform.linear().determinant()) < 1e-12f;
    if (hidden) ++c.hidden;
    bool glass = isGlass(scene, in.material);
    AABB box = meshBox[in.mesh].transformed(in.transform);
    // the version drawn, by its size on screen (both passes use the camera's)
    int level = lodLevel(radiusPixels(box, view.camera.position, pixelsPerRadian), view.lodBias);
    int drawn = scene.lodMesh(in.mesh, level);
    int shadowDrawn = scene.lodMesh(in.mesh, shadowLodLevel(level));
    uint64_t tris = scene.meshes[drawn].triangleCount(), shadowTris = scene.meshes[shadowDrawn].triangleCount();
    bool hasLods = static_cast<size_t>(in.mesh) < scene.lods.size() && scene.lods[static_cast<size_t>(in.mesh)][0] >= 0;
    bool inView = !hidden && eye.visible(box);
    bool inShadow = c.sun && !glass && !hidden && shadow.visible(box);
    if (view.clusters && !glass) {
      // the GPU picks each cluster of the full mesh; the copy's whole box first, on the processor
      const ClusterTree& tree = *trees[static_cast<size_t>(in.mesh)];
      if (!view.cullOffscreen || inView) {
        ++c.sceneThings, ++c.inViewThings, c.fullTriangles += scene.meshes[in.mesh].triangleCount();
        uint64_t before = c.sceneTriangles;
        chooseClusters(tree, in.transform, eye, view.camera.position, pixelsPerRadian, view.clusterError, c.sceneClusters, c.sceneTriangles);
        c.inViewTriangles += c.sceneTriangles - before;
      }
      if (c.sun && (!view.cullOffscreen || inShadow)) {
        ++c.shadowThings, ++c.inShadowThings;
        uint64_t before = c.shadowTriangles;  // the detail you see, in the sun's view
        chooseClusters(tree, in.transform, shadow, view.camera.position, pixelsPerRadian, view.clusterError, c.shadowClusters, c.shadowTriangles);
        c.inShadowTriangles += c.shadowTriangles - before;
      }
      continue;
    }

    if (inView) ++c.inViewThings, c.inViewTriangles += tris, viewBatches.insert({drawn, glass});
    if (inShadow) ++c.inShadowThings, c.inShadowTriangles += shadowTris, shadowViewBatches.insert({shadowDrawn, false});
    if (!view.cullOffscreen || inView) {
      ++c.sceneThings, c.sceneTriangles += tris;
      if (hasLods) ++c.lodCopies[level], c.lodTriangles[level] += tris;
      sceneBatches.insert({drawn, glass});
      if (view.prepass && !glass) c.prepassTriangles += tris;
    }
    if (c.sun && !glass && (!view.cullOffscreen || inShadow)) {
      ++c.shadowThings, c.shadowTriangles += shadowTris;
      shadowBatches.insert({shadowDrawn, false});
    }
  }
  c.sceneDraws = sceneBatches.size(), c.shadowDraws = shadowBatches.size();
  if (view.prepass && !view.clusters)
    for (const auto& b : sceneBatches) c.prepassDraws += !b.second;
  c.inViewDraws = viewBatches.size(), c.inShadowDraws = shadowViewBatches.size();
  if (view.clusters) {
    // one draw for every chosen cluster in each pass, then the glass the old way
    c.sceneDraws += c.sceneClusters > 0, c.shadowDraws += c.shadowClusters > 0;
    c.inViewDraws = c.sceneDraws, c.inShadowDraws = c.shadowDraws;
    c.prepassTriangles = 0;
  }

  // ---- per pixel: rays through a grid of the frame's pixels
  SceneBvh bvh;
  bvh.build(scene);
  Mat4 inv = frameViewProj(view).inverse();
  const int w = std::max(view.width, 1), h = std::max(view.height, 1);
  int stride = std::max(1, static_cast<int>(std::ceil(std::sqrt(static_cast<double>(w) * h / std::max(maxSamples, 1)))));
  int covered = 0, patterned = 0;
  double layers = 0, fronts = 0, shaded = 0, shadedFronts = 0, matter = 0, matter99 = 0;
  std::vector<std::pair<float, bool>> parts;  // (lux, is a lamp)
  for (int y = stride / 2; y < h; y += stride)
    for (int x = stride / 2; x < w; x += stride) {
      ++c.samples;
      float nx = (static_cast<float>(x) + 0.5f) / static_cast<float>(w) * 2 - 1;
      float ny = 1 - (static_cast<float>(y) + 0.5f) / static_cast<float>(h) * 2;
      Vec3 a = inv.point({nx, ny, 0}), b = inv.point({nx, ny, 1});
      float reach = length(b - a);
      Ray ray{a, (b - a) / reach};
      int k = 0, front = 0;
      float tmin = 0;
      SceneHit first;
      for (; k < 64; ++k) {
        SceneHit hit = bvh.closest(ray, tmin, reach);
        if (!hit.valid()) break;
        if (k == 0) first = hit;
        if (dot(bvh.triangle(hit.inst, hit.prim).ng, ray.d) < 0) ++front;
        tmin = hit.t + std::max(1e-4f, hit.t * 1e-5f);
      }
      if (!k) continue;
      ++covered;
      layers += k, fronts += front;
      // clusters: one draw writes which triangle each pixel sees, then each pixel is lit once
      shaded += view.prepass || view.clusters ? 1.0f : shadedOf(view.cullBackFaces ? std::max(front, 1) : k);
      shadedFronts += shadedOf(std::max(front, 1));
      TriInfo tri = bvh.triangle(first.inst, first.prim);
      const int mat = bvh.materialOf(first.inst);
      if (mat >= 0 && mat < static_cast<int>(scene.materials.size())) {
        Pattern::Kind pk = scene.materials[mat].pattern.kind;
        if (pk != Pattern::Kind::kNone && pk != Pattern::Kind::kImage) ++patterned;
      }
      // the lamps that give 95% (and 99%) of this point's direct light (the sun
      // counts toward the light, not as a lamp)
      Vec3 p = ray.at(first.t);
      Vec3 n = dot(tri.ng, ray.d) < 0 ? tri.ng : -tri.ng;
      parts.clear();
      float total = 0;
      if (sun) {
        float e = sun->lux * std::max(dot(n, towardSun), 0.0f);
        if (e > 0) parts.push_back({e, false}), total += e;
      }
      for (const Light& l : scene.lights) {
        if (l.kind == Light::Kind::kSun) continue;
        float e = lampLux(l, p, n);
        if (e > 0) parts.push_back({e, true}), total += e;
      }
      int need = 0, need99 = 0;
      if (total > 0) {
        std::sort(parts.begin(), parts.end(), [](auto& u, auto& v) { return u.first > v.first; });
        float sum = 0;
        for (auto& [e, lamp] : parts) {
          if (sum >= 0.99f * total) break;
          if (sum < 0.95f * total && lamp) ++need;
          sum += e;
          if (lamp) ++need99;
        }
      }
      matter += need, matter99 += need99;
      if (grid.on) {
        const int cell = grid.cellOf(p);
        gridEntries += grid.start[static_cast<size_t>(cell) + 1] - grid.start[static_cast<size_t>(cell)];
      }
      c.lampsThatMatterMost = std::max(c.lampsThatMatterMost, need);
    }
  if (c.samples) c.coverage = static_cast<float>(covered) / static_cast<float>(c.samples);
  if (covered) {
    c.layers = static_cast<float>(layers / covered), c.frontLayers = static_cast<float>(fronts / covered);
    c.shadedPerPixel = static_cast<float>(shaded / covered);
    c.shadedFrontsOnly = static_cast<float>(shadedFronts / covered);
    c.lampsThatMatter = static_cast<float>(matter / covered);
    c.lampsFor99 = static_cast<float>(matter99 / covered);
    if (grid.on) c.lampsComputed = static_cast<float>(gridEntries / covered);
    c.patternShare = static_cast<float>(patterned) / static_cast<float>(covered);
  }
  return c;
}

FrameBudget budgetFor(const std::string& quality) {
  FrameBudget b;
  if (quality == "high") {
    b.quality = "high", b.width = 561, b.height = 1216;  // 1.5 pixels per point, 87%
    b.triangles = 2000000, b.draws = 1000, b.shadedPerPixel = 2.5f, b.lampWorkPerPixel = 60;
  } else {
    b.quality = "phone", b.width = 374, b.height = 811;  // 1 pixel per point, 87%
    b.triangles = 600000, b.draws = 400, b.shadedPerPixel = 2.0f, b.lampWorkPerPixel = 24;
  }
  return b;
}

std::vector<std::string> overBudget(const FrameCost& c, const FrameBudget& b) {
  std::vector<std::string> out;
  char line[160];
  if (c.triangles() > b.triangles) {
    std::snprintf(line, sizeof line, "triangles %.2f M (budget %.2f M)", static_cast<double>(c.triangles()) / 1e6, static_cast<double>(b.triangles) / 1e6);
    out.push_back(line);
  }
  if (c.draws() > b.draws) {
    std::snprintf(line, sizeof line, "draw calls %zu (budget %zu)", c.draws(), b.draws);
    out.push_back(line);
  }
  if (c.shadedPerPixel > b.shadedPerPixel) {
    std::snprintf(line, sizeof line, "surfaces shaded per pixel %.2f (budget %.2f)", static_cast<double>(c.shadedPerPixel), static_cast<double>(b.shadedPerPixel));
    out.push_back(line);
  }
  if (c.lampWorkPerPixel() > b.lampWorkPerPixel) {
    std::snprintf(line, sizeof line, "lamp work per pixel %.0f (budget %.0f)", static_cast<double>(c.lampWorkPerPixel()), static_cast<double>(b.lampWorkPerPixel));
    out.push_back(line);
  }
  return out;
}

}  // namespace loop
