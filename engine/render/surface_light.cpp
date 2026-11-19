#include "engine/render/surface_light.h"

#include <algorithm>
#include <cmath>
#include <unordered_map>

#include "engine/core/random.h"
#include "engine/render/pathtracer.h"

namespace loop {

namespace {
// The way a surface faces most: 0..5 (+x -x +y -y +z -z).
int faceOf(Vec3 n) {
  const Vec3 a{std::fabs(n.x), std::fabs(n.y), std::fabs(n.z)};
  const int ax = a.x >= a.y && a.x >= a.z ? 0 : (a.y >= a.z ? 1 : 2);
  return 2 * ax + (n[ax] < 0 ? 1 : 0);
}
constexpr int kMax[3] = {1024, 512, 1024};
}  // namespace

uint32_t SurfaceLight::key(int x, int y, int z, int f) {
  if (x < 0 || y < 0 || z < 0 || x >= kMax[0] || y >= kMax[1] || z >= kMax[2]) return kEmpty;
  return static_cast<uint32_t>(x) | (static_cast<uint32_t>(y) << 10) | (static_cast<uint32_t>(z) << 19) | (static_cast<uint32_t>(f) << 29);
}

uint32_t SurfaceLight::hash(uint32_t k) {
  k ^= k >> 16, k *= 0x7feb352du;
  k ^= k >> 15, k *= 0x846ca68bu;
  return k ^ (k >> 16);
}

int SurfaceLight::find(uint32_t k) const {
  if (slots.empty() || k == kEmpty) return -1;
  const uint32_t mask = static_cast<uint32_t>(slots.size() - 1);
  for (uint32_t s = hash(k) & mask, n = 0; n <= mask; s = (s + 1) & mask, ++n) {
    const int c = slots[s];
    if (c < 0) return -1;
    if (keys[static_cast<size_t>(c)] == k) return c;
  }
  return -1;
}

Vec3 SurfaceLight::irradiance(Vec3 p, Vec3 n, bool* found) const {
  if (found) *found = false;
  if (!on) return Vec3(0);
  const int f = faceOf(n), a = f / 2, u = (a + 1) % 3, v = (a + 2) % 3;
  const Vec3 q = (p - origin) / cell;
  int c[3];
  c[u] = static_cast<int>(std::floor(q[u])), c[v] = static_cast<int>(std::floor(q[v]));
  // the layer the surface is in (a surface right on a cell's edge may have been found on either side)
  const int k0 = static_cast<int>(std::floor(q[a]));
  int layer = 0;
  bool any = false;
  const float edge = q[a] - static_cast<float>(k0);
  for (int dk : {0, edge < 0.25f ? -1 : 0, edge > 0.75f ? 1 : 0}) {
    c[a] = k0 + dk;
    if (find(key(c[0], c[1], c[2], f)) >= 0) { layer = c[a], any = true; break; }
  }
  if (!any) return Vec3(0);
  const float fu = q[u] - 0.5f, fv = q[v] - 0.5f;
  const int iu = static_cast<int>(std::floor(fu)), iv = static_cast<int>(std::floor(fv));
  const float tu = fu - static_cast<float>(iu), tv = fv - static_cast<float>(iv);
  Vec3 sum(0);
  float w = 0;
  for (int j = 0; j < 4; ++j) {
    c[a] = layer, c[u] = iu + (j & 1), c[v] = iv + (j >> 1);
    const int i = find(key(c[0], c[1], c[2], f));
    if (i < 0) continue;
    const float wj = ((j & 1) ? tu : 1 - tu) * ((j >> 1) ? tv : 1 - tv);
    sum += light[static_cast<size_t>(i)] * wj, w += wj;
  }
  if (w <= 1e-6f) return Vec3(0);
  if (found) *found = true;
  return sum / w;
}

uint32_t packRgb9e5(Vec3 c) {
  const float kMaxV = 65408.0f;  // (511 / 512) * 2^16
  const float r = std::clamp(c.x, 0.0f, kMaxV), g = std::clamp(c.y, 0.0f, kMaxV), b = std::clamp(c.z, 0.0f, kMaxV);
  const float m = std::max({r, g, b});
  int e = std::max(-16, static_cast<int>(std::floor(std::log2(std::max(m, 1e-30f))))) + 1 + 15;
  e = std::clamp(e, 0, 31);
  float scale = std::exp2(static_cast<float>(e - 15 - 9));
  if (static_cast<int>(std::floor(m / scale + 0.5f)) == 512) scale *= 2, e = std::min(e + 1, 31);
  auto part = [&](float x) { return static_cast<uint32_t>(std::clamp(static_cast<int>(std::floor(x / scale + 0.5f)), 0, 511)); };
  return part(r) | (part(g) << 9) | (part(b) << 18) | (static_cast<uint32_t>(e) << 27);
}

Vec3 unpackRgb9e5(uint32_t w) {
  const float scale = std::exp2(static_cast<float>(static_cast<int>(w >> 27) - 15 - 9));
  return Vec3(static_cast<float>(w & 511u), static_cast<float>((w >> 9) & 511u), static_cast<float>((w >> 18) & 511u)) * scale;
}

std::vector<uint32_t> SurfaceLight::packed() const {
  std::vector<uint32_t> out(slots.size() * 2, kEmpty);
  for (size_t s = 0; s < slots.size(); ++s)
    if (slots[s] >= 0) {
      const size_t c = static_cast<size_t>(slots[s]);
      out[2 * s] = keys[c], out[2 * s + 1] = packRgb9e5(light[c]);
    }
  return out;
}

SurfaceLight layOutSurfaceLight(const Scene& scene, const AABB& box, float cell, int maxCells) {
  SurfaceLight b;
  if (!box.valid()) return b;
  const Vec3 ext = box.extent();
  cell = std::max({cell, ext.x / (kMax[0] - 2), ext.y / (kMax[1] - 2), ext.z / (kMax[2] - 2), 1e-3f});
  struct Acc { Vec3 p{0}, n{0}; int count = 0; };
  for (int pass = 0; pass < 8; ++pass) {  // a cell wider each time too many are made
    b = SurfaceLight();
    b.cell = cell, b.origin = box.lo - Vec3(cell);
    std::unordered_map<uint32_t, int> index;
    std::vector<Acc> acc;
    const float step = cell * 0.5f;
    bool tooMany = false;
    for (const MeshInstance& inst : scene.instances) {
      if (inst.bend >= 0 || inst.mesh < 0 || inst.mesh >= static_cast<int>(scene.meshes.size())) continue;
      if (inst.material >= 0 && inst.material < static_cast<int>(scene.materials.size()) && scene.materials[static_cast<size_t>(inst.material)].transmission > 0.5f) continue;
      const Mesh& m = scene.meshes[static_cast<size_t>(inst.mesh)];
      for (size_t t = 0; t + 2 < m.indices.size() && !tooMany; t += 3) {
        const Vec3 A = inst.transform.point(m.positions[m.indices[t]]), B = inst.transform.point(m.positions[m.indices[t + 1]]),
                   C = inst.transform.point(m.positions[m.indices[t + 2]]);
        const Vec3 cr = cross(B - A, C - A);
        const float len = length(cr);
        if (len < 1e-10f) continue;
        AABB tb;
        tb.grow(A), tb.grow(B), tb.grow(C);
        if (tb.hi.x < box.lo.x || tb.lo.x > box.hi.x || tb.hi.y < box.lo.y || tb.lo.y > box.hi.y || tb.hi.z < box.lo.z || tb.lo.z > box.hi.z) continue;
        const Vec3 ng = cr / len;
        const int f = faceOf(ng);
        const float edge = std::max({length(B - A), length(C - B), length(A - C)});
        const int steps = std::clamp(static_cast<int>(std::ceil(edge / step)), 1, 4096);
        for (int i = 0; i < steps && !tooMany; ++i)
          for (int j = 0; i + j < steps; ++j) {
            const float bu = (static_cast<float>(i) + 1.0f / 3) / static_cast<float>(steps), bv = (static_cast<float>(j) + 1.0f / 3) / static_cast<float>(steps);
            const Vec3 p = A + (B - A) * bu + (C - A) * bv;
            if (p.x < box.lo.x || p.y < box.lo.y || p.z < box.lo.z || p.x > box.hi.x || p.y > box.hi.y || p.z > box.hi.z) continue;
            const Vec3 q = (p - b.origin) / cell;
            const uint32_t k = SurfaceLight::key(static_cast<int>(std::floor(q.x)), static_cast<int>(std::floor(q.y)), static_cast<int>(std::floor(q.z)), f);
            if (k == SurfaceLight::kEmpty) continue;
            auto [it, made] = index.try_emplace(k, static_cast<int>(acc.size()));
            if (made) {
              acc.emplace_back(), b.keys.push_back(k);
              if (static_cast<int>(acc.size()) > maxCells) { tooMany = true; break; }
            }
            Acc& a = acc[static_cast<size_t>(it->second)];
            a.p += p, a.n += ng, ++a.count;
          }
      }
      if (tooMany) break;
    }
    if (tooMany) { cell *= 1.5f; continue; }
    const size_t n = acc.size();
    b.at.resize(n), b.normal.resize(n), b.light.assign(n, Vec3(0)), b.rays.assign(n, 0);
    for (size_t i = 0; i < n; ++i) {
      b.at[i] = acc[i].p / static_cast<float>(acc[i].count);
      const float nl = length(acc[i].n);
      b.normal[i] = nl > 1e-6f ? acc[i].n / nl : Vec3(0, 1, 0);
    }
    size_t size = 1;
    while (size < 4 * n + 1) size <<= 1;  // a quarter full at most: few slots looked past (the shader looks at 32 at most)
    b.slots.assign(size, -1);
    const uint32_t mask = static_cast<uint32_t>(size - 1);
    for (size_t i = 0; i < n; ++i) {
      uint32_t s = SurfaceLight::hash(b.keys[i]) & mask;
      while (b.slots[s] >= 0) s = (s + 1) & mask;
      b.slots[s] = static_cast<int>(i);
    }
    b.on = n > 0;
    return b;
  }
  return SurfaceLight();
}

void bakeCell(SurfaceLight& bake, int i, PathTracer& pt, int rays, int bounces, uint32_t seed) {
  const size_t c = static_cast<size_t>(i);
  const Vec3 p = bake.at[c], n = bake.normal[c];
  const uint32_t h = hashCombine(static_cast<uint32_t>(i) * 2654435761u, seed);
  // a rare path that found something very bright by chance would light the
  // whole cell: none may count for more than 16 times the others' average
  std::vector<Vec3> got(static_cast<size_t>(std::max(rays, 1)));
  std::vector<float> lum(got.size());
  for (size_t k = 0; k < got.size(); ++k) got[k] = pt.surfaceLight(p, n, hashCombine(h, static_cast<uint32_t>(k)), bounces), lum[k] = luminance(got[k]);
  std::vector<float> sorted = lum;
  std::sort(sorted.begin(), sorted.end());
  const size_t keep = sorted.size() > 4 ? sorted.size() - 2 : sorted.size();
  double rest = 0;
  for (size_t k = 0; k < keep; ++k) rest += sorted[k];
  const float cap = keep < sorted.size() ? std::max(16.0f * static_cast<float>(rest / static_cast<double>(keep)), 1e-6f) : kInf;
  Vec3 sum(0);
  for (size_t k = 0; k < got.size(); ++k) sum += lum[k] > cap ? got[k] * (cap / lum[k]) : got[k];
  const int was = bake.rays[c], now = was + static_cast<int>(got.size());
  bake.light[c] = (bake.light[c] * static_cast<float>(was) + sum) / static_cast<float>(now);
  bake.rays[c] = now;
}

}  // namespace loop
