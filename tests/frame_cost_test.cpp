// The work budget (engine/render/frame_cost): what one live frame costs.
#include "engine/geometry/mesh.h"
#include "engine/render/frame_cost.h"
#include "tests/test.h"

using namespace loop;

namespace {
// A box in front of the camera, one behind it, a bright lamp by the front box
// and a faint one far away.
Scene twoBoxes() {
  Scene s;
  int mesh = s.addMesh(shapes::box({1, 1, 1}));
  int mat = s.addMaterial(Material{});
  s.add(mesh, mat, Mat4::translation({0, 1, 0}));
  s.add(mesh, mat, Mat4::translation({0, 1, 30}));
  Light near;
  near.position = {0, 2, 1.5f}, near.lumens = 800;
  Light far;
  far.position = {60, 2, -60}, far.lumens = 1;
  s.lights = {near, far};
  return s;
}
FrameView viewOf(const Scene& s) {
  FrameView v;
  v.camera = s.camera;  // at (0, 1.6, 5), looking at (0, 1, 0)
  v.width = 64, v.height = 48;
  return v;
}
}  // namespace

TEST(frame_cost_counts_what_each_pass_draws) {
  Scene s = twoBoxes();
  FrameCost c = measureFrame(s, viewOf(s));
  CHECK(c.things == 2);
  CHECK(c.sceneThings == 2);       // today every copy is drawn, on screen or not
  CHECK(c.inViewThings == 1);      // the box behind the camera is off screen
  CHECK(c.sceneTriangles == 24);
  CHECK(c.inViewTriangles == 12);
  CHECK(c.sceneDraws == 1);        // one shape, one draw call for both copies
  CHECK(!c.sun && c.shadowThings == 0);
  FrameView culled = viewOf(s);
  culled.cullOffscreen = true;
  CHECK(measureFrame(s, culled).sceneThings == 1);
}

TEST(frame_cost_shadow_pass_follows_the_sun) {
  Scene s = twoBoxes();
  Light sun;
  sun.kind = Light::Kind::kSun, sun.direction = normalize(Vec3(0.3f, -1, 0.2f)), sun.lux = 50000;
  s.lights.push_back(sun);
  FrameCost c = measureFrame(s, viewOf(s));
  CHECK(c.sun);
  CHECK(c.shadowThings == 2);  // every opaque copy, near the camera or not
  CHECK(c.lampsComputed == 2);  // the sun is not one of the lamps
}

TEST(frame_cost_pixels_surfaces_and_lamps) {
  Scene s = twoBoxes();
  FrameCost c = measureFrame(s, viewOf(s));
  CHECK(c.samples == 64 * 48);
  CHECK(c.coverage > 0.02f && c.coverage < 0.9f);
  // a box seen from outside: its front, then its back
  CHECK_NEAR(c.layers, 2, 1e-3);
  CHECK_NEAR(c.frontLayers, 1, 1e-3);
  CHECK_NEAR(c.shadedPerPixel, 1.5f, 1e-3);    // 1 + 1/2: the back is shaded half the time
  CHECK_NEAR(c.shadedFrontsOnly, 1, 1e-3);
  CHECK(c.lampsComputed == 2);                 // the shader works out both lamps everywhere
  CHECK(c.lampsThatMatterMost <= 1);           // only the near one matters
  CHECK(c.lampWorkPerPixel() > 2.9f);
}

TEST(frame_cost_budget_names_what_is_over) {
  FrameBudget b = budgetFor("phone");
  CHECK(b.width == 374 && b.height == 811);
  FrameCost c;
  CHECK(overBudget(c, b).empty());
  c.sceneTriangles = 900000;
  c.lampsComputed = 50, c.shadedPerPixel = 2;
  auto over = overBudget(c, b);
  CHECK(over.size() == 2);
  CHECK(budgetFor("high").triangles > b.triangles);
}
