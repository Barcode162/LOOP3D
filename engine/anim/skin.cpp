#include "engine/anim/skin.h"

#include <algorithm>
#include <cmath>
#include <queue>
#include <unordered_map>

#include "engine/core/jobs.h"
#include "engine/geometry/bvh.h"

namespace loop::anim {
namespace {

// The nearest point of the piece a-b to p.
Vec3 nearestOn(Vec3 a, Vec3 b, Vec3 p) {
  const Vec3 ab = b - a;
  const float l2 = lengthSq(ab);
  const float t = l2 > 0 ? std::clamp(dot(p - a, ab) / l2, 0.0f, 1.0f) : 0;
  return a + ab * t;
}

struct Piece {
  int bone;
  Vec3 a, b;
};

}  // namespace

void weigh(Mesh& mesh, const Mat4& meshToWorld, const Skeleton& s, const std::vector<Transform>& boneWorld, const WeighOptions& o) {
  const size_t count = mesh.positions.size();
  mesh.pulls.assign(count, Pulls());
  std::vector<Piece> pieces;
  for (size_t i = 0; i < s.bones.size() && i < boneWorld.size() && i < 65535; ++i)
    if (s.bones[i].length > 0) pieces.push_back({static_cast<int>(i), boneWorld[i].position, boneWorld[i].apply({0, s.bones[i].length, 0})});
  if (pieces.empty() || count == 0) return;

  // ---- the surface as points joined by edges; points at the same place are one (a seam is not a gap)
  struct Key {
    int64_t x, y, z;
    bool operator==(const Key&) const = default;
  };
  struct KeyHash {
    size_t operator()(const Key& k) const { return static_cast<size_t>(k.x * 73856093 ^ k.y * 19349663 ^ k.z * 83492791); }
  };
  std::unordered_map<Key, uint32_t, KeyHash> seen;
  std::vector<uint32_t> nodeOf(count);
  std::vector<Vec3> at;
  for (size_t i = 0; i < count; ++i) {
    const Vec3 p = meshToWorld.point(mesh.positions[i]);
    const Key k{std::llround(p.x * 1e5), std::llround(p.y * 1e5), std::llround(p.z * 1e5)};
    auto it = seen.find(k);
    if (it == seen.end()) it = seen.emplace(k, static_cast<uint32_t>(at.size())).first, at.push_back(p);
    nodeOf[i] = it->second;
  }
  const size_t nodes = at.size();
  std::vector<std::vector<uint32_t>> next(nodes);
  std::vector<Triangle> tris;
  for (size_t i = 0; i + 2 < mesh.indices.size(); i += 3) {
    const uint32_t n[3] = {nodeOf[mesh.indices[i]], nodeOf[mesh.indices[i + 1]], nodeOf[mesh.indices[i + 2]]};
    for (int k = 0; k < 3; ++k) {
      const uint32_t u = n[k], v = n[(k + 1) % 3];
      if (u == v) continue;
      if (std::find(next[u].begin(), next[u].end(), v) == next[u].end()) next[u].push_back(v), next[v].push_back(u);
    }
    tris.push_back({at[n[0]], at[n[1]] - at[n[0]], at[n[2]] - at[n[0]]});
  }
  Bvh surface;
  surface.build(std::move(tris));

  // ---- each point's own bone, the nearest it can see; and every other bone it can see near enough to count
  const float soft = std::max(o.soft, 0.01f);
  const float reach = 7;  // a pull under e^-7 of the strongest is nothing
  std::vector<float> ownFar(nodes, 0);  // how far its own bone is: the limb's thickness there
  std::vector<std::vector<std::pair<int, float>>> sees(nodes);  // a place in `pieces`, and how far in a straight line
  JobSystem::shared().parallelFor(static_cast<int>(nodes), [&](int n) {
    std::vector<std::pair<float, int>> byFar(pieces.size());
    for (size_t k = 0; k < pieces.size(); ++k) byFar[k] = {length(at[n] - nearestOn(pieces[k].a, pieces[k].b, at[n])), static_cast<int>(k)};
    std::sort(byFar.begin(), byFar.end());
    float own = -1;
    for (size_t k = 0; k < byFar.size(); ++k) {
      const float far = byFar[k].first;
      if (own >= 0 && far - own > reach * soft * own) break;
      if (own < 0 && k >= 8) break;
      const Piece& pc = pieces[byFar[k].second];
      const Vec3 from = nearestOn(pc.a, pc.b, at[n]);
      // seen when nothing stands between the bone and the point (the point's own surface, at the far end, does not count)
      if (far > 1e-5f && surface.occluded({from, (at[n] - from) / far}, far * 0.001f, far * 0.999f - 1e-5f)) continue;
      if (own < 0) own = std::max(far, 1e-4f);
      sees[n].push_back({byFar[k].second, std::max(far, 1e-4f)});
    }
    // a point that sees no bone at all (inside a fold) takes the nearest
    if (own < 0) own = std::max(byFar[0].first, 1e-4f), sees[n].push_back({byFar[0].second, own});
    ownFar[n] = own;
  }, 256);

  // ---- each bone's pull spreads from the points that see it along the surface (Dijkstra), as far as it still counts
  std::vector<std::vector<std::pair<uint16_t, float>>> reached(nodes);  // bone and its distance
  std::vector<std::vector<std::pair<uint32_t, float>>> seeds(pieces.size());
  for (size_t n = 0; n < nodes; ++n)
    for (const auto& [k, far] : sees[n]) seeds[k].push_back({static_cast<uint32_t>(n), far});
  std::vector<std::vector<std::pair<uint32_t, float>>> found(pieces.size());
  JobSystem::shared().parallelFor(static_cast<int>(pieces.size()), [&](int k) {
    if (seeds[k].empty()) return;
    std::unordered_map<uint32_t, float> far;
    using Entry = std::pair<float, uint32_t>;
    std::priority_queue<Entry, std::vector<Entry>, std::greater<Entry>> open;
    for (const auto& [n, d] : seeds[k]) far[n] = d, open.push({d, n});
    while (!open.empty()) {
      const auto [d, n] = open.top();
      open.pop();
      if (d > far[n]) continue;
      found[k].push_back({n, d});
      for (uint32_t m : next[n]) {
        const float dm = d + length(at[m] - at[n]);
        if (dm - ownFar[m] > reach * soft * ownFar[m]) continue;
        auto it = far.find(m);
        if (it == far.end() || dm < it->second) far[m] = dm, open.push({dm, m});
      }
    }
  });
  for (size_t k = 0; k < pieces.size(); ++k)
    for (const auto& [n, d] : found[k]) reached[n].push_back({static_cast<uint16_t>(pieces[k].bone), d});

  // ---- distances become pulls: the nearest bone pulls most, the others fade with how much farther they are
  std::vector<Pulls> nodePulls(nodes);
  for (size_t n = 0; n < nodes; ++n) {
    auto& r = reached[n];
    float least = kInf;
    for (const auto& b : r) least = std::min(least, b.second);
    const float width = soft * ownFar[n];
    std::vector<std::pair<float, uint16_t>> w;
    for (const auto& b : r) w.push_back({std::exp(-(b.second - least) / width), b.first});
    std::sort(w.begin(), w.end(), [](const auto& a, const auto& b) { return a.first > b.first || (a.first == b.first && a.second < b.second); });
    if (w.size() > 8) w.resize(8);
    while (w.size() > 1 && w.back().first < 0.004f) w.pop_back();
    float sum = 0;
    for (const auto& x : w) sum += x.first;
    Pulls& p = nodePulls[n];
    for (size_t k = 0; k < w.size(); ++k) p.bone[k] = w[k].second, p.pull[k] = w[k].first / sum;
  }
  for (size_t i = 0; i < count; ++i) mesh.pulls[i] = nodePulls[nodeOf[i]];
}

std::vector<Mat4> bendMatrices(const Mat4& meshToWorld, const std::vector<Transform>& restWorld, const std::vector<Transform>& nowWorld) {
  const Mat4 back = meshToWorld.inverse();
  std::vector<Mat4> out(restWorld.size());
  for (size_t i = 0; i < restWorld.size() && i < nowWorld.size(); ++i) out[i] = back * nowWorld[i].matrix() * restWorld[i].matrix().inverse() * meshToWorld;
  return out;
}

void bend(const Mesh& rest, const std::vector<Mat4>& bones, Mesh& out) {
  out = rest;
  if (rest.pulls.size() != rest.positions.size() || bones.empty()) return;
  const bool hasN = rest.normals.size() == rest.positions.size(), hasT = rest.tangents.size() == rest.positions.size();
  const int count = static_cast<int>(rest.positions.size()), grain = 2048;
  JobSystem::shared().parallelFor((count + grain - 1) / grain, [&](int chunk) {
    for (int i = chunk * grain; i < std::min(count, (chunk + 1) * grain); ++i) {
      const Pulls& p = rest.pulls[i];
      // the mix of the bones' matrices (their top three rows: where a point goes)
      float m[3][4] = {};
      for (int k = 0; k < 8 && p.pull[k] > 0; ++k) {
        if (p.bone[k] >= bones.size()) continue;
        const Mat4& b = bones[p.bone[k]];
        for (int c = 0; c < 4; ++c)
          for (int r = 0; r < 3; ++r) m[r][c] += b.m[c][r] * p.pull[k];
      }
      const Vec3 x{m[0][0], m[1][0], m[2][0]}, y{m[0][1], m[1][1], m[2][1]}, z{m[0][2], m[1][2], m[2][2]};
      const Vec3 q = rest.positions[i];
      out.positions[i] = x * q.x + y * q.y + z * q.z + Vec3{m[0][3], m[1][3], m[2][3]};
      if (hasN) {
        // a normal turns by the mix's inverse transpose: the cross products of its columns
        const Vec3 n = rest.normals[i];
        const Vec3 bent = cross(y, z) * n.x + cross(z, x) * n.y + cross(x, y) * n.z;
        out.normals[i] = lengthSq(bent) > 1e-20f ? normalize(bent) : n;
      }
      if (hasT) {
        const Vec3 t = rest.tangents[i].xyz();
        const Vec3 bent = x * t.x + y * t.y + z * t.z;
        out.tangents[i] = {lengthSq(bent) > 1e-20f ? normalize(bent) : t, rest.tangents[i].w};
      }
    }
  });
}

}  // namespace loop::anim
