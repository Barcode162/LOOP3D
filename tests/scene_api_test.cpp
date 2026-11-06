#include "engine/render/frame_cost.h"
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

// The model library: grass, flowers, ferns, columns, logs, trees (with a
// handle now), and creatures the engine moves by itself.
TEST(scene_api_glade_library) {
  Vm vm;
  Stage st;
  installStandardLibrary(vm);
  installSceneApi(vm, st);
  bool ok = vm.load(R"(
ground 40
let g = grass(around: vec(0, 0, 0), radius: 4, density: 6)
let fl = flowers(around: vec(0, 0, 0), radius: 3, count: 24, kinds: ["daisy", "poppy"])
let f = fern(vec(1, 0, 1))
let c = column(at: vec(0, 0, -3), broken: yes)
let lg = log(at: vec(0, 0, 2))
let t = tree("birch", at: vec(4, 0, 0))
let b = butterfly(around: vec(0, 0, 0))
let ff = fireflies(around: vec(0, 0, 0), count: 10)
let start = b.position
)", "t.loop");
  for (auto& p : vm.problems()) std::printf("    %s\n", p.text().c_str());
  CHECK(ok);
  CHECK(vm.global("g").isObj());
  // grass: many tufts sharing four shapes
  size_t tufts = 0;
  for (const auto& p : st.groups[0].parts) tufts += st.placed[p].instance >= 0;
  CHECK(tufts > 100);
  CHECK(st.groups.size() == 6);  // grass, flowers, fern, log, tree, fireflies
  CHECK(st.critters.size() == 11);
  // creatures move on their own
  Vec3 b0 = st.critters[0].pos;
  for (int i = 0; i < 120; ++i) vm.update(1.0 / 60), st.update(1.0f / 60);
  CHECK(length(st.critters[0].pos - b0) > 0.05f);
  // hiding a model hides every part
  CHECK(vm.load("g.visible = no\nt.visible = no\n", "t2.loop"));
  CHECK(!st.placed[st.groups[0].parts[0]].visible);
  CHECK(st.scene.instances[st.placed[st.groups[4].parts[1]].instance].transform.m[0][0] == 0);
}

// Trees come with simpler versions for far away, and a frame far from them
// draws those (loop budget counts what the renderer draws).
TEST(scene_api_trees_have_far_versions) {
  Vm vm;
  Stage st;
  installStandardLibrary(vm);
  installSceneApi(vm, st);
  CHECK(vm.load("ground 400\nlet t = tree(\"pine\", at: vec(0, 0, 0))\ncamera at: vec(0, 7, 12), look_at: vec(0, 7, 0), lens: 35 mm\n", "t.loop"));
  int bark = st.scene.instances[1].mesh;
  CHECK(static_cast<size_t>(bark) < st.scene.lods.size());
  int simpler = st.scene.lodMesh(bark, 1), simplest = st.scene.lodMesh(bark, 2);
  CHECK(simpler != bark && simplest != simpler);
  CHECK(st.scene.meshes[simplest].triangleCount() * 10 < st.scene.meshes[bark].triangleCount());
  FrameView near;
  near.camera = st.scene.camera, near.width = 374, near.height = 811;
  FrameCost close = measureFrame(st.scene, near, 500);
  FrameView away = near;
  away.camera.position = {0, 7, 400}, away.camera.target = {0, 7, 0};
  FrameCost distant = measureFrame(st.scene, away, 500);
  CHECK(close.lodCopies[0] >= 1);
  CHECK(distant.lodCopies[2] >= 1);
  CHECK(distant.sceneTriangles * 5 < close.sceneTriangles);
}
