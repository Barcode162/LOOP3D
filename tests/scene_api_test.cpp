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

// The editor's two needs: which script line placed each thing (so moving it
// rewrites that line), and what a tap on the screen points at.
TEST(things_know_their_line_and_can_be_picked) {
  Vm vm;
  Stage st;
  installStandardLibrary(vm);
  installSceneApi(vm, st);
  CHECK(vm.load("# a note\nbox size: vec(1, 1, 1), at: vec(0, 0.5, 0)\nsphere 0.5, at: vec(3, 0.5, 0)\n"
                "for i from 0 to 1\n  cylinder 0.2, 1, at: vec(-3 - i, 0.5, 0)\n", "pick.loop"));
  CHECK(st.placed.size() == 4);
  if (st.placed.size() != 4) return;
  CHECK(st.placed[0].line == 2 && st.placed[1].line == 3);
  CHECK(st.placed[2].line == 5 && st.placed[3].line == 5);  // a repeat: one line, many things
  float t = 0;
  CHECK(st.pick(Ray{Vec3(0, 0.5f, 5), Vec3(0, 0, -1)}, &t) == 0);
  CHECK_NEAR(t, 4.5f, 1e-3f);                                       // the box's front face
  CHECK(st.pick(Ray{Vec3(3, 0.5f, 5), Vec3(0, 0, -1)}) == 1);       // the ball
  CHECK(st.pick(Ray{Vec3(0.6f, 0.5f, 5), Vec3(0, 0, -1)}) == -1);   // between them: nothing
  CHECK(st.pick(Ray{Vec3(-4, 0.5f, 5), Vec3(0, 0, -1)}) == 3);
  // the corner of the ball's box but not the ball itself: the triangles decide
  CHECK(st.pick(Ray{Vec3(3.45f, 0.95f, 5), Vec3(0, 0, -1)}) == -1);
  // moved things are picked where they are now; hidden ones are passed through
  st.placed[1].position = Vec3(0, 0.5f, 2);
  st.updateTransform(1);
  CHECK(st.pick(Ray{Vec3(0, 0.5f, 5), Vec3(0, 0, -1)}) == 1);
  st.placed[1].visible = false;
  st.updateTransform(1);
  CHECK(st.pick(Ray{Vec3(0, 0.5f, 5), Vec3(0, 0, -1)}) == 0);
}

// The one clock: a thing moved in a step is drawn part-way between where it
// was and where it is, by how far the clock is into the next step; next time
// it comes back once more, exactly where it is; things that didn't move
// aren't sent at all.
TEST(scene_api_moves_blend_between_steps) {
  Vm vm;
  Stage st;
  installStandardLibrary(vm);
  installSceneApi(vm, st);
  CHECK(vm.load("let b = box(size: 1, at: vec(0, 0, 0))\nlet still = box(size: 1, at: vec(5, 0, 0))\n", "t.loop"));
  std::vector<int> ids;
  std::vector<Mat4> xfs;
  st.takeMoves(1, ids, xfs);  // what loading placed
  // a step that moves the box 1 m
  vm.load("b.position = vec(1, 0, 0)\n", "t2.loop");
  st.update(1.0f / 120);
  st.takeMoves(0.25f, ids, xfs);
  CHECK(ids.size() == 1);
  CHECK(ids.size() == 1 && ids[0] == st.placed[0].instance);
  if (!xfs.empty()) CHECK_NEAR(xfs[0].translationPart().x, 0.25, 1e-5);
  CHECK_NEAR(st.scene.instances[0].transform.translationPart().x, 1, 1e-6);  // the world itself is where it is
  // nothing moved since: the box comes back once, at its place
  st.takeMoves(0.6f, ids, xfs);
  CHECK(ids.size() == 1);
  if (!xfs.empty()) CHECK_NEAR(xfs[0].translationPart().x, 0.6, 1e-5);  // still the latest step's move
  st.update(1.0f / 120);  // a step where nothing moves
  st.takeMoves(0.3f, ids, xfs);
  CHECK(ids.size() == 1);
  if (!xfs.empty()) CHECK_NEAR(xfs[0].translationPart().x, 1, 1e-6);
  st.takeMoves(0.5f, ids, xfs);
  CHECK(ids.empty());
}

TEST(scene_api_world_streams_round_the_player) {
  // world(): the land and trees near the player made, the far ones hidden, as they walk
  Vm vm;
  Stage st;
  installStandardLibrary(vm);
  installSceneApi(vm, st);
  bool ok = vm.load(R"(
let start = world(seed: 3, size: 4096, cells: 129)
)", "w.loop");
  for (auto& p : vm.problems()) std::printf("    %s\n", p.text().c_str());
  CHECK(ok);
  CHECK(st.streamed != nullptr);
  // the start is on land, with the ground under the player
  CHECK(st.playerPos.y > 0);
  CHECK(!st.terrains.empty() && std::fabs(st.terrains.back().height(st.playerPos.x, st.playerPos.z) - st.playerPos.y) < 0.01f);
  const size_t first = st.scene.instances.size();
  auto shown = [&] {
    int n = 0;
    for (const MeshInstance& in : st.scene.instances) n += std::fabs(in.transform.linear().determinant()) > 1e-9f;
    return n;
  };
  const int shownFirst = shown();
  // walk 1.5 km across the land, a step at a time
  const Vec3 from = st.playerPos;
  const Vec3 dir = normalize(Vec3(-from.x, 0, -from.z) + Vec3(1e-3f, 0, 0));
  for (int i = 0; i < 600; ++i) {
    st.playerPos = from + dir * (i * 2.5f);
    st.update(1.0f / 60);
  }
  CHECK(st.scene.instances.size() > first);  // new land made ahead
  // and what was left behind hidden: about as much shown as at the start
  const int shownNow = shown();
  std::printf("    shown at the start %d, after 1.5 km %d, made in all %zu\n", shownFirst, shownNow, st.scene.instances.size());
  CHECK(shownNow < static_cast<int>(st.scene.instances.size()));
  CHECK(shownNow < shownFirst * 2);
}
