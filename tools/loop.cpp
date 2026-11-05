// `loop`: the engine's command line.
//   loop check  <script.loop>                 read a script and report problems
//   loop run    <script.loop> [--seconds N]   run it (no picture) and print what it says
//   loop play   <first.loop> [--choices 0,1]  play a story through every go_to, no picture:
//               it presses Use, picks the choices and prints every line
//   loop render <script.loop> [--quality q] [--width W] [--samples N] [--look L]
//               makes the stills and films the script asks for (render / film)
//               with the cinema renderer; with none asked, one still of its camera
//   loop demo   <studio|valley> [...]         the renderer's reference scenes
#include <cstdio>
#include <algorithm>
#include <cstring>
#include <string>
#include <sys/stat.h>

#include "engine/core/file.h"
#include "engine/core/json.h"
#include "engine/core/settings.h"
#include "engine/core/time.h"
#include "engine/render/demo_scenes.h"
#include "engine/render/film.h"
#include "engine/render/pathtracer.h"
#include "engine/script/scene_api.h"
#include "engine/script/vm.h"

using namespace loop;
using namespace loop::script;

namespace {

int usage() {
  std::puts(
      "loop check  <script.loop>\n"
      "loop run    <script.loop> [--seconds 10]\n"
      "loop play   <first scene.loop> [--choices 0,1,0]\n"
      "loop render <script.loop> [--quality draft|good|cinema|ultra] [--width 1920] [--samples N] [--look name]\n"
      "loop demo   <studio|valley> [--look name] [--size 1280x720] [--samples 64] [-o out.png]\n"
      "loop settings list    every engine setting, its default and range (JSON)\n"
      "loop words <json|js>  every word of the language and its help");
  return 1;
}

struct Options {
  std::string quality, look, out, choices;
  int width = 0, samples = 0, w = 960, h = 540;
  double seconds = 10;
};

void printProblems(const std::vector<Problem>& ps) {
  for (const Problem& p : ps) std::fprintf(stderr, "problem: %s\n", p.text().c_str());
}

// `use "common"` lines bring in another script from the same folder (once).
std::string expandUses(const std::string& src, const std::string& dir, std::vector<std::string>& seen) {
  std::string out, line;
  size_t i = 0;
  while (i <= src.size()) {
    size_t e = src.find('\n', i);
    if (e == std::string::npos) e = src.size();
    line = src.substr(i, e - i);
    size_t a = line.find_first_not_of(" \t");
    if (a != std::string::npos && line.compare(a, 5, "use \"") == 0) {
      size_t q = line.find('"', a + 5);
      std::string name = line.substr(a + 5, q - a - 5);
      std::string file = joinPath(dir, name + ".loop"), text;
      if (std::find(seen.begin(), seen.end(), file) == seen.end()) {
        seen.push_back(file);
        if (readText(file, text)) out += expandUses(text, dir, seen) + "\n";
        else out += "say \"can't find " + name + ".loop\"\n";
      }
      out += "\n";
    } else out += line + "\n";
    i = e + 1;
  }
  return out;
}

// A scene's project (project.json in its folder) brings its settings:
// "settings": {"cinema.quality": "cinema", "physics.iterations": 24, …}
void loadProjectSettings(const std::string& scenePath) {
  std::string text;
  if (!readText(joinPath(directoryOf(scenePath), "project.json"), text)) return;
  Json p;
  std::string err;
  if (!Json::parse(text, p, &err)) {
    std::fprintf(stderr, "project.json: %s\n", err.c_str());
    return;
  }
  Settings::get().clear(Settings::kProject);
  if (p.has("settings") && !Settings::get().setMany(Settings::kProject, p["settings"], &err)) std::fprintf(stderr, "project.json settings: %s\n", err.c_str());
  if (p.has("mode")) Settings::get().set(Settings::kProject, "project.mode", p["mode"]);
}

bool loadScript(const std::string& path, Vm& vm, Stage& stage) {
  loadProjectSettings(path);
  std::string src;
  if (!readText(path, src)) {
    std::fprintf(stderr, "can't read %s\n", path.c_str());
    return false;
  }
  std::vector<std::string> seen;
  src = expandUses(src, directoryOf(path), seen);
  installStandardLibrary(vm);
  installSceneApi(vm, stage);
  vm.onSay = [](const std::string& s) { std::printf("%s\n", s.c_str()); };
  bool ok = vm.load(src, path);
  printProblems(vm.problems());
  return ok;
}

Image renderStill(const Scene& scene, const Camera& cam, int w, int h, int samples, uint32_t frame, const std::string& label,
                  const RenderRequest* req = nullptr) {
  Scene shot = scene;  // the camera may differ per frame
  shot.camera = cam;
  if (req && !req->motionBlur) shot.camera.positionEnd = Vec3(kInf);
  PathTracer pt;
  pt.setScene(shot);
  RenderSettings rs;
  rs.width = w, rs.height = h, rs.samples = samples, rs.seed = frame + 1;
  if (req) rs.maxBounces = req->bounces, rs.denoise = req->denoise, rs.dispersion = req->dispersion;
  rs.progress = [&](float p) {
    std::fprintf(stderr, "\r%s %3d%%", label.c_str(), static_cast<int>(p * 100));
    return true;
  };
  RenderOutput r = pt.render(rs);
  std::fprintf(stderr, "\r%s %.1f s, %.1f M rays/s\n", label.c_str(), r.seconds, r.rays / std::max(r.seconds, 1e-6) / 1e6);
  return develop(r.color, shot.camera, shot.look, frame);
}

int renderScript(const std::string& path, const Options& o) {
  Vm vm;
  Stage stage;
  if (!loadScript(path, vm, stage)) return 1;
  if (!o.look.empty() && !lookByName(o.look, stage.scene.look)) {
    std::fprintf(stderr, "unknown look %s\n", o.look.c_str());
    return 1;
  }
  auto adjust = [&](RenderRequest r) {
    if (!o.quality.empty()) r.samples = o.quality == "draft" ? 16 : o.quality == "good" ? 128 : o.quality == "cinema" ? 1024 : 4096;
    if (o.samples) r.samples = o.samples;
    if (o.width) r.height = r.height * o.width / r.width, r.width = o.width;
    return r;
  };
  std::vector<RenderRequest> todo = stage.renders;
  if (todo.empty()) {
    RenderRequest r;
    std::string base = path.substr(path.find_last_of('/') + 1);
    r.file = base.substr(0, base.find('.')) + ".png";
    r.samples = 64, r.width = 1280, r.height = 720;
    const Config::Cinema& cc = config().cinema;
    r.bounces = cc.bounces, r.denoise = cc.denoise, r.dispersion = cc.dispersion, r.motionBlur = cc.motionBlur;
    if (cc.width > 0) r.width = cc.width, r.height = cc.height > 0 ? cc.height : cc.width * 9 / 16;
    todo.push_back(r);
  }
  for (const RenderRequest& req0 : todo) {
    RenderRequest req = adjust(req0);
    if (!req.film) {
      while (vm.time() + 1e-9 < req.at) {
        double step = std::min(1.0 / 60, req.at - vm.time());
        vm.update(step);
        stage.update(static_cast<float>(step));
      }
      Camera cam = stage.film.shots.empty() ? stage.scene.camera : stage.film.at(req.at, stage.scene.camera);
      Image pic = renderStill(stage.scene, cam, req.width, req.height, req.samples, 0, req.file, &req);
      writePng(req.file, pic, true);
      std::printf("wrote %s\n", req.file.c_str());
      continue;
    }
    // a film: every frame of the shots, the script's time moving with it
    mkdir(req.file.c_str(), 0755);
    float len = stage.film.length();
    int frames = std::max(1, static_cast<int>(std::ceil(len * stage.film.fps)));
    double dt = 1.0 / stage.film.fps;
    for (int f = 0; f < frames; ++f) {
      double t = f * dt;
      while (vm.time() + 1e-9 < t) {
        double step = std::min(1.0 / 60, t - vm.time());
        vm.update(step);
        stage.update(static_cast<float>(step));
      }
      int shotIndex = 0;
      float fade = 1;
      Camera cam = stage.film.at(static_cast<float>(t), stage.scene.camera, &shotIndex, &fade);
      Scene sc = stage.scene;
      const Shot& sh = stage.film.shots[shotIndex];
      if (!sh.look.empty()) lookByName(sh.look, sc.look);
      char name[256];
      std::snprintf(name, sizeof name, "%s/frame_%05d.png", req.file.c_str(), f + 1);
      Image pic = renderStill(sc, cam, req.width, req.height, req.samples, static_cast<uint32_t>(f), name, &req);
      if (fade < 1)
        for (auto& p : pic.px) p = Vec4(p.xyz() * fade, 1);
      writePng(name, pic, true);
    }
    std::printf("wrote %d frames to %s/ (%g fps)\n", frames, req.file.c_str(), stage.film.fps);
  }
  return 0;
}

// Plays a story from its first scene to its last, with no picture: Use is
// pressed every half second, questions are answered (from --choices, then
// the first option) and `go_to` loads the next file from the same folder.
int playStory(const std::string& first, const Options& o) {
  std::vector<int> picks;
  for (size_t i = 0; i < o.choices.size();) {
    size_t e = o.choices.find(',', i);
    if (e == std::string::npos) e = o.choices.size();
    picks.push_back(std::atoi(o.choices.substr(i, e - i).c_str()));
    i = e + 1;
  }
  size_t asked = 0;
  std::string path = first, dir = directoryOf(first);
  int scenes = 0, failures = 0;
  double total = 0;
  while (!path.empty() && scenes < 200) {
    Vm vm;
    Stage stage;
    stage.autoplay = true;
    if (!loadScript(path, vm, stage)) return 1;
    ++scenes;
    std::printf("\n==== %s  (%s, %s)\n", stage.title.c_str(), path.c_str(), stage.mode.c_str());
    double t = 0, nextUse = 0.5, answerAt = -1;
    int answer = 0;
    const double limit = std::max(900.0, o.seconds);
    while (t < limit && stage.nextScene.empty()) {
      vm.update(1.0 / 60), stage.update(1.0f / 60), t += 1.0 / 60;
      for (const Stage::UiMessage& m : stage.ui) {
        if (m.kind == "line" && !m.text.empty())
          std::printf(m.speaker.empty() ? "  %6.1f  %s%s\n" : "  %6.1f  %s: %s\n", t, m.speaker.c_str(), m.text.c_str());
        if (m.kind == "ask") {
          answer = asked < picks.size() ? picks[asked] : 0;
          answer = std::clamp(answer, 0, static_cast<int>(m.options.size()) - 1);
          ++asked;
          std::printf("   ? %s\n", m.text.c_str());
          for (size_t i = 0; i < m.options.size(); ++i) std::printf("     %s %s\n", static_cast<int>(i) == answer ? "->" : "  ", m.options[i].c_str());
          answerAt = t + 0.5;
        }
      }
      stage.ui.clear();
      if (answerAt >= 0 && t >= answerAt) vm.emit("choice", Value::number(answer)), answerAt = -1;
      if (t >= nextUse) vm.emit("use"), nextUse = t + 0.5;
    }
    printProblems(vm.problems());
    failures += static_cast<int>(vm.problems().size());
    total += t;
    if (stage.nextScene.empty()) {
      std::printf("==== the end (%.0f s in this scene)\n", t);
      if (t >= limit) std::printf("     (stopped after %.0f s without a go_to)\n", limit);
      break;
    }
    if (stage.film.shots.empty()) std::printf("     -> %s  (after %.0f s)\n", stage.nextScene.c_str(), t);
    else std::printf("     -> %s  (after %.0f s; the shots last %.0f s)\n", stage.nextScene.c_str(), t, stage.film.length());
    path = joinPath(dir, stage.nextScene + ".loop");
    if (path == first || stage.nextScene + ".loop" == first.substr(first.find_last_of('/') + 1)) {
      std::printf("==== the end (the story goes back to its start)\n");
      break;
    }
  }
  std::printf("\n%d scenes, %.1f minutes, %d problems\n", scenes, total / 60, failures);
  return failures ? 1 : 0;
}

int demo(const std::string& name, const Options& o) {
  Scene scene;
  if (!buildDemoScene(name, scene)) return usage();
  if (!o.look.empty() && !lookByName(o.look, scene.look)) return usage();
  Image pic = renderStill(scene, scene.camera, o.w, o.h, o.samples ? o.samples : 32, 0, name);
  std::string out = o.out.empty() ? name + ".png" : o.out;
  writePng(out, pic, true);
  std::printf("wrote %s\n", out.c_str());
  return 0;
}

}  // namespace

int main(int argc, char** argv) {
  if (argc < 3) return usage();
  std::string cmd = argv[1], target = argv[2];
  Options o;
  for (int i = 3; i < argc; ++i) {
    std::string a = argv[i];
    auto val = [&] { return i + 1 < argc ? std::string(argv[++i]) : std::string(); };
    if (a == "--quality") o.quality = val();
    else if (a == "--look") o.look = val();
    else if (a == "--width") o.width = std::atoi(val().c_str());
    else if (a == "--samples") o.samples = std::atoi(val().c_str());
    else if (a == "--seconds") o.seconds = std::atof(val().c_str());
    else if (a == "--size") std::sscanf(val().c_str(), "%dx%d", &o.w, &o.h);
    else if (a == "-o") o.out = val();
    else if (a == "--choices") o.choices = val();
    else return usage();
  }
  if (cmd == "demo") return demo(target, o);
  if (cmd == "words") {
    // `loop words js` writes web/editor/words.js: every word of Looped Script and its help
    Vm vm;
    Stage stage;
    installStandardLibrary(vm);
    installSceneApi(vm, stage);
    Json words = Json::object();
    for (auto& [name, help] : vm.words()) words.set(name, Json(help));
    std::string body = words.dump(0);
    if (target == "js")
      std::printf("// Every word of Looped Script and its help, from the engine itself.\n// Made by `loop words js > web/editor/words.js`: don't edit by hand.\nwindow.LOOP_WORDS = %s;\n", body.c_str());
    else std::printf("%s\n", words.dump(1).c_str());
    return 0;
  }
  if (cmd == "settings") {
    // `loop settings list` prints every engine setting as JSON (for the editor's checks and the docs)
    if (target != "list") return usage();
    std::printf("%s\n", Settings::get().list().dump(1).c_str());
    return 0;
  }
  if (cmd == "check") {
    std::string src;
    if (!readText(target, src)) return std::fprintf(stderr, "can't read %s\n", target.c_str()), 1;
    std::vector<std::string> seen;
    src = expandUses(src, directoryOf(target), seen);
    std::vector<Problem> ps;
    bool ok = Vm::check(src, target, ps);
    printProblems(ps);
    if (ok) std::printf("%s reads fine\n", target.c_str());
    return ok ? 0 : 1;
  }
  if (cmd == "run") {
    Vm vm;
    Stage stage;
    if (!loadScript(target, vm, stage)) return 1;
    for (double t = 0; t < o.seconds; t += 1.0 / 60) vm.update(1.0 / 60), stage.update(1.0f / 60);
    printProblems(vm.problems());
    size_t tris = 0;
    for (const MeshInstance& in : stage.scene.instances)
      if (in.mesh >= 0 && in.mesh < static_cast<int>(stage.scene.meshes.size())) tris += stage.scene.meshes[in.mesh].triangleCount();
    std::printf("%zu things, %zu lights, %zu shots (%.1f s), %.2f M triangles drawn, %zu shapes\n", stage.scene.instances.size(), stage.scene.lights.size(),
                stage.film.shots.size(), stage.film.length(), tris / 1e6, stage.scene.meshes.size());
    return vm.problems().empty() ? 0 : 1;
  }
  if (cmd == "render") return renderScript(target, o);
  if (cmd == "play") return playStory(target, o);
  return usage();
}
