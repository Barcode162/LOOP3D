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
