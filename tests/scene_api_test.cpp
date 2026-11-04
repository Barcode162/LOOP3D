#include "engine/script/scene_api.h"
#include "tests/test.h"

using namespace loop;
using namespace loop::script;

TEST(scene_api_builds_and_animates) {
  Vm vm;
  Stage st;
  installStandardLibrary(vm);
  installSceneApi(vm, st);
  bool ok = vm.load(R"(
let oak = material("oak", roughness: 0.3)
let b = box(size: vec(1, 2, 1), at: vec(0, 1, 0), material: oak)
box size: vec(1, 2, 1), at: vec(3, 1, 0), material: "brass"
let lamp = light(vec(0, 3, 0), lumens: 500, kelvin: 2700)
sun elevation: 30 deg, toward: 90 deg
camera at: vec(0, 2, 6), look_at: vec(0, 1, 0), lens: 50 mm
look "noir"
shot "a", 2 s, from: {at: vec(0, 2, 6)}, to: {at: vec(0, 2, 3)}
every 0.1 s
  b.position = b.position + vec(0, 0, 1)
)", "t.loop");
  for (auto& p : vm.problems()) std::printf("    %s\n", p.text().c_str());
  CHECK(ok);
  CHECK(st.scene.instances.size() == 2);
  CHECK(st.scene.meshes.size() == 1);  // the same box shape is shared
  CHECK(st.scene.lights.size() == 2);
  CHECK_NEAR(st.scene.camera.focalLength, 50, 1e-3);
  CHECK(st.scene.look.monochrome);
  CHECK(st.film.shots.size() == 1);
  Camera mid = st.film.at(1, st.scene.camera);
  CHECK_NEAR(mid.position.z, 4.5, 0.01);
  for (int i = 0; i < 10; ++i) vm.update(0.05);
  CHECK(st.placed[0].position.z > 3);
  CHECK(st.scene.instances[0].transform.translationPart().z > 3);
}

TEST(scene_api_helpful_errors) {
  Vm vm;
  Stage st;
  installStandardLibrary(vm);
  installSceneApi(vm, st);
  vm.load("box size: 1, material: \"oke\"\n", "t.loop");
  CHECK(!vm.problems().empty() && vm.problems()[0].text().find("did you mean \"oak\"") != std::string::npos);
  vm.clearProblems();
  vm.load("look \"nior\"\n", "t.loop");
  CHECK(!vm.problems().empty() && vm.problems()[0].text().find("did you mean \"noir\"") != std::string::npos);
  vm.clearProblems();
  vm.load("material(roughnes: 1)\n", "t.loop");
  CHECK(!vm.problems().empty() && vm.problems()[0].text().find("roughness") != std::string::npos);
}

TEST(scene_api_living_world) {
  Vm vm;
  Stage st;
  installStandardLibrary(vm);
  installSceneApi(vm, st);
  bool ok = vm.load(R"(
ground 20, material: "floorboards"
box size: vec(1, 1, 1), at: vec(3, 0.5, 0), material: "stone"
let ball = sphere(0.2, at: vec(0, 3, 0), material: "rubber")
solid ball, mass: 0.5, bounce: 0.6
let mira = person(at: vec(-2, 0, 0), height: 1.7)
mira.walk_to = vec(-2, 0, -4)
mira.look_at = vec(0, 1.6, 0)
room 0.4
let fire = sound("fire", at: vec(0, 0.5, -2), volume: 0.8)
)", "t.loop");
  for (auto& p : vm.problems()) std::printf("    %s\n", p.text().c_str());
  CHECK(ok);
  CHECK(st.groundSurface == "wood");
  float maxBounce = 0;
  bool landed = false;
  for (int i = 0; i < 6 * 60; ++i) {
    vm.update(1.0 / 60);
    st.update(1.0f / 60);
    float y = st.placed[st.placed.size() > 0 ? 1 : 0].position.y;
    (void)y;
    const auto& b = st.world.body(0);
    if (b.v.y > 0.5f) landed = true;
    if (landed) maxBounce = std::max(maxBounce, b.x.y);
  }
  CHECK(landed);
  CHECK_NEAR(st.world.body(0).x.y, 0.2, 0.03);  // resting on the floor
  // the ball's shape follows its body
  CHECK(st.scene.instances[2].transform.translationPart().y < 0.3f);
  // Mira walked there and stopped
  const anim::Animator& a = *st.people[0].animator;
  CHECK(length(a.position - Vec3(-2, 0, -4)) < 0.15f);
  CHECK(!st.people[0].walking);
  CHECK(st.mixer.voices() >= 1);  // the fire (and knocks, footsteps)
  std::vector<float> buf(1024 * 2);
  st.mixer.render(buf.data(), 1024);
  float e = 0;
  for (float x : buf) e += x * x;
  CHECK(e > 0);
}
