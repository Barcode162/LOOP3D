// The sameness test: the same scene, run twice from the start for the same
// number of steps, gives exactly the same world, bit for bit. Everything the
// rebuild stands on needs it: films that scrub back and forth by replaying,
// multiplayer that stays in step, picture tests that compare exactly.
#include <cstdint>
#include <cstring>
#include <string>

#include "engine/core/settings.h"
#include "engine/project/project.h"
#include "engine/script/scene_api.h"
#include "tests/test.h"

using namespace loop;
using namespace loop::script;

namespace {

// FNV-1a over the bits of every placed thing's matrix, every lamp and every
// physics body: one number for "where everything is".
struct Fingerprint {
  uint64_t h = 1469598103934665603ull;
  void add(const void* p, size_t n) {
    const auto* b = static_cast<const uint8_t*>(p);
    for (size_t i = 0; i < n; ++i) h = (h ^ b[i]) * 1099511628211ull;
  }
  void add(float f) { add(&f, sizeof f); }
  void add(Vec3 v) { add(v.x), add(v.y), add(v.z); }
};

uint64_t fingerprint(const Stage& st) {
  Fingerprint f;
  for (const MeshInstance& in : st.scene.instances) f.add(&in.transform, sizeof in.transform);
  for (const Light& l : st.scene.lights) f.add(l.position), f.add(l.color), f.add(l.lumens);
  for (size_t i = 0; i < st.world.bodyCount(); ++i) {
    const physics::Body& b = st.world.body(static_cast<int>(i));
    f.add(b.x), f.add(b.v), f.add(b.w), f.add(b.q.x), f.add(b.q.y), f.add(b.q.z), f.add(b.q.w);
  }
  return f.h;
}

// Runs a scene file the way the player does (scripts and the world on the
// fixed clock), pressing Use now and then, and fingerprints the world.
uint64_t runScene(const std::string& path, int steps) {
  Settings::get().resetAll();
  std::string src;
  if (!readSceneSource(path, src)) return 0;
  Vm vm;
  Stage st;
  installStandardLibrary(vm);
  installSceneApi(vm, st);
  if (!vm.load(src, path)) {
    for (const auto& p : vm.problems()) std::printf("    %s\n", p.text().c_str());
    return 0;
  }
  const double tick = 1.0 / 120;
  for (int i = 0; i < steps; ++i) {
    if (i == 60 || i == 400) vm.emit("use");  // before and after, both ways
    vm.update(tick);
    st.update(static_cast<float>(tick));
  }
  return fingerprint(st);
}

}  // namespace

// Physics (a stack of crates knocked by a cannonball, balls bouncing) and the
// people room (walking, breathing, looking, footsteps): run twice, the same.
TEST(sameness_same_start_same_world) {
  for (const char* scene : {"projects/showcase/showcase_physics.loop", "projects/showcase/showcase_people.loop", "projects/showcase/showcase_glade.loop"}) {
    uint64_t a = runScene(scene, 900), b = runScene(scene, 900);
    CHECK(a != 0);
    if (a != b) std::printf("    %s: %016llx then %016llx\n", scene, static_cast<unsigned long long>(a), static_cast<unsigned long long>(b));
    CHECK(a == b);
  }
  // and the fingerprint does see things move (the crates fall and scatter)
  CHECK(runScene("projects/showcase/showcase_physics.loop", 300) != runScene("projects/showcase/showcase_physics.loop", 900));
}

// A scene's random numbers don't depend on what ran before it: scripts each
// start from the same seed (it used to be one stream for the whole program,
// so a forest grew differently after another scene had run).
TEST(sameness_randomness_belongs_to_each_script) {
  auto firstRandom = [](bool warmUp) {
    Vm vm;
    installStandardLibrary(vm);
    if (warmUp) {
      Vm other;
      installStandardLibrary(other);
      other.load("let x = random()\nlet y = random()\n", "other.loop");
    }
    vm.load("let r = random()\n", "t.loop");
    return vm.global("r").n;
  };
  double r = firstRandom(false);
  CHECK(r > 0 && r < 1);
  CHECK(r == firstRandom(true));
}
