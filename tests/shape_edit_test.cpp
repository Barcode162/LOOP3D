// A shape opened and reshaped by hand (docs/STUDIO.md, order T-E2): a box opens with 8 points, a pull moves only
// its point, the text and the shape say the same thing, and a step that names a missing point is refused by name.
#include <string>

#include "engine/geometry/shape_edit.h"
#include "engine/script/scene_api.h"
#include "tests/test.h"

using namespace loop;
using namespace loop::script;

namespace {

struct World {
  Vm vm;
  Stage st;
  bool ok = false;
  explicit World(const std::string& text) {
    installStandardLibrary(vm);
    installSceneApi(vm, st);
    ok = vm.load(text, "t.loop");
  }
  std::string problem() const { return vm.problems().empty() ? "" : vm.problems()[0].text(); }
};

}  // namespace

TEST(shape_edit_a_box_opens_with_eight_points) {
  shapeedit::Shape s = shapeedit::open(shapes::box({2, 2, 2}));
  CHECK(s.base.pointCount() == 8);
  CHECK(s.base.faceCount() == 6);
  CHECK(s.base.check().empty());
  // the same recipe always gives the same names
  shapeedit::Shape t = shapeedit::open(shapes::box({2, 2, 2}));
  CHECK(s.base == t.base);
  for (uint32_t n = 1; n <= 8; ++n) CHECK(s.base.pointByName(n) >= 0);
}

TEST(shape_edit_a_pull_moves_only_that_point) {
  shapeedit::Shape s = shapeedit::open(shapes::box({2, 2, 2}));
  const int p = s.base.pointByName(3);
  CHECK(p >= 0);
  const Vec3 was = s.base.points[p].pos;
  CHECK(shapeedit::apply(s, {3, {0.1f, 0.2f, 0}}).empty());
  CHECK(s.now.check().empty());
  for (size_t i = 0; i < s.now.points.size(); ++i) {
    if (static_cast<int>(i) == p) CHECK_NEAR(length(s.now.points[i].pos - (was + Vec3(0.1f, 0.2f, 0))), 0, 1e-6);
    else CHECK(s.now.points[i].pos == s.base.points[i].pos);
  }
  // the faces round the point follow it: 3 of the box's 6 faces use it, and the drawn shape has moved there only
  int round = 0;
  for (auto& f : s.now.faces)
    for (auto& c : f.corners) round += c.point == static_cast<uint32_t>(p);
  CHECK(round == 3);
  Mesh before = shapeedit::drawn(s.base), after = shapeedit::drawn(s.now);
  CHECK(before.positions.size() == after.positions.size());
  // (a pulled corner may cut its quads along the other diagonal, so look at where the points are, not the order)
  auto has = [](const Mesh& m, Vec3 q) {
    for (auto& v : m.positions)
      if (length(v - q) < 1e-6f) return true;
    return false;
  };
  int gone = 0, came = 0;
  for (auto& v : before.positions) gone += !has(after, v);
  for (auto& v : after.positions) came += !has(before, v);
  CHECK(gone > 0 && came > 0);
  CHECK(!has(after, was) && has(after, was + Vec3(0.1f, 0.2f, 0)));
  // every other corner of the box is where it was
  for (auto& q : s.base.points)
    if (q.name != 3) CHECK(has(after, q.pos));
}

TEST(shape_edit_text_shape_text_is_the_same) {
  const std::string lines = "pull point: 3, by: vec(0.1, 0.2, 0)\n  pull point: 8, by: vec(-0.25, 0.4, 0.0625)\n";
  World w("box size: vec(2, 2, 2)\n  " + lines);
  for (auto& p : w.vm.problems()) std::printf("    %s\n", p.text().c_str());
  CHECK(w.ok);
  CHECK(w.st.placed.size() == 1);
  const int mesh = w.st.scene.instances[w.st.placed[0].instance].mesh;
  shapeedit::Shape* s = shapeedit::recall(&w.st, mesh);
  CHECK(s != nullptr);
  if (!s) return;
  CHECK(s->steps.size() == 2);
  CHECK(shapeedit::stepLine(s->steps[0]) == "pull point: 3, by: vec(0.1, 0.2, 0)");
  CHECK(shapeedit::stepLine(s->steps[1]) == "pull point: 8, by: vec(-0.25, 0.4, 0.0625)");
  // the shape the engine drew is the shape the steps make
  CHECK(w.st.scene.meshes[mesh].positions == shapeedit::drawn(s->now).positions);
  // and the steps, written back and read again, give the same shape
  std::string again = "box size: vec(2, 2, 2)\n";
  for (auto& step : s->steps) again += "  " + shapeedit::stepLine(step) + "\n";
  World w2(again);
  CHECK(w2.ok);
  const int m2 = w2.st.scene.instances[w2.st.placed[0].instance].mesh;
  CHECK(w2.st.scene.meshes[m2].positions == w.st.scene.meshes[mesh].positions);
}

TEST(shape_edit_a_missing_point_is_refused_by_name) {
  World w("box size: vec(1, 1, 1)\n  pull point: 99, by: vec(0, 1, 0)\n");
  CHECK(!w.ok);
  const std::string why = w.problem();
  CHECK(why.find("99") != std::string::npos);
  CHECK(why.find("no point") != std::string::npos);
  // a pull with no box above it says where it goes
  World v("pull point: 1, by: vec(0, 1, 0)\n");
  CHECK(!v.ok);
  CHECK(v.problem().find("indented under the box") != std::string::npos);
}

TEST(shape_edit_a_box_without_lines_is_still_the_shared_box) {
  World w("box size: vec(1, 1, 1)\nbox size: vec(1, 1, 1), at: vec(3, 0, 0)\n  pull point: 1, by: vec(0, 0.5, 0)\n");
  CHECK(w.ok);
  CHECK(w.st.scene.meshes.size() == 2);  // the plain box, and the pulled one
  CHECK(w.st.scene.instances[0].mesh != w.st.scene.instances[1].mesh);
}
