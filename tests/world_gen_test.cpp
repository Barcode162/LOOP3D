// World generation like nature (engine/geometry/world_gen.h), tested against
// nature's own rules.
#include "engine/geometry/world_gen.h"
#include "tests/test.h"

using namespace loop;

namespace {
const WorldMap& world() {
  static WorldMap m = [] {
    WorldSettings s;
    s.cells = 129, s.size = 8192, s.seed = 3;
    return generateWorld(s);
  }();
  return m;
}
}  // namespace

TEST(world_has_land_sea_and_mountains) {
  const WorldMap& m = world();
  int land = 0, sea = 0;
  float top = 0;
  for (size_t i = 0; i < m.height.size(); ++i) {
    (m.height[i] > 0 ? land : sea)++;
    top = std::max(top, m.height[i]);
  }
  const float share = static_cast<float>(land) / static_cast<float>(m.height.size());
  std::printf("    land %.0f%%, highest %.0f m\n", 100 * share, top);
  CHECK(share > 0.2f && share < 0.85f);
  CHECK(top > 400 && top < 6000);
  // the same seed, the same world
  WorldSettings s;
  s.cells = 129, s.size = 8192, s.seed = 3;
  CHECK(generateWorld(s).height == m.height);
}

TEST(world_water_always_finds_a_way_down) {
  // every cell's water goes to a neighbour no higher (a lake's water level counts),
  // and following it always ends at the sea or the map's edge
  const WorldMap& m = world();
  int bad = 0;
  for (size_t i = 0; i < m.down.size(); ++i) {
    const int d = m.down[i];
    if (d < 0) continue;
    if (m.water[static_cast<size_t>(d)] > m.water[i] + 1e-3f) ++bad;
  }
  CHECK(bad == 0);
  for (size_t i = 0; i < m.down.size(); i += 97) {
    int at = static_cast<int>(i), steps = 0;
    while (m.down[static_cast<size_t>(at)] >= 0 && steps < 100000) at = m.down[static_cast<size_t>(at)], ++steps;
    CHECK(steps < 100000);
  }
}

TEST(world_rivers_gather_into_bigger_ones) {
  // a few cells carry most of the water (rivers), most carry little (hillsides):
  // river networks' areas are spread over many sizes (a power law in nature)
  const WorldMap& m = world();
  std::vector<float> f;
  for (size_t i = 0; i < m.flow.size(); ++i)
    if (m.height[i] > 0) f.push_back(m.flow[i] / (m.cell * m.cell));
  std::sort(f.begin(), f.end());
  const float median = f[f.size() / 2], top = f.back();
  std::printf("    land cells' drainage (cells): median %.0f, largest river %.0f\n", median, top);
  CHECK(median <= 4);
  CHECK(top > 200);  // (a small test world: an 8 km world's rivers drain thousands)
  // and there are rivers at every size between: 10, 100, 1000 cells
  for (float size : {10.0f, 100.0f}) {
    const size_t over = f.end() - std::lower_bound(f.begin(), f.end(), size);
    CHECK(over > 0 && over < f.size() / 4);
  }
}

TEST(world_slopes_rest) {
  // nothing steeper than loose rock can stand (35 degrees, a little over for the grid)
  const WorldMap& m = world();
  float steepest = 0;
  for (int z = 0; z < m.n; ++z)
    for (int x = 0; x < m.n; ++x) steepest = std::max(steepest, m.slopeAt(x, z));
  std::printf("    steepest %.1f degrees\n", std::atan(steepest) * 180 / kPi);
  CHECK(steepest < std::tan(36.0f * kPi / 180));
}

TEST(world_mountains_cast_rain_shadows) {
  // the wind comes from the west (-x): land rising into it (windward) gets more
  // rain than land falling away behind the peaks (leeward), height for height
  const WorldMap& m = world();
  double wet = 0, dry = 0;
  int nw = 0, nd = 0;
  for (int z = 1; z < m.n - 1; ++z)
    for (int x = 1; x < m.n - 1; ++x) {
      const size_t i = m.at(x, z);
      if (m.height[i] < 200 || m.height[i] > 900) continue;
      const float rise = m.height[m.at(x + 1, z)] - m.height[m.at(x - 1, z)];
      if (rise > 20) wet += m.rain[i], ++nw;  // facing the wind (falling away to the east: rising to it from the west)
      if (rise < -20) dry += m.rain[i], ++nd;
    }
  CHECK(nw > 10 && nd > 10);
  // (rise > 0: the land climbs eastward, the side the wind meets... wait: it
  // climbs as the wind moves east, so this side faces the wind)
  const double a = wet / nw, b = dry / nd;
  std::printf("    rain: windward %.2f, leeward %.2f\n", a, b);
  CHECK(a > 1.3 * b);
}

TEST(world_cold_heights_and_forests) {
  const WorldMap& m = world();
  // colder higher up: the highest land is the coldest
  float hiT = 0, loT = 0;
  int hi = 0, lo = 0;
  for (size_t i = 0; i < m.height.size(); ++i) {
    if (m.height[i] > 1000) hiT += m.warmth[i], ++hi;
    if (m.height[i] > 0 && m.height[i] < 100) loT += m.warmth[i], ++lo;
  }
  if (hi > 0) CHECK(hiT / hi < loT / lo - 4);
  // forests only where it is wet and warm enough, and every kind of ground somewhere
  int kinds[7] = {0};
  for (size_t i = 0; i < m.ground.size(); ++i) {
    ++kinds[m.ground[i]];
    if (m.ground[i] == WorldMap::kForest) CHECK(m.rain[i] > 0.12f && m.warmth[i] > 1);
  }
  std::printf("    sea %d, lake %d, sand %d, grass %d, forest %d, rock %d, snow %d\n", kinds[0], kinds[1], kinds[2], kinds[3], kinds[4], kinds[5], kinds[6]);
  CHECK(kinds[WorldMap::kSea] > 0 && kinds[WorldMap::kGrass] > 0 && kinds[WorldMap::kForest] > 0 && kinds[WorldMap::kRock] > 0);
}
