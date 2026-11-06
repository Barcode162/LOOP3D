#include "engine/render/light_grid.h"

#include <algorithm>
#include <cmath>

namespace loop {

std::vector<GridLamp> gridLamps(const Scene& scene, int maxLamps) {
  std::vector<GridLamp> out;
  for (const Light& l : scene.lights) {
    if (l.kind == Light::Kind::kSun) continue;
    if (static_cast<int>(out.size()) >= maxLamps) break;
    GridLamp g;
    g.position = l.position;
    // the brightest direction's intensity, as the live renderer lights it
    if (l.kind == Light::Kind::kSpot) {
      float ci = std::cos(l.innerAngle), co = std::cos(std::max(l.outerAngle, l.innerAngle + 1e-3f));
      g.candela = l.lumens / (kTwoPi * (1 - 0.5f * (ci + co)));
    } else if (l.kind == Light::Kind::kRect) {
      g.candela = l.lumens / kPi;
    } else {
      g.candela = l.lumens / (4 * kPi);
    }
    out.push_back(g);
  }
  return out;
}

int LightGrid::cellOf(Vec3 p) const {
  if (!on) return -1;
  Vec3 q = (p - origin) / cell;
  int ix = static_cast<int>(std::floor(q.x)), iy = static_cast<int>(std::floor(q.y)), iz = static_cast<int>(std::floor(q.z));
  if (ix < 0 || iy < 0 || iz < 0 || ix >= nx || iy >= ny || iz >= nz) return -1;
  return (iz * ny + iy) * nx + ix;
}

Vec3 LightGrid::cellCenter(int c) const {
  int ix = c % nx, iy = (c / nx) % ny, iz = c / (nx * ny);
  return origin + Vec3(static_cast<float>(ix) + 0.5f, static_cast<float>(iy) + 0.5f, static_cast<float>(iz) + 0.5f) * cell;
}

namespace {

float reachFor(float candela, float cutoffLux) { return std::clamp(std::sqrt(std::max(candela, 0.0f) / cutoffLux), 0.3f, 300.0f); }

// The box and cells for a cutoff, and how many lamps each cell lists.
void layOut(const std::vector<GridLamp>& lamps, float cutoffLux, int maxCells, LightGrid& g, std::vector<int>& counts) {
  const int n = static_cast<int>(lamps.size());
  g.cutoffLux = cutoffLux;
  g.reach.resize(static_cast<size_t>(n));
  AABB box;
  for (int k = 0; k < n; ++k) {
    float r = g.reach[static_cast<size_t>(k)] = reachFor(lamps[static_cast<size_t>(k)].candela, cutoffLux);
    box.grow(AABB(lamps[static_cast<size_t>(k)].position - Vec3(r), lamps[static_cast<size_t>(k)].position + Vec3(r)));
  }
  Vec3 e = vmax(box.extent(), Vec3(0.5f));
  float cell = std::max(0.5f, std::cbrt(e.x * e.y * e.z / static_cast<float>(maxCells)));
  for (;;) {
    g.nx = std::max(1, static_cast<int>(std::ceil(e.x / cell)));
    g.ny = std::max(1, static_cast<int>(std::ceil(e.y / cell)));
    g.nz = std::max(1, static_cast<int>(std::ceil(e.z / cell)));
    if (g.cells() <= maxCells) break;
    cell *= 1.05f;
  }
  g.cell = cell, g.origin = box.lo;
  counts.assign(static_cast<size_t>(g.cells()), 0);
  for (int k = 0; k < n; ++k) {
    const Vec3 p = lamps[static_cast<size_t>(k)].position;
    const float r = g.reach[static_cast<size_t>(k)];
    Vec3 lo = (p - Vec3(r) - g.origin) / cell, hi = (p + Vec3(r) - g.origin) / cell;
    int x0 = std::max(0, static_cast<int>(std::floor(lo.x))), x1 = std::min(g.nx - 1, static_cast<int>(std::floor(hi.x)));
    int y0 = std::max(0, static_cast<int>(std::floor(lo.y))), y1 = std::min(g.ny - 1, static_cast<int>(std::floor(hi.y)));
    int z0 = std::max(0, static_cast<int>(std::floor(lo.z))), z1 = std::min(g.nz - 1, static_cast<int>(std::floor(hi.z)));
    for (int z = z0; z <= z1; ++z)
      for (int y = y0; y <= y1; ++y)
        for (int x = x0; x <= x1; ++x) {
          // does the lamp's reach touch this cell? (the cell's nearest point)
          Vec3 c0 = g.origin + Vec3(static_cast<float>(x), static_cast<float>(y), static_cast<float>(z)) * cell;
          Vec3 q = vmin(vmax(p, c0), c0 + Vec3(cell));
          if (dot(q - p, q - p) <= r * r) ++counts[static_cast<size_t>((z * g.ny + y) * g.nx + x)];
        }
  }
}

// How many lamps a lit point typically has listed: each cell's count weighted
// by the count itself, so the crowded cells where the lamps (and what they
// light) are count most, not the sparse margin around them.
float averageOf(const std::vector<int>& counts) {
  double sum = 0, sum2 = 0;
  for (int c : counts) sum += c, sum2 += static_cast<double>(c) * c;
  return sum > 0 ? static_cast<float>(sum2 / sum) : 0;
}

}  // namespace

LightGrid buildLightGrid(const std::vector<GridLamp>& lamps, int perCell, int maxCells) {
  LightGrid g;
  const int n = static_cast<int>(lamps.size());
  if (n <= std::max(perCell, 1)) return g;
  std::vector<int> counts;
  // the dimmest cutoff (the longest reach) whose cells list perCell lamps or
  // fewer on average: lower cutoffs list more
  float lo = std::log(1e-4f), hi = std::log(1e4f);
  for (int i = 0; i < 28; ++i) {
    float mid = 0.5f * (lo + hi);
    layOut(lamps, std::exp(mid), maxCells, g, counts);
    if (averageOf(counts) > static_cast<float>(perCell)) lo = mid;
    else hi = mid;
  }
  layOut(lamps, std::exp(hi), maxCells, g, counts);
  g.on = true;
  // each cell's list, brightest at its middle first (the shader gives its few
  // shadow rays to the first lamps)
  g.start.assign(static_cast<size_t>(g.cells()) + 1, 0);
  std::vector<std::vector<std::pair<float, uint32_t>>> lists(static_cast<size_t>(g.cells()));
  for (int k = 0; k < n; ++k) {
    const Vec3 p = lamps[static_cast<size_t>(k)].position;
    const float r = g.reach[static_cast<size_t>(k)];
    Vec3 lo3 = (p - Vec3(r) - g.origin) / g.cell, hi3 = (p + Vec3(r) - g.origin) / g.cell;
    int x0 = std::max(0, static_cast<int>(std::floor(lo3.x))), x1 = std::min(g.nx - 1, static_cast<int>(std::floor(hi3.x)));
    int y0 = std::max(0, static_cast<int>(std::floor(lo3.y))), y1 = std::min(g.ny - 1, static_cast<int>(std::floor(hi3.y)));
    int z0 = std::max(0, static_cast<int>(std::floor(lo3.z))), z1 = std::min(g.nz - 1, static_cast<int>(std::floor(hi3.z)));
    for (int z = z0; z <= z1; ++z)
      for (int y = y0; y <= y1; ++y)
        for (int x = x0; x <= x1; ++x) {
          Vec3 c0 = g.origin + Vec3(static_cast<float>(x), static_cast<float>(y), static_cast<float>(z)) * g.cell;
          Vec3 q = vmin(vmax(p, c0), c0 + Vec3(g.cell));
          if (dot(q - p, q - p) > r * r) continue;
          int c = (z * g.ny + y) * g.nx + x;
          Vec3 m = g.cellCenter(c) - p;
          lists[static_cast<size_t>(c)].push_back({lamps[static_cast<size_t>(k)].candela / std::max(dot(m, m), 0.01f), static_cast<uint32_t>(k)});
        }
  }
  double sum = 0;
  int used = 0;
  for (int c = 0; c < g.cells(); ++c) {
    auto& l = lists[static_cast<size_t>(c)];
    std::sort(l.begin(), l.end(), [](auto& a, auto& b) { return a.first > b.first; });
    g.start[static_cast<size_t>(c)] = static_cast<uint32_t>(g.lamps.size());
    for (auto& e : l) g.lamps.push_back(e.second);
    if (!l.empty()) sum += static_cast<double>(l.size()), ++used;
    g.mostLamps = std::max(g.mostLamps, static_cast<int>(l.size()));
  }
  g.start[static_cast<size_t>(g.cells())] = static_cast<uint32_t>(g.lamps.size());
  g.averageLamps = used ? static_cast<float>(sum / used) : 0;
  return g;
}

}  // namespace loop
