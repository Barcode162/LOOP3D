#pragma once
// Many lamps, cheaply (the picture pipeline's phase 5, docs/PIPELINE.md): a
// tree of lamps and a grid of cells over them. Lamps near each other (and of
// the same kind, facing the same way) are grouped, groups into bigger groups;
// a group seen from far enough away looks like one lamp at its middle with all
// their light, so it is worked out as one. Each cell of the grid lists what its
// pixels work out: every lamp near it on its own, and the groups far enough
// from all of it (a "cut" through the tree). The bar's 103 lamps become a few
// dozen entries per cell, and nothing is left out: every lamp's light reaches
// every pixel, alone or in its group, both its soft light and its shine.
//
// It knows nothing of the graphics chip: the renderer (engine/gpu) makes each
// group a lamp of its own (its members' light added up) and sends the cells'
// lists; this is tested on its own (tests/light_grid_test.cpp).
#include <cstdint>
#include <vector>

#include "engine/core/math.h"
#include "engine/render/scene.h"

namespace loop {

// One lamp as the grid sees it (scene order, the sun left out).
struct GridLamp {
  Vec3 position;
  float candela = 0;  // brightest intensity (cd): orders each cell's list
  int kind = 0;       // lamps group only with their own kind: points together,
  Vec3 facing;        // panels facing the same way (spots never group)
};
std::vector<GridLamp> gridLamps(const Scene& scene, int maxLamps);

struct LightGrid {
  bool on = false;          // off: every lamp everywhere (few lamps, or render.lightGrid off)
  // the tree's groups (after the lamps: entry lamps + g is group g)
  struct Group {
    Vec3 center;               // its members' middle, weighted by their light
    float radius = 0;          // the ball round `center` holding them all
    std::vector<uint32_t> members;  // lamp indices
    int a = -1, b = -1;        // its two halves (entries)
  };
  std::vector<Group> groups;
  int lampCount = 0;
  // the grid
  Vec3 origin;              // its low corner
  float cell = 1;           // cell size (m)
  int nx = 0, ny = 0, nz = 0;
  std::vector<uint32_t> start;    // per cell, where its entries start in `entries` (cells + 1)
  std::vector<uint32_t> entries;  // lamps (< lampCount) and groups (lampCount + g), brightest at its middle first
  float averageEntries = 0;       // per cell
  int mostEntries = 0;
  int cells() const { return nx * ny * nz; }
  int cellOf(Vec3 p) const;       // the nearest cell (a point outside takes the edge's)
  Vec3 cellCenter(int c) const;
  Vec3 cellLow(int c) const;
};

// A group may stand in for its members from a distance of `farness` times its
// radius (from the nearest point of a cell): the larger, the closer to working
// every lamp out alone. With `fewest` lamps or fewer, no grid.
LightGrid buildLightGrid(const std::vector<GridLamp>& lamps, float farness = 4, int fewest = 8, int maxCells = 4096);

}  // namespace loop
