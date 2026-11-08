// The light grid (engine/render/light_grid): many lamps, few per pixel, none left out.
#include "engine/render/light_grid.h"
#include "tests/test.h"

using namespace loop;

namespace {
// A room of small lamps: a grid of bulbs under a ceiling, like the bar's strings.
std::vector<GridLamp> bulbs(int nx, int nz, float lumens) {
  std::vector<GridLamp> out;
  for (int z = 0; z < nz; ++z)
    for (int x = 0; x < nx; ++x) {
      GridLamp g;
      g.position = Vec3(x * 1.5f, 2.5f, z * 1.5f), g.candela = lumens / (4 * kPi);
      out.push_back(g);
    }
  return out;
}
// Every lamp a cell's entries stand for, alone or in a group.
std::vector<int> covered(const LightGrid& g, int c) {
  std::vector<int> times(static_cast<size_t>(g.lampCount), 0);
  for (uint32_t j = g.start[static_cast<size_t>(c)]; j < g.start[static_cast<size_t>(c) + 1]; ++j) {
    uint32_t e = g.entries[j];
    if (e < static_cast<uint32_t>(g.lampCount)) ++times[e];
    else
      for (uint32_t k : g.groups[e - static_cast<uint32_t>(g.lampCount)].members) ++times[k];
  }
  return times;
}
}  // namespace

TEST(light_grid_few_lamps_need_none) {
  LightGrid g = buildLightGrid(bulbs(2, 3, 100));
  CHECK(!g.on);  // 6 lamps: every pixel works out all of them
  CHECK(g.cellOf({0, 0, 0}) == -1);
}

TEST(light_grid_every_cell_has_every_lamp_once) {
  auto lamps = bulbs(12, 10, 120);  // 120 lamps
  LightGrid g = buildLightGrid(lamps);
  CHECK(g.on);
  CHECK(g.start.size() == static_cast<size_t>(g.cells()) + 1);
  for (int c = 0; c < g.cells(); ++c) {
    auto times = covered(g, c);
    bool once = true;
    for (int t : times) once &= t == 1;
    CHECK(once);
    if (!once) break;
  }
  // and far fewer entries than lamps
  CHECK(g.averageEntries < 60 && g.averageEntries > 1);  // of 120
}

TEST(light_grid_groups_only_from_far_enough) {
  auto lamps = bulbs(12, 10, 120);
  const float farness = 4;
  LightGrid g = buildLightGrid(lamps, farness);
  int groupsUsed = 0;
  for (int c = 0; c < g.cells(); ++c) {
    Vec3 lo = g.cellLow(c), hi = lo + Vec3(g.cell);
    for (uint32_t j = g.start[static_cast<size_t>(c)]; j < g.start[static_cast<size_t>(c) + 1]; ++j) {
      if (g.entries[j] < static_cast<uint32_t>(g.lampCount)) continue;
      const LightGrid::Group& grp = g.groups[g.entries[j] - static_cast<uint32_t>(g.lampCount)];
      Vec3 q = vmin(vmax(grp.center, lo), hi);
      CHECK(length(grp.center - q) >= farness * grp.radius - 1e-4f);
      ++groupsUsed;
    }
  }
  CHECK(groupsUsed > 100);
  // a point outside the grid takes the nearest cell
  CHECK(g.cellOf(Vec3(-50, 2, -50)) == 0);
}

TEST(light_grid_kinds_group_apart) {
  auto lamps = bulbs(6, 4, 120);
  for (int k = 0; k < 6; ++k) {  // six windows facing +x, among the bulbs
    GridLamp w;
    w.position = Vec3(k * 1.5f, 2.0f, 0.3f), w.candela = 50, w.kind = 2, w.facing = Vec3(1, 0, 0);
    lamps.push_back(w);
  }
  GridLamp spot;
  spot.position = Vec3(3, 3, 3), spot.candela = 400, spot.kind = 1, spot.facing = Vec3(0, -1, 0);
  lamps.push_back(spot);
  LightGrid g = buildLightGrid(lamps);
  for (const LightGrid::Group& grp : g.groups) {
    int kind = lamps[grp.members[0]].kind;
    for (uint32_t k : grp.members) CHECK(lamps[k].kind == kind);
    CHECK(kind != 1);  // spots stay alone
  }
}

TEST(light_grid_brightest_first) {
  auto lamps = bulbs(12, 10, 120);
  lamps[5].candela *= 50;  // one bright lamp among the bulbs
  LightGrid g = buildLightGrid(lamps);
  int c = g.cellOf(lamps[5].position);
  CHECK(c >= 0 && g.start[static_cast<size_t>(c) + 1] > g.start[static_cast<size_t>(c)]);
  CHECK(g.entries[g.start[static_cast<size_t>(c)]] == 5u);
}
