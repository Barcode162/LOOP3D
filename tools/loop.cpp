// `loop`: the engine's command line.
//   loop check  <script.loop>                 read a script and report problems
//   loop run    <script.loop> [--seconds N]   run it (no picture) and print what it says
//   loop play   <first.loop> [--choices 0,1]  play a story through every go_to, no picture:
//               it presses Use, picks the choices and prints every line
//   loop render <script.loop> [--quality q] [--width W] [--samples N] [--look L]
//               makes the stills and films the script asks for (render / film)
//               with the cinema renderer; with none asked, one still of its camera
//   loop demo   <studio|valley> [...]         the renderer's reference scenes
//   loop score  <script.loop> <live.png> [--filmtime T] [--samples N] [-o folder]
//   loop score  tests/pictures [--samples N] [-o folder]
//               how far the live picture is from the cinema renderer's picture
//               of the same view (engine/render/score.h): one number per scene
//   loop research cascades                    radiance cascades against the light cache (phase 9)
//   loop project <name.looped>                a project's cover page as JSON (its name and first scene)
//   loop new "My Game"                        a new project in projects/, copied from projects/template
//   loop cook   <script.loop>                 what the cook makes of a scene's shapes: each mesh's
//               cluster tree (Nanite-style levels), how long it took
//   loop library list|check|json              the library (library/): its entries; check refuses
//               an entry with no credit or licence; json is for the site and the editor
//   loop library picture <name> [-o out.png] [--seed n]
//               one entry placed on a neutral floor under a soft sky and one sun,
//               seen three-quarter at 35 mm from its size, 512 by 512, quality good
//   Every command that takes a scene also takes a project's cover page (.looped):
//   it opens the project's first scene.
//   loop budget <script.loop> [--quality phone|high] [--seconds S] [--size WxH]
//               what one live frame costs (things, triangles, draw calls,
//               surfaces and lamps per pixel) against that quality's budget
#include <cstdio>
#include <algorithm>
#include <cstring>
#include <filesystem>
#include <string>
#include <sys/stat.h>

#include "engine/core/file.h"
#include "engine/core/json.h"
#include "engine/core/settings.h"
#include "engine/core/time.h"
#include "engine/core/jobs.h"
#include "engine/geometry/cook.h"
#include "engine/project/library.h"
#include "engine/project/project.h"
#include "engine/render/demo_scenes.h"
#include "engine/render/film.h"
#include "engine/render/frame_cost.h"
#include "engine/render/pathtracer.h"
#include "engine/geometry/world_gen.h"
#include "engine/render/light_cache.h"
#include "engine/render/radiance_cascades.h"
#include "engine/geometry/mesh.h"
#include "engine/core/random.h"
#include "engine/render/score.h"
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
      "loop budget <script.loop or folder> [--quality phone|high] [--seconds 1] [--size 374x811] [--set name=value]\n"
      "loop score  <script.loop> <live.png> [--filmtime T] [--samples 256] [-o folder]   how far live is from cinema\n"
      "loop score  tests/pictures [--samples 256] [-o folder] [--reuse]   every picture test's scene, scored (--reuse: the folder's saved cinema pictures)\n"
      "loop research cascades  phase 9: radiance cascades against the light cache (error, rays)\n"
      "loop world <seed> [-o map.png] [--cells 257]   a generated world's map (plates, rivers, climate, ground)\n"
      "loop library list|check|json|picture <name> [-o out.png] [--seed n]   the library; one entry's picture\n"
      "loop settings list   every engine setting, its default and range (JSON)\n"
      "loop words <json|js>  every word of the language and its help\n"
      "loop project <name.looped>  a project's cover page as JSON\n"
      "loop new \"My Game\"         a new project in projects/, copied from projects/template\n"
      "(a project's cover page, name.looped, can stand in for a scene: its first scene opens)");
  return 1;
}

struct Options {
  bool reuse = false;  // loop score --reuse: the -o folder's saved cinema pictures
  std::string quality, look, out, choices, name;  // name: loop library picture <name>
  int seed = 0;
  int width = 0, samples = 0, w = 960, h = 540, worldCells = 257;
  double seconds = 10, filmtime = 0;
  bool secondsGiven = false, sizeGiven = false;
  std::string live;  // loop score: the live picture
  std::vector<std::pair<std::string, std::string>> sets;  // --set name=value (engine settings)
};

void printProblems(const std::vector<Problem>& ps) {
  for (const Problem& p : ps) std::fprintf(stderr, "problem: %s\n", p.text().c_str());
}


// A scene's project (project.json in its folder) brings its settings:
// "settings": {"cinema.quality": "cinema", "physics.iterations": 24, …}
void loadProjectSettings(const std::string& scenePath) {
  // a project with a cover page (<folder>/<folder>.looped, the scene beside it
  // or in its places/): its settings
  std::string folder = directoryOf(scenePath);
  auto coverIn = [](std::string dir) {
    std::string name = dir;
    while (!name.empty() && name.back() == '/') name.pop_back();
    name = name.substr(name.find_last_of('/') + 1);
    std::string cover = joinPath(dir, name + ".looped");
    struct stat sb;
    return !name.empty() && stat(cover.c_str(), &sb) == 0 ? cover : std::string();
  };
  std::string cover = coverIn(folder);
  if (cover.empty() && folder.size() >= 7 && folder.compare(folder.size() - 7, 7, "places/") == 0) cover = coverIn(folder.substr(0, folder.size() - 7));
  if (!cover.empty()) {
    Project pr;
    std::vector<ProjectProblem> ps;
    Settings::get().clear(Settings::kProject);
    if (!readProject(cover, pr, ps)) {
      for (const ProjectProblem& p : ps) std::fprintf(stderr, "problem: %s\n", p.message(cover).c_str());
      return;
    }
    for (const Project::Setting& s : pr.settings) Settings::get().set(Settings::kProject, s.name, s.value);
    return;
  }
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

// Loads scene text that is already read: `folder` is where its `use` lines
// look first (then the library); `label` names it in problems.
bool loadSource(std::string src, const std::string& folder, const std::string& label, Vm& vm, Stage& stage) {
  std::vector<std::string> seen;
  src = expandUses(src, folder, seen);
  installStandardLibrary(vm);
  installSceneApi(vm, stage);
  stage.folder = folder;
  vm.onSay = [](const std::string& s) { std::printf("%s\n", s.c_str()); };
  bool ok = vm.load(src, label);
  printProblems(vm.problems());
  std::string why;
  if (stage.decodePictures(&why)) std::fprintf(stderr, "problem: %s\n", why.c_str());
  return ok;
}

bool loadScript(const std::string& path, Vm& vm, Stage& stage) {
  loadProjectSettings(path);
  std::string src;
  if (!readText(path, src)) {
    std::fprintf(stderr, "can't read %s\n", path.c_str());
    return false;
  }
  return loadSource(src, directoryOf(path), path, vm, stage);
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

// `loop library picture <name>`: the entry's `place:` line on a neutral floor
// under a soft sky and one sun. The thing is placed once with no camera to
// measure it; then the camera is put three-quarter, 35 mm, far enough back that
// the thing fills about two thirds of the picture's width or height.
int libraryPicture(const LibraryEntry& e, const std::string& libFolder, const Options& o) {
  // the entry's `AT` stands for the middle of the floor
  std::string place;
  auto isWord = [](char c) { return std::isalnum(static_cast<unsigned char>(c)) || c == '_'; };
  for (size_t i = 0; i < e.place.size();) {
    bool word = isWord(e.place[i]);
    size_t j = i;
    while (j < e.place.size() && isWord(e.place[j]) == word) ++j;
    std::string piece = e.place.substr(i, j - i);
    place += word && piece == "AT" ? "vec(0, 0, 0)" : piece;
    i = j;
  }
  std::string head = "use \"" + e.name + "\"\n";
  if (o.seed) head += "seed " + std::to_string(o.seed) + "\n";
  // the folder holding library/ (its `use` lines find the entry there)
  std::string folder = libFolder;
  while (folder.size() > 1 && folder.back() == '/') folder.pop_back();
  folder = directoryOf(folder);
  auto bounds = [&](const Stage& st) {
    AABB b;
    for (const MeshInstance& in : st.scene.instances) {
      const AABB mb = st.scene.meshes[in.mesh].bounds();
      if (!mb.valid()) continue;
      AABB ib;
      for (int c = 0; c < 8; ++c) ib.grow(in.transform.point(Vec3(c & 1 ? mb.hi.x : mb.lo.x, c & 2 ? mb.hi.y : mb.lo.y, c & 4 ? mb.hi.z : mb.lo.z)));
      Vec3 ex = ib.extent();
      if (std::max({ex.x, ex.y, ex.z}) > 1e-5f) b.grow(ib);  // a hidden part is scaled to nothing
    }
    return b;
  };
  AABB box;
  {
    Vm vm;
    Stage stage;
    if (!loadSource(head + place + "\n", folder, e.name, vm, stage)) return 1;
    box = bounds(stage);
  }
  if (!box.valid()) {
    std::fprintf(stderr, "problem: %s: its place line makes nothing to see\n", e.name.c_str());
    return 1;
  }
  // three-quarter: from the front-right, a little above
  const Vec3 centre = box.center();
  const Vec3 dir = normalize(Vec3(0.6f, 0.4f, 0.7f));
  const Vec3 right = normalize(cross(Vec3(0, 1, 0), dir)), up = cross(dir, right);
  Camera cam;
  cam.focalLength = 35;
  const float t = std::tan(cam.fovY() * 0.5f) * (2.0f / 3.0f);
  float dist = 0;
  for (int c = 0; c < 8; ++c) {  // every corner must land inside the two thirds
    Vec3 p = Vec3(c & 1 ? box.hi.x : box.lo.x, c & 2 ? box.hi.y : box.lo.y, c & 4 ? box.hi.z : box.lo.z) - centre;
    dist = std::max({dist, std::fabs(dot(p, right)) / t + dot(p, dir), std::fabs(dot(p, up)) / t + dot(p, dir)});
  }
  cam.position = centre + dir * dist, cam.target = centre;
  const float floorSize = std::max(200.0f, 40 * length(box.extent()));
  char setting[256];
  std::snprintf(setting, sizeof setting, "ground %g, material: rgb(0.5, 0.5, 0.5)\nsun elevation: 40 deg, toward: 215 deg, lux: 60000\nsky \"physical\", haze: 1.5\n", floorSize);
  Vm vm;
  Stage stage;
  if (!loadSource(head + setting + place + "\n", folder, e.name, vm, stage)) return 1;
  int samples = o.samples ? o.samples : o.quality == "draft" ? 16 : o.quality == "cinema" ? 1024 : 128;
  RenderRequest req;
  const Config::Cinema& cc = config().cinema;
  req.bounces = cc.bounces, req.denoise = cc.denoise, req.dispersion = cc.dispersion, req.motionBlur = false;
  Image pic = renderStill(stage.scene, cam, 512, 512, samples, 0, e.name, &req);
  std::string out = o.out.empty() ? e.name + ".png" : o.out;
  writePng(out, pic, true);
  std::printf("wrote %s\n", out.c_str());
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

// `loop budget`: what one live frame of a scene costs on a quality, counted the
// way the live renderer draws it today, with what skipping off-screen things
// and the backs of shapes would leave, against that quality's budget. A folder
// (a project, or projects/) gives one line per scene.
struct Measured {
  std::string title, name;
  FrameCost cost;
  FrameBudget budget;
  FrameView view;
  size_t lamps = 0;
  // the shapes with the most triangles in the whole scene (triangles x copies)
  struct Heavy {
    std::string material;
    size_t triangles = 0, copies = 0;
  };
  std::vector<Heavy> heaviest;
};

bool measureScene(const std::string& path, const Options& o, Measured& m) {
  Vm vm;
  Stage stage;
  if (!loadScript(path, vm, stage)) return false;
  // --set: engine settings over the project's and the scene's (as the editor's are)
  for (auto& [name, value] : o.sets) {
    Json v;
    if (!Json::parse(value, v)) v = Json(value);
    if (!Settings::get().set(Settings::kEditor, name, v)) std::fprintf(stderr, "--set: no setting %s, or a value it can't take\n", name.c_str());
  }
  double secs = o.secondsGiven ? o.seconds : 1;
  for (double t = 0; t < secs; t += 1.0 / 60) vm.update(1.0 / 60), stage.update(1.0f / 60);
  m.budget = budgetFor(o.quality == "high" ? "high" : "phone");
  const Config& C = config();
  FrameView& v = m.view;
  v.camera = stage.scene.camera;
  if (stage.mode == "movie" && !stage.film.shots.empty()) v.camera = stage.film.at(static_cast<float>(secs), stage.scene.camera);
  v.width = o.sizeGiven ? o.w : m.budget.width, v.height = o.sizeGiven ? o.h : m.budget.height;
  v.drawDistance = C.world.drawDistance, v.maxLights = C.render.maxLights;
  v.cullOffscreen = C.render.cull;
  v.lodBias = C.world.lodBias;
  v.portraitWide = C.camera.portraitWide;
  v.clusters = C.render.clusters, v.clusterError = C.render.clusterError;
  v.lightGrid = C.render.lightGrid, v.lampGrouping = C.render.lampGrouping;
  {
    int lamps = 0;
    for (const Light& l : stage.scene.lights) lamps += l.kind != Light::Kind::kSun;
    v.prepass = prepassOn(C.render.prepass, std::min(lamps, std::max(C.render.maxLights, 1)));
  }
  v.shadowRadius = C.render.shadowDistance > 0 ? C.render.shadowDistance : m.budget.quality == "high" ? 40.0f : 25.0f;
  m.cost = measureFrame(stage.scene, v);
  m.title = stage.title;
  {
    const Scene& sc = stage.scene;
    std::vector<Measured::Heavy> per(sc.meshes.size());
    for (const MeshInstance& in : sc.instances) {
      if (in.mesh < 0 || in.mesh >= static_cast<int>(sc.meshes.size())) continue;
      Measured::Heavy& h = per[in.mesh];
      h.triangles = sc.meshes[in.mesh].triangleCount(), ++h.copies;
      if (h.material.empty() && in.material >= 0 && in.material < static_cast<int>(sc.materials.size())) h.material = sc.materials[in.material].name;
    }
    std::sort(per.begin(), per.end(), [](auto& a, auto& b) { return a.triangles * a.copies > b.triangles * b.copies; });
    for (size_t i = 0; i < per.size() && i < 5 && per[i].copies; ++i) m.heaviest.push_back(per[i]);
  }
  for (const Light& l : stage.scene.lights) m.lamps += l.kind != Light::Kind::kSun;
  return true;
}

int budgetScene(const std::string& path, const Options& o) {
  Measured m;
  if (!measureScene(path, o, m)) return 1;
  const FrameCost& c = m.cost;
  std::string q = m.budget.quality == "high" ? "High" : "Phone";
  std::printf("%s · %s · %d x %d pixels · %zu things (%zu hidden) · %zu lamps%s\n", m.title.c_str(), q.c_str(), m.view.width, m.view.height,
              c.things, c.hidden, m.lamps, c.sun ? " · sun" : "");
  if (c.clustered)
    std::printf("  drawn in clusters             %zu + %zu clusters (scene + shadow); the copies hold %.2f M triangles at full detail, %.2f M drawn\n",
                c.sceneClusters, c.shadowClusters, static_cast<double>(c.fullTriangles) / 1e6, static_cast<double>(c.sceneTriangles) / 1e6);
  std::printf("                                today          skipping what's off screen\n");
  std::printf("  things drawn (scene + shadow) %6zu + %-6zu  %6zu + %zu\n", c.sceneThings, c.shadowThings, c.inViewThings, c.inShadowThings);
  std::printf("  triangles (all passes)        %8.2f M      %8.2f M\n", static_cast<double>(c.triangles()) / 1e6,
              static_cast<double>(c.inViewTriangles + c.inShadowTriangles) / 1e6);
  std::printf("  draw calls                    %8zu        %8zu\n", c.draws(), c.inViewDraws + c.inShadowDraws);
  std::printf("  surfaces behind a pixel       %8.1f        (%.1f facing the camera)\n", static_cast<double>(c.layers), static_cast<double>(c.frontLayers));
  std::printf("  surfaces shaded per pixel     %8.2f        %8.2f with the backs of shapes skipped\n", static_cast<double>(c.shadedPerPixel),
              static_cast<double>(c.shadedFrontsOnly));
  std::printf("  lamps worked out per surface  %8.1f      lamps giving 95%% of the light: %.1f on average, %d at most (99%%: %.1f)\n",
              static_cast<double>(c.lampsComputed), static_cast<double>(c.lampsThatMatter), c.lampsThatMatterMost, static_cast<double>(c.lampsFor99));
  std::printf("  lamp work per pixel           %8.0f        %8.0f if each pixel added only the lamps that matter\n",
              static_cast<double>(c.lampWorkPerPixel()), static_cast<double>(c.shadedFrontsOnly * c.lampsThatMatter));
  if (c.lodCopies[0] + c.lodCopies[1] + c.lodCopies[2])
    std::printf("  copies with simpler versions  full %zu (%.2f M) · simpler %zu (%.2f M) · simplest %zu (%.2f M)\n", c.lodCopies[0],
                static_cast<double>(c.lodTriangles[0]) / 1e6, c.lodCopies[1], static_cast<double>(c.lodTriangles[1]) / 1e6, c.lodCopies[2],
                static_cast<double>(c.lodTriangles[2]) / 1e6);
  std::printf("  sky                           %7.0f%%\n", static_cast<double>(1 - c.coverage) * 100);
  std::printf("  computed patterns (wood...)   %7.0f%% of the surfaces seen\n", static_cast<double>(c.patternShare) * 100);
  std::printf("  heaviest shapes in the scene (triangles x copies):\n");
  for (const auto& h : m.heaviest)
    std::printf("    %-24s %8zu x %-5zu = %6.2f M\n", h.material.c_str(), h.triangles, h.copies, static_cast<double>(h.triangles * h.copies) / 1e6);
  std::vector<std::string> over = overBudget(c, m.budget);
  if (over.empty()) {
    std::printf("fits the %s budget\n", q.c_str());
    return 0;
  }
  std::printf("over the %s budget:\n", q.c_str());
  for (const std::string& s : over) std::printf("  %s\n", s.c_str());
  return 2;
}

// Every scene of every project.json in a folder (the folder itself, or one
// level down), one line each.
int budgetFolder(const std::string& dir, const Options& o) {
  namespace fs = std::filesystem;
  std::vector<fs::path> manifests;
  if (fs::exists(fs::path(dir) / "project.json")) manifests.push_back(fs::path(dir) / "project.json");
  else
    for (const auto& e : fs::directory_iterator(dir))
      if (e.is_directory() && fs::exists(e.path() / "project.json")) manifests.push_back(e.path() / "project.json");
  std::sort(manifests.begin(), manifests.end());
  std::printf("%-28s %15s %15s %11s %7s %13s %11s  %s\n", "scene", "things drawn", "triangles (M)", "draw calls", "shaded", "lamps (95%)",
              "lamp work", "over the budget on");
  std::printf("%-28s %15s %15s %11s %7s %13s %11s\n", "", "today > seen", "today > seen", "today > seen", "/pixel", "each > need", "/pixel");
  int overCount = 0, scenes = 0;
  for (const fs::path& mf : manifests) {
    std::string text;
    Json p;
    if (!readText(mf.string(), text) || !Json::parse(text, p)) continue;
    for (const Json& name : p["scenes"].items()) {
      std::string path = (mf.parent_path() / (name.string() + ".loop")).string();
      Measured m;
      if (!measureScene(path, o, m)) continue;
      const FrameCost& c = m.cost;
      std::vector<std::string> over = overBudget(c, m.budget);
      std::string words;
      for (const std::string& w : over) words += (words.empty() ? "" : ", ") + w.substr(0, w.find(' ', w.find(' ') == 4 ? 5 : 0));
      char things[32], tris[32], draws[32], lamps[32];
      std::snprintf(things, sizeof things, "%zu > %zu", c.sceneThings + c.shadowThings, c.inViewThings + c.inShadowThings);
      std::snprintf(tris, sizeof tris, "%.2f > %.2f", static_cast<double>(c.triangles()) / 1e6, static_cast<double>(c.inViewTriangles + c.inShadowTriangles) / 1e6);
      std::snprintf(draws, sizeof draws, "%zu > %zu", c.draws(), c.inViewDraws + c.inShadowDraws);
      std::snprintf(lamps, sizeof lamps, "%.0f > %.1f", static_cast<double>(c.lampsComputed), static_cast<double>(c.lampsThatMatter));
      std::printf("%-28s %15s %15s %11s %7.2f %13s %11.0f  %s\n", name.string().c_str(), things, tris, draws, static_cast<double>(c.shadedPerPixel), lamps,
                  static_cast<double>(c.lampWorkPerPixel()), over.empty() ? "fits" : words.c_str());
      ++scenes, overCount += !over.empty();
    }
  }
  std::printf("%d of %d scenes over the %s budget\n", overCount, scenes, o.quality == "high" ? "High" : "Phone");
  return overCount ? 2 : 0;
}

// `loop score`: the cinema renderer's picture of the very view a live picture
// shows (the camera the player starts with, or the film's at `filmtime`; the
// world held at its first moment, as the picture tests hold it; widened for an
// upright screen, as the player does), scored against it.
struct Scored {
  std::string title;
  PictureScore s;
  double seconds = 0;
};
bool scoreScene(const std::string& path, const std::string& livePath, double filmtime, int samples, const std::string& outDir, Scored& out,
                bool reuse = false) {
  Image live;
  if (!readPng(livePath, live, true)) {
    std::fprintf(stderr, "can't read %s\n", livePath.c_str());
    return false;
  }
  Vm vm;
  Stage stage;
  if (!loadScript(path, vm, stage)) return false;
  Scene sc = stage.scene;
  Camera cam = sc.camera;
  if (stage.mode == "movie" && !stage.film.shots.empty()) {
    int shot = 0;
    float fade = 1;
    cam = stage.film.at(static_cast<float>(filmtime), sc.camera, &shot, &fade);
    if (!stage.film.shots[shot].look.empty()) lookByName(stage.film.shots[shot].look, sc.look);
  }
  if (config().camera.portraitWide) cam.widenForPortrait(static_cast<float>(live.width) / static_cast<float>(std::max(live.height, 1)));
  cam.positionEnd = Vec3(kInf);  // a still: no motion blur
  RenderRequest req;
  const Config::Cinema& cc = config().cinema;
  req.bounces = cc.bounces, req.denoise = cc.denoise, req.dispersion = cc.dispersion, req.motionBlur = false;
  std::string base = path.substr(path.find_last_of('/') + 1);
  base = base.substr(0, base.find('.'));
  // --reuse: the cinema picture a run before saved in the -o folder (the cinema side doesn't change)
  Image cinema;
  const std::string saved = outDir + "/" + base + "_cinema.png";
  if (!(reuse && !outDir.empty() && readPng(saved, cinema, true) && cinema.width == live.width && cinema.height == live.height)) {
    cinema = renderStill(sc, cam, live.width, live.height, samples, 0, base, &req);
    for (Vec4& p : cinema.px) p = Vec4(srgbToLinear(p.x), srgbToLinear(p.y), srgbToLinear(p.z), 1);
  }
  out.title = stage.title;
  out.s = scorePictures(live, cinema);
  if (!outDir.empty()) {
    std::filesystem::create_directories(outDir);
    writePng(outDir + "/" + base + "_cinema.png", cinema);
    writePng(outDir + "/" + base + "_difference.png", out.s.difference);
  }
  return true;
}

// A scene's script by its name, anywhere under projects/ (the picture tests name scenes).
std::string findScene(const std::string& name) {
  namespace fs = std::filesystem;
  if (!fs::exists("projects")) return "";
  for (const auto& e : fs::recursive_directory_iterator("projects"))
    if (e.is_regular_file() && e.path().filename() == name + ".loop") return e.path().string();
  return "";
}

int score(const std::string& target, const Options& o) {
  const int samples = o.samples ? o.samples : 256;
  if (!std::filesystem::is_directory(target)) {
    if (o.live.empty()) return usage();
    Scored r;
    if (!scoreScene(target, o.live, o.filmtime, samples, o.out, r, o.reuse)) return 1;
    std::printf("%s: %.1f (worst tenth over %.1f; brightness live/cinema %.2f)\n", r.title.c_str(), static_cast<double>(r.s.score),
                static_cast<double>(r.s.worstTenth), static_cast<double>(r.s.brightness));
    return 0;
  }
  // the picture tests' folder: its scenes.json names the scenes and their moments
  std::string text;
  Json cfg;
  if (!readText(target + "/scenes.json", text) || !Json::parse(text, cfg)) {
    std::fprintf(stderr, "no scenes.json in %s\n", target.c_str());
    return 1;
  }
  std::printf("%-22s %7s %12s %11s\n", "scene", "score", "worst tenth", "brightness");
  double total = 0;
  int n = 0;
  for (const Json& s : cfg["scenes"].items()) {
    std::string name = s["name"].string(), path = findScene(name);
    if (path.empty()) {
      std::printf("%-22s  (no script found)\n", name.c_str());
      continue;
    }
    Scored r;
    double ft = s.has("filmtime") ? s["filmtime"].number() : 0;
    if (!scoreScene(path, target + "/" + name + ".png", ft, samples, o.out, r, o.reuse)) continue;
    std::printf("%-22s %7.1f %12.1f %11.2f\n", name.c_str(), static_cast<double>(r.s.score), static_cast<double>(r.s.worstTenth),
                static_cast<double>(r.s.brightness));
    total += r.s.score, ++n;
  }
  if (n) std::printf("average %.1f over %d scenes (ΔE: 0 the same picture, about 2 barely visible, 10 plainly different)\n", total / n, n);
  return n ? 0 : 1;
}

int budget(const std::string& target, const Options& o) {
  if (std::filesystem::is_directory(target)) return budgetFolder(target, o);
  return budgetScene(target, o);
}

// `loop research cascades`: the picture pipeline's phase 9 experiment. Radiance
// cascades against the light cache, on the same rooms, with the same ray oracle
// (the film renderer's): how far each one's soft light is from the truth (many
// rays at the very point), and how many rays it cost.
namespace research {
Scene roomScene(bool twoRooms, bool redWall) {
  Scene s;
  s.sky.kind = Sky::Kind::kColor;
  s.sky.color = Vec3(1);
  s.sky.colorNits = 5000;
  Material white, red;
  white.baseColor = Vec3(0.7f), white.roughness = 1, white.specular = 0;
  red.baseColor = Vec3(0.8f, 0.05f, 0.05f), red.roughness = 1, red.specular = 0;
  const int w = s.addMaterial(white), r = s.addMaterial(red);
  auto slab = [&](Vec3 size, Vec3 at, int m) { s.add(s.addMesh(shapes::box(size)), m, Mat4::translation(at)); };
  const float L = twoRooms ? 8.0f : 4.0f, t = 0.2f;
  slab({L + 2 * t, t, 4 + 2 * t}, {0, -t / 2, 0}, w);
  slab({L + 2 * t, t, 4 + 2 * t}, {0, 3 + t / 2, 0}, w);
  slab({t, 3, 4}, {-L / 2 - t / 2, 1.5f, 0}, redWall ? r : w);
  slab({t, 3, 4}, {L / 2 + t / 2, 1.5f, 0}, w);
  // the long sides, with a window (an opening) in one when there is one room
  slab({L + 2 * t, 3, t}, {0, 1.5f, -2 - t / 2}, w);
  if (twoRooms) {
    slab({L + 2 * t, 3, t}, {0, 1.5f, 2 + t / 2}, w);
    slab({t, 3, 1.4f}, {0, 1.5f, -1.3f}, w);  // the wall between, with a door 1.2 m wide on +z
    slab({t, 3, 1.4f}, {0, 1.5f, 1.3f}, w);
    slab({t, 0.8f, 1.2f}, {0, 2.6f, 0}, w);   // over the door
  } else {
    slab({L + 2 * t, 1, t}, {0, 0.5f, 2 + t / 2}, w);  // under the window
    slab({L + 2 * t, 0.6f, t}, {0, 2.7f, 2 + t / 2}, w);  // over it
  }
  Light l;
  l.position = {twoRooms ? -2.0f : 0.0f, 2.5f, 0};
  l.lumens = 800;
  l.radius = 0.05f;
  s.lights.push_back(l);
  return s;
}
float truth(PathTracer& pt, Vec3 p, Vec3 n, int rays) {
  // cosine-weighted rays: the light falling on the surface is pi times their average
  const Vec3 t = std::fabs(n.y) < 0.9f ? normalize(cross(n, Vec3(0, 1, 0))) : normalize(cross(n, Vec3(1, 0, 0))), b = cross(n, t);
  double sum = 0;
  Rng rng(77);
  for (int k = 0; k < rays; ++k) {
    const float u = rng.uniform(), v = rng.uniform(), r = std::sqrt(u), ph = kTwoPi * v;
    const Vec3 d = normalize(t * (r * std::cos(ph)) + b * (r * std::sin(ph)) + n * std::sqrt(std::max(0.0f, 1 - u)));
    sum += luminance(pt.probeLight(p, d, hashCombine(9u, static_cast<uint32_t>(k)), 2, nullptr));
  }
  return static_cast<float>(kPi * sum / rays);
}
int cascades() {
  struct Case { const char* name; bool two, red; };
  const Case cases[] = {{"one room, a window", false, false}, {"one room, a red wall", false, true}, {"two rooms, a door", true, false}};
  std::printf("%-22s %-34s %10s %8s %9s\n", "room", "method", "rays", "error", "worst");
  for (const Case& c : cases) {
    Scene s = roomScene(c.two, c.red);
    PathTracer pt;
    pt.setScene(s);
    const float L = c.two ? 8.0f : 4.0f;
    const AABB inside(Vec3(-L / 2 + 0.1f, 0.1f, -1.9f), Vec3(L / 2 - 0.1f, 2.9f, 1.9f));
    // the points: on a grid through the room, each facing the six ways
    struct Point { Vec3 p, n; float e; };
    std::vector<Point> pts;
    Rng rng(5);
    for (int k = 0; k < 48; ++k) {
      const Vec3 p(inside.lo.x + rng.uniform() * (inside.hi.x - inside.lo.x), 0.2f + rng.uniform() * 2.6f, -1.8f + rng.uniform() * 3.6f);
      static const Vec3 ns[6] = {{1, 0, 0}, {-1, 0, 0}, {0, 1, 0}, {0, -1, 0}, {0, 0, 1}, {0, 0, -1}};
      pts.push_back({p, ns[k % 6], 0});
    }
    for (Point& q : pts) q.e = truth(pt, q.p, q.n, 2048);
    auto measure = [&](const char* method, uint64_t rays, auto&& at) {
      double err = 0, worst = 0, ref = 0;
      for (const Point& q : pts) ref += q.e;
      ref /= static_cast<double>(pts.size());
      for (const Point& q : pts) {
        // the error against the room's average light (a dark corner's small light counts as small)
        const double d = std::fabs(at(q.p, q.n) - q.e) / std::max(ref, 1e-6);
        err += d, worst = std::max(worst, d);
      }
      std::printf("%-22s %-34s %10llu %7.1f%% %8.1f%%\n", c.name, method, static_cast<unsigned long long>(rays), 100 * err / pts.size(), 100 * worst);
    };
    // the light cache as the live renderer has it: 1 m apart, 64 rays a probe, filled and smoothed
    for (float spacing : {1.0f, 0.5f}) {
      LightCache lc = layOutLightCache(inside, 4096, spacing);
      lc.origin = inside.lo;  // (laid out from the room's corner, like the cascades)
      const uint64_t before = 0;
      for (int i = 0; i < lc.probes(); ++i) bakeProbe(lc, i, pt, 64, 2);
      fillInside(lc);
      LightCache sm = smoothed(lc);
      char name[64];
      std::snprintf(name, sizeof name, "light cache, %.1f m, 64 rays", spacing);
      measure(name, before + static_cast<uint64_t>(lc.probes()) * 64, [&](Vec3 p, Vec3 n) { return luminance(sm.irradiance(p, n)); });
    }
    // radiance cascades: level 0 0.5 m apart, 16 or 64 directions, 3 levels
    for (int dirs : {4, 8}) {
      RadianceCascades rc = buildCascades(pt, inside, 0.5f, 3, dirs, 0.5f, 2);
      char name[64];
      std::snprintf(name, sizeof name, "cascades, 0.5 m, %d dirs, 3 levels", dirs * dirs);
      measure(name, rc.rays, [&](Vec3 p, Vec3 n) { return luminance(rc.irradiance(p, n)); });
    }
  }
  std::printf("(error: the average difference from the truth, over the room's average light; worst: the largest)\n");
  return 0;
}
// `loop world <seed> [-o map.png] [--size 257]`: a generated world's map
// (engine/geometry/world_gen.h): its ground in colour, shaded by the sun.
int worldMap(const std::string& seed, const std::string& out, int cells) {
  WorldSettings ws;
  ws.seed = static_cast<uint32_t>(std::atoi(seed.c_str()));
  ws.cells = cells;
  Stopwatch sw;
  WorldMap m = generateWorld(ws);
  const double secs = sw.seconds();
  Image img(m.n, m.n, {0, 0, 0, 1});
  static const Vec3 colour[7] = {{0.10f, 0.22f, 0.42f}, {0.20f, 0.42f, 0.62f}, {0.83f, 0.78f, 0.58f}, {0.45f, 0.62f, 0.30f},
                                 {0.16f, 0.36f, 0.16f}, {0.52f, 0.49f, 0.45f}, {0.95f, 0.96f, 0.98f}};
  int kinds[7] = {0};
  float top = 0;
  for (int z = 0; z < m.n; ++z)
    for (int x = 0; x < m.n; ++x) {
      const size_t i = m.at(x, z);
      ++kinds[m.ground[i]];
      top = std::max(top, m.height[i]);
      Vec3 c = colour[m.ground[i]];
      if (m.ground[i] != WorldMap::kSea && m.ground[i] != WorldMap::kLake) {
        // the sun from the north-west
        const float dx = m.height[m.at(std::min(x + 1, m.n - 1), z)] - m.height[m.at(std::max(x - 1, 0), z)];
        const float dz = m.height[m.at(x, std::min(z + 1, m.n - 1))] - m.height[m.at(x, std::max(z - 1, 0))];
        const Vec3 n = normalize(Vec3(-dx, 2 * m.cell, -dz));
        c *= 0.45f + 0.75f * std::max(0.0f, dot(n, normalize(Vec3(-1, 1.2f, -1))));
        if (m.flow[i] / (m.cell * m.cell) > 150) c = vlerp(c, colour[1], 0.7f);  // a river
      }
      img.at(x, z) = {c, 1};
    }
  writePng(out, img, true);
  std::printf("world %u: %d x %d cells of %.0f m in %.1f s; highest %.0f m; sea %d, lakes %d, sand %d, grass %d, forest %d, rock %d, snow %d -> %s\n",
              ws.seed, m.n, m.n, m.cell, secs, top, kinds[0], kinds[1], kinds[2], kinds[3], kinds[4], kinds[5], kinds[6], out.c_str());
  return 0;
}
}  // namespace research

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
    else if (a == "--reuse") o.reuse = true;
    else if (a == "--seconds") o.seconds = std::atof(val().c_str()), o.secondsGiven = true;
    else if (a == "--filmtime") o.filmtime = std::atof(val().c_str());
    else if (cmd == "score" && a[0] != '-' && o.live.empty()) o.live = a;
    else if (a == "--size") std::sscanf(val().c_str(), "%dx%d", &o.w, &o.h), o.sizeGiven = true;
    else if (a == "-o") o.out = val();
    else if (a == "--seed") o.seed = std::atoi(val().c_str());
    else if (cmd == "library" && target == "picture" && a[0] != '-' && o.name.empty()) o.name = a;
    else if (a == "--cells") o.worldCells = std::atoi(val().c_str());
    else if (a == "--choices") o.choices = val();
    else if (a == "--set") {
      std::string kv = val();
      size_t eq = kv.find('=');
      if (eq == std::string::npos) return usage();
      o.sets.push_back({kv.substr(0, eq), kv.substr(eq + 1)});
    }
    else return usage();
  }
  if (cmd == "library") {
    std::string lib = findLibrary(".");
    if (lib.empty()) {
      std::fprintf(stderr, "problem: there's no library/ folder here or above\n");
      return 1;
    }
    std::vector<LibraryEntry> entries;
    std::vector<std::string> problems;
    bool ok = readLibrary(lib, entries, problems);
    for (const std::string& p : problems) std::fprintf(stderr, "problem: %s\n", p.c_str());
    if (target == "json") {
      Json all = Json::array();
      for (const LibraryEntry& e : entries) {
        Json j = Json::object();
        j.set("name", Json(e.name)), j.set("what", Json(e.what)), j.set("about", Json(e.about)), j.set("kind", Json(e.kind));
        j.set("place", Json(e.place)), j.set("credit", Json(e.credit)), j.set("licence", Json(e.licence)), j.set("from", Json(e.from));
        all.push(j);
      }
      std::printf("%s\n", all.dump(1).c_str());
    } else if (target == "picture") {
      for (const LibraryEntry& e : entries)
        if (e.name == o.name) return libraryPicture(e, lib, o);
      std::fprintf(stderr, "problem: no library entry called \"%s\" (loop library list shows them)\n", o.name.c_str());
      return 1;
    } else if (target == "list" || target == "check") {
      // each entry's triangles and size class; an entry over its size's budget fails
      std::vector<size_t> counts;
      std::vector<std::string> budgetProblems;
      if (!checkLibraryBudgets(lib, entries, counts, budgetProblems)) ok = false;
      for (const std::string& p : budgetProblems) std::fprintf(stderr, "problem: %s\n", p.c_str());
      std::string kind;
      for (size_t i = 0; i < entries.size(); i++) {
        const LibraryEntry& e = entries[i];
        size_t tris = i < counts.size() ? counts[i] : 0, budget = libraryBudget(e.size);
        std::string sz = e.size.empty() ? "no size" : e.size;
        if (budget) sz += ", budget " + std::to_string(budget);
        if (target == "list") {
          if (e.kind != kind) std::printf("%s%ss\n", kind.empty() ? "" : "\n", (kind = e.kind).c_str());
          std::printf("  %-16s %s (from %s) · %zu triangles (%s)\n", e.name.c_str(), e.what.c_str(), e.from.c_str(), tris, sz.c_str());
        } else {
          std::printf("  %-16s %6zu triangles (%s)\n", e.name.c_str(), tris, sz.c_str());
        }
      }
      if (target == "check" && ok) std::printf("the library's %zu entries are in order\n", entries.size());
    } else {
      return usage();
    }
    return ok ? 0 : 1;
  }
  if (cmd == "new") {
    std::string folder, why;
    if (!makeProject("projects/template", "projects", target, folder, why)) {
      std::fprintf(stderr, "problem: %s\n", why.c_str());
      return 1;
    }
    std::string f = projectFolderName(target);
    std::printf("made %s (\"%s\"): its cover page %s/%s.looped opens %s_room\n", folder.c_str(), target.c_str(), folder.c_str(), f.c_str(), f.c_str());
    return 0;
  }
  // a project's cover page: read it, then work on its first scene
  if (target.size() > 7 && target.compare(target.size() - 7, 7, ".looped") == 0) {
    Project pr;
    std::vector<ProjectProblem> ps;
    bool ok = readProject(target, pr, ps);
    for (const ProjectProblem& p : ps) std::fprintf(stderr, "problem: %s\n", p.message(target).c_str());
    if (!ok) return 1;
    std::string first = pr.scenePath(pr.start);
    if (first.empty()) {
      std::fprintf(stderr, "problem: %s: its first scene \"%s\" has no file (looked for places/%s.loop and %s.loop)\n", target.c_str(), pr.start.c_str(),
                   pr.start.c_str(), pr.start.c_str());
      return 1;
    }
    if (cmd == "project") {
      Json j = Json::object(), settings = Json::object();
      for (const Project::Setting& s : pr.settings) settings.set(s.name, s.value);
      j.set("name", Json(pr.name)), j.set("start", Json(pr.start)), j.set("settings", settings);
      std::printf("%s\n", j.dump(1).c_str());
      return 0;
    }
    if (cmd == "check")
      std::printf("%s: \"%s\", opens %s, %zu setting%s\n", target.c_str(), pr.name.c_str(), pr.start.c_str(), pr.settings.size(), pr.settings.size() == 1 ? "" : "s");
    target = first;
  } else if (cmd == "project") {
    std::fprintf(stderr, "loop project takes a project's cover page (name.looped)\n");
    return 1;
  }
  if (cmd == "cook") {
    Vm vm;
    Stage stage;
    if (!loadScript(target, vm, stage)) return 1;
    Stopwatch sw;
    Cook::shared().cookAll(stage.scene.meshes);
    double ms = sw.ms();
    size_t tris = 0, clusters = 0, coarsest = 0;
    int deepest = 0;
    for (const Mesh& m : stage.scene.meshes) {
      auto t = Cook::shared().clusters(m);
      tris += m.triangleCount(), clusters += t->clusters.size(), deepest = std::max(deepest, t->levels);
      for (const Cluster& c : t->clusters)
        if (c.parentError >= kInf) coarsest += c.count;
    }
    std::printf("%s: %zu meshes, %zu triangles, cooked into %zu clusters, up to %d levels; at their coarsest %zu triangles (%.1f%%); %.0f ms on %d cores\n",
                target.c_str(), stage.scene.meshes.size(), tris, clusters, deepest, coarsest, tris ? 100.0 * coarsest / tris : 0.0, ms, JobSystem::shared().threads());
    return 0;
  }
  if (cmd == "demo") return demo(target, o);
  if (cmd == "budget") return budget(target, o);
  if (cmd == "score") return score(target, o);
  if (cmd == "research" && target == "cascades") return research::cascades();
  if (cmd == "world") return research::worldMap(target, o.out.empty() ? "world.png" : o.out, o.worldCells);
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
