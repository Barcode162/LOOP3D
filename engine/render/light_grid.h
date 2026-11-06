#pragma once
// Many lamps, cheaply (the live renderer): each lamp is worked out exactly only
// within its reach; beyond it, its light reaches a pixel through a coarse grid
// of soft "far light" (the light of every lamp out of reach, summed each frame
// on the GPU). Each pixel then works out the few lamps listed in its grid cell
// instead of every lamp in the scene. The renderer (engine/gpu) and
// `loop budget` build the same grid from the same lamps, so they count alike.
//
// The split is seamless: a lamp's exact light fades out between 75% and 100% of
// its reach (nearWindow) exactly as its share of the far light fades in, so
// near + far always adds up to the lamp's whole light; the reach only decides
// how much is worked out exactly.
#include <algorithm>
#include <cstdint>
#include <vector>

#include "engine/core/math.h"
#include "engine/render/scene.h"

namespace loop {

// One lamp as the grid sees it (scene order, the sun left out).
struct GridLamp {
  Vec3 position;
  float candela = 0;  // brightest intensity (cd): what decides its reach
};
std::vector<GridLamp> gridLamps(const Scene& scene, int maxLamps);

// 1 near the lamp, 0 from its reach on: how much of its light is worked out
// exactly at a distance d (the rest comes from the far light).
inline float nearWindow(float d, float reach) {
  float t = std::clamp((d - 0.75f * reach) / (0.25f * reach), 0.0f, 1.0f);
  return 1 - t * t * (3 - 2 * t);
}

struct LightGrid {
  bool on = false;          // off: every lamp everywhere (few lamps, or render.lightGrid off)
  Vec3 origin;              // the grid's low corner
  float cell = 1;           // cell size (m)
  int nx = 0, ny = 0, nz = 0;
  float cutoffLux = 0;      // a lamp reaches as far as it gives this much light
  std::vector<float> reach;       // per lamp (m)
  std::vector<uint32_t> start;    // per cell, where its lamps start in `lamps` (cells + 1 entries)
  std::vector<uint32_t> lamps;    // lamp indices, each cell's brightest first (at its middle)
  float averageLamps = 0;         // per cell
  int mostLamps = 0;
  int cells() const { return nx * ny * nz; }
  int cellOf(Vec3 p) const;       // -1 outside
  Vec3 cellCenter(int c) const;
};

// The grid for these lamps: reach chosen so a cell lists about `perCell` lamps
// on average; at most `maxCells` cells. With `perCell` lamps or fewer in all,
// no grid (on = false): every pixel works out every lamp, exactly.
LightGrid buildLightGrid(const std::vector<GridLamp>& lamps, int perCell, int maxCells = 32768);

}  // namespace loop
