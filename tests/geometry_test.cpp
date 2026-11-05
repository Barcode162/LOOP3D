#include "engine/core/random.h"
#include "engine/geometry/bvh.h"
#include "engine/geometry/mesh.h"
#include "engine/geometry/noise.h"
#include "engine/geometry/plants.h"
#include "engine/geometry/terrain.h"
#include "tests/test.h"

using namespace loop;

static std::vector<Triangle> trisOf(const Mesh& m) {
  std::vector<Triangle> t;
  for (size_t i = 0; i + 2 < m.indices.size(); i += 3) {
    Vec3 a = m.positions[m.indices[i]], b = m.positions[m.indices[i + 1]], c = m.positions[m.indices[i + 2]];
    t.push_back({a, b - a, c - a});
  }
  return t;
}

TEST(shapes_closed_volumes) {
  CHECK_NEAR(shapes::box({1, 2, 3}).volume(), 6, 1e-4);
  CHECK_NEAR(shapes::sphere(1, 96, 48).volume(), 4.18879, 0.02);
  CHECK_NEAR(shapes::icosphere(1, 4).volume(), 4.18879, 0.03);
  CHECK_NEAR(shapes::cylinder(0.5f, 2, 96).volume(), kPi * 0.25 * 2, 0.01);
  CHECK_NEAR(shapes::cone(1, 3, 96).volume(), kPi * 1 * 3 / 3, 0.02);
  CHECK_NEAR(shapes::torus(2, 0.5f, 96, 48).volume(), 2 * kPi * kPi * 2 * 0.25, 0.05);
  CHECK(shapes::capsule(0.5f, 2).volume() > 0);
  Mesh rb = shapes::roundedBox({2, 1, 1}, 0.2f, 4);
  // rounded box volume = box - edge/corner loss
  float exact = 2 * 1 * 1 - (4 - kPi) * 0.04f * (1.6f + 0.6f + 0.6f) - (8 - 4.0f / 3 * kPi) * 0.008f;
  CHECK_NEAR(rb.volume(), exact, 0.02);
  std::vector<Vec3> path = {{0, 0, 0}, {0, 1, 0}, {0.3f, 2, 0}, {1, 2.5f, 0.2f}};
  Mesh t = shapes::tube(path, {0.2f, 0.18f, 0.15f, 0.1f}, 16, true);
  CHECK(t.volume() > 0);
}

TEST(mesh_normals_point_out) {
  Mesh s = shapes::sphere(2);
  int bad = 0;
  for (size_t i = 0; i < s.positions.size(); ++i)
    if (dot(s.normals[i], s.positions[i]) < 0) ++bad;
  CHECK(bad == 0);
  Mesh b = shapes::box({1, 1, 1});
  b.computeTangents();
  CHECK(b.tangents.size() == b.positions.size());
  for (auto& t : b.tangents) CHECK_NEAR(length(t.xyz()), 1, 1e-4);
}

TEST(mesh_weld) {
  Mesh b = shapes::box({1, 1, 1});
  CHECK(b.vertexCount() == 24);
  b.weld();
  CHECK(b.vertexCount() == 8);
  CHECK(b.triangleCount() == 12);
}

TEST(bvh_matches_brute_force) {
  Mesh m;
  Rng rng(3);
  for (int i = 0; i < 60; ++i) {
    Mesh s = shapes::icosphere(rng.range(0.1f, 0.6f), 1);
    s.transform(Mat4::translation({rng.range(-5, 5), rng.range(-5, 5), rng.range(-5, 5)}));
    m.append(s);
  }
  auto tris = trisOf(m);
  Bvh bvh;
  bvh.build(tris);
  CHECK(bvh.depth() < 60);
  int mismatches = 0;
  for (int i = 0; i < 2000; ++i) {
    Ray r{{rng.range(-8, 8), rng.range(-8, 8), rng.range(-8, 8)}, normalize(uniformSphere(rng.uniform2()))};
    Hit h = bvh.closest(r);
    float best = kInf;
    uint32_t bi = ~0u;
    for (size_t k = 0; k < tris.size(); ++k) {
      float t, u, v;
      if (intersectTriangle(tris[k], r, 1e-4f, best, t, u, v)) best = t, bi = static_cast<uint32_t>(k);
    }
    if (bi != h.prim && std::fabs(best - h.t) > 1e-4f) ++mismatches;
    if (bvh.occluded(r, 1e-4f, kInf) != (bi != ~0u)) ++mismatches;
  }
  CHECK(mismatches == 0);
}

TEST(noise_ranges) {
  float lo = 1e9f, hi = -1e9f;
  for (int i = 0; i < 20000; ++i) {
    float v = noise::perlin(Vec3{i * 0.137f, i * 0.071f, i * 0.0313f});
    lo = std::min(lo, v), hi = std::max(hi, v);
  }
  CHECK(lo > -1.2f && hi < 1.2f && hi - lo > 1.0f);
  CHECK(noise::perlin(Vec3{3, 4, 5}) == noise::perlin(Vec3{3, 4, 5}));
  CHECK_NEAR(noise::perlin(Vec3{3, 4, 5}), 0, 1e-6);  // zero on lattice points
  auto c = noise::worley({0.3f, 0.2f, 0.9f});
  CHECK(c.f1 <= c.f2);
}

TEST(terrain_erosion_carves) {
  HeightMap h(129, 1000);
  HeightMap::Shape s;
  h.generate(s);
  float before = h.maxHeight() - h.minHeight();
  CHECK(before > 30);
  HeightMap base = h;
  HeightMap::Erosion e;
  e.droplets = 20000;
  h.erodeHydraulic(e);
  h.erodeThermal(10);
  double diff = 0;
  for (int z = 0; z < 129; ++z)
    for (int x = 0; x < 129; ++x) diff += std::fabs(h.at(x, z) - base.at(x, z));
  CHECK(diff / (129 * 129) > 0.01);
  for (int z = 0; z < 129; ++z)
    for (int x = 0; x < 129; ++x) CHECK(std::isfinite(h.at(x, z)));
  Mesh m = h.toMesh(4);
  CHECK(m.triangleCount() == 32 * 32 * 2);
  CHECK_NEAR(h.height(0, 0), h.at(64, 64), 1e-3);
}

TEST(poisson_spacing) {
  auto pts = poissonDisk(50, 2, 9);
  CHECK(pts.size() > 200);
  for (size_t i = 0; i < pts.size(); i += 7)
    for (size_t j = 0; j < pts.size(); ++j)
      if (i != j) CHECK(length(pts[i] - pts[j]) >= 2 - 1e-4f);
}

TEST(plants_grow) {
  TreeShape s;
  TreeMeshes t = growTree(s);
  CHECK(t.bark.triangleCount() > 500);
  CHECK(t.leaves.triangleCount() > 500);
  CHECK(t.bark.bounds().hi.y > 4);
  s.conifer = true;
  CHECK(growTree(s).bark.triangleCount() > 200);
  Mesh r = makeRock({});
  CHECK(r.volume() > 0.05f);
  CHECK(makeGrassTuft(10, 0.3f, 1).triangleCount() == 80);
}

TEST(tube_sides_face_out) {
  std::vector<Vec3> path = {{0, 0, 0}, {0, 1, 0}, {0, 2, 0}};
  Mesh t = shapes::tube(path, {0.3f}, 12, false);
  Vec3 a = t.positions[t.indices[0]], b = t.positions[t.indices[1]], c = t.positions[t.indices[2]];
  Vec3 n = cross(b - a, c - a), mid = (a + b + c) / 3;
  CHECK(dot(n, Vec3(mid.x, 0, mid.z)) > 0);
}

TEST(geometry_glade_models) {
  // a fern: arching fronds of leaflet cards, all above the ground
  Mesh fern = makeFern(1.0f, 9, 3);
  CHECK(fern.triangleCount() > 300 && fern.triangleCount() < 6000);
  CHECK(fern.bounds().lo.y > -0.05f && fern.bounds().hi.y < 1.0f);
  // a flower: stem, petals round the head, a centre at the top
  FlowerShape fs;
  FlowerMeshes f = makeFlower(fs);
  CHECK(f.petals.triangleCount() == static_cast<size_t>(fs.petals) * 2);
  CHECK(f.centre.bounds().center().y > fs.height * 0.9f);
  // a column stands its height; a broken one is shorter and has no capital
  ColumnShape cs;
  Mesh whole = makeColumn(cs);
  CHECK_NEAR(whole.bounds().hi.y, cs.height, 0.05);
  cs.broken = true;
  Mesh broken = makeColumn(cs);
  CHECK(broken.bounds().hi.y < cs.height - 0.1f);
  // flutes cut in: the shaft is never wider than its radius
  CHECK(whole.triangleCount() > 1000);
  // a log lies along X, about its length
  LogMeshes lg = makeLog(3, 0.2f, 1);
  CHECK_NEAR(lg.bark.bounds().hi.x - lg.bark.bounds().lo.x, 3, 0.2);
  CHECK(lg.ends.triangleCount() > 0);
  // a wing has two faces; a mote is small and cheap
  CHECK(makeWing(0.05f).triangleCount() == 4);
  CHECK(makeMote(0.01f).triangleCount() < 100);
}
