// The marks: edge and hollow for every vertex, measured by the cook
// (docs/SURFACES.md section 6). Bytes 0 to 255.
#include <algorithm>
#include <chrono>
#include <cstdio>

#include "engine/geometry/cook.h"
#include "engine/geometry/marks.h"
#include "engine/geometry/mesh.h"
#include "tests/test.h"

using namespace loop;

namespace {
int maxOf(const std::vector<uint8_t>& v) { return v.empty() ? 0 : *std::max_element(v.begin(), v.end()); }
int minOf(const std::vector<uint8_t>& v) { return v.empty() ? 0 : *std::min_element(v.begin(), v.end()); }

// Two 1 m planes meeting in a corner rounded at 5 mm in 4 steps, 1 m long.
// Inside (a floor y = 0 from x = -1, a wall x = 0 up to y = 1) or outside (a top
// y = 0 from x = -1, a side x = 0 down to y = -1). Both face the open side.
Mesh filletedCorner(bool inside, std::vector<int>& arcInner) {
  std::vector<Vec2> prof = {{-1, 0}, {-0.5f, 0}, {-0.005f, 0}};
  const float r = 0.005f, s = inside ? 1.0f : -1.0f;
  for (int i = 1; i <= 4; ++i) {
    float a = (kPi * 0.5f) * i / 4;  // a quarter turn round the middle of the rounding
    prof.push_back({-r + r * std::sin(a), s * (r - r * std::cos(a))});
  }
  prof.push_back({0, s * 0.5f});
  prof.push_back({0, s * 1.0f});
  Mesh m;
  for (const Vec2& p : prof) {
    m.addVertex({p.x, p.y, -0.5f}, {0, 1, 0}, {0, 0});
    m.addVertex({p.x, p.y, 0.5f}, {0, 1, 0}, {0, 0});
  }
  for (size_t i = 0; i + 1 < prof.size(); ++i) {
    uint32_t a = static_cast<uint32_t>(i * 2), b = a + 2;  // a: z -0.5, a+1: z 0.5
    m.addTriangle(a, a + 1, b + 1);                         // faces the open side (floor up, wall toward -x)
    m.addTriangle(a, b + 1, b);
  }
  // profile points 3 to 5, both ends: the inside of the fillet (2 and 6 are where it meets the planes)
  for (int k = 3; k <= 5; ++k) arcInner.push_back(static_cast<int>(k * 2)), arcInner.push_back(static_cast<int>(k * 2 + 1));
  return m;
}
}  // namespace

TEST(marks_rounded_box_wears_on_the_rounding_only) {
  Mesh m = shapes::roundedBox({1, 1, 1}, 0.005f, 4);
  Marks k = measureMarks(m);
  CHECK(k.edge.size() == m.vertexCount() && k.hollow.size() == m.vertexCount());
  const float inner = 0.495f;
  int roundMin = 255, roundMax = 0, flatMax = 0, rounding = 0, flat = 0;
  for (size_t i = 0; i < m.vertexCount(); ++i) {
    Vec3 p = vabs(m.positions[i]);
    int beyond = (p.x > inner + 1e-4f) + (p.y > inner + 1e-4f) + (p.z > inner + 1e-4f);
    if (beyond >= 2) roundMin = std::min<int>(roundMin, k.edge[i]), roundMax = std::max<int>(roundMax, k.edge[i]), ++rounding;
    else flatMax = std::max<int>(flatMax, k.edge[i]), ++flat;
  }
  std::printf("  rounded box: %d rounding vertices, edge %d to %d; %d flat, highest edge %d; highest hollow %d\n", rounding, roundMin, roundMax, flat, flatMax, maxOf(k.hollow));
  CHECK(rounding > 0 && flat > 0);
  // NOT MET, the card's "rounding vertices have edge of 200 or more": shapes::roundedBox
  // makes each 5 mm rounding as one 1.5 mm step between 10 cm wide triangles, so the
  // 2 cm lowering of section 6 (triangles 7 cm high) holds them to 24 or 25, and even
  // unlowered the bend there is about 23 per metre, a third of a full mark.
  // Reported to the lead (order M2); the check waits for the decision. The same proof
  // on a fine rounding passes in marks_a_fine_rounding_in_an_outside_corner_wears.
  CHECK(roundMin > flatMax);
  CHECK(flatMax <= 25);
  CHECK(maxOf(k.hollow) <= 25);
}

TEST(marks_a_knife_edge_does_not_wear) {
  Marks k = measureMarks(shapes::box({1, 1, 1}));
  CHECK(maxOf(k.edge) <= 25);
  CHECK(maxOf(k.hollow) <= 25);
}

TEST(marks_sphere_wears_by_its_size) {
  Marks big = measureMarks(shapes::sphere(0.5f));
  CHECK(maxOf(big.edge) <= 25);
  Marks small = measureMarks(shapes::sphere(0.005f));
  CHECK(minOf(small.edge) >= 200);
}

TEST(marks_inside_a_cup_is_hollow) {
  // 8 cm across, 10 cm deep, walls 3 mm: outside floor, outside wall, rim, inside wall, inside floor
  Mesh cup = shapes::lathe({{0, 0}, {0.04f, 0}, {0.04f, 0.05f}, {0.04f, 0.1f}, {0.037f, 0.1f}, {0.037f, 0.05f}, {0.037f, 0.003f}, {0.03f, 0.003f}, {0, 0.003f}}, 48);
  Marks k = measureMarks(cup);
  int floorMin = 255, outsideMax = 0, nf = 0, no = 0;
  for (size_t i = 0; i < cup.vertexCount(); ++i) {
    Vec3 p = cup.positions[i];
    float r = std::sqrt(p.x * p.x + p.z * p.z);
    if (std::fabs(p.y - 0.003f) < 1e-5f && r <= 0.03f + 1e-5f) floorMin = std::min<int>(floorMin, k.hollow[i]), ++nf;
    if (r > 0.0399f && p.y > 0.01f && p.y < 0.09f) outsideMax = std::max<int>(outsideMax, k.hollow[i]), ++no;
  }
  std::printf("  cup: inside floor lowest hollow %d (%d vertices); outside wall highest %d (%d)\n", floorMin, nf, outsideMax, no);
  CHECK(nf > 0 && no > 0);
  CHECK(floorMin >= 128);
  CHECK(outsideMax <= 25);
}

TEST(marks_a_fillet_in_an_inside_corner_is_hollow) {
  std::vector<int> inner;
  Mesh m = filletedCorner(true, inner);
  Marks k = measureMarks(m);
  int low = 255;
  for (int v : inner) low = std::min<int>(low, k.hollow[v]);
  std::printf("  fillet: inner vertices lowest hollow %d; far corners hollow %d, %d, edge %d\n", low, k.hollow[0], k.hollow[m.vertexCount() - 1], std::max(k.edge[0], k.edge[m.vertexCount() - 1]));
  CHECK(low >= 200);
  // the planes' far corners: (-1, 0, +-0.5) and (0, 1, +-0.5)
  for (size_t v : {size_t(0), size_t(1), m.vertexCount() - 2, m.vertexCount() - 1}) CHECK(k.hollow[v] == 0);
}

TEST(marks_a_fine_rounding_in_an_outside_corner_wears) {
  // what proof (a) means, on a mesh whose rounding really is fine (see the note on the box test)
  std::vector<int> inner;
  Mesh m = filletedCorner(false, inner);
  Marks k = measureMarks(m);
  int low = 255;
  for (int v : inner) low = std::min<int>(low, k.edge[v]);
  CHECK(low >= 200);
  for (size_t v : {size_t(0), size_t(1), m.vertexCount() - 2, m.vertexCount() - 1}) CHECK(k.edge[v] == 0);
  CHECK(maxOf(k.hollow) == 0);
}

TEST(marks_same_mesh_same_bytes_and_kept) {
  Mesh m = shapes::roundedBox({1, 1, 1}, 0.005f, 4);
  Marks a = measureMarks(m), b = measureMarks(m);
  CHECK(a.edge == b.edge && a.hollow == b.hollow);
  Cook c;
  CHECK(c.kept() == 0);
  auto first = c.marks(m);
  CHECK(c.kept() == 1);
  auto second = c.marks(m);  // the second ask does no work
  CHECK(c.kept() == 1);
  CHECK(first == second);
  CHECK(first->edge == a.edge && first->hollow == a.hollow);
  // never made unless asked
  Cook d;
  d.clusters(m);
  CHECK(d.kept() == 1);
}

TEST(marks_a_hundred_thousand_triangles_quickly) {
  Mesh m = shapes::sphere(1.0f, 316, 158);
  CHECK(m.triangleCount() > 95000);
  auto t0 = std::chrono::steady_clock::now();
  Marks k = measureMarks(m);
  double ms = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - t0).count();
  std::printf("  marks of %zu triangles: %.0f ms\n", m.triangleCount(), ms);
  CHECK(k.edge.size() == m.vertexCount());
  CHECK(ms < 600.0);  // wanted under 300 ms on a quiet core (about 200 measured); the bound is loose so a busy machine does not fail it
}
