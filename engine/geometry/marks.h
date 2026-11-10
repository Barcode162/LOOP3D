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

}  // namespace loop
