#pragma once
// Ray casts against a whole scene with instancing: every mesh gets its own
// BVH once (in its own space), and a top-level tree over the placed copies
// finds which copies a ray might hit. A forest of 5,000 trees costs the
// memory of the few tree shapes, not 5,000 copies.
#include <cstdint>
#include <vector>

#include "engine/geometry/bvh.h"
#include "engine/render/scene.h"

namespace loop {

struct SceneHit {
  float t = kInf, u = 0, v = 0;
  uint32_t prim = ~0u;
  int inst = -1;
  bool valid() const { return inst >= 0; }
};

// A hit triangle's data in world space.
struct TriInfo {
  Vec3 n[3];   // vertex normals
  Vec2 uv[3];
  Vec3 ng;     // geometric normal (unit)
  Vec3 v0, e1, e2;
  int material = -1;
};

class SceneBvh {
 public:
  void build(const Scene& s);
  SceneHit closest(const Ray& r, float tmin = 1e-4f, float tmax = kInf) const;
  bool occluded(const Ray& r, float tmin, float tmax) const;
  TriInfo triangle(int inst, uint32_t prim) const;
  size_t instanceCount() const { return inst_.size(); }
  uint32_t triangleCount(int inst) const;
  int materialOf(int inst) const { return inst_[inst].material; }
  uint64_t totalTriangles() const;  // as if every copy were real
  AABB bounds() const { return top_.empty() ? AABB() : top_[0].box; }

 private:
  struct Blas {
    Bvh bvh;
    const Mesh* mesh = nullptr;
  };
  struct Inst {
    int blas = -1, material = 0;
    Mat4 xf, inv;
    Mat3 normalXf;
    AABB box;
    bool identity = true;
  };
  struct Node {
    AABB box;
    uint32_t left = 0, count = 0;
  };
  std::vector<Blas> blas_;
  std::vector<Inst> inst_;
  std::vector<Node> top_;
  std::vector<uint32_t> order_;
  template <bool kAny>
  bool traverse(const Ray& r, float tmin, float tmax, SceneHit* out) const;
};

}  // namespace loop
