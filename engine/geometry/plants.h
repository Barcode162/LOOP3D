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
  float detail = 1;         // < 1: fewer bark segments and sides, fewer leaf cards (trees far away)
  int barkLevels = 99;      // bark only up to this branching level (far away the twigs' bark is left out; their leaves stay)
  float leafShare = -1;     // share of the leaf cards kept, bigger so the crown stays as full (-1: from detail)
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

// A fern: `fronds` arching stems with paired leaflet cards (uv 0..1 per card,
// for a leaf-shaped cut), about `size` across. Stems and leaflets are one mesh.
Mesh makeFern(float size, int fronds, uint32_t seed);

// A wild flower: a thin stem, petal cards round the head, a round centre.
struct FlowerShape {
  float height = 0.3f, headSize = 0.035f;
  int petals = 8;
  float cup = 0.15f;  // 0 flat (daisy) .. 1 closed (bell)
  uint32_t seed = 1;
};
struct FlowerMeshes {
  Mesh stem, petals, centre;
};
FlowerMeshes makeFlower(const FlowerShape& s);

// A stone column: fluted shaft with a base and a capital; `broken` snaps it
// off at a slant with a ragged top (no capital).
struct ColumnShape {
  float height = 3, radius = 0.28f;
  int flutes = 16;
  float fluteDepth = 0.06f;  // of the radius
  bool broken = false;
  uint32_t seed = 1;
};
Mesh makeColumn(const ColumnShape& s);

// A fallen log: a slightly bent, knobbly tube along +X, `length` long. The
// rings at its ends are a separate mesh (cut wood).
struct LogMeshes {
  Mesh bark, ends;
};
LogMeshes makeLog(float length, float radius, uint32_t seed);

// One butterfly wing: a card from the body out along +X (uv 0..1), hinged at x = 0.
Mesh makeWing(float span);
// A small low-poly ball (a mote, a firefly): about 80 triangles.
Mesh makeMote(float radius);

}  // namespace loop
