// The Loop3D player: runs a Looped Script world in the browser and draws it
// live with WebGPU. In "movie" mode it plays the script's shots; in "game"
// mode you walk (left thumb / WASD) and look (right thumb / mouse).
//
// Motion is smooth at any frame rate: scripts run on a fixed 120 Hz clock,
// walking and looking ease toward where the controls point (no snapping),
// and the camera never jumps between frames.
#include <emscripten/emscripten.h>
#include <emscripten/html5.h>

#include <algorithm>
#include <cstdio>
#include <cstring>
#include <vector>
#include <memory>
#include <string>

#include "engine/core/json.h"
#include "engine/gpu/renderer.h"
#include "engine/script/scene_api.h"
#include "engine/script/vm.h"

using namespace loop;
using namespace loop::script;

namespace {

struct Player {
  std::unique_ptr<gpu::Renderer> renderer;
  std::unique_ptr<Vm> vm;
  std::unique_ptr<Stage> stage;
  uint64_t seenVersion = ~0ull, seenTransforms = ~0ull, seenLights = ~0ull;
  uint64_t lastPresented = 0;
  size_t seenMeshes = 0, seenInstances = 0, seenMaterials = 0, seenLightCount = 0;
  uint64_t skyKey = 0;
  double lastTime = 0, accumulator = 0, filmTime = 0;
  bool movie = true;
  // walking
  Vec3 pos{0, 1.6f, 5}, vel{0};
  float yaw = 0, pitch = 0, yawTarget = 0, pitchTarget = 0;
  Vec2 move{0};  // -1..1 from keys or the thumb
  bool keys[8] = {};
  std::string pendingScript, problems;
  bool autoplay = false;
  int frames = 0;
  double fpsTime = 0;
  float fps = 0;
  double cpuMs = 0, frameStart = 0;
  int quality = 1;              // 0 Phone, 1 High, 2 Ultra
  uint64_t scriptSerial = 0;    // counts scenes started
  bool awaitingFirstFrame = false;
  double lastGpuReset = -1e9;
  // The editor's overrides, kept across scene changes: the film look, fog and
  // camera fields it has set (JSON objects; empty = the scene's own).
  Json lookOverride = Json::object(), worldOverride = Json::object();
  float walkSpeed = 1.6f, runSpeed = 4.5f, lookSpeed = 1;
  bool paused = false;    // the editor's pause: the world and the film hold, the view still moves
  double timeScale = 1;   // slow motion / fast forward
  int stepFrames = 0;     // paused: advance this many script steps
  std::string log;  // the engine's messages for the editor's console
};
Player g;

gpu::Quality qualityOf(int q) { return q == 0 ? gpu::Quality::kPhone : q == 1 ? gpu::Quality::kHigh : gpu::Quality::kUltra; }

// The frame recorder: the time between frames that reached the screen (what
// the eye feels), the CPU and GPU time of each, and the resolution drawn.
// A benchmark records one scene at a time and gets its numbers as JSON.
struct Perf {
  bool on = false, waitingFirst = false;
  uint64_t serialAtBegin = 0;  // load time counts from the next scene's script
  double scriptStart = 0, lastShown = 0, loadMs = -1, started = 0;
  uint64_t rafFrames = 0, shownAtStart = 0;
  std::vector<float> interval, cpu, gpu, scale;
  float ring[240] = {};  // the last 240 shown frames, for the live graph
  int ringAt = 0;
} perf;

void perfShown(double now, double cpuMs) {
  if (perf.lastShown > 0) {
    float iv = static_cast<float>(now - perf.lastShown);
    perf.ring[perf.ringAt] = iv;
    perf.ringAt = (perf.ringAt + 1) % 240;
    if (perf.on && !perf.waitingFirst) {
      perf.interval.push_back(iv);
      perf.cpu.push_back(static_cast<float>(cpuMs));
      perf.gpu.push_back(g.renderer->lastGpuMs());
      perf.scale.push_back(g.renderer->renderScale());
    }
  }
  if (perf.on && perf.waitingFirst && g.scriptSerial > perf.serialAtBegin) {
    perf.waitingFirst = false;
    perf.loadMs = emscripten_get_now() - perf.scriptStart;  // same clock as scriptStart
    perf.started = now;
    perf.shownAtStart = g.renderer->presentedFrames();
  }
  perf.lastShown = now;
}

float pct(std::vector<float> v, float p) {
  if (v.empty()) return 0;
  size_t k = std::min(v.size() - 1, static_cast<size_t>(p * (v.size() - 1) + 0.5f));
  std::nth_element(v.begin(), v.begin() + k, v.end());
  return v[k];
}
float mean(const std::vector<float>& v) {
  double t = 0;
  for (float x : v) t += x;
  return v.empty() ? 0 : static_cast<float>(t / v.size());
}
// fps of the slowest share of frames ("1% low"): what the worst moments feel like
float lowFps(std::vector<float> v, float share) {
  if (v.empty()) return 0;
  std::sort(v.begin(), v.end(), std::greater<float>());
  size_t n = std::max<size_t>(1, static_cast<size_t>(v.size() * share));
  double t = 0;
  for (size_t i = 0; i < n; ++i) t += v[i];
  return static_cast<float>(1000.0 / (t / n));
}

uint64_t skySignature(const Scene& s) {
  uint64_t h = 1469598103934665603ULL;
  auto mix = [&](float f) {
    uint32_t b;
    std::memcpy(&b, &f, 4);
    h = (h ^ b) * 1099511628211ULL;
  };
  mix(static_cast<float>(static_cast<int>(s.sky.kind)));
  mix(s.sky.turbidity), mix(s.sky.starNits), mix(s.sky.intensity), mix(s.sky.colorNits);
  for (const Light& l : s.lights)
    if (l.kind == Light::Kind::kSun) mix(l.direction.x), mix(l.direction.y), mix(l.direction.z), mix(l.lux);
  return h;
}

// The renderer has nothing of the world yet: send all of it on the next frame.
void forgetSynced() {
  g.seenVersion = ~0ull, g.seenTransforms = ~0ull, g.seenLights = ~0ull, g.seenMeshes = 0, g.seenInstances = 0, g.seenMaterials = 0, g.seenLightCount = 0;
  g.skyKey = 0;
}

// The browser took the GPU away (a driver reset, memory running out, a frame
// that took too long). Before, the picture stayed black for good; now we ask
// for a new GPU, send it the whole world again and carry on. Lost twice in a
// minute: this quality is too much for this GPU, so step down one.
void recoverGpu() {
  g.renderer.reset();
  double now = emscripten_get_now();
  if (now - g.lastGpuReset < 60000 && g.quality > 0) --g.quality;
  g.lastGpuReset = now;
  EM_ASM({ if (window.loopGpuLost) window.loopGpuLost(); });
  gpu::Renderer::create("#view", [](std::unique_ptr<gpu::Renderer> r) {
    if (!r) {
      EM_ASM({ if (window.loopNoGpu) window.loopNoGpu(); });
      return;
    }
    g.renderer = std::move(r);
    g.renderer->setQuality(qualityOf(g.quality));
    g.lastPresented = 0;  // the new renderer counts its frames from zero
    forgetSynced();
    EM_ASM({ if (window.loopGpuBack) window.loopGpuBack($0); }, g.quality);
  });
}

void logLine(const std::string& text) {
  g.log += text + "\n";
  if (g.log.size() > 64000) g.log.erase(0, g.log.size() - 48000);
}

float num(const Json& o, const char* k, float v) { return o.has(k) ? static_cast<float>(o[k].number(v)) : v; }
Vec3 vec(const Json& o, const char* k, Vec3 v) {
  if (!o.has(k)) return v;
  const Json& a = o[k];
  if (a.isNumber()) return Vec3(static_cast<float>(a.number()));
  if (a.isArray() && a.size() == 3) return Vec3(static_cast<float>(a[0].number()), static_cast<float>(a[1].number()), static_cast<float>(a[2].number()));
  return v;
}

// The editor's film settings over the scene's (or the shot's) look.
void applyLookOverride(Look& l) {
  const Json& o = g.lookOverride;
  if (o.size() == 0) return;
  if (o.has("preset") && !o["preset"].string().empty()) lookByName(o["preset"].string(), l);
  if (o.has("tonemap")) {
    const std::string& t = o["tonemap"].string();
    l.tonemap = t == "aces" ? Look::Tonemap::kAces : t == "filmic" ? Look::Tonemap::kFilmic : t == "linear" ? Look::Tonemap::kLinear : Look::Tonemap::kAgX;
  }
  l.contrast = num(o, "contrast", l.contrast), l.saturation = num(o, "saturation", l.saturation);
  l.temperature = num(o, "temperature", l.temperature), l.tint = num(o, "tint", l.tint);
  l.lift = vec(o, "lift", l.lift), l.gamma = vec(o, "gamma", l.gamma), l.gain = vec(o, "gain", l.gain);
  l.shadowTint = vec(o, "shadowTint", l.shadowTint), l.highlightTint = vec(o, "highlightTint", l.highlightTint);
  l.blackLevel = num(o, "blackLevel", l.blackLevel);
  l.bloom = num(o, "bloom", l.bloom), l.bloomRadius = num(o, "bloomRadius", l.bloomRadius), l.halation = num(o, "halation", l.halation);
  l.vignette = num(o, "vignette", l.vignette), l.grain = num(o, "grain", l.grain), l.grainSize = num(o, "grainSize", l.grainSize);
  l.chromaticAberration = num(o, "chromaticAberration", l.chromaticAberration), l.distortion = num(o, "distortion", l.distortion);
  l.sharpen = num(o, "sharpen", l.sharpen), l.letterbox = num(o, "letterbox", l.letterbox);
  if (o.has("monochrome")) l.monochrome = o["monochrome"].boolean(l.monochrome);
}

// The editor's fog and camera settings over the scene's.
void applyWorldOverride(Scene& s) {
  const Json& o = g.worldOverride;
  if (o.size() == 0) return;
  s.fog.density = num(o, "fogDensity", s.fog.density), s.fog.albedo = vec(o, "fogAlbedo", s.fog.albedo);
  s.fog.anisotropy = num(o, "fogAnisotropy", s.fog.anisotropy), s.fog.top = num(o, "fogTop", s.fog.top);
}
void applyCameraOverride(Camera& c) {
  const Json& o = g.worldOverride;
  if (o.size() == 0) return;
  c.focalLength = num(o, "focalLength", c.focalLength), c.fStop = num(o, "fStop", c.fStop);
  c.focusDistance = num(o, "focusDistance", c.focusDistance), c.shutter = num(o, "shutter", c.shutter), c.iso = num(o, "iso", c.iso);
  c.exposureCompensation = num(o, "exposureCompensation", c.exposureCompensation);
  if (o.has("autoExposure")) c.autoExposure = o["autoExposure"].boolean(c.autoExposure);
  if (o.has("blades")) c.blades = static_cast<int>(o["blades"].number(c.blades));
}

void startScript(const std::string& src) {
  perf.scriptStart = emscripten_get_now();
  ++g.scriptSerial;
  g.awaitingFirstFrame = true;
  perf.lastShown = 0;
  if (g.renderer) g.renderer->reset();
  g.vm = std::make_unique<Vm>();
  g.stage = std::make_unique<Stage>();
  g.stage->autoplay = g.autoplay;
  installStandardLibrary(*g.vm);
  installSceneApi(*g.vm, *g.stage);
  g.vm->onSay = [](const std::string& s) { EM_ASM({ if (window.loopSay) window.loopSay(UTF8ToString($0)); }, s.c_str()); };
  g.problems.clear();
  g.vm->load(src, "scene.loop");
  for (auto& p : g.vm->problems()) g.problems += p.text() + "\n";
  logLine("scene started: " + g.stage->title + " (" + std::to_string(g.stage->scene.instances.size()) + " things, " +
          std::to_string(g.stage->scene.lights.size()) + " lights)");
  if (g.worldOverride.size()) applyWorldOverride(g.stage->scene), ++g.stage->version;
  if (!g.problems.empty()) EM_ASM({ if (window.loopProblems) window.loopProblems(UTF8ToString($0)); }, g.problems.c_str());
  forgetSynced();
  g.movie = g.stage->mode == "movie" && !g.stage->film.shots.empty();
  g.filmTime = 0;
  const Camera& c = g.stage->scene.camera;
  g.pos = c.position;
  Vec3 f = normalize(c.target - c.position);
  g.yaw = g.yawTarget = std::atan2(f.x, -f.z);
  g.pitch = g.pitchTarget = std::asin(clamp(f.y, -1.0f, 1.0f));
  g.vel = Vec3(0);
  EM_ASM({ if (window.loopStarted) window.loopStarted(UTF8ToString($0), UTF8ToString($1), $2); }, g.stage->title.c_str(), g.stage->mode.c_str(),
         static_cast<int>(g.stage->film.shots.size()));
}

void syncScene() {
  Stage& s = *g.stage;
  // new shapes, or anything else about the world: read it all again
  if (s.version != g.seenVersion || s.scene.meshes.size() != g.seenMeshes || s.scene.instances.size() != g.seenInstances || s.scene.materials.size() != g.seenMaterials ||
      s.scene.lights.size() != g.seenLightCount) {
    uint64_t sk = skySignature(s.scene);
    bool geometry = s.scene.meshes.size() != g.seenMeshes;
    g.renderer->sync(s.scene, geometry, sk != g.skyKey);
    g.skyKey = sk;
    g.seenMeshes = s.scene.meshes.size();
    g.seenInstances = s.scene.instances.size();
    g.seenMaterials = s.scene.materials.size();
    g.seenLightCount = s.scene.lights.size();
    g.seenVersion = s.version;
    g.seenTransforms = s.transformsVersion;
    g.seenLights = s.lightsVersion;
    return;
  }
  if (s.lightsVersion != g.seenLights) {
    g.renderer->syncLights(s.scene);
    g.seenLights = s.lightsVersion;
  }
  if (s.transformsVersion != g.seenTransforms) {
    g.renderer->syncTransforms(s.scene);
    g.seenTransforms = s.transformsVersion;
  }
}

Camera walkCamera(float dt) {
  // ease the look toward where the controls point, and the body toward the
  // speed asked for: starting and stopping feel like a person, not a robot
  float kLook = 1 - std::exp(-18 * dt), kMove = 1 - std::exp(-7 * dt);
  g.yaw += (g.yawTarget - g.yaw) * kLook;
  g.pitch += (g.pitchTarget - g.pitch) * kLook;
  Vec2 m = g.move;
  if (g.keys[0]) m.y += 1;
  if (g.keys[1]) m.y -= 1;
  if (g.keys[2]) m.x -= 1;
  if (g.keys[3]) m.x += 1;
  if (length(m) > 1) m = normalize(m);
  float speed = g.keys[4] ? g.runSpeed : g.walkSpeed;
  Vec3 fwd{std::sin(g.yaw), 0, -std::cos(g.yaw)}, right{std::cos(g.yaw), 0, std::sin(g.yaw)};
  Vec3 want = (fwd * m.y + right * m.x) * speed;
  g.vel += (want - g.vel) * kMove;
  // walk through the world, not through its walls: slide and step like a body
  Vec3 body = g.pos - Vec3(0, 0.75f, 0);
  g.stage->world.moveCharacter(body, Vec3(g.vel.x * dt, 0, g.vel.z * dt), 0.25f, 0.55f, 0.3f);
  g.pos.x = body.x, g.pos.z = body.z;
  // stand on the land, with a gentle step bob when walking
  float ground = 0;
  if (!g.stage->terrains.empty()) ground = g.stage->terrains.back().height(g.pos.x, g.pos.z);
  static float bobPhase = 0;
  float sp = length(g.vel);
  bobPhase += sp * dt * 2.1f;
  float bob = std::sin(bobPhase * kTwoPi) * 0.025f * std::min(1.0f, sp / 1.6f);
  float eye = ground + 1.6f + bob;
  g.pos.y += (eye - g.pos.y) * (1 - std::exp(-12 * dt));
  Camera c = g.stage->scene.camera;
  c.position = g.pos;
  Vec3 dir{std::sin(g.yaw) * std::cos(g.pitch), std::sin(g.pitch), -std::cos(g.yaw) * std::cos(g.pitch)};
  c.target = g.pos + dir;
  return c;
}

bool frameBody(double now);
// The time each frame spends on the CPU (script, world, sending to the GPU),
// smoothed: shown in the stats so a slow script is easy to tell from a slow GPU.
bool frame(double now, void*) {
  double t0 = emscripten_get_now();
  g.frameStart = t0;
  bool r = frameBody(now);
  g.cpuMs += (emscripten_get_now() - t0 - g.cpuMs) * 0.05;
  return r;
}
bool frameBody(double now) {
  if (!g.pendingScript.empty()) {
    startScript(g.pendingScript);
    g.pendingScript.clear();
  }
  if (g.renderer && g.renderer->lost()) recoverGpu();
  if (!g.renderer || !g.vm) return true;
  double dt = g.lastTime > 0 ? std::min(0.1, (now - g.lastTime) / 1000.0) : 1.0 / 60;
  g.lastTime = now;
  // scripts tick on a fixed clock. After a slow frame they catch up (a step
  // costs well under a millisecond), but never for more than 8 ms in one
  // frame: past that the world runs a little slower for a moment instead of
  // each catch-up making the next frame slower still.
  double sdt = g.paused ? 0.0 : dt * g.timeScale;  // the world's time (walking keeps real time)
  if (g.paused && g.stepFrames > 0) sdt = 1.0 / 120 * g.stepFrames, g.stepFrames = 0;
  g.accumulator += sdt;
  int steps = 0;
  double stepStart = emscripten_get_now();
  while (g.accumulator >= 1.0 / 120 && steps < 12 && (steps < 2 || emscripten_get_now() - stepStart < 8)) {
    g.vm->update(1.0 / 120);
    g.stage->update(1.0f / 120);
    g.accumulator -= 1.0 / 120;
    ++steps;
  }
  double dropped = 0;
  if (g.accumulator >= 1.0 / 120) dropped = g.accumulator, g.accumulator = 0;
  syncScene();
  Stage& s = *g.stage;
  // lines, questions and the next scene go to the page
  for (const Stage::UiMessage& m : s.ui) {
    std::string opts;
    for (auto& o : m.options) opts += (opts.empty() ? "" : "\n") + o;
    EM_ASM({ if (window.loopUi) window.loopUi(UTF8ToString($0), UTF8ToString($1), UTF8ToString($2), UTF8ToString($3), $4); }, m.kind.c_str(), m.speaker.c_str(),
           m.text.c_str(), opts.c_str(), m.seconds);
  }
  s.ui.clear();
  if (!s.nextScene.empty()) {
    EM_ASM({ if (window.loopGoTo) window.loopGoTo(UTF8ToString($0)); }, s.nextScene.c_str());
    s.nextScene.clear();
  }
  Camera cam;
  float fade = 1;
  Look look = s.scene.look;
  if (g.movie) {
    g.filmTime = std::max(0.0, g.filmTime + sdt - dropped);  // the camera keeps step with the script (lines, cues)
    float len = s.film.length();
    if (len > 0 && g.filmTime > len) {
      // a story scene holds its last shot (just before it fades) until the
      // script moves on; anything else plays again
      if (s.story) g.filmTime = len - std::max(s.film.shots.back().fadeOut, 0.05f);
      else g.filmTime = 0;
    }
    int shot = 0;
    cam = s.film.at(static_cast<float>(g.filmTime), s.scene.camera, &shot, &fade);
    if (!s.film.shots[shot].look.empty()) lookByName(s.film.shots[shot].look, look);
  } else {
    if (s.teleport) {
      g.pos = s.teleportTo + Vec3(0, 1.6f, 0);
      Vec3 f = normalize(s.teleportLook - g.pos);
      g.yaw = g.yawTarget = std::atan2(f.x, -f.z);
      g.pitch = g.pitchTarget = std::asin(clamp(f.y, -1.0f, 1.0f));
      g.vel = Vec3(0);
      s.teleport = false;
    }
    cam = walkCamera(static_cast<float>(dt));
  }
  applyLookOverride(look);
  applyCameraOverride(cam);
  g.stage->setListener(cam);
  g.stage->playerPos = g.movie ? cam.position : g.pos - Vec3(0, 1.6f, 0);
  if (g.renderer->draw(cam, look, static_cast<float>(dt), static_cast<float>(g.vm->time()), fade)) {
    perfShown(now, emscripten_get_now() - g.frameStart);
    if (g.awaitingFirstFrame) {  // the page can take its loading screen away
      g.awaitingFirstFrame = false;
      EM_ASM({ if (window.loopShown) window.loopShown(); });
    }
  }
  if (perf.on) ++perf.rafFrames;
  // frames per second that reached the screen (dropped ones don't count)
  g.fpsTime += dt;
  if (g.fpsTime > 0.5) {
    uint64_t shown = g.renderer->presentedFrames();
    g.fps = static_cast<float>((shown - g.lastPresented) / g.fpsTime);
    g.lastPresented = shown, g.fpsTime = 0;
  }
  return true;
}

}  // namespace

extern "C" {
EMSCRIPTEN_KEEPALIVE void loop_run_script(const char* text) { g.pendingScript = text ? text : ""; }
EMSCRIPTEN_KEEPALIVE void loop_key(int code, int down) {
  if (code >= 0 && code < 8) g.keys[code] = down != 0;
}
EMSCRIPTEN_KEEPALIVE void loop_move(float x, float y) { g.move = {x, y}; }
EMSCRIPTEN_KEEPALIVE void loop_look(float dx, float dy) {
  g.yawTarget += dx * 0.0045f * g.lookSpeed;
  g.pitchTarget = clamp(g.pitchTarget - dy * 0.0045f * g.lookSpeed, -1.45f, 1.45f);
}
EMSCRIPTEN_KEEPALIVE void loop_set_mode(int movie) {
  g.movie = movie != 0 && g.stage && !g.stage->film.shots.empty();
  if (!g.movie && g.stage) {
    // carry on walking from where the film's camera was
    Camera c = g.stage->film.shots.empty() ? g.stage->scene.camera : g.stage->film.at(static_cast<float>(g.filmTime), g.stage->scene.camera);
    g.pos = c.position;
    Vec3 f = normalize(c.target - c.position);
    g.yaw = g.yawTarget = std::atan2(f.x, -f.z);
    g.pitch = g.pitchTarget = std::asin(clamp(f.y, -1.0f, 1.0f));
  }
}
EMSCRIPTEN_KEEPALIVE void loop_set_quality(int q) {
  g.quality = std::clamp(q, 0, 2);
  if (g.renderer) g.renderer->setQuality(qualityOf(g.quality));
}
EMSCRIPTEN_KEEPALIVE void loop_emit(const char* event) {
  if (g.vm) g.vm->emit(event ? event : "");
}
// The page's answer to `ask`: which option (0 = the first).
EMSCRIPTEN_KEEPALIVE void loop_choose(int option) {
  if (g.vm) g.vm->emit("choice", Value::number(option));
}
// Reads the next frame back and hands it to window.loopCaptured(w, h, rgbaPointer).
EMSCRIPTEN_KEEPALIVE void loop_capture() {
  if (!g.renderer) return;
  g.renderer->capture([](int w, int h, const uint8_t* rgba) {
    EM_ASM({ if (window.loopCaptured) window.loopCaptured($0, $1, $2); }, w, h, rgba);
  });
}
// The page's audio callback asks for interleaved stereo frames.
EMSCRIPTEN_KEEPALIVE void loop_audio(float* stereo, int frames) {
  if (g.stage) g.stage->mixer.render(stereo, frames);
  else std::memset(stereo, 0, sizeof(float) * frames * 2);
}
// The benchmark: start recording (the clock for "load time" starts at the
// scene's script), and stop, getting the scene's numbers as JSON.
EMSCRIPTEN_KEEPALIVE void loop_perf_begin() {
  perf.on = true;
  perf.waitingFirst = true;
  perf.serialAtBegin = g.scriptSerial;
  perf.loadMs = -1;
  perf.rafFrames = 0;
  perf.interval.clear(), perf.cpu.clear(), perf.gpu.clear(), perf.scale.clear();
}
EMSCRIPTEN_KEEPALIVE const char* loop_perf_end() {
  static std::string out;
  perf.on = false;
  const auto& iv = perf.interval;
  float p50 = pct(iv, 0.5f);
  float hitchAt = std::max(2 * p50, 25.0f);
  int hitches = 0;
  double total = 0, hitchTime = 0;
  for (float x : iv) {
    total += x;
    if (x > hitchAt) ++hitches, hitchTime += x;
  }
  uint64_t shown = iv.size();
  long dropped = static_cast<long>(perf.rafFrames) - static_cast<long>(shown) - 1;
  float maxMs = iv.empty() ? 0 : *std::max_element(iv.begin(), iv.end());
  float scaleMin = perf.scale.empty() ? 1 : *std::min_element(perf.scale.begin(), perf.scale.end());
  std::string title = g.stage ? g.stage->title : "";
  for (char& c : title)
    if (c == '"' || c == '\\' || static_cast<unsigned char>(c) < 32) c = '\'';
  char buf[1400];
  std::snprintf(buf, sizeof buf,
                "{\"title\":\"%s\",\"mode\":\"%s\",\"seconds\":%.2f,\"frames\":%llu,\"fps\":%.2f,\"avgMs\":%.2f,\"p50Ms\":%.2f,\"p95Ms\":%.2f,"
                "\"p99Ms\":%.2f,\"maxMs\":%.1f,\"low1Fps\":%.2f,\"low01Fps\":%.2f,\"hitches\":%d,\"hitchPct\":%.2f,\"dropped\":%ld,"
                "\"cpuMs\":%.2f,\"cpuP99Ms\":%.2f,\"gpuMs\":%.2f,\"gpuP95Ms\":%.2f,\"scaleAvg\":%.3f,\"scaleMin\":%.3f,\"loadMs\":%.0f,"
                "\"things\":%zu,\"lights\":%zu,\"width\":%d,\"height\":%d}",
                title.c_str(), g.movie ? "movie" : "game", total / 1000.0, static_cast<unsigned long long>(shown),
                total > 0 ? shown * 1000.0 / total : 0.0, mean(iv), p50, pct(iv, 0.95f), pct(iv, 0.99f), maxMs, lowFps(iv, 0.01f), lowFps(iv, 0.001f), hitches,
                total > 0 ? 100.0 * hitchTime / total : 0.0, std::max(0L, dropped), mean(perf.cpu), pct(perf.cpu, 0.99f), mean(perf.gpu), pct(perf.gpu, 0.95f),
                mean(perf.scale), scaleMin, perf.loadMs, g.stage ? g.stage->scene.instances.size() : 0, g.stage ? g.stage->scene.lights.size() : 0,
                EM_ASM_INT({ return document.getElementById('view').width; }), EM_ASM_INT({ return document.getElementById('view').height; }));
  out = buf;
  return out.c_str();
}
// The last 240 shown frames' times (ms), oldest first, for the live graph.
EMSCRIPTEN_KEEPALIVE const float* loop_perf_graph() {
  static float out[240];
  for (int i = 0; i < 240; ++i) out[i] = perf.ring[(perf.ringAt + i) % 240];
  return out;
}
// Benchmarks play by themselves: the player counts as near everything.
EMSCRIPTEN_KEEPALIVE void loop_set_autoplay(int on) {
  if (g.stage) g.stage->autoplay = on != 0;
  g.autoplay = on != 0;
}
// The editor: set one engine switch by name, live.
//   view (debug view 0-4), skip (pass bits), res (0 auto, 0.2-1 held),
//   lamps / rays (lamp shadows, -1 the quality's own), prepass (0/1),
//   walk / run (m/s), look (mouse speed multiplier)
// Returns 1 if the name is known.
EMSCRIPTEN_KEEPALIVE int loop_set_option(const char* name, double value) {
  std::string n = name ? name : "";
  int i = static_cast<int>(value);
  if (n == "view") gpu::g_debugView = std::clamp(i, 0, 4);
  else if (n == "skip") gpu::g_skipPasses = i & ~256;  // 256 (no screen) is for tests only
  else if (n == "res") gpu::g_fixedScale = value <= 0 ? 0.0f : std::clamp(static_cast<float>(value), 0.2f, 1.0f);
  else if (n == "lamps" || n == "rays") {
    (n == "lamps" ? gpu::g_shadowLamps : gpu::g_shadowRays) = std::max(-1, i);
    if (g.renderer) g.renderer->setQuality(qualityOf(g.quality));
  } else if (n == "prepass") gpu::g_prepass = i != 0;
  else if (n == "walk") g.walkSpeed = std::clamp(static_cast<float>(value), 0.1f, 20.0f);
  else if (n == "run") g.runSpeed = std::clamp(static_cast<float>(value), 0.1f, 40.0f);
  else if (n == "look") g.lookSpeed = std::clamp(static_cast<float>(value), 0.05f, 10.0f);
  else if (n == "pause") g.paused = i != 0;
  else if (n == "step") g.stepFrames = std::clamp(i, 1, 120);
  else if (n == "timescale") g.timeScale = std::clamp(value, 0.0, 8.0);
  else if (n == "filmtime") g.filmTime = std::max(0.0, value);
  else return 0;
  if (n == "step" || n == "filmtime") return 1;  // too frequent for the log
  logLine("set " + n + " = " + std::to_string(value));
  return 1;
}
// The editor's film look over the scene's: a JSON object of Look fields
// ({"preset":"noir","bloom":0.1,"lift":[0,0,0.02]}); "{}" gives the scene's own back.
EMSCRIPTEN_KEEPALIVE int loop_set_look(const char* json) {
  Json o;
  if (!Json::parse(json ? json : "{}", o) || !o.isObject()) return 0;
  g.lookOverride = o;
  return 1;
}
// Fog and camera fields over the scene's (fogDensity, fogAlbedo, fogAnisotropy,
// fogTop, focalLength, fStop, focusDistance, shutter, iso, exposureCompensation,
// autoExposure, blades). They stay set for every scene until "{}".
EMSCRIPTEN_KEEPALIVE int loop_set_world(const char* json) {
  Json o;
  if (!Json::parse(json ? json : "{}", o) || !o.isObject()) return 0;
  g.worldOverride = o;
  if (g.stage) applyWorldOverride(g.stage->scene), ++g.stage->version;
  return 1;
}
// What's in the scene, for the editor's outliner and inspector.
EMSCRIPTEN_KEEPALIVE const char* loop_scene_json() {
  static std::string out;
  if (!g.stage) return "{}";
  const Stage& st = *g.stage;
  const Scene& sc = st.scene;
  auto v3 = [](Vec3 v) {
    Json a = Json::array();
    a.push(Json(static_cast<double>(v.x))), a.push(Json(static_cast<double>(v.y))), a.push(Json(static_cast<double>(v.z)));
    return a;
  };
  Json root = Json::object();
  root.set("title", Json(st.title));
  root.set("mode", Json(st.mode));
  root.set("shots", Json(static_cast<int>(st.film.shots.size())));
  root.set("length", Json(static_cast<double>(st.film.length())));
  Json shots = Json::array();
  for (const Shot& sh : st.film.shots) {
    Json o = Json::object();
    o.set("name", Json(sh.name)), o.set("duration", Json(static_cast<double>(sh.duration)));
    o.set("orbit", Json(sh.orbit)), o.set("moves", Json(sh.moves)), o.set("look", Json(sh.look));
    o.set("fadeIn", Json(static_cast<double>(sh.fadeIn))), o.set("fadeOut", Json(static_cast<double>(sh.fadeOut)));
    shots.push(o);
  }
  root.set("shotList", shots);
  root.set("paused", Json(g.paused)), root.set("timeScale", Json(g.timeScale));
  root.set("meshes", Json(static_cast<int>(sc.meshes.size())));
  root.set("instances", Json(static_cast<int>(sc.instances.size())));
  Json things = Json::array();
  for (size_t i = 0; i < st.placed.size() && i < 4000; ++i) {
    const Stage::Placed& p = st.placed[i];
    Json t = Json::object();
    t.set("name", Json(p.name));
    t.set("kind", Json(std::string(1, p.kind)));
    t.set("position", v3(p.position)), t.set("rotation", v3(p.rotation)), t.set("scale", v3(p.scale));
    t.set("visible", Json(p.visible)), t.set("solid", Json(p.body >= 0));
    if (p.instance >= 0 && p.instance < static_cast<int>(sc.instances.size())) {
      int m = sc.instances[p.instance].material;
      if (m >= 0 && m < static_cast<int>(sc.materials.size())) t.set("material", Json(sc.materials[m].name));
    }
    things.push(t);
  }
  root.set("things", things);
  Json lights = Json::array();
  for (const Light& l : sc.lights) {
    Json o = Json::object();
    const char* kinds[] = {"point", "spot", "rect", "sun"};
    o.set("kind", Json(kinds[static_cast<int>(l.kind)]));
    o.set("position", v3(l.position)), o.set("direction", v3(l.direction)), o.set("color", v3(l.color));
    o.set("lumens", Json(static_cast<double>(l.lumens))), o.set("lux", Json(static_cast<double>(l.lux)));
    o.set("shadows", Json(l.castShadows));
    lights.push(o);
  }
  root.set("lights", lights);
  Json people = Json::array();
  for (size_t i = 0; i < st.people.size(); ++i) {
    Json o = Json::object();
    o.set("walking", Json(st.people[i].walking)), o.set("visible", Json(st.people[i].visible));
    o.set("speed", Json(static_cast<double>(st.people[i].speed)));
    people.push(o);
  }
  root.set("people", people);
  root.set("sounds", Json(static_cast<int>(st.sounds.size())));
  const Camera& c = sc.camera;
  Json cam = Json::object();
  cam.set("position", v3(c.position)), cam.set("target", v3(c.target));
  cam.set("focalLength", Json(static_cast<double>(c.focalLength))), cam.set("fStop", Json(static_cast<double>(c.fStop)));
  cam.set("iso", Json(static_cast<double>(c.iso))), cam.set("autoExposure", Json(c.autoExposure));
  root.set("camera", cam);
  Json fog = Json::object();
  fog.set("density", Json(static_cast<double>(sc.fog.density))), fog.set("albedo", v3(sc.fog.albedo));
  root.set("fog", fog);
  Json look = Json::object();
  look.set("name", Json(sc.look.name));
  look.set("bloom", Json(static_cast<double>(sc.look.bloom))), look.set("vignette", Json(static_cast<double>(sc.look.vignette)));
  look.set("contrast", Json(static_cast<double>(sc.look.contrast))), look.set("saturation", Json(static_cast<double>(sc.look.saturation)));
  root.set("look", look);
  Json walker = Json::object();
  walker.set("position", v3(g.pos)), walker.set("yaw", Json(static_cast<double>(g.yaw))), walker.set("pitch", Json(static_cast<double>(g.pitch)));
  root.set("player", walker);
  out = root.dump();
  return out.c_str();
}
// The engine's messages since the last call (scene starts, problems, settings).
EMSCRIPTEN_KEEPALIVE const char* loop_log() {
  static std::string out;
  out.swap(g.log);
  g.log.clear();
  return out.c_str();
}
EMSCRIPTEN_KEEPALIVE const char* loop_stats() {
  static char buf[512];
  if (!g.stage) return "{}";
  std::snprintf(buf, sizeof buf, "{\"fps\":%.1f,\"cpu\":%.1f,\"gpu\":%.1f,\"scale\":%.2f,\"things\":%zu,\"lights\":%zu,\"draws\":%zu,\"ratio\":%.2f,\"time\":%.2f,\"movie\":%d,\"film\":%.2f,\"inflight\":%d,\"gpuLast\":%.1f}", g.fps, g.cpuMs, g.renderer ? g.renderer->gpuMs() : 0.0f,
                g.renderer ? g.renderer->renderScale() : 1.0f, g.stage->scene.instances.size(), g.stage->scene.lights.size(), g.renderer ? g.renderer->drawCalls() : 0,
                g.renderer ? g.renderer->pixelRatio() : 1.0f, g.vm ? g.vm->time() : 0.0, g.movie ? 1 : 0, g.filmTime, g.renderer ? g.renderer->framesInFlight() : 0, g.renderer ? g.renderer->lastGpuMs() : 0.0f);
  return buf;
}
}

int main() {
  gpu::g_debugView = EM_ASM_INT({ return +(new URLSearchParams(location.search).get('view') || 0); });
  gpu::g_skipPasses = EM_ASM_INT({ return +(new URLSearchParams(location.search).get('skip') || 0); });
  gpu::g_fixedScale = static_cast<float>(EM_ASM_DOUBLE({ return +(new URLSearchParams(location.search).get('res') || 0); }));
  gpu::Renderer::create("#view", [](std::unique_ptr<gpu::Renderer> r) {
    if (!r) {
      EM_ASM({ if (window.loopNoGpu) window.loopNoGpu(); });
      return;
    }
    g.renderer = std::move(r);
    bool phone = EM_ASM_INT({ return /iPhone|iPad|Android/i.test(navigator.userAgent) ? 1 : 0; });
    g.quality = phone ? 0 : 1;
    g.renderer->setQuality(qualityOf(g.quality));
    EM_ASM({ if (window.loopReady) window.loopReady(); });
    emscripten_request_animation_frame_loop(frame, nullptr);
  });
  return 0;
}
