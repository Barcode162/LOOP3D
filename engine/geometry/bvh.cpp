#include "engine/geometry/bvh.h"

#include <numeric>

namespace loop {

void Bvh::build(std::vector<Triangle> tris, int maxLeaf) {
  nodes_.clear();
  order_.resize(tris.size());
  std::iota(order_.begin(), order_.end(), 0u);
  tris_ = std::move(tris);
  if (tris_.empty()) return;
  std::vector<Vec3> centroids(tris_.size());
  std::vector<AABB> boxes(tris_.size());
  for (size_t i = 0; i < tris_.size(); ++i) {
    const Triangle& t = tris_[i];
    AABB b;
    b.grow(t.v0), b.grow(t.v0 + t.e1), b.grow(t.v0 + t.e2);
    boxes[i] = b;
    centroids[i] = b.center();
  }
  nodes_.reserve(tris_.size() * 2);
  nodes_.push_back({});
  nodes_[0].left = 0;
  nodes_[0].count = static_cast<uint32_t>(tris_.size());
  for (const AABB& b : boxes) nodes_[0].box.grow(b);
  subdivide(0, maxLeaf, centroids, boxes);
  // Reorder triangles to leaf order.
  std::vector<Triangle> sorted(tris_.size());
  for (size_t i = 0; i < order_.size(); ++i) sorted[i] = tris_[order_[i]];
  tris_.swap(sorted);
}

void Bvh::subdivide(uint32_t ni, int maxLeaf, std::vector<Vec3>& cen, std::vector<AABB>& boxes) {
  // Iterative with an explicit stack (deep scenes would overflow recursion).
  std::vector<uint32_t> stack = {ni};
  while (!stack.empty()) {
    uint32_t n = stack.back();
    stack.pop_back();
    Node node = nodes_[n];
    if (node.count <= static_cast<uint32_t>(maxLeaf)) continue;
    AABB cb;
    for (uint32_t i = 0; i < node.count; ++i) cb.grow(cen[order_[node.left + i]]);
    constexpr int kBins = 16;
    float bestCost = kInf;
    int bestAxis = -1, bestSplit = 0;
    for (int axis = 0; axis < 3; ++axis) {
      float lo = cb.lo[axis], hi = cb.hi[axis];
      if (hi - lo < 1e-9f) continue;
      AABB bins[kBins];
      uint32_t counts[kBins] = {};
      float scale = kBins / (hi - lo);
      for (uint32_t i = 0; i < node.count; ++i) {
        uint32_t p = order_[node.left + i];
        int b = std::min(kBins - 1, static_cast<int>((cen[p][axis] - lo) * scale));
        bins[b].grow(boxes[p]);
        counts[b]++;
      }
      float leftArea[kBins - 1], rightArea[kBins - 1];
      uint32_t leftCount[kBins - 1], rightCount[kBins - 1];
      AABB l, r;
      uint32_t lc = 0, rc = 0;
      for (int i = 0; i < kBins - 1; ++i) {
        l.grow(bins[i]), lc += counts[i];
        leftArea[i] = l.area(), leftCount[i] = lc;
        r.grow(bins[kBins - 1 - i]), rc += counts[kBins - 1 - i];
        rightArea[kBins - 2 - i] = r.area(), rightCount[kBins - 2 - i] = rc;
      }
      for (int i = 0; i < kBins - 1; ++i) {
        if (!leftCount[i] || !rightCount[i]) continue;
        float cost = leftArea[i] * leftCount[i] + rightArea[i] * rightCount[i];
        if (cost < bestCost) bestCost = cost, bestAxis = axis, bestSplit = i;
      }
    }
    float leafCost = node.box.area() * node.count;
    if (bestAxis < 0 || (bestCost >= leafCost && node.count <= 16u)) {
      if (bestAxis < 0 && node.count > 16u) {
        // all centroids equal: split the list in half
        bestAxis = 0;
      } else continue;
    }
    uint32_t mid;
    if (bestAxis >= 0 && cb.hi[bestAxis] - cb.lo[bestAxis] > 1e-9f) {
      float lo = cb.lo[bestAxis], scale = kBins / (cb.hi[bestAxis] - lo);
      auto first = order_.begin() + node.left, last = first + node.count;
      auto it = std::partition(first, last, [&](uint32_t p) {
        int b = std::min(kBins - 1, static_cast<int>((cen[p][bestAxis] - lo) * scale));
        return b <= bestSplit;
      });
      mid = static_cast<uint32_t>(it - order_.begin());
    } else mid = node.left + node.count / 2;
    if (mid == node.left || mid == node.left + node.count) mid = node.left + node.count / 2;
    uint32_t li = static_cast<uint32_t>(nodes_.size());
    Node left, right;
    left.left = node.left, left.count = mid - node.left;
    right.left = mid, right.count = node.count - left.count;
    for (uint32_t i = 0; i < left.count; ++i) left.box.grow(boxes[order_[left.left + i]]);
    for (uint32_t i = 0; i < right.count; ++i) right.box.grow(boxes[order_[right.left + i]]);
    nodes_.push_back(left);
    nodes_.push_back(right);
    nodes_[n].left = li;
    nodes_[n].count = 0;
    stack.push_back(li);
    stack.push_back(li + 1);
  }
}

Hit Bvh::closest(const Ray& r, float tmin, float tmax) const {
  Hit h;
  if (nodes_.empty()) return h;
  Vec3 inv{1 / (std::fabs(r.d.x) > 1e-20f ? r.d.x : 1e-20f), 1 / (std::fabs(r.d.y) > 1e-20f ? r.d.y : 1e-20f), 1 / (std::fabs(r.d.z) > 1e-20f ? r.d.z : 1e-20f)};
  h.t = tmax;
  uint32_t stack[128];
  int sp = 0;
  if (intersect(nodes_[0].box, r.o, inv, tmin, h.t) >= kInf) return Hit{};
  stack[sp++] = 0;
  while (sp) {
    const Node& n = nodes_[stack[--sp]];
    if (n.count) {
      for (uint32_t i = 0; i < n.count; ++i) {
        float t, u, v;
        if (intersectTriangle(tris_[n.left + i], r, tmin, h.t, t, u, v)) h.t = t, h.u = u, h.v = v, h.prim = order_[n.left + i];
      }
      continue;
    }
    float d0 = intersect(nodes_[n.left].box, r.o, inv, tmin, h.t);
    float d1 = intersect(nodes_[n.left + 1].box, r.o, inv, tmin, h.t);
    uint32_t a = n.left, b = n.left + 1;
    if (d1 < d0) std::swap(d0, d1), std::swap(a, b);
    if (d1 < kInf && sp < 128) stack[sp++] = b;
    if (d0 < kInf && sp < 128) stack[sp++] = a;
  }
  if (!h.valid()) h.t = kInf;
  return h;
}

bool Bvh::occluded(const Ray& r, float tmin, float tmax) const {
  if (nodes_.empty()) return false;
  Vec3 inv{1 / (std::fabs(r.d.x) > 1e-20f ? r.d.x : 1e-20f), 1 / (std::fabs(r.d.y) > 1e-20f ? r.d.y : 1e-20f), 1 / (std::fabs(r.d.z) > 1e-20f ? r.d.z : 1e-20f)};
  uint32_t stack[128];
  int sp = 0;
  stack[sp++] = 0;
  while (sp) {
    const Node& n = nodes_[stack[--sp]];
    if (intersect(n.box, r.o, inv, tmin, tmax) >= kInf) continue;
    if (n.count) {
      for (uint32_t i = 0; i < n.count; ++i) {
        float t, u, v;
        if (intersectTriangle(tris_[n.left + i], r, tmin, tmax, t, u, v)) return true;
      }
      continue;
    }
    if (sp < 127) stack[sp++] = n.left, stack[sp++] = n.left + 1;
  }
  return false;
}

int Bvh::depth() const {
  if (nodes_.empty()) return 0;
  std::vector<std::pair<uint32_t, int>> st = {{0, 1}};
  int best = 0;
  while (!st.empty()) {
    auto [i, d] = st.back();
    st.pop_back();
    best = std::max(best, d);
    if (!nodes_[i].count) st.push_back({nodes_[i].left, d + 1}), st.push_back({nodes_[i].left + 1, d + 1});
  }
  return best;
}

}  // namespace loop
