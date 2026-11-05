// Engine settings: layers, names, and the script words that use them.
#include <cmath>
#include <vector>

#include "engine/audio/audio.h"
#include "engine/core/settings.h"
#include "engine/script/scene_api.h"
#include "tests/test.h"

using namespace loop;
using namespace loop::script;

namespace {
// Every test starts and ends with every setting at its default.
struct Fresh {
  Fresh() { Settings::get().resetAll(); }
  ~Fresh() { Settings::get().resetAll(); }
};
bool run(Vm& vm, Stage& st, const char* src) {
  installStandardLibrary(vm);
  installSceneApi(vm, st);
  bool ok = vm.load(src, "t.loop");
  for (auto& p : vm.problems()) std::printf("    %s\n", p.text().c_str());
  return ok;
}
}  // namespace

TEST(settings_layers_and_precedence) {
  Fresh f;
  Settings& s = Settings::get();
  CHECK(config().physics.iterations == 16);
  CHECK(s.set(Settings::kProject, "physics.iterations", Json(20)));
  CHECK(config().physics.iterations == 20);
  CHECK(s.set(Settings::kScene, "physics.iterations", Json(24)));
  CHECK(config().physics.iterations == 24);
  CHECK(s.set(Settings::kEditor, "physics.iterations", Json(30)));
  CHECK(config().physics.iterations == 30);
  // clearing the editor's change gives the scene's back, then the project's
  CHECK(s.set(Settings::kEditor, "physics.iterations", Json()));
  CHECK(config().physics.iterations == 24);
  s.clear(Settings::kScene);
  CHECK(config().physics.iterations == 20);
  // out of range is held to the range
  CHECK(s.set(Settings::kEditor, "physics.iterations", Json(9999)));
  CHECK(config().physics.iterations == 64);
  // kinds
  CHECK(s.set(Settings::kEditor, "physics.gravity", Json::array()) == false);
  Json g = Json::array();
  g.push(Json(0.0)), g.push(Json(-1.62)), g.push(Json(0.0));
  CHECK(s.set(Settings::kEditor, "physics.gravity", g));
  CHECK_NEAR(config().physics.gravity.y, -1.62, 1e-5);
  CHECK(s.set(Settings::kEditor, "story.speakerColor", Json("#ff0000")));
  CHECK(config().story.speakerColor == "#ff0000");
}

TEST(settings_unknown_names_suggest) {
  Fresh f;
  std::string err;
  CHECK(!Settings::get().set(Settings::kEditor, "physics.iteratons", Json(3), &err));
  CHECK(err.find("did you mean \"physics.iterations\"") != std::string::npos);
  CHECK(Settings::get().suggest("render.shadowDistanse") == "render.shadowDistance");
  // every setting is listed, with its default and value
  Json l = Settings::get().list();
  CHECK(l.size() == Settings::get().entries().size());
  CHECK(l.size() > 100);
}

TEST(settings_watchers_hear_changes) {
  Fresh f;
  std::vector<std::string> heard;
  int h = Settings::get().watch("render.", [&](const std::string& id) { heard.push_back(id); });
  Settings::get().set(Settings::kEditor, "render.msaa", Json(1));
  Settings::get().set(Settings::kEditor, "audio.master", Json(0.5));
  Settings::get().unwatch(h);
  Settings::get().set(Settings::kEditor, "render.fxaa", Json(true));
  CHECK(heard.size() == 1 && heard[0] == "render.msaa");
}

TEST(settings_script_words) {
  Fresh f;
  Vm vm;
  Stage st;
  CHECK(run(vm, st, R"(
setting "story.textSpeed", 30
settings {physics_iterations: 24, "audio.reverb": 1.5}
let speed = get_setting("story.textSpeed")
gravity vec(0, -1.62, 0)
let secs = line("Mira", "abcdefghijklmnopqrstuvwxyzabcd")
)"));
  CHECK_NEAR(config().story.textSpeed, 30, 1e-6);
  CHECK(config().physics.iterations == 24);
  CHECK_NEAR(config().audio.reverb, 1.5, 1e-6);
  CHECK_NEAR(config().physics.gravity.y, -1.62, 1e-5);
  // 30 letters at 30 a second, plus a second to take it in
  CHECK(!st.ui.empty());
  CHECK_NEAR(st.ui.back().seconds, 2.0, 1e-4);
  // the editor still wins over what the scene asked
  Settings::get().set(Settings::kEditor, "physics.iterations", Json(8));
  CHECK(config().physics.iterations == 8);
  // a new scene forgets the last scene's settings
  Vm vm2;
  Stage st2;
  CHECK(run(vm2, st2, "title \"next\"\n"));
  CHECK_NEAR(config().story.textSpeed, 15, 1e-6);
  CHECK_NEAR(config().physics.gravity.y, -9.81, 1e-5);
}

TEST(settings_script_errors_are_helpful) {
  Fresh f;
  Vm vm;
  Stage st;
  installStandardLibrary(vm);
  installSceneApi(vm, st);
  vm.load("setting \"physics.iteratons\", 3\n", "t.loop");
  CHECK(!vm.problems().empty());
  CHECK(vm.problems()[0].text().find("physics.iterations") != std::string::npos);
}

TEST(settings_reach_physics_and_scene_words) {
  Fresh f;
  Settings::get().set(Settings::kProject, "physics.friction", Json(0.1));
  Settings::get().set(Settings::kProject, "world.foliageDensity", Json(0.0));
  Vm vm;
  Stage st;
  CHECK(run(vm, st, R"(
let ball = sphere(radius: 0.2, at: vec(0, 3, 0))
solid ball
let trees = forest(kind: "pine", radius: 20)
look "natural", sharpen: 0.4, distortion: 0.1, grain_size: 2, tone: "aces", fringes: 1.5
sky "physical", ground: vec(0.1, 0.2, 0.3)
sun elevation: 30 deg, size: 2 deg
camera at: vec(0, 2, 5), look_at: vec(0, 1, 0), sensor: "s35", blades: 9
)"));
  CHECK_NEAR(st.world.body(0).d.friction, 0.1, 1e-6);
  CHECK(st.scene.instances.size() == 1);  // no trees at density 0
  CHECK_NEAR(st.scene.look.sharpen, 0.4, 1e-6);
  CHECK(st.scene.look.tonemap == Look::Tonemap::kAces);
  CHECK_NEAR(st.scene.sky.groundAlbedo.z, 0.3, 1e-6);
  CHECK_NEAR(st.scene.camera.sensor.x, 24.89, 1e-3);
  CHECK(st.scene.camera.blades == 9);
  bool sized = false;
  for (const Light& l : st.scene.lights)
    if (l.kind == Light::Kind::kSun) sized = std::fabs(l.radius - 0.01745f) < 1e-3f;
  CHECK(sized);
  // gravity and the solver follow the settings as the world steps
  Settings::get().set(Settings::kEditor, "physics.gravity", Json(0.0));
  st.update(0.5f);
  CHECK_NEAR(st.world.body(0).x.y, 3.0, 1e-3);
}

TEST(settings_audio_categories) {
  Fresh f;
  auto loudness = [](audio::Category c) {
    audio::Mixer m;
    m.play(audio::makeAmbience("rain", 1, 1), {}, 1, false, true, c);
    std::vector<float> buf(4800 * 2);
    m.render(buf.data(), 4800);
    double e = 0;
    for (float x : buf) e += x * x;
    return e;
  };
  double full = loudness(audio::Category::kAmbience);
  Settings::get().set(Settings::kEditor, "audio.ambience", Json(0.0));
  CHECK(loudness(audio::Category::kAmbience) < full * 1e-6);
  CHECK(loudness(audio::Category::kEffects) > full * 0.5);  // other sounds untouched
  Settings::get().set(Settings::kEditor, "audio.master", Json(0.0));
  CHECK(loudness(audio::Category::kEffects) < full * 1e-6);
}

// The live picture has no film grain unless asked for (it fizzed on the phone,
// 2026-11-05), while a scene's own grain stays in its look for stills.
TEST(settings_live_film_grain_is_off_by_default) {
  Fresh f;
  CHECK(!config().render.filmGrain);
  Vm vm;
  Stage st;
  CHECK(run(vm, st, "look \"noir\", grain: 0.05\nsetting \"render.filmGrain\", yes\n"));
  CHECK(std::fabs(st.scene.look.grain - 0.05f) < 1e-6f);
  CHECK(config().render.filmGrain);
}
