#include "engine/geometry/marks.h"

#include <algorithm>
#include <cmath>
#include <unordered_map>

#include "engine/geometry/bvh.h"

namespace loop {
namespace {

// The numbers fixed by docs/SURFACES.md section 6.
constexpr float kBendScale = 0.01f;    // 1 cm: a bend of 1/cm or tighter is a full mark
constexpr float kRayLength = 0.10f;    // 10 cm
constexpr float kRayLift = 0.001f;     // rays start 1 mm off the surface
constexpr float kBigTriangle = 0.02f;  // 2 cm: bigger triangles lower the marks
constexpr int kRays = 16;
// The flattest of the 16 rays leaves the surface at about 10 degrees (see
// rayDirections): it rises this much for every metre sideways, rounded down.
constexpr float kLowestRise = 0.17f;
constexpr float kWeld = 1e-5f;  // vertices this close are one place

struct Tri {
  uint32_t v[3];  // welded places
  Vec3 n;         // unit, from the winding
  Vec3 centre;
  float height;  // the smallest of its three
};

uint8_t toByte(float v) { return static_cast<uint8_t>(std::lround(saturate(v) * 255.0f)); }

// 16 directions over the hemisphere round `n`, more of them near `n` (the same every time).
void rayDirections(Vec3 n, Vec3 out[kRays]) {
  Vec3 a = std::fabs(n.x) < 0.7f ? Vec3{1, 0, 0} : Vec3{0, 1, 0};
  Vec3 t = normalize(cross(n, a)), b = cross(n, t);
  for (int i = 0; i < kRays; ++i) {
    float r = std::sqrt((i + 0.5f) / kRays), z = std::sqrt(1.0f - (i + 0.5f) / kRays);
    float phi = i * 2.39996323f;  // the golden angle
    out[i] = t * (r * std::cos(phi)) + b * (r * std::sin(phi)) + n * z;
  }
}

}  // namespace

Marks measureMarks(const Mesh& m) {
  const size_t n = m.positions.size();
  Marks out;
  out.edge.assign(n, 0);
  out.hollow.assign(n, 0);
  if (n == 0 || m.indices.size() < 3) return out;

  // 1. weld by position: every place gets one answer
  struct Key {
    int64_t x, y, z;
    bool operator==(const Key&) const = default;
  };
  struct KeyHash {
    size_t operator()(const Key& k) const { return static_cast<size_t>(k.x * 73856093 ^ k.y * 19349663 ^ k.z * 83492791); }
  };
  std::unordered_map<Key, uint32_t, KeyHash> seen;
  seen.reserve(n);
  std::vector<uint32_t> place(n);
  std::vector<Vec3> wpos;
  for (size_t i = 0; i < n; ++i) {
    const Vec3 p = m.positions[i];
    Key k{std::llround(p.x / kWeld), std::llround(p.y / kWeld), std::llround(p.z / kWeld)};
    auto it = seen.emplace(k, static_cast<uint32_t>(wpos.size()));
    if (it.second) wpos.push_back(p);
    place[i] = it.first->second;
  }
  const size_t W = wpos.size();

  // 2. the triangles that have an area, and at each place the biggest of their heights (`a` of the card)
  std::vector<Tri> tris;
  std::vector<Triangle> bvhTris;
  std::vector<Vec3> sumNormal(W, Vec3{0, 0, 0});
  std::vector<float> biggest(W, 0.0f);
  for (size_t t = 0; t + 2 < m.indices.size(); t += 3) {
    uint32_t i0 = m.indices[t], i1 = m.indices[t + 1], i2 = m.indices[t + 2];
    if (i0 >= n || i1 >= n || i2 >= n) continue;
    Vec3 p0 = m.positions[i0], p1 = m.positions[i1], p2 = m.positions[i2];
    Vec3 c = cross(p1 - p0, p2 - p0);
    float len = length(c);
    if (len < 1e-12f) continue;
    Tri tr;
    tr.v[0] = place[i0], tr.v[1] = place[i1], tr.v[2] = place[i2];
    if (tr.v[0] == tr.v[1] || tr.v[1] == tr.v[2] || tr.v[0] == tr.v[2]) continue;
    tr.n = c / len;
    tr.centre = (p0 + p1 + p2) / 3.0f;
    float longest = std::sqrt(std::max({lengthSq(p1 - p0), lengthSq(p2 - p1), lengthSq(p0 - p2)}));
    tr.height = len / longest;  // twice the area over the longest side
    for (int k = 0; k < 3; ++k) {
      biggest[tr.v[k]] = std::max(biggest[tr.v[k]], tr.height);
      sumNormal[tr.v[k]] += c;  // area-weighted
    }
    tris.push_back(tr);
    bvhTris.push_back({p0, p1 - p0, p2 - p0});
  }
  if (tris.empty()) return out;

  // 3. the sharpest bend through each place, out (edge) and in (hollow), in 1/metres.
  // Across each edge of two triangles: the angle between their faces over the
  // distance between their middles, which is 1/radius on a round shape.
  struct Side {
    uint64_t key;
    uint32_t tri;
  };
  std::vector<Side> sides;
  sides.reserve(tris.size() * 3);
  for (uint32_t t = 0; t < tris.size(); ++t)
    for (int e = 0; e < 3; ++e) {
      uint32_t a = tris[t].v[e], b = tris[t].v[(e + 1) % 3];
      sides.push_back({(static_cast<uint64_t>(std::min(a, b)) << 32) | std::max(a, b), t});
    }
  std::sort(sides.begin(), sides.end(), [](const Side& x, const Side& y) { return x.key != y.key ? x.key < y.key : x.tri < y.tri; });
  std::vector<float> outK(W, 0.0f), inK(W, 0.0f);
  for (size_t i = 0; i + 1 < sides.size(); ++i) {
    if (sides[i].key != sides[i + 1].key) continue;
    if (i + 2 < sides.size() && sides[i + 2].key == sides[i].key) {  // three or more: not a plain surface, skip them all
      while (i + 2 < sides.size() && sides[i + 2].key == sides[i].key) ++i;
      continue;
    }
    const Tri &A = tris[sides[i].tri], &B = tris[sides[i + 1].tri];
    uint32_t a = static_cast<uint32_t>(sides[i].key >> 32), b = static_cast<uint32_t>(sides[i].key & 0xffffffffu);
    // the way across the edge: the gap between their middles, seen from along the edge
    Vec3 gap = B.centre - A.centre;
    Vec3 along = wpos[b] - wpos[a];
    float alongLen = length(along);
    if (alongLen > 1e-12f) gap = gap - (along / alongLen) * dot(gap, along / alongLen);
    float dist = length(gap);
    if (dist < 1e-9f) continue;
    float k = std::acos(clamp(dot(A.n, B.n), -1.0f, 1.0f)) / dist;
    bool convex = dot(A.n, B.centre - A.centre) < 0;
    float& ka = convex ? outK[a] : inK[a];
    float& kb = convex ? outK[b] : inK[b];
    ka = std::max(ka, k), kb = std::max(kb, k);
    ++i;  // the pair is used
  }
  // the sharpest wins, with its sign
  std::vector<float> edge(W, 0.0f), hollow(W, 0.0f);
  for (size_t v = 0; v < W; ++v) {
    if (outK[v] >= inK[v]) edge[v] = saturate(outK[v] * kBendScale);
    else hollow[v] = saturate(inK[v] * kBendScale);
  }

  // 4. hollow raised by the share of 16 short rays that meet the same mesh
  // One walk of the Bvh gathers the triangles a place's rays could meet: within
  // reach (the box round it, 10 cm and a hair) and not wholly below the plane
  // the rays start from (they all point up from it). Its 16 rays are tried
  // against those alone, so an open surface costs next to nothing.
  Bvh bvh;
  bvh.build(bvhTris, 16);
  const auto& nodes = bvh.nodes();
  const auto& order = bvh.order();
  std::vector<Triangle> near;
  std::vector<uint32_t> stack;
  const float reach = kRayLength + kRayLift;
  for (size_t v = 0; v < W; ++v) {
    if (hollow[v] >= 1.0f) continue;  // can't rise
    float len = length(sumNormal[v]);
    if (len < 1e-12f) continue;
    Vec3 nrm = sumNormal[v] / len;
    const Vec3 lo = wpos[v] - Vec3(reach), hi = wpos[v] + Vec3(reach);
    const Vec3 origin = wpos[v] + nrm * kRayLift;
    const float planeD = dot(nrm, origin);
    near.clear();
    stack.assign(1, 0);
    while (!stack.empty()) {
      const Bvh::Node& node = nodes[stack.back()];
      stack.pop_back();
      if (node.box.lo.x > hi.x || node.box.lo.y > hi.y || node.box.lo.z > hi.z || node.box.hi.x < lo.x || node.box.hi.y < lo.y || node.box.hi.z < lo.z) continue;
      // The box's highest corner above the start plane, and its nearest point
      // to the start: a ray rises at least kLowestRise of every metre it goes
      // sideways, so a box that stays lower than that can never be met.
      float top = std::max(nrm.x * node.box.lo.x, nrm.x * node.box.hi.x) + std::max(nrm.y * node.box.lo.y, nrm.y * node.box.hi.y) + std::max(nrm.z * node.box.lo.z, nrm.z * node.box.hi.z) - planeD;
      if (top < 0) continue;
      Vec3 q = vmin(vmax(origin, node.box.lo), node.box.hi) - origin;
      float d2 = lengthSq(q);
      if (d2 > reach * reach) continue;
      if (top < kLowestRise * std::sqrt(std::max(0.0f, d2 - top * top))) continue;
      if (node.count) {
        for (uint32_t i = 0; i < node.count; ++i) {
          const Triangle& tr = bvhTris[order[node.left + i]];
          float h0 = dot(nrm, tr.v0), h1 = dot(nrm, tr.v0 + tr.e1), h2 = dot(nrm, tr.v0 + tr.e2);
          if (std::max({h0, h1, h2}) >= planeD) near.push_back(tr);
        }
      } else {
        stack.push_back(node.left), stack.push_back(node.left + 1);
      }
    }
    Vec3 dirs[kRays];
    rayDirections(nrm, dirs);
    int hits = 0;
    for (int i = 0; i < kRays; ++i) {
      Ray ray{wpos[v] + nrm * kRayLift, dirs[i]};
      for (const Triangle& tr : near) {
        float t, u, w;
        if (intersectTriangle(tr, ray, 1e-5f, kRayLength, t, u, w)) {
          ++hits;
          break;
        }
      }
    }
    hollow[v] = std::max(hollow[v], static_cast<float>(hits) / kRays);
  }
  // 5. lowered by how big the triangles round the place are; the bytes, one answer for every copy of a place
  for (size_t i = 0; i < n; ++i) {
    uint32_t w = place[i];
    float a = biggest[w], f = a > kBigTriangle ? kBigTriangle / a : 1.0f;
    out.edge[i] = toByte(edge[w] * f);
    out.hollow[i] = toByte(hollow[w] * f);
  }
  return out;
}

}  // namespace loop
