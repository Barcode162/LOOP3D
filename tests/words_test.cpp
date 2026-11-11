// Examples of testing a word with runScene (tests/test.h): run a script, look at the stage.
#include "tests/test.h"

TEST(words_box_and_light_land_on_the_stage) {
  auto r = runScene(R"(
box size: vec(1, 2, 1), at: vec(0, 1, 0), material: "oak"
light(vec(0, 3, 0), lumens: 500, kelvin: 2700)
camera at: vec(0, 2, 6), look_at: vec(0, 1, 0), lens: 50 mm
)");
  CHECK(r->ok);
  CHECK(r->problems().empty());
  CHECK(r->stage.scene.instances.size() == 1);
  CHECK(r->stage.scene.lights.size() == 1);
  CHECK_NEAR(r->stage.scene.camera.focalLength, 50, 1e-3);
}

TEST(words_a_misspelt_name_gets_a_hint) {
  auto r = runScene("box size: 1, material: \"oke\"\n");
  CHECK(r->hasProblem("did you mean \"oak\""));
  CHECK(!runScene("look \"nior\"\n")->problems().empty());
}

TEST(words_lathe_spins_a_vase_from_six_points) {
  auto r = runScene(R"(
lathe [vec(0, 0), vec(0.12, 0), vec(0.2, 0.2), vec(0.3, 0.45), vec(0.15, 0.7), vec(0.12, 0.8)], at: vec(1, 0, 2), name: "vase"
)");
  CHECK(r->ok);
  CHECK(r->problems().empty());
  CHECK(r->stage.scene.instances.size() == 1);
  const auto& inst = r->stage.scene.instances[0];
  loop::AABB bx = r->stage.scene.meshes[inst.mesh].bounds();
  CHECK_NEAR(bx.lo.y, 0, 1e-3);
  CHECK_NEAR(bx.hi.y, 0.8, 1e-3);
  CHECK_NEAR(bx.hi.x, 0.3, 1e-3);
  CHECK_NEAR(bx.lo.x, -0.3, 1e-3);
  CHECK_NEAR(r->stage.placed[0].position.x, 1, 1e-6);
  // the same outline twice shares one mesh
  auto r2 = runScene("lathe [vec(0, 0), vec(0.3, 0.4)]\nlathe [vec(0, 0), vec(0.3, 0.4)], at: vec(1, 0, 0)\n");
  CHECK(r2->stage.scene.meshes.size() == 1);
}

TEST(words_lathe_names_its_problems) {
  CHECK(runScene("lathe [vec(0.3, 0)]\n")->hasProblem("at least two points"));
  CHECK(runScene("lathe [vec(0.3, 0), vec(-0.1, 1)]\n")->hasProblem("negative radius"));
  CHECK(runScene("lathe 5\n")->hasProblem("needs a profile"));
}

// group: what its block places is measured from its place and turn
TEST(words_group_places_and_turns_as_one) {
  auto r = runScene(R"(
to pane(at: vec(0, 0, 0), width: 4)
  box size: vec(width, 2, 0.02), at: at + vec(0, 1, 0)
let g = group(at: vec(0, 0, 0), turn: 90 deg)
  box size: vec(0.2, 0.2, 0.2), at: vec(1, 0, 0)
  light(vec(2, 1, 0), lumens: 500, aim: vec(2, 0, 0))
let wall = group(at: vec(10, 0, 0), turn: 90 deg)
  pane(at: vec(0, 0, 0), width: 4)
let outer = group(at: vec(0, 5, 0))
  group at: vec(1, 0, 0), turn: 90 deg
    box size: vec(0.1, 0.1, 0.1), at: vec(1, 0, 0)
g.position = vec(0, 0, 3)
)");
  CHECK(r->ok);
  CHECK(r->problems().empty());
  auto& st = r->stage;
  if (st.placed.size() < 3 || st.scene.lights.empty()) return;
  // the box at vec(1, 0, 0) in a group turned 90 deg lands at vec(0, 0, -1); then the group moved 3 along z
  CHECK_NEAR(st.placed[0].position.x, 0, 1e-3);
  CHECK_NEAR(st.placed[0].position.z, 2, 1e-3);
  // its lamp turned and moved with it, still aimed straight down
  CHECK_NEAR(st.scene.lights[0].position.x, 0, 1e-3);
  CHECK_NEAR(st.scene.lights[0].position.z, 1, 1e-3);
  CHECK_NEAR(st.scene.lights[0].direction.y, -1, 1e-3);
  // a recipe called inside a turned group lands in the group: its sheet (4 wide along x) now runs along z
  const loop::Mat4& gx = st.scene.instances[st.placed[1].instance].transform;
  loop::Vec3 along = gx.vector({1, 0, 0});
  CHECK_NEAR(std::fabs(along.z), 1, 1e-3);
  CHECK_NEAR(st.placed[1].position.x, 10, 1e-3);
  // a group in a group: (1,0,0) turned 90 deg is (0,0,-1), moved by (1,0,0), then by (0,5,0)
  const auto& last = st.placed.back();
  CHECK_NEAR(last.position.x, 1, 1e-3);
  CHECK_NEAR(last.position.y, 5, 1e-3);
  CHECK_NEAR(last.position.z, -1, 1e-3);
}

TEST(words_group_hides_its_things_and_lamps) {
  auto r = runScene(R"(
let g = group(at: vec(0, 0, 0))
  box size: vec(1, 1, 1), at: vec(0, 0.5, 0)
  light(vec(0, 2, 0), lumens: 500)
g.visible = no
g.turn = 180 deg
g.visible = yes
)");
  CHECK(r->ok);
  CHECK(r->problems().empty());
  if (r->stage.placed.empty() || r->stage.scene.lights.empty()) return;
  CHECK(r->stage.placed[0].visible);
  CHECK_NEAR(r->stage.scene.lights[0].lumens, 500, 1e-3);
  CHECK(!runScene("group at: vec(0, 0, 0)\n")->problems().empty());  // no lines under it
}
