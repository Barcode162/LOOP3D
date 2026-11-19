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
  float wide = 0;     // a panel: how near it shows its size (six of its diagonals, as the renderer
                      // lights it): nearer, it is worked out alone, never merged (render.lampShare)
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
                                  // (with a `share` under 1: the most each could give anywhere in the cell, first)
  std::vector<uint32_t> sure;     // per cell, how many of its first entries every pixel works out alone; the
                                  // rest each pixel merges into one lamp of its own (render.lampShare)
  float averageSure = 0;          // per cell
  float averageEntries = 0;       // per cell
  int mostEntries = 0;
  int cells() const { return nx * ny * nz; }
  int count(int c) const { return static_cast<int>(start[static_cast<size_t>(c) + 1] - start[static_cast<size_t>(c)]); }
  // a pixel's lamp work in cell c: its sure entries, and (with any left) the
  // merged lamp: each of the rest added up (its light there and its highlight's
  // shape, about half a lamp's work: no shadow, no Fresnel), and that worked out
  float work(int c) const {
    const int s = static_cast<int>(sure[static_cast<size_t>(c)]);
    return static_cast<float>(s) + (s < count(c) ? 1.0f + kWeighCost * static_cast<float>(count(c) - s) : 0.0f);
  }
  static constexpr float kWeighCost = 0.5f;
  int cellOf(Vec3 p) const;       // the nearest cell (a point outside takes the edge's)
  Vec3 cellCenter(int c) const;
  Vec3 cellLow(int c) const;
};

// A group may stand in for its members from a distance of `farness` times its
// radius (from the nearest point of a cell): the larger, the closer to working
// every lamp out alone. With `fewest` lamps or fewer, no grid.
// `tolerance` (render.lampError) lets a nearer group stand in too, as
// lightcuts do: when the most its light could be wrong anywhere in the cell
// (lit from its nearest member's place rather than its middle) is under that
// share of all the light reaching the cell. A string of dim bulbs beside a
// bright pendant is then one lamp; 0 keeps distance as the only rule.
// `share` (render.lampShare): a cell's entries are ordered by the most light
// each could give anywhere in it, and the first ones holding that share of it
// all are `sure`, worked out alone; each pixel merges the rest into one lamp
// made for its own point: the soft light they bring it, added up lamp by lamp
// (exact), and each one's highlight shape (only the shadows of dim lamps, and
// the slowly changing parts of their shine, are approximate). It is worked out at
// the pixel, so no cell's edge shows (unlike a group standing in nearer), and
// nothing is random, so nothing flickers. 1: every entry is worked out alone.
LightGrid buildLightGrid(const std::vector<GridLamp>& lamps, float farness = 4, int fewest = 8, int maxCells = 4096, float tolerance = 0,
                         float share = 1);

}  // namespace loop
