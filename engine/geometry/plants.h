#pragma once
// Grown things: trees (branching tubes with leaf cards), rocks (noisy
// stones), grass tufts. Each comes back as separate meshes per material so
// the renderer can give bark, leaves and stone their own look.
#include <cstdint>

#include "engine/geometry/mesh.h"

namespace loop {

struct TreeShape {
  float height = 9, trunkRadius = 0.22f;
  int levels = 4;           // branching depth
  int branches = 5;         // children per branch
  float spread = 0.75f;     // radians off the parent
  float lengthFalloff = 0.62f, radiusFalloff = 0.55f;
  float gnarl = 0.25f;      // random bend
  float upward = 0.35f;     // how much branches reach for the sky
  float leafSize = 0.28f;
  int leavesPerTip = 14;
  bool conifer = false;     // a straight trunk with whorls, getting shorter up top
  uint32_t seed = 1;
};
struct TreeMeshes {
  Mesh bark, leaves;
};
TreeMeshes growTree(const TreeShape& s);

struct RockShape {
  float size = 1;
  float roughness = 0.35f;
  float flatten = 0.6f;  // squash in Y
  uint32_t seed = 1;
};
Mesh makeRock(const RockShape& s);

// Thin blades for a tuft of grass, `blades` of them about `height` tall.
Mesh makeGrassTuft(int blades, float height, uint32_t seed);

}  // namespace loop
