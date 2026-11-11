#pragma once
// The marks: two bytes for every vertex, measured once on the mesh (the cook
// keeps them), read later by surfaces (docs/SURFACES.md, section 6).
//   edge:   how much the vertex stands out (a rounded edge, a small bump).
//   hollow: how much it is tucked in (a crease, a fillet, the inside of a cup).
// 0 to 255 stand for 0 to 1. Both are lowered where the triangles are big, so
// a large flat face never gets any, and a knife edge wears nothing.
#include <cstdint>
#include <vector>

#include "engine/geometry/mesh.h"

namespace loop {

struct Marks {
  std::vector<uint8_t> edge, hollow;  // one each per vertex of the mesh
};

Marks measureMarks(const Mesh& m);

// The vertices where the marks change, for the simplifier (simplify.h's
// `locked`): 0 for a vertex whose neighbours (across a triangle's edge) all
// have marks within `step` (0 to 255) of its own, else a number of 2 or more
// for its marks' kind (edge and hollow in eighths), so it may only slide onto a
// vertex of the same kind. A simpler mesh then still holds each worn or dirty
// band to its width: dropping a flat point beside a rounding would stretch the
// rounding's wear across the whole face (the cluster cook uses it).
std::vector<uint8_t> markBorders(const Mesh& m, const Marks& marks, int step = 16);

}  // namespace loop
