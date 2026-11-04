#pragma once
// A bounding volume hierarchy over triangles, built with binned SAH and
// flattened for fast ray casts (closest hit and any hit). Used by the path
// tracer, picking, physics ray casts and light baking.
#include <cstdint>
#include <vector>

#include "engine/core/math.h"

namespace loop {

struct Triangle {
  Vec3 v0, e1, e2;  // v0 and edges (v1 - v0, v2 - v0)
};

struct Hit {
  float t = kInf;
  float u = 0, v = 0;   // barycentrics of v1 and v2
  uint32_t prim = ~0u;  // triangle index
  bool valid() const { return prim != ~0u; }
};

class Bvh {
 public:
  struct Node {
    AABB box;
    uint32_t left = 0;   // first child (inner) or first primitive (leaf)
    uint32_t count = 0;  // primitives in a leaf, 0 for inner
  };
  // Builds over the triangles (copied).
  void build(std::vector<Triangle> tris, int maxLeaf = 4);
  bool empty() const { return nodes_.empty(); }
  Hit closest(const Ray& r, float tmin = 1e-4f, float tmax = kInf) const;
  bool occluded(const Ray& r, float tmin, float tmax) const;
  const std::vector<Node>& nodes() const { return nodes_; }
  const std::vector<uint32_t>& order() const { return order_; }  // leaf slot -> original triangle
  size_t triangleCount() const { return tris_.size(); }
  AABB bounds() const { return nodes_.empty() ? AABB() : nodes_[0].box; }
  int depth() const;

 private:
  void subdivide(uint32_t node, int maxLeaf, std::vector<Vec3>& centroids, std::vector<AABB>& boxes);
  std::vector<Node> nodes_;
  std::vector<Triangle> tris_;   // in leaf order
  std::vector<uint32_t> order_;  // leaf order -> input index
};

// Möller–Trumbore; true with t, u, v on a hit in (tmin, tmax).
inline bool intersectTriangle(const Triangle& tr, const Ray& r, float tmin, float tmax, float& t, float& u, float& v) {
  Vec3 p = cross(r.d, tr.e2);
  float det = dot(tr.e1, p);
  if (std::fabs(det) < 1e-12f) return false;
  float inv = 1.0f / det;
  Vec3 s = r.o - tr.v0;
  u = dot(s, p) * inv;
  if (u < 0 || u > 1) return false;
  Vec3 q = cross(s, tr.e1);
  v = dot(r.d, q) * inv;
  if (v < 0 || u + v > 1) return false;
  t = dot(tr.e2, q) * inv;
  return t > tmin && t < tmax;
}

}  // namespace loop
