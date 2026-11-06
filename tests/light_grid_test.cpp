// The light grid (engine/render/light_grid): many lamps, few per pixel.
#include "engine/core/random.h"
#include "engine/render/light_grid.h"
#include "tests/test.h"

using namespace loop;

namespace {
// A room of small lamps: a grid of bulbs under a ceiling, like the bar's strings.
std::vector<GridLamp> bulbs(int nx, int nz, float lumens) {
  std::vector<GridLamp> out;
  for (int z = 0; z < nz; ++z)
    for (int x = 0; x < nx; ++x) out.push_back({Vec3(x * 1.5f, 2.5f, z * 1.5f), lumens / (4 * kPi)});
  return out;
}
}  // namespace

TEST(light_grid_few_lamps_need_none) {
  LightGrid g = buildLightGrid(bulbs(2, 3, 100), 8);
  CHECK(!g.on);  // 6 lamps: every pixel works out all of them
  CHECK(g.cellOf({0, 0, 0}) == -1);
}

TEST(light_grid_lists_every_lamp_that_reaches) {
  auto lamps = bulbs(12, 10, 120);  // 120 lamps
  LightGrid g = buildLightGrid(lamps, 8);
  CHECK(g.on);
  CHECK(g.averageLamps <= 8.01f && g.averageLamps > 3);
  CHECK(g.start.size() == static_cast<size_t>(g.cells()) + 1);
  // any point: each lamp whose reach covers it is in its cell's list
  Rng rng(7);
  int checked = 0;
  for (int i = 0; i < 400; ++i) {
    Vec3 p{rng.uniform() * 18, rng.uniform() * 2.5f, rng.uniform() * 15};
    int c = g.cellOf(p);
    CHECK(c >= 0);
    if (c < 0) continue;
    for (size_t k = 0; k < lamps.size(); ++k) {
      if (length(lamps[k].position - p) >= g.reach[k]) continue;
      bool listed = false;
      for (uint32_t j = g.start[static_cast<size_t>(c)]; j < g.start[static_cast<size_t>(c) + 1]; ++j) listed |= g.lamps[j] == k;
      CHECK(listed);
      ++checked;
    }
  }
  CHECK(checked > 100);
}

TEST(light_grid_split_adds_up) {
  // near (worked out exactly, faded by nearWindow) + far (the rest) is the whole
  for (float r : {1.0f, 4.0f, 20.0f})
    for (float d = 0; d < r * 1.2f; d += r * 0.05f) {
      float nearShare = nearWindow(d, r);
      CHECK(nearShare >= 0 && nearShare <= 1);
      if (d <= 0.75f * r) CHECK_NEAR(nearShare, 1, 1e-6);
      if (d >= r) CHECK_NEAR(nearShare, 0, 1e-6);
    }
}

TEST(light_grid_brightest_first) {
  auto lamps = bulbs(12, 10, 120);
  lamps[5].candela *= 50;  // one bright lamp among the bulbs
  LightGrid g = buildLightGrid(lamps, 8);
  int c = g.cellOf(lamps[5].position);
  CHECK(c >= 0 && g.start[static_cast<size_t>(c) + 1] > g.start[static_cast<size_t>(c)]);
  CHECK(g.lamps[g.start[static_cast<size_t>(c)]] == 5u);
}
