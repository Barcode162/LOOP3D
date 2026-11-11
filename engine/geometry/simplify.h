#pragma once
// Making a mesh simpler without moving a single vertex: edges collapse onto
// one of their ends (half-edge collapses), cheapest first by how far the
// surface would move (Garland & Heckbert's quadric error metric). Every vertex
// kept is an original one, so colours, pictures and normals stay exactly as
// they were. Vertices on the mesh's open edges, on seams (one place with
// several normals or picture positions) and any the caller locks never go, so
// a simplified piece still meets its neighbours exactly: this is what lets the
// cluster tree (cluster.h) put simpler and fuller pieces side by side with no
// cracks.
#include <cstdint>
#include <vector>

#include "engine/core/math.h"

namespace loop {

struct SimplifyResult {
  std::vector<uint32_t> indices;  // the simpler triangles, over the same vertices
  float error = 0;                // the farthest the surface moved, in the mesh's units (metres)
};

// `indices`: triangles over `positions`. `locked[v]` (empty: none): 0 free, 1
// v never goes, 2 and more: v may go only onto a vertex of the same number (it
// slides along a band of its own kind, never off it). A list shorter than
// `positions` leaves the rest free. Collapses the cheapest edges until `targetTriangles` remain, or no
// collapse is left that keeps every triangle facing the way it did and the
// surface within `maxError`.
SimplifyResult simplify(const std::vector<Vec3>& positions, const std::vector<uint32_t>& indices, size_t targetTriangles,
                        const std::vector<uint8_t>& locked = {}, float maxError = kInf);

// For each vertex, the first vertex at exactly its place (vertices split at a
// seam share a place): the mesh's shape without its seams.
std::vector<uint32_t> weldMap(const std::vector<Vec3>& positions);

}  // namespace loop
