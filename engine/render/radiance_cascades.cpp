#include "engine/render/radiance_cascades.h"

#include <algorithm>
#include <cmath>

#include "engine/core/random.h"
#include "engine/render/pathtracer.h"

namespace loop {

namespace {
Vec3 octDecode(float ex, float ey) {
  Vec3 n(ex, ey, 1 - std::fabs(ex) - std::fabs(ey));
  if (n.z < 0) {
    const float x = (1 - std::fabs(n.y)) * (n.x >= 0 ? 1.0f : -1.0f), y = (1 - std::fabs(n.x)) * (n.y >= 0 ? 1.0f : -1.0f);
    n.x = x, n.y = y;
  }
  return normalize(n);
}
// the solid angle of the spherical triangle a b c (Van Oosterom and Strackee)
float triangleAngle(Vec3 a, Vec3 b, Vec3 c) {
  const float num = std::fabs(dot(a, cross(b, c)));
  const float den = 1 + dot(a, b) + dot(b, c) + dot(c, a);
  return 2 * std::atan2(num, den);
}
}  // namespace

Vec3 octDirection(int u, int v, int n) {
  return octDecode((static_cast<float>(u) + 0.5f) / static_cast<float>(n) * 2 - 1, (static_cast<float>(v) + 0.5f) / static_cast<float>(n) * 2 - 1);
}

float octSolidAngle(int u, int v, int n) {
  // the texel cut in four round its middle, each quarter as two spherical triangles
  // (a texel's edges bend on the sphere; quarters follow them closely enough)
  const float fn = static_cast<float>(n);
  auto at = [&](float x, float y) { return octDecode(x / fn * 2 - 1, y / fn * 2 - 1); };
  const float x0 = static_cast<float>(u), y0 = static_cast<float>(v);
  float total = 0;
  for (int qy = 0; qy < 2; ++qy)
    for (int qx = 0; qx < 2; ++qx) {
      const float ax = x0 + qx * 0.5f, ay = y0 + qy * 0.5f;
      const Vec3 a = at(ax, ay), b = at(ax + 0.5f, ay), c = at(ax + 0.5f, ay + 0.5f), d = at(ax, ay + 0.5f);
      total += triangleAngle(a, b, c) + triangleAngle(a, c, d);
    }
  return total;
}

RadianceCascades buildCascades(PathTracer& pt, const AABB& box, float spacing, int levels, int dirs, float reach, int bounces, uint32_t seed) {
  RadianceCascades rc;
  const Vec3 e = vmax(box.extent(), Vec3(0.01f));
  for (int i = 0; i < levels; ++i) {
    RadianceCascades::Level L;
    // about spacing x 2^i apart, the grid fitted to the box exactly (a probe
    // outside a room's walls would see the sky and bring it in)
    const float want = spacing * static_cast<float>(1 << i);
    L.origin = box.lo;
    L.nx = std::max(2, static_cast<int>(std::lround(e.x / want)) + 1);
    L.ny = std::max(2, static_cast<int>(std::lround(e.y / want)) + 1);
    L.nz = std::max(2, static_cast<int>(std::lround(e.z / want)) + 1);
    L.step = Vec3(e.x / static_cast<float>(L.nx - 1), e.y / static_cast<float>(L.ny - 1), e.z / static_cast<float>(L.nz - 1));
    L.dirs = dirs << i;
    L.t0 = reach * static_cast<float>((1 << i) - 1);
    L.t1 = i == levels - 1 ? 1e5f : reach * static_cast<float>((2 << i) - 1);
    const size_t n = static_cast<size_t>(L.probes()) * static_cast<size_t>(L.dirs * L.dirs);
    L.light.assign(n, Vec3(0));
    L.through.assign(n, 0);
    rc.levels.push_back(std::move(L));
  }
  // every level's rays, each over its own stretch
  for (int i = 0; i < levels; ++i) {
    RadianceCascades::Level& L = rc.levels[static_cast<size_t>(i)];
    for (int z = 0; z < L.nz; ++z)
      for (int y = 0; y < L.ny; ++y)
        for (int x = 0; x < L.nx; ++x) {
          const Vec3 p = L.probePos(x, y, z);
          for (int v = 0; v < L.dirs; ++v)
            for (int u = 0; u < L.dirs; ++u) {
              const int d = v * L.dirs + u;
              const size_t k = L.at(x, y, z, d);
              bool through = false;
              const uint32_t s = hashCombine(hashCombine(seed, static_cast<uint32_t>(k)), static_cast<uint32_t>(i) * 7919u);
              L.light[k] = pt.probeInterval(p, octDirection(u, v, L.dirs), L.t0, L.t1, s, bounces, &through);
              L.through[k] = through ? 1 : 0;
              ++rc.rays;
            }
        }
  }
  // merged from the top down: a ray that met nothing takes the light the level
  // above found farther on that way (its four directions there, blended between its probes)
  for (int i = levels - 2; i >= 0; --i) {
    RadianceCascades::Level& L = rc.levels[static_cast<size_t>(i)];
    const RadianceCascades::Level& U = rc.levels[static_cast<size_t>(i + 1)];
    for (int z = 0; z < L.nz; ++z)
      for (int y = 0; y < L.ny; ++y)
        for (int x = 0; x < L.nx; ++x) {
          const Vec3 q = (L.probePos(x, y, z) - U.origin) / U.step;
          const int x0 = std::clamp(static_cast<int>(std::floor(q.x)), 0, std::max(U.nx - 2, 0)),
                    y0 = std::clamp(static_cast<int>(std::floor(q.y)), 0, std::max(U.ny - 2, 0)),
                    z0 = std::clamp(static_cast<int>(std::floor(q.z)), 0, std::max(U.nz - 2, 0));
          const Vec3 f = vmin(vmax(q - Vec3(static_cast<float>(x0), static_cast<float>(y0), static_cast<float>(z0)), Vec3(0)), Vec3(1));
          for (int v = 0; v < L.dirs; ++v)
            for (int u = 0; u < L.dirs; ++u) {
              const size_t k = L.at(x, y, z, v * L.dirs + u);
              if (!L.through[k]) continue;
              Vec3 far(0);
              float wSum = 0;
              const Vec3 here = L.probePos(x, y, z), dir = octDirection(u, v, L.dirs);
              for (int c = 0; c < 8; ++c) {
                const int px = std::min(x0 + (c & 1), U.nx - 1), py = std::min(y0 + ((c >> 1) & 1), U.ny - 1), pz = std::min(z0 + (c >> 2), U.nz - 1);
                const float w = ((c & 1) ? f.x : 1 - f.x) * (((c >> 1) & 1) ? f.y : 1 - f.y) * ((c >> 2) ? f.z : 1 - f.z);
                if (w <= 0) continue;
                // the fix for leaks: a parent's rays start t0 out from it; only a
                // start this probe can see (no wall between) may give it light
                if (!pt.clear(here + dir * (L.t1 * 0.5f), U.probePos(px, py, pz) + dir * U.t0)) continue;
                ++rc.rays;
                Vec3 four(0);
                for (int j = 0; j < 4; ++j) four += U.light[U.at(px, py, pz, (2 * v + (j >> 1)) * U.dirs + 2 * u + (j & 1))];
                far += four * (0.25f * w);
                wSum += w;
              }
              if (wSum > 0) L.light[k] += far / wSum;
            }
        }
  }
  return rc;
}

Vec3 RadianceCascades::irradiance(Vec3 p, Vec3 n) const {
  if (levels.empty()) return Vec3(0);
  const Level& L = levels[0];
  const Vec3 q = (p - L.origin) / L.step;
  const int x0 = std::clamp(static_cast<int>(std::floor(q.x)), 0, std::max(L.nx - 2, 0)), y0 = std::clamp(static_cast<int>(std::floor(q.y)), 0, std::max(L.ny - 2, 0)),
            z0 = std::clamp(static_cast<int>(std::floor(q.z)), 0, std::max(L.nz - 2, 0));
  const Vec3 f = vmin(vmax(q - Vec3(static_cast<float>(x0), static_cast<float>(y0), static_cast<float>(z0)), Vec3(0)), Vec3(1));
  Vec3 out(0);
  for (int c = 0; c < 8; ++c) {
    const int x = std::min(x0 + (c & 1), L.nx - 1), y = std::min(y0 + ((c >> 1) & 1), L.ny - 1), z = std::min(z0 + (c >> 2), L.nz - 1);
    const float w = ((c & 1) ? f.x : 1 - f.x) * (((c >> 1) & 1) ? f.y : 1 - f.y) * ((c >> 2) ? f.z : 1 - f.z);
    if (w <= 0) continue;
    Vec3 e(0);
    for (int v = 0; v < L.dirs; ++v)
      for (int u = 0; u < L.dirs; ++u) {
        const float cosT = dot(n, octDirection(u, v, L.dirs));
        if (cosT > 0) e += L.light[L.at(x, y, z, v * L.dirs + u)] * (cosT * octSolidAngle(u, v, L.dirs));
      }
    out += e * w;
  }
  return out;
}

}  // namespace loop
