#include "engine/geometry/cluster.h"

#include <algorithm>
#include <functional>
#include <cmath>
#include <cstdlib>
#include <map>
#include <unordered_map>

#include "engine/geometry/simplify.h"

namespace loop {

namespace {

// A sphere round points (Ritter: start from the box's middle, grow to fit).
Vec4 sphereOf(const std::vector<Vec3>& pos, const uint32_t* idx, size_t count) {
  if (count == 0) return Vec4(0, 0, 0, 0);
  AABB box;
  for (size_t k = 0; k < count; ++k) box.grow(pos[idx[k]]);
  Vec3 c = box.center();
  float r = 0;
  for (size_t k = 0; k < count; ++k) r = std::max(r, length(pos[idx[k]] - c));
  return Vec4(c, r);
}
// A sphere round two spheres.
Vec4 enclose(Vec4 a, Vec4 b) {
  Vec3 ca = a.xyz(), cb = b.xyz();
  float d = length(cb - ca);
  if (d + b.w <= a.w) return a;
  if (d + a.w <= b.w) return b;
  float r = (d + a.w + b.w) * 0.5f;
  Vec3 c = ca + (cb - ca) * ((r - a.w) / std::max(d, 1e-20f));
  return Vec4(c, r * 1.0001f);
}
// Morton order of points in a box: things close in space come close in order.
uint32_t spread10(uint32_t v) {
  v &= 1023;
  v = (v | (v << 16)) & 0x030000FF;
  v = (v | (v << 8)) & 0x0300F00F;
  v = (v | (v << 4)) & 0x030C30C3;
  v = (v | (v << 2)) & 0x09249249;
  return v;
}
uint32_t morton(Vec3 p, const AABB& box) {
  Vec3 ext = box.extent();
  float e = std::max(std::max(ext.x, ext.y), std::max(ext.z, 1e-20f));  // a cube: the same scale on every axis
  Vec3 q = (p - box.lo) / e;
  auto c = [](float x) { return static_cast<uint32_t>(std::clamp(x, 0.0f, 1.0f) * 1023.0f); };
  return spread10(c(q.x)) | spread10(c(q.y)) << 1 | spread10(c(q.z)) << 2;
}
uint64_t edgeKey(uint32_t a, uint32_t b) { return a < b ? (static_cast<uint64_t>(a) << 32 | b) : (static_cast<uint64_t>(b) << 32 | a); }

}  // namespace

size_t ClusterTree::triangles(int level) const {
  size_t n = 0;
  for (const Cluster& c : clusters)
    if (c.level == level) n += c.count;
  return n;
}

std::vector<std::vector<uint32_t>> cutIntoClusters(const std::vector<Vec3>& positionsIn, const std::vector<uint32_t>& indicesIn, int maxTriangles) {
  std::vector<std::vector<uint32_t>> out;
  // just the vertices these triangles use
  std::vector<uint32_t> used(indicesIn);
  std::sort(used.begin(), used.end());
  used.erase(std::unique(used.begin(), used.end()), used.end());
  std::vector<Vec3> pos(used.size());
  for (size_t k = 0; k < used.size(); ++k) pos[k] = positionsIn[used[k]];
  std::vector<uint32_t> idx(indicesIn.size());
  for (size_t k = 0; k < idx.size(); ++k) idx[k] = static_cast<uint32_t>(std::lower_bound(used.begin(), used.end(), indicesIn[k]) - used.begin());
  const size_t T = idx.size() / 3;
  if (T == 0) return out;
  std::vector<uint32_t> weld = weldMap(pos);
  auto emit = [&](const std::vector<uint32_t>& tris) {
    std::vector<uint32_t> c;
    c.reserve(tris.size() * 3);
    for (uint32_t t : tris)
      for (int k = 0; k < 3; ++k) c.push_back(used[idx[t * 3 + k]]);
    out.push_back(std::move(c));
  };
  // the connected pieces (triangles that share a place belong together)
  std::vector<uint32_t> parent(pos.size());
  for (size_t i = 0; i < parent.size(); ++i) parent[i] = static_cast<uint32_t>(i);
  auto root = [&](uint32_t v) {
    while (parent[v] != v) v = parent[v] = parent[parent[v]];
    return v;
  };
  for (size_t t = 0; t < T; ++t)
    for (int k = 1; k < 3; ++k) {
      uint32_t a = root(weld[idx[t * 3]]), b = root(weld[idx[t * 3 + k]]);
      if (a != b) parent[std::max(a, b)] = std::min(a, b);
    }
  std::unordered_map<uint32_t, std::vector<uint32_t>> pieces;
  for (size_t t = 0; t < T; ++t) pieces[root(weld[idx[t * 3]])].push_back(static_cast<uint32_t>(t));
  std::vector<Vec3> mid(T);
  for (size_t t = 0; t < T; ++t) mid[t] = (pos[idx[t * 3]] + pos[idx[t * 3 + 1]] + pos[idx[t * 3 + 2]]) / 3.0f;
  // triangles round each place (for growing through touching triangles)
  std::vector<uint32_t> start(pos.size() + 1, 0), list(T * 3);
  for (uint32_t v : idx) ++start[weld[v] + 1];
  for (size_t i = 1; i < start.size(); ++i) start[i] += start[i - 1];
  {
    std::vector<uint32_t> fill(start.begin(), start.end() - 1);
    for (size_t t = 0; t < T; ++t)
      for (int k = 0; k < 3; ++k) list[fill[weld[idx[t * 3 + k]]]++] = static_cast<uint32_t>(t);
  }
  std::vector<uint8_t> taken(T, 0);
  std::vector<uint32_t> inCluster(pos.size(), ~0u), seenAs(T, ~0u);  // stamps: which cluster
  uint32_t id = 0;
  // Big pieces (a surface): clusters grow through touching triangles, the
  // neighbour that brings the fewest new corners first, then the nearest.
  // Small pieces (grass blades, leaf cards, flowers) are kept whole and packed
  // below by halving space.
  struct Small {
    Vec3 at;
    std::vector<uint32_t> tris;
    int vertices = 0;  // its own (pieces share none)
  };
  std::vector<Small> small;
  std::vector<uint32_t> keys;
  for (auto& [k, tris] : pieces) keys.push_back(k);
  std::sort(keys.begin(), keys.end());  // the same order every time
  for (uint32_t key : keys) {
    std::vector<uint32_t>& piece = pieces[key];
    if (static_cast<int>(piece.size()) * 2 <= maxTriangles) {
      Small sm{Vec3(0), piece};
      std::vector<uint32_t> vs;
      for (uint32_t t : piece) {
        sm.at = sm.at + mid[t];
        for (int k = 0; k < 3; ++k) vs.push_back(idx[t * 3 + k]);
      }
      std::sort(vs.begin(), vs.end());
      sm.vertices = static_cast<int>(std::unique(vs.begin(), vs.end()) - vs.begin());
      sm.at = sm.at / static_cast<float>(piece.size());
      small.push_back(std::move(sm));
      continue;
    }
    AABB box;
    for (uint32_t t : piece) box.grow(mid[t]);
    std::sort(piece.begin(), piece.end(), [&](uint32_t a, uint32_t b) {
      uint32_t ma = morton(mid[a], box), mb = morton(mid[b], box);
      return ma != mb ? ma < mb : a < b;
    });
    for (uint32_t seed : piece) {
      if (taken[seed]) continue;
      std::vector<uint32_t> tris, candidates;
      Vec3 centre(0);
      int vertices = 0;
      auto add = [&](uint32_t t) {
        taken[t] = 1;
        tris.push_back(t);
        centre = centre + (mid[t] - centre) / static_cast<float>(tris.size());
        for (int k = 0; k < 3; ++k) {
          uint32_t w = weld[idx[t * 3 + k]];
          if (inCluster[w] != id) inCluster[w] = id, ++vertices;
          for (uint32_t i = start[w]; i < start[w + 1]; ++i) {
            uint32_t n = list[i];
            if (!taken[n] && seenAs[n] != id) seenAs[n] = id, candidates.push_back(n);
          }
        }
      };
      add(seed);
      while (static_cast<int>(tris.size()) < maxTriangles) {
        int best = -1, bestShared = -1;
        float bestDist = kInf;
        for (size_t c = 0; c < candidates.size(); ++c) {
          uint32_t t = candidates[c];
          if (taken[t]) continue;
          int shared = 0;
          for (int k = 0; k < 3; ++k) shared += inCluster[weld[idx[t * 3 + k]]] == id;
          float d = lengthSq(mid[t] - centre);
          if (shared > bestShared || (shared == bestShared && d < bestDist)) best = static_cast<int>(c), bestShared = shared, bestDist = d;
        }
        if (best < 0 || vertices + (3 - bestShared) > 255) break;
        uint32_t t = candidates[best];
        candidates[best] = candidates.back(), candidates.pop_back();
        add(t);
      }
      emit(tris);
      ++id;
    }
  }
  // the small pieces: halve space (along its longest side, at the middle
  // piece) until each part fits a cluster, so nearby pieces share one
  std::function<void(std::vector<Small*>&)> pack = [&](std::vector<Small*>& part) {
    size_t tris = 0;
    int vertices = 0;
    for (Small* sm : part) tris += sm->tris.size(), vertices += sm->vertices;
    if (tris <= static_cast<size_t>(maxTriangles) && vertices <= 255) {
      std::vector<uint32_t> all;
      for (Small* sm : part) all.insert(all.end(), sm->tris.begin(), sm->tris.end());
      if (!all.empty()) emit(all);
      return;
    }
    if (part.size() == 1) {  // one piece too big to share (can't happen: small means at most half a cluster)
      emit(part[0]->tris);
      return;
    }
    AABB b;
    for (Small* sm : part) b.grow(sm->at);
    Vec3 e = b.extent();
    int axis = e.x >= e.y && e.x >= e.z ? 0 : e.y >= e.z ? 1 : 2;
    auto coord = [axis](const Small* sm) { return axis == 0 ? sm->at.x : axis == 1 ? sm->at.y : sm->at.z; };
    std::sort(part.begin(), part.end(), [&](const Small* x, const Small* y) { return coord(x) < coord(y); });
    // split where half the triangles fall on each side
    size_t half = 0, k = 0;
    while (k + 1 < part.size() && half + part[k]->tris.size() <= tris / 2) half += part[k++]->tris.size();
    k = std::max<size_t>(k, 1);
    std::vector<Small*> lo(part.begin(), part.begin() + k), hi(part.begin() + k, part.end());
    pack(lo), pack(hi);
  };
  std::vector<Small*> all;
  for (Small& sm : small) all.push_back(&sm);
  if (!all.empty()) pack(all);
  return out;
}

ClusterTree buildClusterTree(const Mesh& mesh, int maxTriangles) {
  ClusterTree tree;
  const std::vector<Vec3>& pos = mesh.positions;
  const std::vector<uint32_t> weld = weldMap(pos);
  auto addCluster = [&](const std::vector<uint32_t>& tris, int level, float error, Vec4 lodSphere) {
    Cluster c;
    c.first = static_cast<uint32_t>(tree.indices.size());
    c.count = static_cast<uint32_t>(tris.size() / 3);
    c.bounds = sphereOf(pos, tris.data(), tris.size());
    c.lodSphere = level == 0 ? c.bounds : lodSphere;
    c.error = error;
    c.level = level;
    tree.indices.insert(tree.indices.end(), tris.begin(), tris.end());
    tree.clusters.push_back(c);
    tree.levels = std::max(tree.levels, level + 1);
    return static_cast<int>(tree.clusters.size() - 1);
  };
  std::vector<int> roots;
  for (const auto& c : cutIntoClusters(pos, mesh.indices, maxTriangles)) roots.push_back(addCluster(c, 0, 0, Vec4()));
  for (int level = 0; roots.size() > 1 && level < 32; ++level) {
    // which clusters touch (share edges), and how much
    std::unordered_map<uint64_t, std::vector<int>> edgeClusters;
    for (int c : roots) {
      const Cluster& cl = tree.clusters[c];
      for (uint32_t t = 0; t < cl.count; ++t)
        for (int e = 0; e < 3; ++e) {
          uint32_t a = weld[tree.indices[cl.first + t * 3 + e]], b = weld[tree.indices[cl.first + t * 3 + (e + 1) % 3]];
          std::vector<int>& v = edgeClusters[edgeKey(a, b)];
          if (v.empty() || v.back() != c) v.push_back(c);
        }
    }
    std::map<int, std::map<int, int>> touch;
    for (auto& [key, cs] : edgeClusters)
      for (size_t i = 0; i < cs.size(); ++i)
        for (size_t j = i + 1; j < cs.size(); ++j) ++touch[cs[i]][cs[j]], ++touch[cs[j]][cs[i]];
    // groups of up to eight touching clusters, in space order (bigger groups:
    // less of each is its locked outer edge, so it simplifies more evenly)
    AABB box;
    for (int c : roots) box.grow(tree.clusters[c].lodSphere.xyz());
    std::vector<int> order(roots);
    std::sort(order.begin(), order.end(), [&](int a, int b) { return morton(tree.clusters[a].lodSphere.xyz(), box) < morton(tree.clusters[b].lodSphere.xyz(), box); });
    std::map<int, int> groupOf;
    std::vector<std::vector<int>> groups;
    for (int c : order) {
      if (groupOf.count(c)) continue;
      std::vector<int> g{c};
      groupOf[c] = static_cast<int>(groups.size());
      while (g.size() < 8) {
        std::map<int, int> pull;
        for (int m : g)
          for (auto& [n, w] : touch[m])
            if (!groupOf.count(n)) pull[n] += w;
        if (pull.empty()) break;
        int best = std::max_element(pull.begin(), pull.end(), [](auto& a, auto& b) { return a.second < b.second; })->first;
        groupOf[best] = static_cast<int>(groups.size());
        g.push_back(best);
      }
      groups.push_back(std::move(g));
    }
    // a lone cluster (its neighbours all grouped already) joins the group it
    // touches most, so it isn't left with nothing to simplify with
    for (size_t gi = 0; gi < groups.size(); ++gi) {
      if (groups[gi].size() != 1) continue;
      int c = groups[gi][0], best = -1, bestW = 0;
      for (auto& [n, w] : touch[c])
        if (groupOf[n] != static_cast<int>(gi) && w > bestW && groups[groupOf[n]].size() < 12) best = groupOf[n], bestW = w;
      if (best < 0) continue;
      groups[best].push_back(c), groupOf[c] = best, groups[gi].clear();
    }
    // each group simplified to half, its outer edges locked, cut again
    std::vector<int> next;
    bool simplified = false;
    for (const std::vector<int>& g : groups) {
      if (g.empty()) continue;
      std::vector<uint32_t> tris;
      float childError = 0;
      Vec4 sphere = tree.clusters[g[0]].lodSphere;
      for (int c : g) {
        const Cluster& cl = tree.clusters[c];
        tris.insert(tris.end(), tree.indices.begin() + cl.first, tree.indices.begin() + cl.first + cl.count * 3);
        childError = std::max(childError, cl.error);
        sphere = enclose(sphere, cl.lodSphere);
      }
      // simplified alone, the group's outer edges are its open edges, which
      // simplify never moves: the new pieces meet their neighbours exactly
      size_t have = tris.size() / 3;
      SimplifyResult r = simplify(pos, tris, have / 2);
      if (r.indices.size() / 3 > have * 85 / 100) {
        // it won't go simpler now: its clusters go up as they are, to be
        // grouped with others next time
        next.insert(next.end(), g.begin(), g.end());
        continue;
      }
      simplified = true;
      float error = std::max(childError, r.error);
      for (int c : g) tree.clusters[c].parentError = error, tree.clusters[c].parentSphere = sphere;
      for (const auto& sub : cutIntoClusters(pos, r.indices, maxTriangles)) next.push_back(addCluster(sub, level + 1, error, sphere));
    }
    if (!simplified) break;  // nothing goes simpler: the coarsest of every part is reached
    roots = std::move(next);
  }
  return tree;
}

float projectedError(float error, Vec4 sphere, Vec3 eye, float perPixel) {
  if (error >= kInf) return kInf;
  float d = length(sphere.xyz() - eye) - sphere.w;
  if (d <= 1e-4f) return error > 0 ? kInf : 0;
  return error / d * perPixel;
}

std::vector<int> selectClusters(const ClusterTree& tree, Vec3 eye, float perPixel, float pixels) {
  std::vector<int> out;
  for (size_t i = 0; i < tree.clusters.size(); ++i) {
    const Cluster& c = tree.clusters[i];
    if (projectedError(c.error, c.lodSphere, eye, perPixel) <= pixels && projectedError(c.parentError, c.parentSphere, eye, perPixel) > pixels)
      out.push_back(static_cast<int>(i));
  }
  return out;
}

}  // namespace loop
