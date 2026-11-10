#include "engine/render/light_cache.h"

#include <algorithm>
#include <cmath>

#include "engine/core/random.h"
#include "engine/render/pathtracer.h"

namespace loop {

Vec3 LightCache::probePos(int i) const {
  const int x = i % nx, y = (i / nx) % ny, z = i / (nx * ny);
  return origin + Vec3(static_cast<float>(x), static_cast<float>(y), static_cast<float>(z)) * spacing;
}

Vec3 LightCache::irradiance(Vec3 p, Vec3 n) const {
  if (!on) return Vec3(0);
  Vec3 q = (p - origin) / spacing;
  q = vmin(vmax(q, Vec3(0)), Vec3(static_cast<float>(nx - 1), static_cast<float>(ny - 1), static_cast<float>(nz - 1)));
  const int x0 = std::min(static_cast<int>(q.x), std::max(nx - 2, 0)), y0 = std::min(static_cast<int>(q.y), std::max(ny - 2, 0)),
            z0 = std::min(static_cast<int>(q.z), std::max(nz - 2, 0));
  Vec3 out(0);
  for (int c = 0; c < 8; ++c) {
    const int x = std::min(x0 + (c & 1), nx - 1), y = std::min(y0 + ((c >> 1) & 1), ny - 1), z = std::min(z0 + (c >> 2), nz - 1);
    const float wx = (c & 1) ? q.x - x0 : 1 - (q.x - x0), wy = ((c >> 1) & 1) ? q.y - y0 : 1 - (q.y - y0), wz = (c >> 2) ? q.z - z0 : 1 - (q.z - z0);
    const auto& f = face[static_cast<size_t>(probeAt(x, y, z))];
    const Vec3 e = (n.x > 0 ? f[0] : f[1]) * (n.x * n.x) + (n.y > 0 ? f[2] : f[3]) * (n.y * n.y) + (n.z > 0 ? f[4] : f[5]) * (n.z * n.z);
    out += e * (std::max(wx, 0.0f) * std::max(wy, 0.0f) * std::max(wz, 0.0f));
  }
  return vmax(out, Vec3(0));
}

LightCache layOutLightCache(const AABB& box, int maxProbes, float minSpacing) {
  LightCache c;
  if (!(box.hi.x > box.lo.x) || maxProbes < 8) return c;
  const Vec3 e = vmax(box.extent(), Vec3(0.1f));
  float s = std::max(minSpacing, std::cbrt(e.x * e.y * e.z / static_cast<float>(maxProbes)));
  for (;;) {
    c.nx = static_cast<int>(std::floor(e.x / s)) + 2;
    c.ny = static_cast<int>(std::floor(e.y / s)) + 2;
    c.nz = static_cast<int>(std::floor(e.z / s)) + 2;
    if (c.probes() <= maxProbes) break;
    s *= 1.05f;
  }
  c.spacing = s;
  // centred on the box
  const Vec3 span = Vec3(static_cast<float>(c.nx - 1), static_cast<float>(c.ny - 1), static_cast<float>(c.nz - 1)) * s;
  c.origin = (box.lo + box.hi) * 0.5f - span * 0.5f;
  const size_t n = static_cast<size_t>(c.probes());
  c.face.assign(n, std::array<Vec3, 6>{});
  c.state.assign(n, LightCache::kWaiting);
  c.open.assign(n, 0), c.tested.assign(n, 0);
  c.on = true;
  return c;
}

void bakeProbe(LightCache& cache, int i, PathTracer& pt, int rays, int bounces, uint32_t seed) {
  const Vec3 p = cache.probePos(i);
  // evenly spread directions (a Fibonacci sphere), turned per probe
  const uint32_t h = hashCombine(static_cast<uint32_t>(i) * 2654435761u, seed);
  const float turn = toUnit(h) * kTwoPi;
  std::vector<Vec3> dirs(static_cast<size_t>(rays)), light(static_cast<size_t>(rays));
  int back = 0;
  for (int k = 0; k < rays; ++k) {
    const float z = 1 - (2 * k + 1.0f) / static_cast<float>(rays);
    const float r = std::sqrt(std::max(0.0f, 1 - z * z));
    const float phi = k * 2.39996323f + turn;
    dirs[static_cast<size_t>(k)] = Vec3(r * std::cos(phi), z, r * std::sin(phi));
    bool inside = false;
    light[static_cast<size_t>(k)] = pt.probeLight(p, dirs[static_cast<size_t>(k)], hashCombine(h, static_cast<uint32_t>(k)), bounces, &inside);
    back += inside;
  }
  // a rare ray that found something very bright by chance (a "firefly") would
  // light the whole probe: none may be brighter than 8 times the others' average
  std::vector<float> lum(light.size());
  for (size_t k = 0; k < light.size(); ++k) lum[k] = luminance(light[k]);
  std::vector<float> sorted = lum;
  std::sort(sorted.begin(), sorted.end());
  double rest = 0;
  const size_t keep = sorted.size() > 4 ? sorted.size() - 2 : sorted.size();
  for (size_t k = 0; k < keep; ++k) rest += sorted[k];
  const float cap = keep ? 8.0f * static_cast<float>(rest / static_cast<double>(keep)) : kInf;
  std::array<Vec3, 6> f{};
  const float w = 4 * kPi / static_cast<float>(rays);  // each ray's share of the sphere
  for (size_t k = 0; k < light.size(); ++k) {
    Vec3 L = light[k];
    if (lum[k] > cap && cap > 0) L *= cap / lum[k];
    const Vec3 d = dirs[k];
    for (int ax = 0; ax < 3; ++ax) {
      f[2 * ax] += L * (std::max(d[ax], 0.0f) * w);
      f[2 * ax + 1] += L * (std::max(-d[ax], 0.0f) * w);
    }
  }
  if (back * 4 > rays) {  // more than a quarter see the backs of surfaces: inside something
    cache.state[static_cast<size_t>(i)] = LightCache::kInside;
    return;
  }
  cache.face[static_cast<size_t>(i)] = f;
  cache.state[static_cast<size_t>(i)] = LightCache::kBaked;
  // its links to the neighbours (+x, +y, +z here; the others when they bake theirs)
  if (cache.open.size() != cache.state.size()) cache.open.assign(cache.state.size(), 0), cache.tested.assign(cache.state.size(), 0);
  const int x = i % cache.nx, y = (i / cache.nx) % cache.ny, z = i / (cache.nx * cache.ny);
  for (int d = 0; d < 6; ++d) {
    const int ax = d / 2, s = d % 2 ? -1 : 1;
    const int qx = x + (ax == 0 ? s : 0), qy = y + (ax == 1 ? s : 0), qz = z + (ax == 2 ? s : 0);
    if (qx < 0 || qy < 0 || qz < 0 || qx >= cache.nx || qy >= cache.ny || qz >= cache.nz) continue;
    const int j = cache.probeAt(qx, qy, qz);
    // two rays a little apart: one can slip along the seam between two triangles
    const Vec3 q = cache.probePos(j), off = Vec3(0.031f, 0.047f, 0.023f) * cache.spacing;
    const bool seen = pt.clear(p, q) && pt.clear(p + off, q + off);
    const uint8_t bit = static_cast<uint8_t>(1u << d), back = static_cast<uint8_t>(1u << (d ^ 1));
    cache.tested[static_cast<size_t>(i)] |= bit, cache.tested[static_cast<size_t>(j)] |= back;
    if (seen) cache.open[static_cast<size_t>(i)] |= bit, cache.open[static_cast<size_t>(j)] |= back;
    else cache.open[static_cast<size_t>(i)] &= static_cast<uint8_t>(~bit), cache.open[static_cast<size_t>(j)] &= static_cast<uint8_t>(~back);
  }
}

namespace {
// the neighbour of probe i that way, or -1
int neighbour(const LightCache& c, int i, int d) {
  const int x = i % c.nx, y = (i / c.nx) % c.ny, z = i / (c.nx * c.ny);
  const int ax = d / 2, s = d % 2 ? -1 : 1;
  const int qx = x + (ax == 0 ? s : 0), qy = y + (ax == 1 ? s : 0), qz = z + (ax == 2 ? s : 0);
  if (qx < 0 || qy < 0 || qz < 0 || qx >= c.nx || qy >= c.ny || qz >= c.nz) return -1;
  return c.probeAt(qx, qy, qz);
}
bool linked(const LightCache& c, int i, int d) {
  const size_t si = static_cast<size_t>(i);
  if (si >= c.tested.size() || !(c.tested[si] & (1u << d))) return true;  // not known: as open
  return (c.open[si] >> d) & 1u;
}
}  // namespace

int markReached(LightCache& c, const AABB& box, Vec3 at) {
  if (!c.on) return 0;
  // the probes round the box, as markNear
  const Vec3 lo = (box.lo - c.origin) / c.spacing - Vec3(1), hi = (box.hi - c.origin) / c.spacing + Vec3(1);
  auto inBox = [&](int i) {
    const int x = i % c.nx, y = (i / c.nx) % c.ny, z = i / (c.nx * c.ny);
    return x >= std::floor(lo.x) && x <= std::ceil(hi.x) && y >= std::floor(lo.y) && y <= std::ceil(hi.y) && z >= std::floor(lo.z) && z <= std::ceil(hi.z);
  };
  // start: the eight probes round the lamp (those not inside things)
  Vec3 q = (at - c.origin) / c.spacing;
  const int x0 = std::clamp(static_cast<int>(std::floor(q.x)), 0, c.nx - 1), y0 = std::clamp(static_cast<int>(std::floor(q.y)), 0, c.ny - 1),
            z0 = std::clamp(static_cast<int>(std::floor(q.z)), 0, c.nz - 1);
  std::vector<uint8_t> seen(c.state.size(), 0);
  std::vector<int> todo;
  for (int k = 0; k < 8; ++k) {
    const int x = std::min(x0 + (k & 1), c.nx - 1), y = std::min(y0 + ((k >> 1) & 1), c.ny - 1), z = std::min(z0 + (k >> 2), c.nz - 1);
    const int i = c.probeAt(x, y, z);
    if (!seen[static_cast<size_t>(i)] && c.state[static_cast<size_t>(i)] != LightCache::kInside) seen[static_cast<size_t>(i)] = 1, todo.push_back(i);
  }
  for (size_t t = 0; t < todo.size(); ++t) {
    const int i = todo[t];
    for (int d = 0; d < 6; ++d) {
      const int j = neighbour(c, i, d);
      if (j < 0 || seen[static_cast<size_t>(j)] || !inBox(j)) continue;
      if (c.state[static_cast<size_t>(j)] == LightCache::kInside) {
        seen[static_cast<size_t>(j)] = 2;  // takes its light from those beside it: marked, not spread through
        continue;
      }
      if (!linked(c, i, d)) continue;
      seen[static_cast<size_t>(j)] = 1, todo.push_back(j);
    }
  }
  int n = 0;
  for (size_t i = 0; i < seen.size(); ++i)
    if (seen[i] && c.state[i] != LightCache::kWaiting) c.state[i] = LightCache::kWaiting, ++n;
  return n;
}

std::vector<int> rooms(const LightCache& c) {
  std::vector<int> room(c.state.size(), -1);
  int next = 0;
  for (int s = 0; s < c.probes(); ++s) {
    if (room[static_cast<size_t>(s)] >= 0 || c.state[static_cast<size_t>(s)] == LightCache::kInside) continue;
    std::vector<int> todo{s};
    room[static_cast<size_t>(s)] = next;
    for (size_t t = 0; t < todo.size(); ++t)
      for (int d = 0; d < 6; ++d) {
        const int j = neighbour(c, todo[t], d);
        if (j < 0 || room[static_cast<size_t>(j)] >= 0 || c.state[static_cast<size_t>(j)] == LightCache::kInside || !linked(c, todo[t], d)) continue;
        room[static_cast<size_t>(j)] = next, todo.push_back(j);
      }
    ++next;
  }
  return room;
}

void fillInside(LightCache& c) {
  // each pass, an inside probe next to lit ones takes their average and is lit
  std::vector<uint8_t> lit(c.state.size());
  for (size_t i = 0; i < lit.size(); ++i) lit[i] = c.state[i] == LightCache::kBaked;
  for (bool changed = true; changed;) {
    changed = false;
    std::vector<uint8_t> next = lit;
    for (int z = 0; z < c.nz; ++z)
      for (int y = 0; y < c.ny; ++y)
        for (int x = 0; x < c.nx; ++x) {
          const int i = c.probeAt(x, y, z);
          if (lit[static_cast<size_t>(i)] || c.state[static_cast<size_t>(i)] != LightCache::kInside) continue;
          std::array<Vec3, 6> sum{};
          int n = 0;
          for (int d = 0; d < 6; ++d) {
            const int ax = d / 2, s = d % 2 ? -1 : 1;
            const int qx = x + (ax == 0 ? s : 0), qy = y + (ax == 1 ? s : 0), qz = z + (ax == 2 ? s : 0);
            if (qx < 0 || qy < 0 || qz < 0 || qx >= c.nx || qy >= c.ny || qz >= c.nz) continue;
            const int j = c.probeAt(qx, qy, qz);
            if (!lit[static_cast<size_t>(j)]) continue;
            for (int f = 0; f < 6; ++f) sum[static_cast<size_t>(f)] += c.face[static_cast<size_t>(j)][static_cast<size_t>(f)];
            ++n;
          }
          if (!n) continue;
          const float k = 1.0f / static_cast<float>(n);
          for (int f = 0; f < 6; ++f) c.face[static_cast<size_t>(i)][static_cast<size_t>(f)] = sum[static_cast<size_t>(f)] * k;
          next[static_cast<size_t>(i)] = 1;
          changed = true;
        }
    lit.swap(next);
  }
}

LightCache smoothed(const LightCache& c) {
  LightCache out = c;
  auto lit = [&](int i) { return c.state[static_cast<size_t>(i)] != LightCache::kWaiting; };
  for (int z = 0; z < c.nz; ++z)
    for (int y = 0; y < c.ny; ++y)
      for (int x = 0; x < c.nx; ++x) {
        const int i = c.probeAt(x, y, z);
        if (!lit(i)) continue;
        const size_t si = static_cast<size_t>(i);
        std::array<Vec3, 6> sum{};
        for (int f = 0; f < 6; ++f) sum[static_cast<size_t>(f)] = c.face[si][static_cast<size_t>(f)] * 0.4f;
        float w = 0.4f;
        for (int d = 0; d < 6; ++d) {
          const int ax = d / 2, s = d % 2 ? -1 : 1;
          const int qx = x + (ax == 0 ? s : 0), qy = y + (ax == 1 ? s : 0), qz = z + (ax == 2 ? s : 0);
          if (qx < 0 || qy < 0 || qz < 0 || qx >= c.nx || qy >= c.ny || qz >= c.nz) continue;
          const int j = c.probeAt(qx, qy, qz);
          if (!lit(j)) continue;
          const size_t sj = static_cast<size_t>(j);
          for (int f = 0; f < 6; ++f) sum[static_cast<size_t>(f)] += c.face[sj][static_cast<size_t>(f)] * 0.1f;
          w += 0.1f;
        }
        for (int f = 0; f < 6; ++f) out.face[si][static_cast<size_t>(f)] = sum[static_cast<size_t>(f)] / w;
      }
  return out;
}

int markNear(LightCache& c, const AABB& box) {
  if (!c.on) return 0;
  // the probes round the box (a probe a spacing outside still blends into it)
  const Vec3 lo = (box.lo - c.origin) / c.spacing - Vec3(1), hi = (box.hi - c.origin) / c.spacing + Vec3(1);
  const int x0 = std::max(0, static_cast<int>(std::floor(lo.x))), x1 = std::min(c.nx - 1, static_cast<int>(std::ceil(hi.x)));
  const int y0 = std::max(0, static_cast<int>(std::floor(lo.y))), y1 = std::min(c.ny - 1, static_cast<int>(std::ceil(hi.y)));
  const int z0 = std::max(0, static_cast<int>(std::floor(lo.z))), z1 = std::min(c.nz - 1, static_cast<int>(std::ceil(hi.z)));
  int n = 0;
  for (int z = z0; z <= z1; ++z)
    for (int y = y0; y <= y1; ++y)
      for (int x = x0; x <= x1; ++x) {
        uint8_t& st = c.state[static_cast<size_t>(c.probeAt(x, y, z))];
        if (st != LightCache::kWaiting) st = LightCache::kWaiting, ++n;
      }
  return n;
}

std::vector<int> waitingOrder(const LightCache& c, Vec3 eye) {
  std::vector<int> out;
  for (int i : bakeOrder(c, eye))
    if (c.state[static_cast<size_t>(i)] == LightCache::kWaiting) out.push_back(i);
  return out;
}

std::vector<int> bakeOrder(const LightCache& c, Vec3 eye) {
  std::vector<std::pair<float, int>> d;
  for (int i = 0; i < c.probes(); ++i) {
    const Vec3 v = c.probePos(i) - eye;
    d.push_back({dot(v, v), i});
  }
  std::sort(d.begin(), d.end());
  std::vector<int> out;
  for (auto& [dist, i] : d) out.push_back(i);
  return out;
}

}  // namespace loop
