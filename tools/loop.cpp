// `loop`: the engine's command line.
//   loop check  <script.loop>                 read a script and report problems
//   loop run    <script.loop> [--seconds N]   run it (no picture) and print what it says
//   loop render <script.loop> [--quality q] [--width W] [--samples N] [--look L]
//               makes the stills and films the script asks for (render / film)
//               with the cinema renderer; with none asked, one still of its camera
//   loop demo   <studio|valley> [...]         the renderer's reference scenes
#include <cstdio>
#include <cstring>
#include <string>
#include <sys/stat.h>

#include "engine/core/file.h"
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
      "loop render <script.loop> [--quality draft|good|cinema|ultra] [--width 1920] [--samples N] [--look name]\n"
      "loop demo   <studio|valley> [--look name] [--size 1280x720] [--samples 64] [-o out.png]");
  return 1;
}

struct Options {
  std::string quality, look, out;
  int width = 0, samples = 0, w = 960, h = 540;
  double seconds = 10;
};

void printProblems(const std::vector<Problem>& ps) {
  for (const Problem& p : ps) std::fprintf(stderr, "problem: %s\n", p.text().c_str());
}

bool loadScript(const std::string& path, Vm& vm, Stage& stage) {
  std::string src;
  if (!readText(path, src)) {
    std::fprintf(stderr, "can't read %s\n", path.c_str());
    return false;
  }
  installStandardLibrary(vm);
  installSceneApi(vm, stage);
  vm.onSay = [](const std::string& s) { std::printf("%s\n", s.c_str()); };
  bool ok = vm.load(src, path);
  printProblems(vm.problems());
  return ok;
}

Image renderStill(const Scene& scene, const Camera& cam, int w, int h, int samples, uint32_t frame, const std::string& label) {
  Scene shot = scene;  // the camera may differ per frame
  shot.camera = cam;
  PathTracer pt;
  pt.setScene(shot);
  RenderSettings rs;
  rs.width = w, rs.height = h, rs.samples = samples, rs.seed = frame + 1;
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
      Image pic = renderStill(stage.scene, cam, req.width, req.height, req.samples, 0, req.file);
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
      Image pic = renderStill(sc, cam, req.width, req.height, req.samples, static_cast<uint32_t>(f), name);
      if (fade < 1)
        for (auto& p : pic.px) p = Vec4(p.xyz() * fade, 1);
      writePng(name, pic, true);
    }
    std::printf("wrote %d frames to %s/ (%g fps)\n", frames, req.file.c_str(), stage.film.fps);
  }
  return 0;
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
    else return usage();
  }
  if (cmd == "demo") return demo(target, o);
  if (cmd == "check") {
    std::string src;
    if (!readText(target, src)) return std::fprintf(stderr, "can't read %s\n", target.c_str()), 1;
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
    std::printf("%zu things, %zu lights, %zu shots (%.1f s)\n", stage.scene.instances.size(), stage.scene.lights.size(), stage.film.shots.size(),
                stage.film.length());
    return vm.problems().empty() ? 0 : 1;
  }
  if (cmd == "render") return renderScript(target, o);
  return usage();
}
