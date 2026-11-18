#include "engine/render/light_grid.h"

#include <algorithm>
#include <cmath>
#include <map>
#include <tuple>

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
      g.kind = 1, g.facing = normalize(l.direction);
    } else if (l.kind == Light::Kind::kRect) {
      g.candela = l.lumens / kPi;
      g.kind = 2, g.facing = normalize(l.direction);
      g.wide = 6 * length(l.size);
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
  int ix = std::clamp(static_cast<int>(std::floor(q.x)), 0, nx - 1);
  int iy = std::clamp(static_cast<int>(std::floor(q.y)), 0, ny - 1);
  int iz = std::clamp(static_cast<int>(std::floor(q.z)), 0, nz - 1);
  return (iz * ny + iy) * nx + ix;
}

Vec3 LightGrid::cellLow(int c) const {
  int ix = c % nx, iy = (c / nx) % ny, iz = c / (nx * ny);
  return origin + Vec3(static_cast<float>(ix), static_cast<float>(iy), static_cast<float>(iz)) * cell;
}

Vec3 LightGrid::cellCenter(int c) const { return cellLow(c) + Vec3(cell * 0.5f); }

namespace {

struct Builder {
  const std::vector<GridLamp>& lamps;
  LightGrid& g;
  // an entry for these lamps: the lamp itself, or a group split in two along
  // its longest side (at the middle lamp)
  int build(std::vector<uint32_t> set) {
    if (set.size() == 1) return static_cast<int>(set[0]);
    LightGrid::Group grp;
    double w = 0;
    Vec3 c(0), lo(kInf), hi(-kInf);
    for (uint32_t k : set) {
      const GridLamp& l = lamps[k];
      float wk = std::max(l.candela, 1e-6f);
      c = c + l.position * wk, w += wk;
      lo = vmin(lo, l.position), hi = vmax(hi, l.position);
    }
    grp.center = c / static_cast<float>(w);
    for (uint32_t k : set) grp.radius = std::max(grp.radius, length(lamps[k].position - grp.center));
    grp.members = set;
    Vec3 e = hi - lo;
    int axis = e.x >= e.y && e.x >= e.z ? 0 : e.y >= e.z ? 1 : 2;
    std::sort(set.begin(), set.end(), [&](uint32_t a, uint32_t b) {
      return std::make_tuple(lamps[a].position[axis], a) < std::make_tuple(lamps[b].position[axis], b);
    });
    const size_t half = set.size() / 2;
    const int index = static_cast<int>(g.groups.size());
    g.groups.push_back(grp);
    int a = build(std::vector<uint32_t>(set.begin(), set.begin() + static_cast<long>(half)));
    int b = build(std::vector<uint32_t>(set.begin() + static_cast<long>(half), set.end()));
    g.groups[static_cast<size_t>(index)].a = a, g.groups[static_cast<size_t>(index)].b = b;
    return g.lampCount + index;
  }
};

// How far a point is from a box (0 inside).
float distanceToBox(Vec3 p, Vec3 lo, Vec3 hi) {
  Vec3 q = vmin(vmax(p, lo), hi);
  return length(p - q);
}

}  // namespace

LightGrid buildLightGrid(const std::vector<GridLamp>& lamps, float farness, int fewest, int maxCells, float tolerance, float share) {
  LightGrid g;
  const int n = static_cast<int>(lamps.size());
  if (n <= std::max(fewest, 1)) return g;
  g.on = true;
  g.lampCount = n;
  // ---- the tree: lamps group only with their own kind (panels facing about
  // the same way; spots each alone)
  std::map<std::tuple<int, int, int, int>, std::vector<uint32_t>> kinds;
  for (int k = 0; k < n; ++k) {
    const GridLamp& l = lamps[static_cast<size_t>(k)];
    std::tuple<int, int, int, int> key{0, 0, 0, 0};
    if (l.kind == 1) key = {1, k, 0, 0};
    else if (l.kind == 2)
      key = {2, static_cast<int>(std::lround(l.facing.x * 8)), static_cast<int>(std::lround(l.facing.y * 8)), static_cast<int>(std::lround(l.facing.z * 8))};
    kinds[key].push_back(static_cast<uint32_t>(k));
  }
  Builder b{lamps, g};
  std::vector<int> roots;
  for (auto& [key, set] : kinds) roots.push_back(b.build(set));
  // ---- the grid over the lamps, a cell's width round them
  Vec3 lo(kInf), hi(-kInf);
  for (const GridLamp& l : lamps) lo = vmin(lo, l.position), hi = vmax(hi, l.position);
  Vec3 e = vmax(hi - lo, Vec3(0.5f));
  float cell = std::max(0.5f, std::cbrt(e.x * e.y * e.z / static_cast<float>(maxCells)));
  for (;;) {
    g.nx = static_cast<int>(std::ceil(e.x / cell)) + 2;
    g.ny = static_cast<int>(std::ceil(e.y / cell)) + 2;
    g.nz = static_cast<int>(std::ceil(e.z / cell)) + 2;
    if (g.cells() <= maxCells) break;
    cell *= 1.05f;
  }
  g.cell = cell;
  g.origin = lo - Vec3(cell);
  // each group's light (its members' added up)
  std::vector<float> groupCandela(g.groups.size(), 0.0f);
  for (size_t i = 0; i < g.groups.size(); ++i)
    for (uint32_t k : g.groups[i].members) groupCandela[i] += lamps[k].candela;
  // ---- each cell's cut through the tree
  g.start.assign(static_cast<size_t>(g.cells()) + 1, 0);
  g.sure.assign(static_cast<size_t>(g.cells()), 0);
  const bool picking = share < 1;
  std::vector<std::pair<float, uint32_t>> list;
  std::vector<uint32_t> wide;  // panels near enough to show their size
  double sum = 0, sureSum = 0;
  for (int c = 0; c < g.cells(); ++c) {
    const Vec3 clo = g.cellLow(c), chi = clo + Vec3(cell), mid = g.cellCenter(c);
    list.clear(), wide.clear();
    auto light = [&](Vec3 p, float candela) {
      Vec3 d = p - mid;
      return candela / std::max(dot(d, d), 0.25f);
    };
    // the most a lamp (or a group, `reach` its radius) could give anywhere in the cell
    auto most = [&](Vec3 p, float candela, float reach) {
      float d = std::max(distanceToBox(p, clo, chi) - reach, 0.0f);
      return candela / std::max(d * d, 0.25f);
    };
    // all the light reaching the cell, roughly (each root as one lamp at its middle): what an error is a share of
    float total = 0;
    if (tolerance > 0)
      for (int r : roots) {
        if (r < n) total += light(lamps[static_cast<size_t>(r)].position, lamps[static_cast<size_t>(r)].candela);
        else total += light(g.groups[static_cast<size_t>(r - n)].center, groupCandela[static_cast<size_t>(r - n)]);
      }
    std::vector<int> stack(roots.rbegin(), roots.rend());
    while (!stack.empty()) {
      int e2 = stack.back();
      stack.pop_back();
      if (e2 < n) {
        const GridLamp& l = lamps[static_cast<size_t>(e2)];
        if (picking && l.wide > 0 && distanceToBox(l.position, clo, chi) < l.wide) {
          wide.push_back(static_cast<uint32_t>(e2));
          continue;
        }
        list.push_back({picking ? most(l.position, l.candela, 0) : light(l.position, l.candela), static_cast<uint32_t>(e2)});
        continue;
      }
      const LightGrid::Group& grp = g.groups[static_cast<size_t>(e2 - n)];
      const float candela = groupCandela[static_cast<size_t>(e2 - n)];
      const float near = distanceToBox(grp.center, clo, chi);
      // the most its light can be wrong in the cell: between its members nearest and farthest (no nearer than 0.5 m, as `light`)
      const float worst = candela / std::max((near - grp.radius) * (near - grp.radius), 0.25f) -
                          candela / std::max((near + grp.radius) * (near + grp.radius), 0.25f);
      if (near >= farness * grp.radius || (tolerance > 0 && worst < tolerance * total)) {
        list.push_back({picking ? most(grp.center, candela, grp.radius) : light(grp.center, candela), static_cast<uint32_t>(e2)});
      } else {
        stack.push_back(grp.b), stack.push_back(grp.a);
      }
    }
    std::stable_sort(list.begin(), list.end(), [](auto& x, auto& y) { return x.first > y.first; });
    g.start[static_cast<size_t>(c)] = static_cast<uint32_t>(g.entries.size());
    for (uint32_t id : wide) g.entries.push_back(id);  // first: always worked out alone
    for (auto& [w, id] : list) g.entries.push_back(id);
    // the sure ones: the first holding `share` of the most light (each of the rest under them)
    size_t sure = list.size();
    if (picking && list.empty()) sure = 0;
    else if (picking) {
      double all = 0;
      for (auto& [w, id] : list) all += w;
      double left = all;
      sure = 0;
      while (sure < list.size() && left > (1.0 - share) * all) left -= list[sure++].first;
      if (sure + 1 == list.size()) sure = list.size();  // one left over: as cheap to work it out
    }
    sure += wide.size();
    g.sure[static_cast<size_t>(c)] = static_cast<uint32_t>(sure);
    sureSum += static_cast<double>(sure);
    sum += static_cast<double>(list.size() + wide.size());
    g.mostEntries = std::max(g.mostEntries, static_cast<int>(list.size() + wide.size()));
  }
  g.start[static_cast<size_t>(g.cells())] = static_cast<uint32_t>(g.entries.size());
  g.averageEntries = static_cast<float>(sum / g.cells());
  g.averageSure = static_cast<float>(sureSum / g.cells());
  return g;
}

}  // namespace loop
