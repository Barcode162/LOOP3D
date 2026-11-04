#include "engine/render/scene_bvh.h"

#include <numeric>

namespace loop {

void SceneBvh::build(const Scene& s) {
  blas_.assign(s.meshes.size(), {});
  inst_.clear();
  top_.clear();
  std::vector<bool> used(s.meshes.size(), false);
  for (const MeshInstance& in : s.instances)
    if (in.mesh >= 0 && in.mesh < static_cast<int>(s.meshes.size())) used[in.mesh] = true;
  for (size_t m = 0; m < s.meshes.size(); ++m) {
    if (!used[m]) continue;
    const Mesh& mesh = s.meshes[m];
    std::vector<Triangle> tris;
    tris.reserve(mesh.triangleCount());
    for (size_t i = 0; i + 2 < mesh.indices.size(); i += 3) {
      Vec3 a = mesh.positions[mesh.indices[i]], b = mesh.positions[mesh.indices[i + 1]], c = mesh.positions[mesh.indices[i + 2]];
      tris.push_back({a, b - a, c - a});
    }
    blas_[m].bvh.build(std::move(tris));
    blas_[m].mesh = &mesh;
  }
  for (const MeshInstance& in : s.instances) {
    if (in.mesh < 0 || in.mesh >= static_cast<int>(s.meshes.size()) || blas_[in.mesh].bvh.empty()) continue;
    Inst I;
    I.blas = in.mesh;
    I.material = s.materials.empty() ? -1 : clamp(in.material, 0, static_cast<int>(s.materials.size()) - 1);
    I.xf = in.transform;
    I.inv = in.transform.inverse();
    I.normalXf = in.transform.linear().inverse().transposed();
    I.box = blas_[in.mesh].bvh.bounds().transformed(in.transform);
    const Mat4 id;
    I.identity = true;
    for (int c = 0; c < 4; ++c)
      for (int r = 0; r < 4; ++r) I.identity = I.identity && in.transform.m[c][r] == id.m[c][r];
    inst_.push_back(I);
  }
  // top level: median splits on the longest axis (instances are few)
  order_.resize(inst_.size());
  std::iota(order_.begin(), order_.end(), 0u);
  if (inst_.empty()) return;
  top_.push_back({});
  top_[0].left = 0, top_[0].count = static_cast<uint32_t>(inst_.size());
  std::vector<uint32_t> stack = {0};
  while (!stack.empty()) {
    uint32_t n = stack.back();
    stack.pop_back();
    Node& node = top_[n];
    AABB b, cb;
    for (uint32_t i = 0; i < node.count; ++i) b.grow(inst_[order_[node.left + i]].box), cb.grow(inst_[order_[node.left + i]].box.center());
    node.box = b;
    if (node.count <= 2) continue;
    int axis = maxAxis(cb.extent());
    uint32_t mid = node.left + node.count / 2;
    std::nth_element(order_.begin() + node.left, order_.begin() + mid, order_.begin() + node.left + node.count,
                     [&](uint32_t a, uint32_t c) { return inst_[a].box.center()[axis] < inst_[c].box.center()[axis]; });
    Node l, r;
    l.left = node.left, l.count = mid - node.left;
    r.left = mid, r.count = node.left + node.count - mid;
    uint32_t li = static_cast<uint32_t>(top_.size());
    top_[n].left = li, top_[n].count = 0;
    top_.push_back(l);
    top_.push_back(r);
    stack.push_back(li);
    stack.push_back(li + 1);
  }
  // fill boxes of the children created last
  for (int i = static_cast<int>(top_.size()) - 1; i >= 0; --i) {
    Node& nd = top_[i];
    if (nd.count) {
      AABB b;
      for (uint32_t k = 0; k < nd.count; ++k) b.grow(inst_[order_[nd.left + k]].box);
      nd.box = b;
    } else {
      AABB b = top_[nd.left].box;
      b.grow(top_[nd.left + 1].box);
      nd.box = b;
    }
  }
}

template <bool kAny>
bool SceneBvh::traverse(const Ray& r, float tmin, float tmax, SceneHit* out) const {
  if (top_.empty()) return false;
  auto inv3 = [](Vec3 d) {
    return Vec3{1 / (std::fabs(d.x) > 1e-20f ? d.x : 1e-20f), 1 / (std::fabs(d.y) > 1e-20f ? d.y : 1e-20f), 1 / (std::fabs(d.z) > 1e-20f ? d.z : 1e-20f)};
  };
  Vec3 inv = inv3(r.d);
  float best = tmax;
  uint32_t stack[64];
  int sp = 0;
  stack[sp++] = 0;
  bool any = false;
  while (sp) {
    const Node& n = top_[stack[--sp]];
    if (intersect(n.box, r.o, inv, tmin, best) >= kInf) continue;
    if (n.count) {
      for (uint32_t i = 0; i < n.count; ++i) {
        int ii = static_cast<int>(order_[n.left + i]);
        const Inst& I = inst_[ii];
        if (intersect(I.box, r.o, inv, tmin, best) >= kInf) continue;
        // the ray in the mesh's own space (direction not renormalised, so t is shared)
        Ray lr = I.identity ? r : Ray{I.inv.point(r.o), I.inv.vector(r.d)};
        const Bvh& b = blas_[I.blas].bvh;
        if (kAny) {
          if (b.occluded(lr, tmin, best)) return true;
        } else {
          Hit h = b.closest(lr, tmin, best);
          if (h.valid() && h.t < best) {
            best = h.t;
            out->t = h.t, out->u = h.u, out->v = h.v, out->prim = h.prim, out->inst = ii;
            any = true;
          }
        }
      }
      continue;
    }
    if (sp < 62) stack[sp++] = n.left, stack[sp++] = n.left + 1;
  }
  return any;
}

SceneHit SceneBvh::closest(const Ray& r, float tmin, float tmax) const {
  SceneHit h;
  traverse<false>(r, tmin, tmax, &h);
  return h;
}

bool SceneBvh::occluded(const Ray& r, float tmin, float tmax) const { return traverse<true>(r, tmin, tmax, nullptr); }

TriInfo SceneBvh::triangle(int inst, uint32_t prim) const {
  const Inst& I = inst_[inst];
  const Mesh& m = *blas_[I.blas].mesh;
  TriInfo t;
  uint32_t idx[3] = {m.indices[prim * 3], m.indices[prim * 3 + 1], m.indices[prim * 3 + 2]};
  Vec3 p[3];
  for (int k = 0; k < 3; ++k) {
    p[k] = I.xf.point(m.positions[idx[k]]);
    Vec3 n = idx[k] < m.normals.size() ? m.normals[idx[k]] : Vec3{};
    t.n[k] = lengthSq(n) > 0.5f ? normalize(I.normalXf * n) : Vec3{};
    t.uv[k] = idx[k] < m.uvs.size() ? m.uvs[idx[k]] : Vec2{};
  }
  t.v0 = p[0], t.e1 = p[1] - p[0], t.e2 = p[2] - p[0];
  t.ng = normalize(cross(t.e1, t.e2));
  for (auto& n : t.n)
    if (lengthSq(n) < 0.5f) n = t.ng;
  t.material = I.material;
  return t;
}

uint32_t SceneBvh::triangleCount(int inst) const { return static_cast<uint32_t>(blas_[inst_[inst].blas].mesh->triangleCount()); }

uint64_t SceneBvh::totalTriangles() const {
  uint64_t s = 0;
  for (const Inst& I : inst_) s += blas_[I.blas].mesh->triangleCount();
  return s;
}

}  // namespace loop
