#pragma once
// Clusters: a mesh cut into small pieces (up to 128 triangles) the graphics
// chip can skip or draw one by one, each with simpler versions arranged in a
// tree. This is the idea behind Nanite (Karis et al., 2021):
//
//   1. Cut the mesh into clusters of neighbouring triangles.
//      (Separate small pieces that can't be simplified, like grass blades and
//      leaf cards, are thinned instead: half kept, each bigger.)
//   2. Group about four clusters that touch, simplify the group to half its
//      triangles with its outer edges locked (simplify.h), and cut the result
//      into new clusters: the next level. Repeat until nothing simplifies.
//   3. Every cluster made from a group gets the group's error (how far its
//      surface may be from the full mesh) and the group's bounds; the clusters
//      it replaces remember the same pair as their "parent".
//   4. To draw, take each cluster whose own error looks smaller than a pixel
//      while its parent's looks bigger. Because a group's old and new clusters
//      share exactly one (error, bounds) pair, every choice is all-or-nothing
//      per group, and because outer edges were locked, neighbours at different
//      levels meet without cracks.
//
// The pieces near you come in full detail and far ones in few triangles, for
// any mesh, with no hand-made simpler versions. The cook (cook.h) builds this
// once per mesh; step 4 of the rebuild draws it on the GPU.
#include <cstdint>
#include <vector>

#include "engine/core/math.h"
#include "engine/geometry/mesh.h"

namespace loop {

struct Cluster {
  uint32_t first = 0, count = 0;  // its triangles: ClusterTree::indices[first .. first + 3 * count)
  Vec4 bounds;                    // a sphere round its own triangles (centre, radius): for skipping it
  Vec4 lodSphere;                 // the sphere its error is judged from (its group's)
  float error = 0;                // how far its surface may be from the full mesh (mesh units)
  Vec4 parentSphere;              // its replacement's, when it has one
  float parentError = kInf;       // kInf: nothing replaces it (the coarsest of its part)
  int level = 0;                  // 0: full detail
};

struct ClusterTree {
  std::vector<Cluster> clusters;
  std::vector<uint32_t> indices;  // over the mesh's vertices, then the extra ones below
  // Separate small pieces (grass blades, leaf cards, flowers) get simpler by
  // keeping half of them, each kept one made bigger to cover the same area:
  // their bigger corners are extra vertices, numbered after the mesh's own,
  // each copying its normal and picture place from an original corner.
  std::vector<Vec3> extraPositions;
  std::vector<uint32_t> extraSource;  // the mesh's vertex each extra one copies
  int levels = 1;
  size_t triangles(int level) const;  // how many triangles that level holds
};

// Cuts triangles (over `positions`) into clusters of neighbouring triangles,
// at most `maxTriangles` each: each cluster's indices.
std::vector<std::vector<uint32_t>> cutIntoClusters(const std::vector<Vec3>& positions, const std::vector<uint32_t>& indices, int maxTriangles = 128);

// The whole tree for a mesh. `parallel`: its groups shared over every core
// (the same tree; not from inside another job of the shared job system).
// `keep` (empty: none): simplify's `locked` for the mesh's vertices: for a mesh
// drawn in a surface, where its marks change (markBorders).
ClusterTree buildClusterTree(const Mesh& mesh, int maxTriangles = 128, bool parallel = false, const std::vector<uint8_t>& keep = {});

// How big an error of `error` looks, in pixels, from `eye` for something
// inside `sphere`: `perPixel` is the screen's pixels per metre at 1 m
// (its height in pixels / (2 tan(field of view / 2))). Nearer looks bigger;
// from inside the sphere, anything is too big.
float projectedError(float error, Vec4 sphere, Vec3 eye, float perPixel);

// The clusters to draw from `eye` (in the mesh's space) so no error looks
// bigger than `pixels`: each cluster whose own error looks small enough while
// its parent's doesn't.
std::vector<int> selectClusters(const ClusterTree& tree, Vec3 eye, float perPixel, float pixels = 1);

}  // namespace loop
