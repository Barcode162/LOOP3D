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
