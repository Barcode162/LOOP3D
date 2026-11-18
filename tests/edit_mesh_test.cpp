// The editable mesh (docs/STUDIO.md, order T-E1): faces of any number of sides, the four moves, smoothing,
// the way to and from a Mesh, and the text form.
#include <cmath>

#include "engine/core/random.h"
#include "engine/geometry/edit_mesh.h"
#include "tests/test.h"

using namespace loop;

namespace {

EditMesh cube() { return EditMesh::fromMesh(shapes::box({2, 2, 2})); }  // corners at +-1

bool hasPoint(const EditMesh& m, Vec3 p, float eps = 1e-5f) {
  for (auto& q : m.points)
    if (q.alive && length(q.pos - p) < eps) return true;
  return false;
}

bool allFacesHave(const EditMesh& m, size_t sides) {
  for (auto& f : m.faces)
    if (f.alive && f.corners.size() != sides) return false;
  return true;
}

int pointAt(const EditMesh& m, Vec3 p) {
  for (size_t i = 0; i < m.points.size(); ++i)
    if (m.points[i].alive && m.points[i].pos == p) return static_cast<int>(i);
  return -1;
}

}  // namespace

TEST(edit_mesh_from_mesh_joins_quads) {
  EditMesh c = cube();
  CHECK(c.check().empty());
  CHECK(c.pointCount() == 8);
  CHECK(c.faceCount() == 6);  // 12 triangles back into 6 quads
  CHECK(allFacesHave(c, 4));
  // a grid of 2 x 2 cells is four quads; a sphere's rings and segments are mostly quads
  EditMesh g = EditMesh::fromMesh(shapes::plane({2, 2}, 2, 2));
  CHECK(g.faceCount() == 4 && g.pointCount() == 9 && g.check().empty());
  EditMesh s = EditMesh::fromMesh(shapes::sphere(1, 16, 8));
  size_t quads = 0;
  for (auto& f : s.faces) quads += f.alive && f.corners.size() == 4;
  CHECK(quads > 60);
  CHECK(s.check().empty());
}

TEST(edit_mesh_to_mesh_for_the_cook) {
  EditMesh c = cube();
  Mesh m = c.toMesh();
  CHECK(m.triangleCount() == 12);
  CHECK_NEAR(m.volume(), 8.0f, 1e-4);
  // a pentagon is three triangles
  EditMesh p;
  for (int i = 0; i < 5; ++i) p.addPoint({std::cos(i * 1.2566371f), 0, std::sin(i * 1.2566371f)});
  CHECK(p.addFace({0, 1, 2, 3, 4}) >= 0);
  CHECK(p.toMesh().triangleCount() == 3);
  // bones' pulls go with the points
  EditMesh sk = cube();
  sk.points[0].pulls.bone[0] = 3, sk.points[0].pulls.pull[0] = 1;
  Mesh sm = sk.toMesh();
  CHECK(sm.pulls.size() == sm.vertexCount());
}

TEST(edit_mesh_half_edges) {
  EditMesh c = cube();
  auto h = c.halfEdges();
  CHECK(h.he.size() == 24);
  for (size_t i = 0; i < h.he.size(); ++i) {
    const auto& e = h.he[i];
    CHECK(e.twin >= 0);
    CHECK(h.he[e.twin].twin == static_cast<int>(i));
    CHECK(h.he[e.twin].from == e.to && h.he[e.twin].to == e.from);
    CHECK(h.he[e.next].prev == static_cast<int>(i));
    CHECK(h.he[e.next].from == e.to);
  }
  c.removeFace(0);
  auto h2 = c.halfEdges();
  int boundary = 0;
  for (auto& e : h2.he) boundary += e.twin < 0;
  CHECK(boundary == 4);  // the hole's four edges
}

TEST(edit_mesh_smooth_cube_by_hand) {
  // The rule on a cube of corners +-1: a face's point is the middle of its corners, (+-1,0,0) and so on; an
  // edge's point is (A + B + F1 + F2) / 4 = (0.75,0.75,0) and so on; a corner's point is (F + 2R + 0P) / 3 with
  // F = (1/3,1/3,1/3) and R = (2/3,2/3,2/3), which is (5/9,5/9,5/9).
  EditMesh s = cube().smoothed(1);
  CHECK(s.check().empty());
  CHECK(s.pointCount() == 26);
  CHECK(s.faceCount() == 24);
  CHECK(allFacesHave(s, 4));
  const float k = 5.0f / 9.0f, e = 0.75f;
  for (int sx = -1; sx <= 1; sx += 2)
    for (int sy = -1; sy <= 1; sy += 2)
      for (int sz = -1; sz <= 1; sz += 2) CHECK(hasPoint(s, {sx * k, sy * k, sz * k}));
  for (int a = -1; a <= 1; a += 2) {
    for (int b = -1; b <= 1; b += 2) {
      CHECK(hasPoint(s, {a * e, b * e, 0}));
      CHECK(hasPoint(s, {a * e, 0, b * e}));
      CHECK(hasPoint(s, {0, a * e, b * e}));
    }
    CHECK(hasPoint(s, {static_cast<float>(a), 0, 0}));
    CHECK(hasPoint(s, {0, static_cast<float>(a), 0}));
    CHECK(hasPoint(s, {0, 0, static_cast<float>(a)}));
  }
  // twice: 26 + 48 edges + 24 faces = 98 points, 96 faces
  EditMesh s2 = cube().smoothed(2);
  CHECK(s2.pointCount() == 98 && s2.faceCount() == 96 && s2.check().empty());
  // the old points keep their names
  EditMesh c = cube();
  EditMesh s1 = c.smoothed(1);
  for (auto& p : c.points) CHECK(s1.pointByName(p.name) >= 0);
  // a smooth cube is inside the cube and has lost much of its volume
  Mesh sm = s2.toMesh();
  CHECK(sm.volume() < 8.0f && sm.volume() > 2.0f);
}

TEST(edit_mesh_crease_stays_sharp) {
  // One edge of the cube, from (1,1,1) to (1,1,-1), held sharp: the new point on it stays on the line.
  EditMesh c = cube();
  int a = pointAt(c, {1, 1, 1}), b = pointAt(c, {1, 1, -1});
  CHECK(a >= 0 && b >= 0);
  c.setCrease(a, b, 3);
  CHECK(c.creaseOf(a, b) == 3);
  EditMesh s = c.smoothed(1);
  CHECK(hasPoint(s, {1, 1, 0}));  // on the edge, where a smooth one would be (0.75,0.75,0)
  CHECK(s.creases.size() == 2);   // the edge is now two, one level less sharp
  for (auto& kv : s.creases) CHECK_NEAR(kv.second, 2.0f, 1e-6);
  EditMesh s3 = c.smoothed(3);  // after 3 levels the sharpness has run out
  CHECK(s3.creases.empty());
  CHECK(s3.check().empty());
  // every edge held sharp for good: nothing leaves the cube and the corners stay
  EditMesh all = cube();
  for (auto& f : all.faces)
    for (size_t k = 0; k < f.corners.size(); ++k) all.setCrease(f.corners[k].point, f.corners[(k + 1) % 4].point, 100);
  EditMesh as = all.smoothed(2);
  CHECK(as.check().empty());
  for (auto& p : as.points)
    CHECK(std::fabs(p.pos.x) <= 1.0001f && std::fabs(p.pos.y) <= 1.0001f && std::fabs(p.pos.z) <= 1.0001f);
  CHECK(hasPoint(as, {1, 1, 1}) && hasPoint(as, {-1, 1, -1}));
  CHECK(hasPoint(as, {1, 1, 0.5f}));  // a sharp edge is only cut in two, in line
  Mesh am = as.toMesh();
  CHECK_NEAR(am.volume(), 8.0f, 1e-3);
}

TEST(edit_mesh_moves_one_by_one) {
  EditMesh c = cube();
  uint32_t a = c.faces[0].corners[0].point, b = c.faces[0].corners[1].point;
  uint32_t nameBefore = c.points[a].name;
  int n = c.splitEdge(a, b);
  CHECK(n >= 0 && c.check().empty());
  CHECK(c.pointCount() == 9);
  int fives = 0;
  for (auto& f : c.faces) fives += f.corners.size() == 5;
  CHECK(fives == 2);
  CHECK(c.points[a].name == nameBefore);
  CHECK(c.findEdge(a, b) < 0);  // a-b is now two edges
  // cut a five-sided face from its corner 0 to corner 2
  int f5 = -1;
  for (size_t i = 0; i < c.faces.size(); ++i)
    if (c.faces[i].corners.size() == 5) f5 = static_cast<int>(i);
  uint32_t nm = c.faces[f5].name;
  int g = c.splitFace(static_cast<uint32_t>(f5), 0, 2);
  CHECK(g >= 0 && c.check().empty());
  CHECK(c.faces[f5].name == nm);  // the old face keeps its name
  CHECK(c.faceByName(c.faces[g].name) == g);
  CHECK(c.faceCount() == 7);
  CHECK(c.splitFace(static_cast<uint32_t>(f5), 0, 1) < 0);  // next-door corners make no edge
  // join the new point back into the corner beside it
  CHECK(c.joinPoints(a, static_cast<uint32_t>(n)));
  CHECK(c.check().empty());
  // removing a face leaves an open mesh
  EditMesh d = cube();
  CHECK(d.removeFace(0) && d.check().empty());
  CHECK(d.faceCount() == 5 && d.pointCount() == 8);
  CHECK(!d.removeFace(0));  // already gone
  // take every face away, one at a time: whole each time, no point left over at the end
  EditMesh e = cube();
  while (e.faceCount() > 0) {
    for (size_t i = 0; i < e.faces.size(); ++i)
      if (e.faces[i].alive) {
        e.removeFace(static_cast<uint32_t>(i));
        break;
      }
    CHECK(e.check().empty());
  }
  CHECK(e.pointCount() == 0);
}

TEST(edit_mesh_join_refuses_what_would_not_be_whole) {
  // two corners across a face of a cube: joining them would pinch that face
  EditMesh c = cube();
  EditMesh before = c;
  uint32_t p = c.faces[0].corners[0].point, q = c.faces[0].corners[2].point;
  CHECK(!c.joinPoints(p, q));
  CHECK(c == before);
  CHECK(!c.joinPoints(p, p));
  // a plain edge folds away
  CHECK(c.joinPoints(p, c.faces[0].corners[1].point));
  CHECK(c.pointCount() == 7 && c.check().empty());
}

TEST(edit_mesh_ten_thousand_random_moves) {
  Rng rng(7);
  EditMesh m = cube();
  int done[4] = {0, 0, 0, 0}, tries[4] = {0, 0, 0, 0}, restarts = 0;
  for (int step = 0; step < 10000; ++step) {
    if (m.faceCount() < 3) m = cube(), ++restarts;
    std::vector<uint32_t> live;
    for (size_t i = 0; i < m.faces.size(); ++i)
      if (m.faces[i].alive) live.push_back(static_cast<uint32_t>(i));
    uint32_t f = live[static_cast<size_t>(rng.below(static_cast<int>(live.size())))];
    const auto cs = m.faces[f].corners;
    int n = static_cast<int>(cs.size());
    int op = rng.below(4);
    if (m.faceCount() > 120 && (op == 0 || op == 1)) op = 2 + rng.below(2);  // keep it from growing without end
    if (m.faceCount() < 12 && op == 3) op = 0;
    ++tries[op];
    switch (op) {
      case 0: {
        int k = rng.below(n);
        done[0] += m.splitEdge(cs[static_cast<size_t>(k)].point, cs[static_cast<size_t>((k + 1) % n)].point, rng.range(0.1f, 0.9f)) >= 0;
        break;
      }
      case 1:
        done[1] += m.splitFace(f, static_cast<uint32_t>(rng.below(n)), static_cast<uint32_t>(rng.below(n))) >= 0;
        break;
      case 2: {
        int k = rng.below(n);
        uint32_t a = cs[static_cast<size_t>(k)].point, b = cs[static_cast<size_t>((k + 1) % n)].point;
        if (rng.below(4) == 0) b = m.faces[live[static_cast<size_t>(rng.below(static_cast<int>(live.size())))]].corners[0].point;
        done[2] += m.joinPoints(a, b);
        break;
      }
      default: done[3] += m.removeFace(f);
    }
    std::string why = m.check();
    if (!why.empty()) {
      CHECK(why.empty());
      std::printf("  step %d, move %d: %s\n", step, op, why.c_str());
      break;
    }
    if (step % 500 == 0) {  // creases and smoothing ride along
      for (auto& face : m.faces)
        if (face.alive && rng.below(3) == 0) m.setCrease(face.corners[0].point, face.corners[1].point, rng.range(0.5f, 3));
      CHECK(m.check().empty());
      EditMesh s = m.smoothed(1);
      CHECK(s.check().empty());
    }
  }
  for (int i = 0; i < 4; ++i) CHECK(done[i] > 100);
  std::printf("  moves done (split edge, split face, join, remove): %d %d %d %d of %d %d %d %d; restarts %d\n", done[0], done[1], done[2],
              done[3], tries[0], tries[1], tries[2], tries[3], restarts);
}

TEST(edit_mesh_text_round_trip) {
  // a mesh with everything on it: unwrap, colour, bones, mask, creases, names, removed parts
  EditMesh m = cube();
  m.points[2].pulls.bone[0] = 4, m.points[2].pulls.pull[0] = 0.75f;
  m.points[2].pulls.bone[1] = 9, m.points[2].pulls.pull[1] = 0.25f;
  m.points[3].mask = 0.3f;
  m.faces[1].corners[2].uv = {0.123456789f, 0.987654321f};
  m.faces[1].corners[2].color = {0.1f, 0.2f, 0.3f, 0.4f};
  m.setCrease(m.faces[0].corners[0].point, m.faces[0].corners[1].point, 2.5f);
  m.splitEdge(m.faces[2].corners[0].point, m.faces[2].corners[1].point, 0.3f);
  m.removeFace(4);
  CHECK(m.check().empty());
  std::string text = m.toText();
  CHECK(text.rfind("loop-editmesh 1\n", 0) == 0);
  CHECK(text.find("points ") != std::string::npos && text.find("faces ") != std::string::npos);
  EditMesh back;
  std::string err;
  CHECK(EditMesh::fromText(text, back, &err));
  if (!err.empty()) std::printf("  %s\n", err.c_str());
  EditMesh same = m;
  same.compact();  // the text is always written compact
  CHECK(back == same);
  CHECK(back.toText() == text);
  CHECK(back.points[2].pulls.bone[1] == 9);
  CHECK(back.faces[1].corners[2].uv.x == 0.123456789f);
  // a smoothed mesh too
  EditMesh sm = m.smoothed(2), smBack;
  sm.compact();
  CHECK(EditMesh::fromText(sm.toText(), smBack) && smBack == sm);
  // names count on after reading: a new point does not reuse one
  uint32_t fresh = back.addPoint({5, 5, 5});
  for (size_t i = 0; i < fresh; ++i) CHECK(back.points[i].name != back.points[fresh].name);
  // bad text is refused, with a reason
  EditMesh junk;
  std::string why;
  CHECK(!EditMesh::fromText("hello", junk, &why) && !why.empty());
  CHECK(!EditMesh::fromText("loop-editmesh 1\npoints 1\nfaces 0\ncreases 0\nnext-name 2\n", junk, &why));
}

TEST(edit_mesh_smoothing_carries_corner_values_and_bones) {
  EditMesh c = cube();
  for (auto& f : c.faces)
    for (size_t k = 0; k < f.corners.size(); ++k) f.corners[k].uv = {static_cast<float>(k & 1), static_cast<float>(k >> 1)};
  for (size_t i = 0; i < c.points.size(); ++i) c.points[i].pulls.bone[0] = static_cast<uint16_t>(i % 3), c.points[i].pulls.pull[0] = 1;
  EditMesh s = c.smoothed(1);
  for (auto& p : s.points) {
    float sum = 0;
    for (int i = 0; i < 8; ++i) sum += p.pulls.pull[i];
    CHECK_NEAR(sum, 1.0f, 1e-5);
  }
  for (auto& f : s.faces)
    for (auto& k : f.corners) CHECK(k.uv.x >= 0 && k.uv.x <= 1 && k.uv.y >= 0 && k.uv.y <= 1);
}
