// The cook: making meshes simpler without moving a vertex (simplify.h), and
// the tree of clusters Nanite-style drawing picks from (cluster.h).
#include <array>
#include <map>
#include <set>

#include "engine/geometry/mesh.h"
#include "engine/geometry/simplify.h"
#include "tests/test.h"

using namespace loop;

namespace {
// How many triangles share each edge of the welded shape (closed: all 2).
std::map<std::pair<uint32_t, uint32_t>, int> edgeUses(const std::vector<Vec3>& pos, const std::vector<uint32_t>& idx) {
  std::vector<uint32_t> w = weldMap(pos);
  std::map<std::pair<uint32_t, uint32_t>, int> uses;
  for (size_t t = 0; t + 2 < idx.size(); t += 3)
    for (int e = 0; e < 3; ++e) {
      uint32_t a = w[idx[t + e]], b = w[idx[t + (e + 1) % 3]];
      ++uses[{std::min(a, b), std::max(a, b)}];
    }
  return uses;
}
float farthestFromSphere(const std::vector<Vec3>& pos, const std::vector<uint32_t>& idx, float r) {
  // the middle of every triangle: how far inside the true sphere it sank
  float worst = 0;
  for (size_t t = 0; t + 2 < idx.size(); t += 3) worst = std::max(worst, r - length((pos[idx[t]] + pos[idx[t + 1]] + pos[idx[t + 2]]) / 3.0f));
  return worst;
}
}  // namespace

TEST(simplify_sphere_keeps_its_shape_closed) {
  Mesh s = shapes::sphere(1.0f, 48, 24);
  size_t before = s.triangleCount();
  SimplifyResult r = simplify(s.positions, s.indices, before / 4);
  size_t after = r.indices.size() / 3;
  CHECK(after <= before / 4 + 8);
  CHECK(after > 50);
  // no new holes: the same open edges as before (the sphere's seam sits a hair
  // apart from rounding, so it counts as open), every other edge between two
  // triangles
  auto openEdges = [&](const std::vector<uint32_t>& idx) {
    std::set<std::pair<uint32_t, uint32_t>> open;
    for (auto& [e, n] : edgeUses(s.positions, idx)) {
      CHECK(n <= 2);
      if (n == 1) open.insert(e);
    }
    return open;
  };
  CHECK(openEdges(r.indices) == openEdges(s.indices));
  // still a sphere: no part sank far, and the reported error covers it
  float sank = farthestFromSphere(s.positions, r.indices, 1.0f);
  CHECK(sank < 0.12f);
  CHECK(r.error > 0 && r.error < 0.2f);
  // every vertex kept is an original one, untouched
  for (uint32_t i : r.indices) CHECK(i < s.vertexCount());
}

TEST(simplify_flat_ground_to_almost_nothing_but_keeps_its_edges) {
  Mesh g = shapes::plane({4, 4}, 16, 16);
  SimplifyResult r = simplify(g.positions, g.indices, 1);
  // flat: nothing moves out of the plane
  CHECK(r.error < 1e-4f);
  CHECK(r.indices.size() / 3 < g.triangleCount() / 4);
  // the open edge stays exactly where it was: every border place is still used
  std::set<uint32_t> used;
  std::vector<uint32_t> w = weldMap(g.positions);
  for (uint32_t i : r.indices) used.insert(w[i]);
  for (size_t v = 0; v < g.vertexCount(); ++v) {
    Vec3 p = g.positions[v];
    bool border = std::fabs(std::fabs(p.x) - 2) < 1e-4f || std::fabs(std::fabs(p.z) - 2) < 1e-4f;
    if (border) CHECK(used.count(w[v]) == 1);
  }
}

TEST(simplify_honours_locked_vertices_and_the_error_limit) {
  Mesh s = shapes::sphere(1.0f, 32, 16);
  std::vector<uint8_t> locked(s.vertexCount(), 0);
  for (size_t v = 0; v < s.vertexCount(); ++v) locked[v] = s.positions[v].y > 0.5f;  // the cap stays
  SimplifyResult r = simplify(s.positions, s.indices, 1, locked);
  std::set<uint32_t> used(r.indices.begin(), r.indices.end());
  std::vector<uint32_t> w = weldMap(s.positions);
  std::set<uint32_t> usedPlaces;
  for (uint32_t i : used) usedPlaces.insert(w[i]);
  for (size_t v = 0; v < s.vertexCount(); ++v)
    if (locked[v]) CHECK(usedPlaces.count(w[v]) == 1);
  // a limit on how far the surface may move stops it early
  SimplifyResult careful = simplify(s.positions, s.indices, 1, {}, 0.01f);
  CHECK(careful.error <= 0.01f);
  CHECK(careful.indices.size() > r.indices.size());
}

#include "engine/geometry/cluster.h"

namespace {
// Over the chosen clusters: how many triangles use each welded edge.
std::map<std::pair<uint32_t, uint32_t>, int> cutEdges(const Mesh& m, const ClusterTree& tree, const std::vector<int>& chosen) {
  std::vector<uint32_t> idx;
  for (int c : chosen) {
    const Cluster& cl = tree.clusters[c];
    idx.insert(idx.end(), tree.indices.begin() + cl.first, tree.indices.begin() + cl.first + cl.count * 3);
  }
  return edgeUses(m.positions, idx);
}
}  // namespace

TEST(clusters_hold_every_triangle_once) {
  Mesh s = shapes::icosphere(1.0f, 4);
  auto clusters = cutIntoClusters(s.positions, s.indices, 128);
  std::multiset<std::array<uint32_t, 3>> before, after;
  for (size_t t = 0; t < s.indices.size(); t += 3) before.insert({s.indices[t], s.indices[t + 1], s.indices[t + 2]});
  size_t biggest = 0;
  for (const auto& c : clusters) {
    biggest = std::max(biggest, c.size() / 3);
    for (size_t t = 0; t < c.size(); t += 3) after.insert({c[t], c[t + 1], c[t + 2]});
  }
  CHECK(before == after);
  CHECK(biggest <= 128);
  CHECK(clusters.size() <= s.triangleCount() / 128 * 2 + 4);  // mostly full clusters
}

// Separate pieces (a meadow of blades, leaf cards) still fill whole clusters,
// nearby ones together, not a cluster per piece.
TEST(clusters_gather_separate_pieces_nearby) {
  Mesh meadow;
  for (int i = 0; i < 1000; ++i) {
    Mesh card = shapes::plane({0.05f, 0.05f});
    card.transform(Mat4::trs(Vec3((i % 40) * 0.2f, 0, (i / 40) * 0.2f), Quat(), Vec3(1)));
    meadow.append(card);
  }
  auto clusters = cutIntoClusters(meadow.positions, meadow.indices, 128);
  CHECK(clusters.size() <= 2000 / 128 + 4);
  // and each cluster stays together in space
  float widest = 0;
  for (const auto& c : clusters) {
    AABB b;
    for (uint32_t v : c) b.grow(meadow.positions[v]);
    widest = std::max(widest, length(b.extent()));
  }
  CHECK(widest < 4.0f);  // the meadow is 8 m across
}

TEST(cluster_tree_any_view_meets_without_cracks) {
  Mesh s = shapes::icosphere(1.0f, 5);  // 20,480 triangles, closed, no seams
  ClusterTree tree = buildClusterTree(s);
  CHECK(tree.levels >= 4);
  CHECK(tree.triangles(0) == s.triangleCount());
  // errors only grow toward the coarse end, and a parent's sphere holds its child's
  bool monotonic = true;
  for (const Cluster& c : tree.clusters) {
    if (c.parentError >= kInf) continue;
    monotonic = monotonic && c.parentError >= c.error;
    monotonic = monotonic && length(c.parentSphere.xyz() - c.lodSphere.xyz()) + c.lodSphere.w <= c.parentSphere.w * 1.001f + 1e-5f;
  }
  CHECK(monotonic);
  // each cluster's error says how far it really is from the sphere: never
  // under (things would pop), and, level by level, not far over (detail would
  // stay too long; thin slivers once made it 17 times too much). A cluster
  // takes its whole group's error, so a quiet one in a busy group says more.
  bool never_under = true;
  std::vector<float> levelError(tree.levels, 0), levelReal(tree.levels, 0);
  for (const Cluster& c : tree.clusters) {
    if (c.level == 0) continue;
    float real = 0;
    for (uint32_t k = 0; k < c.count; ++k) {
      const uint32_t* i = &tree.indices[c.first + k * 3];
      real = std::max(real, 1.0f - length((s.positions[i[0]] + s.positions[i[1]] + s.positions[i[2]]) / 3.0f));
    }
    never_under = never_under && real <= c.error * 2 + 2e-4f;
    levelError[c.level] = std::max(levelError[c.level], c.error), levelReal[c.level] = std::max(levelReal[c.level], real);
  }
  CHECK(never_under);
  for (int l = 1; l < tree.levels; ++l) CHECK(levelError[l] <= levelReal[l] * 4 + 0.01f);
  // seen from near, mid and far: closed (every edge between exactly two
  // triangles: no cracks, no overlaps), and fewer triangles farther away
  size_t last = s.triangleCount() + 1;
  for (float dist : {1.5f, 4.0f, 20.0f, 200.0f}) {
    std::vector<int> chosen = selectClusters(tree, Vec3(0.3f, 0.2f, dist), 1000, 1);
    size_t tris = 0;
    for (int c : chosen) tris += tree.clusters[c].count;
    bool closed = !chosen.empty();
    for (auto& [e, n] : cutEdges(s, tree, chosen)) closed = closed && n == 2;
    if (!closed) std::printf("    from %.1f m: not closed\n", dist);
    CHECK(closed);
    CHECK(tris <= last);
    last = tris;
  }
  CHECK(last < s.triangleCount() / 10);  // far away, a tenth or less
}

TEST(cluster_tree_keeps_open_edges_of_land) {
  Mesh g = shapes::heightfield({20, 20}, 64, 64, [](float x, float z) { return std::sin(x * 0.7f) * std::cos(z * 0.5f) * 1.5f; });
  ClusterTree tree = buildClusterTree(g);
  std::vector<int> full = selectClusters(tree, Vec3(0, 3, 0), 1e9f, 1e-9f);  // everything at full detail
  std::set<std::pair<uint32_t, uint32_t>> openFull, openFar;
  for (auto& [e, n] : cutEdges(g, tree, full))
    if (n == 1) openFull.insert(e);
  std::vector<int> far = selectClusters(tree, Vec3(0, 60, 0), 300, 1);
  bool ok = true;
  for (auto& [e, n] : cutEdges(g, tree, far)) {
    ok = ok && n <= 2;
    if (n == 1) openFar.insert(e);
  }
  CHECK(ok);
  CHECK(openFar == openFull);  // the land's own edge, nothing more: no cracks inside
  size_t farTris = 0;
  for (int c : far) farTris += tree.clusters[c].count;
  CHECK(farTris < g.triangleCount());
}

#include "engine/geometry/cook.h"

// The cook makes a mesh's tree once and keeps it: the same shape asked again
// (another copy, another mesh with the same numbers) costs nothing.
TEST(cook_keeps_one_tree_per_shape) {
  Cook::shared().forget();
  std::vector<Mesh> meshes = {shapes::icosphere(1.0f, 3), shapes::icosphere(1.0f, 3), shapes::torus(1.0f, 0.3f, 32, 16)};
  Cook::shared().cookAll(meshes);
  CHECK(Cook::shared().kept() == 2);
  CHECK(Cook::shared().clusters(meshes[0]) == Cook::shared().clusters(meshes[1]));
  CHECK(Cook::shared().clusters(meshes[2])->triangles(0) == meshes[2].triangleCount());
  Mesh moved = meshes[0];
  moved.positions[0].x += 0.001f;  // any change is another recipe
  CHECK(Cook::fingerprint(moved) != Cook::fingerprint(meshes[0]));
  Cook::shared().forget();
}
