// The Loop3D player: runs a Looped Script world in the browser and draws it
// live with WebGPU. In "movie" mode it plays the script's shots; in "game"
// mode you walk (left thumb / WASD) and look (right thumb / mouse).
//
// Motion is smooth at any frame rate: scripts run on a fixed 120 Hz clock,
// walking and looking ease toward where the controls point (no snapping),
// and the camera never jumps between frames.
#include <emscripten/emscripten.h>
#include <emscripten/heap.h>
#include <emscripten/html5.h>
#include <malloc.h>

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <vector>
#include <memory>
#include <string>

#include "engine/core/json.h"
#include "engine/core/jobs.h"
#include "engine/core/meter.h"
#include "engine/core/settings.h"
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
  Vec3 prevPos{0, 1.6f, 5};  // the walking body's eye one step ago (the eye is drawn between)
  double walkAcc = 0;        // the walking body's own fixed clock (real time, also while paused)
  float alpha = 1;           // how far the world's clock is into its next step (drawing blends)
  std::vector<int> movedIds;
  std::vector<Mat4> movedXfs;
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
  bool paused = false;    // the editor's pause: the world and the film hold, the view still moves
  bool halted = false;    // script.onError "stop": the scene's script stopped at a problem
  size_t problemsSeen = 0;
  int stepFrames = 0;     // paused: advance this many script steps
  double sunClock = 0;    // the day cycle: world seconds since the sun last moved
  float aspect = 16.0f / 9;
  Camera lastCam;
  std::string log;  // the engine's messages for the editor's console
  // The editor's own camera (it turns round what you look at, slides and
  // zooms) over the walk and the film while it's on; the world keeps running.
  bool editCam = false;
  Vec3 editAt{0, 2, 6}, editLook{0, 0.5f, 0};
  float editLens = 0;  // mm; 0 = the scene camera's lens
  // The editor's work light: a lamp at the editor camera so things can be
  // seen while building a dark world. Only while the editor camera is on;
  // never part of the world (the script doesn't know it).
  bool workLight = false;
  int workIndex = -1;  // its place in the scene's lights, or -1
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
  mix(s.sky.color.x), mix(s.sky.color.y), mix(s.sky.color.z), mix(s.sky.groundAlbedo.x), mix(s.sky.groundAlbedo.y), mix(s.sky.groundAlbedo.z);
  for (const Light& l : s.lights)
    if (l.kind == Light::Kind::kSun)
      mix(l.direction.x), mix(l.direction.y), mix(l.direction.z), mix(l.lux), mix(l.radius), mix(l.color.x), mix(l.color.y), mix(l.color.z);
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

Light* sunOf(Scene& s) {
  for (Light& l : s.lights)
    if (l.kind == Light::Kind::kSun) return &l;
  return nullptr;
}
// sun.elevation and sun.azimuth (degrees) → the light's direction
void aimSun(Light& sun, float elevDeg, float azDeg) {
  float e = elevDeg * kPi / 180, a = azDeg * kPi / 180;
  Vec3 toSun{std::cos(e) * std::sin(a), std::sin(e), -std::cos(e) * std::cos(a)};
  sun.direction = -toSun;
}
void sunAngles(const Light& sun, float& elevDeg, float& azDeg) {
  Vec3 t = -sun.direction;
  elevDeg = std::asin(clamp(t.y, -1.0f, 1.0f)) * 180 / kPi;
  azDeg = std::atan2(t.x, -t.z) * 180 / kPi;
  if (azDeg < 0) azDeg += 360;
}

// The editor's fog, sky and sun settings over the scene's.
void applyWorldOverride(Scene& s) {
  const Json& o = g.worldOverride;
  if (o.size() == 0) return;
  s.fog.density = num(o, "fogDensity", s.fog.density), s.fog.albedo = vec(o, "fogAlbedo", s.fog.albedo);
  s.fog.anisotropy = num(o, "fogAnisotropy", s.fog.anisotropy), s.fog.top = num(o, "fogTop", s.fog.top);
  if (o.has("skyKind")) {
    const std::string& k = o["skyKind"].string();
    s.sky.kind = k == "color" ? Sky::Kind::kColor : k == "none" ? Sky::Kind::kNone : Sky::Kind::kPhysical;
  }
  s.sky.turbidity = num(o, "skyTurbidity", s.sky.turbidity), s.sky.intensity = num(o, "skyIntensity", s.sky.intensity);
  if (o.has("skyStars")) s.sky.starNits = num(o, "skyStars", 0) * 0.02f;  // 0..1 → a clear night's stars
  if (o.has("skyColor") && o["skyColor"].isString()) {
    unsigned rgb = 0;
    if (std::sscanf(o["skyColor"].string().c_str(), "#%6x", &rgb) == 1)
      s.sky.color = Vec3(((rgb >> 16) & 255) / 255.0f, ((rgb >> 8) & 255) / 255.0f, (rgb & 255) / 255.0f);
  }
  s.sky.groundAlbedo = vec(o, "skyGround", s.sky.groundAlbedo);
  if (o.has("sunElevation") || o.has("sunAzimuth") || o.has("sunLux") || o.has("sunSize")) {
    Light* sun = sunOf(s);
    if (!sun) {
      s.lights.push_back(Light());
      sun = &s.lights.back();
      sun->kind = Light::Kind::kSun;
      aimSun(*sun, 45, 180);
    }
    float e, a;
    sunAngles(*sun, e, a);
    aimSun(*sun, num(o, "sunElevation", e), num(o, "sunAzimuth", a));
    sun->lux = num(o, "sunLux", sun->lux);
    if (o.has("sunSize")) sun->radius = num(o, "sunSize", 0.53f) * kPi / 360;  // degrees across → radius (rad)
  }
}

// sun.dayCycle: the sun goes round once every sun.daySpeed minutes of world time.
void dayCycle(double dt) {
  const Json& o = g.worldOverride;
  if (!o.has("dayCycle") || !o["dayCycle"].boolean(false) || !g.stage) return;
  Light* sun = sunOf(g.stage->scene);
  if (!sun) return;
  g.sunClock += dt;
  if (g.sunClock < 0.25) return;  // the sky is rebuilt at most four times a second
  float minutes = std::max(0.1f, num(o, "daySpeed", 24));
  float e, a;
  sunAngles(*sun, e, a);
  // move along the arc from east (rising) over the top to west, then below
  float step = static_cast<float>(g.sunClock / (minutes * 60) * 360);
  g.sunClock = 0;
  Vec3 t = -sun->direction;
  Vec3 axis = normalize(Vec3(std::cos(a * kPi / 180), 0, std::sin(a * kPi / 180)));
  Quat q = Quat::axisAngle(axis, step * kPi / 180);
  sun->direction = -normalize(q.rotate(t));
  ++g.stage->version;
}
void applyCameraOverride(Camera& c) {
  const Json& o = g.worldOverride;
  if (o.size() == 0) return;
  c.focalLength = num(o, "focalLength", c.focalLength), c.fStop = num(o, "fStop", c.fStop);
  c.focusDistance = num(o, "focusDistance", c.focusDistance), c.shutter = num(o, "shutter", c.shutter), c.iso = num(o, "iso", c.iso);
  c.exposureCompensation = num(o, "exposureCompensation", c.exposureCompensation);
  if (o.has("sensor")) sensorByName(o["sensor"].string(), c.sensor);
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
  g.stage->folder = "/scene";  // the page puts the scene's models in /scene/models
  g.vm->onSay = [](const std::string& s) { EM_ASM({ if (window.loopSay) window.loopSay(UTF8ToString($0)); }, s.c_str()); };
  g.problems.clear();
  g.workIndex = -1;  // a new world: the work light is added again if it's on
  g.vm->load(src, "scene.loop");
  for (auto& p : g.vm->problems()) g.problems += p.text() + "\n";
  g.problemsSeen = g.vm->problems().size();
  g.halted = false;
  g.sunClock = 0;
  if (g.problemsSeen) {
    const std::string& onError = config().script.onError;
    if (onError == "stop") g.halted = true;
    else if (onError == "pause") g.paused = true;
  }
  logLine("scene started: " + g.stage->title + " (" + std::to_string(g.stage->scene.instances.size()) + " things, " +
          std::to_string(g.stage->scene.lights.size()) + " lights)");
  if (g.worldOverride.size()) applyWorldOverride(g.stage->scene), ++g.stage->version;
  if (!g.problems.empty()) EM_ASM({ if (window.loopProblems) window.loopProblems(UTF8ToString($0)); }, g.problems.c_str());
  forgetSynced();
  g.movie = g.stage->mode == "movie" && !g.stage->film.shots.empty();
  g.filmTime = 0;
  const Camera& c = g.stage->scene.camera;
  g.pos = g.prevPos = c.position;
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
    std::string why;
    if (s.decodePictures(&why)) logLine("a model's picture can't be read: " + why);  // the models' pictures, all at once
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
    s.takeMoves(1, g.movedIds, g.movedXfs);  // the whole world was just read, as it is
    return;
  }
  if (s.lightsVersion != g.seenLights) {
    g.renderer->syncLights(s.scene);
    g.seenLights = s.lightsVersion;
  }
  // only what moved, drawn between the clock's last two steps
  s.takeMoves(g.alpha, g.movedIds, g.movedXfs);
  if (!g.movedIds.empty()) g.renderer->moveInstances(s.scene, g.movedIds, g.movedXfs);
  g.seenTransforms = s.transformsVersion;
}

// The walking body moves in fixed steps (the same at any frame rate); the eye
// is drawn between its last two places, and the look follows every frame.
void stepWalker(float dt) {
  // ease the body toward the speed asked for: starting and stopping feel like
  // a person, not a robot
  const Config::Player& pc = config().player;
  const Config::Physics& body = config().physics;
  float kMove = 1 - std::exp(-pc.accel * dt);
  g.prevPos = g.pos;
  Vec2 m = g.move;
  if (g.keys[0]) m.y += 1;
  if (g.keys[1]) m.y -= 1;
  if (g.keys[2]) m.x -= 1;
  if (g.keys[3]) m.x += 1;
  if (length(m) > 1) m = normalize(m);
  float speed = g.keys[4] ? pc.run : pc.walk;
  Vec3 fwd{std::sin(g.yaw), 0, -std::cos(g.yaw)}, right{std::cos(g.yaw), 0, std::sin(g.yaw)};
  Vec3 want = (fwd * m.y + right * m.x) * speed;
  g.vel += (want - g.vel) * kMove;
  // walk through the world, not through its walls: slide and step like a body
  // the walking body (physics.char*): a capsule whose centre is below the eyes
  float half = std::max(0.05f, body.charHeight * 0.5f - body.charRadius);
  Vec3 centre = g.pos - Vec3(0, pc.eye - body.charHeight * 0.5f, 0);
  MeterScope walkTime(Part::kPhysics);  // the walking body is physics work
  g.stage->world.moveCharacter(centre, Vec3(g.vel.x * dt, 0, g.vel.z * dt), body.charRadius, half, body.charStep);
  g.pos.x = centre.x, g.pos.z = centre.z;
  // stand on the land, or on whatever fixed thing is underfoot (a dock, a
  // bridge, a stair), with a gentle step bob when walking
  float ground = 0;
  if (!g.stage->terrains.empty()) ground = g.stage->terrains.back().height(g.pos.x, g.pos.z);
  float feet = g.pos.y - pc.eye;
  float floor = g.stage->world.floorUnder(Vec3(g.pos.x, feet, g.pos.z), body.charStep);
  if (!std::isnan(floor)) ground = g.stage->terrains.empty() ? floor : std::max(ground, floor);  // never below the land
  static float bobPhase = 0;
  float sp = length(g.vel);
  bobPhase += sp * dt * 2.1f;
  float bob = std::sin(bobPhase * kTwoPi) * pc.bob * std::min(1.0f, sp / 1.6f);
  float eye = ground + pc.eye + bob;
  g.pos.y += (eye - g.pos.y) * (1 - std::exp(-12 * dt));
}
Camera walkCamera(float dt, float alpha) {
  // ease the look toward where the controls point
  float kLook = 1 - std::exp(-config().camera.smoothing * dt);
  g.yaw += (g.yawTarget - g.yaw) * kLook;
  g.pitch += (g.pitchTarget - g.pitch) * kLook;
  Vec3 eyeAt = vlerp(g.prevPos, g.pos, alpha);
  Camera c = g.stage->scene.camera;
  c.position = eyeAt;
  // cam.shake: a little handheld drift
  float sh = config().camera.shake;
  float t = static_cast<float>(emscripten_get_now() / 1000.0);
  float sy = sh * 0.02f * (std::sin(t * 1.3f) + 0.5f * std::sin(t * 3.1f)), sp2 = sh * 0.015f * (std::sin(t * 1.7f + 1) + 0.5f * std::sin(t * 2.3f));
  Vec3 dir{std::sin(g.yaw + sy) * std::cos(g.pitch + sp2), std::sin(g.pitch + sp2), -std::cos(g.yaw + sy) * std::cos(g.pitch + sp2)};
  c.target = eyeAt + dir;
  return c;
}

// The work light follows the editor camera, brighter the farther it looks.
void updateWorkLight(Stage& s, const Camera& cam) {
  bool want = g.workLight && g.editCam;
  int n = static_cast<int>(s.scene.lights.size());
  if (g.workIndex >= n) g.workIndex = -1;
  if (want) {
    if (g.workIndex < 0) {
      Light l;
      l.castShadows = false, l.radius = 0.05f, l.color = Vec3(1);
      s.scene.lights.push_back(l);
      g.workIndex = n;
    }
    Light& l = s.scene.lights[g.workIndex];
    float d = length(cam.target - cam.position) / 4;
    l.position = cam.position + Vec3(0, 0.3f, 0);
    l.lumens = 1500 * (1 + d * d);
    ++s.lightsVersion;
  } else if (g.workIndex >= 0) {
    s.scene.lights.erase(s.scene.lights.begin() + g.workIndex);
    g.workIndex = -1;
    ++s.version;
  }
}

float viewAspect();
bool frameBody(double now);
// The time each frame spends on the CPU (script, world, sending to the GPU),
// smoothed: shown in the stats so a slow script is easy to tell from a slow GPU.
bool frame(double now, void*) {
  double t0 = emscripten_get_now();
  g.frameStart = t0;
  bool loading = !g.pendingScript.empty();  // this frame starts a scene
  bool r = frameBody(now);
  if (loading) {
    Meter::get().reset();  // the loading doesn't linger in the readings
  } else {
    Meter::get().endFrame(emscripten_get_now() - t0);
    g.cpuMs = Meter::get().frameMs();
  }
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
  const Config::Script& sc = config().script;
  double sdt = g.paused ? 0.0 : dt * sc.timeScale;  // the world's time (walking keeps real time)
  if (g.paused && g.stepFrames > 0) sdt = 1.0 / 120 * g.stepFrames, g.stepFrames = 0;
  g.accumulator += sdt;
  int steps = 0;
  double stepStart = emscripten_get_now();
  // script.tick steps a second; at most script.maxSteps and script.catchUp ms a frame
  const double tick = 1.0 / std::clamp(sc.tick, 30, 480);
  while (g.accumulator >= tick && steps < sc.maxSteps && (steps < 2 || emscripten_get_now() - stepStart < sc.catchUp)) {
    if (!g.halted) {
      MeterScope scriptTime(Part::kScript);
      g.vm->update(tick);
    }
    g.stage->update(static_cast<float>(tick));
    // the day and the film move with the world, step by step
    dayCycle(tick);
    if (g.movie) g.filmTime += tick;  // the camera keeps step with the script (lines, cues)
    g.accumulator -= tick;
    ++steps;
  }
  if (g.accumulator >= tick) g.accumulator = 0;  // too far behind: the world runs a little slower instead
  // how far into the next step: the drawing blends between the last two
  // (paused, things are drawn exactly where they are, as the editor moves them)
  g.alpha = g.paused ? 1.0f : static_cast<float>(g.accumulator / tick);
  // the walking body, on its own fixed steps (real time: it walks while the
  // world is paused or slowed)
  if (!g.movie) {
    g.walkAcc = std::min(g.walkAcc + dt, 0.25);
    while (g.walkAcc >= tick) stepWalker(static_cast<float>(tick)), g.walkAcc -= tick;
  }
  // a problem while running: script.onError says what happens
  if (g.vm->problems().size() > g.problemsSeen) {
    std::string fresh;
    for (size_t i = g.problemsSeen; i < g.vm->problems().size(); ++i) fresh += g.vm->problems()[i].text() + "\n";
    g.problemsSeen = g.vm->problems().size();
    EM_ASM({ if (window.loopProblems) window.loopProblems(UTF8ToString($0)); }, fresh.c_str());
    if (sc.onError == "stop") g.halted = true, logLine("the script stopped at a problem (script.onError: stop)");
    else if (sc.onError == "pause") g.paused = true, logLine("paused at a problem (script.onError: pause)");
  }
  {
    MeterScope sendTime(Part::kSend);
    syncScene();
  }
  Stage& s = *g.stage;
  s.autoplay = g.autoplay || config().script.autoplay;
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
    float len = s.film.length();
    if (len > 0 && g.filmTime > len) {
      // a story scene holds its last shot (just before it fades) until the
      // script moves on; anything else plays again
      if (s.story) g.filmTime = len - std::max(s.film.shots.back().fadeOut, 0.05f);
      else g.filmTime = 0;
    }
    int shot = 0;
    // between steps, as the things it films
    double at = g.filmTime + (g.paused ? 0.0 : g.alpha * tick);
    if (len > 0) at = std::min<double>(at, len);
    cam = s.film.at(static_cast<float>(at), s.scene.camera, &shot, &fade);
    if (!s.film.shots[shot].look.empty()) lookByName(s.film.shots[shot].look, look);
  } else {
    if (s.teleport) {
      g.pos = g.prevPos = s.teleportTo + Vec3(0, 1.6f, 0);
      Vec3 f = normalize(s.teleportLook - g.pos);
      g.yaw = g.yawTarget = std::atan2(f.x, -f.z);
      g.pitch = g.pitchTarget = std::asin(clamp(f.y, -1.0f, 1.0f));
      g.vel = Vec3(0);
      s.teleport = false;
    }
    cam = walkCamera(static_cast<float>(dt), static_cast<float>(g.walkAcc / tick));
  }
  if (g.editCam) {
    cam = s.scene.camera;
    cam.position = g.editAt, cam.target = g.editLook, cam.up = Vec3(0, 1, 0);
    if (g.editLens > 0) cam.focalLength = g.editLens;
    cam.positionEnd = Vec3(kInf);
  }
  applyLookOverride(look);
  applyCameraOverride(cam);
  if (config().camera.portraitWide) cam.widenForPortrait(viewAspect());
  g.lastCam = cam;
  updateWorkLight(s, cam);
  g.stage->setListener(cam);
  g.stage->playerPos = g.movie ? cam.position : g.pos - Vec3(0, 1.6f, 0);
  double drawStart = Meter::nowMs();
  bool drawn = g.renderer->draw(cam, look, static_cast<float>(dt), static_cast<float>(g.vm->time()), fade);
  Meter::get().add(Part::kDraw, Meter::nowMs() - drawStart);
  if (drawn) {
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

float viewAspect() {
  double cw = 16, ch = 9;
  emscripten_get_element_css_size("#view", &cw, &ch);
  g.aspect = static_cast<float>(cw / std::max(ch, 1.0));
  return g.aspect;
}
Json vec3Json(Vec3 v) {
  Json a = Json::array();
  a.push(Json(static_cast<double>(v.x))), a.push(Json(static_cast<double>(v.y))), a.push(Json(static_cast<double>(v.z)));
  return a;
}
Json matJson(const Mat4& m) {
  Json a = Json::array();
  for (int c = 0; c < 4; ++c)
    for (int r = 0; r < 4; ++r) a.push(Json(static_cast<double>(m.m[c][r])));
  return a;
}
}  // namespace

extern "C" {
EMSCRIPTEN_KEEPALIVE void loop_run_script(const char* text) { g.pendingScript = text ? text : ""; }
EMSCRIPTEN_KEEPALIVE void loop_key(int code, int down) {
  if (code >= 0 && code < 8) g.keys[code] = down != 0;
}
EMSCRIPTEN_KEEPALIVE void loop_move(float x, float y) { g.move = {x, y}; }
EMSCRIPTEN_KEEPALIVE void loop_look(float dx, float dy) {
  const Config::Player& pc = config().player;
  g.yawTarget += dx * 0.0045f * pc.look;
  g.pitchTarget = clamp(g.pitchTarget - dy * 0.0045f * pc.look * (pc.invertY ? -1 : 1), -1.45f, 1.45f);
}
EMSCRIPTEN_KEEPALIVE void loop_set_mode(int movie) {
  g.movie = movie != 0 && g.stage && !g.stage->film.shots.empty();
  if (!g.movie && g.stage) {
    // carry on walking from where the film's camera was
    Camera c = g.stage->film.shots.empty() ? g.stage->scene.camera : g.stage->film.at(static_cast<float>(g.filmTime), g.stage->scene.camera);
    g.pos = g.prevPos = c.position;
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
// The benchmark and the live recorder: start recording (the clock for "load
// time" starts at the scene's script), and stop, getting the scene's numbers
// as JSON.
EMSCRIPTEN_KEEPALIVE void loop_perf_begin() {
  if (g.renderer) g.renderer->takeStepAverages();  // the steps' times count from here
  perf.on = true;
  perf.waitingFirst = true;
  perf.serialAtBegin = g.scriptSerial;
  perf.loadMs = -1;
  perf.rafFrames = 0;
  perf.interval.clear(), perf.cpu.clear(), perf.gpu.clear(), perf.scale.clear();
}
// Each step's time (the frame graph's, from the graphics chip's timers) put
// into a JSON object before its closing brace: ...,"steps":{"shadow":0.42,...}}
void addSteps(std::string& json, const std::vector<std::pair<std::string, float>>& steps) {
  std::string o = ",\"steps\":{";
  char num[32];
  for (size_t i = 0; i < steps.size(); ++i) {
    std::snprintf(num, sizeof num, "%.3f", static_cast<double>(steps[i].second));
    o += (i ? ",\"" : "\"") + steps[i].first + "\":" + num;
  }
  json.insert(json.size() - 1, o + "}");
}

// Drawing in clusters: the pieces the view drew a moment ago, its first pass
// and its second (what had been hidden last frame and showed in this one),
// and the sun's shadow's (at its last redraw).
void addPieces(std::string& json) {
  if (!g.renderer || !g.renderer->clustered()) return;
  auto [first, second, sun] = g.renderer->piecesDrawn();
  json.insert(json.size() - 1, ",\"pieces\":[" + std::to_string(first) + "," + std::to_string(second) + "]" +
                               ",\"sunPieces\":" + std::to_string(sun));
}

// The numbers recorded so far, as JSON.
const char* perfJson() {
  static std::string out;
  const auto& iv = perf.interval;
  float p50 = pct(iv, 0.5f);
  float hitchAt = std::max(2 * p50, config().perf.hitch);
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
  addPieces(out);
  // each step's average over these frames (then counted again from here)
  if (g.renderer && g.renderer->timers()) addSteps(out, g.renderer->takeStepAverages());
  return out.c_str();
}
EMSCRIPTEN_KEEPALIVE const char* loop_perf_end() {
  perf.on = false;
  return perfJson();
}
// The live recorder cuts a long visit into parts: the numbers since the last
// part, and on at once with the next (the load time counts in a scene's first).
EMSCRIPTEN_KEEPALIVE const char* loop_perf_part() {
  const char* out = perfJson();
  perf.interval.clear(), perf.cpu.clear(), perf.gpu.clear(), perf.scale.clear();
  perf.rafFrames = 0;
  if (!perf.waitingFirst) perf.loadMs = -1;
  return out;
}
// Recording again at once, in the same scene (back from another app): the
// time away is not a frame.
EMSCRIPTEN_KEEPALIVE void loop_perf_go() {
  if (g.renderer) g.renderer->takeStepAverages();
  perf.on = true;
  perf.waitingFirst = false;
  perf.loadMs = -1;
  perf.rafFrames = 0;
  perf.lastShown = 0;
  perf.interval.clear(), perf.cpu.clear(), perf.gpu.clear(), perf.scale.clear();
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
// A setting by name in the editor's layer (Json text; "null" gives the
// project's or scene's value back). Returns 1, or 0 with the reason logged.
EMSCRIPTEN_KEEPALIVE int loop_set_setting(const char* id, const char* json) {
  Json v;
  if (!Json::parse(json ? json : "null", v)) return 0;
  std::string err;
  if (!Settings::get().set(Settings::kEditor, id ? id : "", v, &err)) {
    logLine(err);
    return 0;
  }
  return 1;
}
// A setting's value in effect, as JSON.
EMSCRIPTEN_KEEPALIVE const char* loop_get_setting(const char* id) {
  static std::string out;
  out = Settings::get().value(id ? id : "").dump();
  return out.c_str();
}
// Every engine setting: id, kind, default, value, range, help, layers.
EMSCRIPTEN_KEEPALIVE const char* loop_settings_json() {
  static std::string out;
  out = Settings::get().list().dump();
  return out.c_str();
}
// The project's settings (project.json "settings"), under the scene's and the editor's.
EMSCRIPTEN_KEEPALIVE int loop_set_project_settings(const char* json) {
  Json v;
  if (!Json::parse(json ? json : "{}", v) || !v.isObject()) return 0;
  Settings::get().clear(Settings::kProject);
  std::string err;
  bool ok = Settings::get().setMany(Settings::kProject, v, &err);
  if (!ok) logLine("project settings: " + err);
  return ok ? 1 : 0;
}
// Older name for a few settings, and the editor's play controls:
//   pause, step (frames), filmtime (s), skip (test pass bits)
EMSCRIPTEN_KEEPALIVE int loop_set_option(const char* name, double value) {
  std::string n = name ? name : "";
  int i = static_cast<int>(value);
  if (n == "pause") g.paused = i != 0;
  else if (n == "step") g.stepFrames = std::clamp(i, 1, 120);
  else if (n == "filmtime") g.filmTime = std::max(0.0, value);
  else if (n == "skip") gpu::g_skipPasses = i & ~256;  // 256 (no screen) is for tests only
  else if (n == "worklight") g.workLight = i != 0;
  else {
    static const std::pair<const char*, const char*> alias[] = {{"view", "render.view"},  {"res", "render.res"},   {"lamps", "render.lamps"},
                                                                {"rays", "render.rays"},  {"prepass", "render.prepass"}, {"cull", "render.cull"}, {"lod", "world.lodBias"}, {"walk", "player.walk"},
                                                                {"run", "player.run"},    {"look", "player.look"},   {"timescale", "script.timeScale"}};
    for (auto& [from, to] : alias)
      if (n == from) return Settings::get().set(Settings::kEditor, to, Json(value)) ? 1 : 0;
    return 0;
  }
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
  root.set("paused", Json(g.paused)), root.set("timeScale", Json(static_cast<double>(config().script.timeScale)));
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
    t.set("size", v3(p.size * p.scale));
    t.set("line", Json(p.line));
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
  // where things are on screen, for the editor's overlay (light icons, boxes)
  {
    double cw = 16, ch = 9;
    emscripten_get_element_css_size("#view", &cw, &ch);
    g.aspect = static_cast<float>(cw / std::max(ch, 1.0));
    const Camera& lc = g.lastCam;
    Mat4 vp = Mat4::perspective(lc.fovY(), g.aspect, 0.05f, std::max(config().world.drawDistance, 1.0f)) * Mat4::lookAt(lc.position, lc.target, lc.up);
    Json m = Json::array();
    for (int c = 0; c < 4; ++c)
      for (int r = 0; r < 4; ++r) m.push(Json(static_cast<double>(vp.m[c][r])));
    root.set("viewProj", m);
  }
  Json walker = Json::object();
  walker.set("position", v3(g.pos)), walker.set("yaw", Json(static_cast<double>(g.yaw))), walker.set("pitch", Json(static_cast<double>(g.pitch)));
  root.set("player", walker);
  out = root.dump();
  return out.c_str();
}
// ---- the editor's hands on the world

// The editor camera: {"on": true, "at": [x, y, z], "look_at": [x, y, z], "lens": 24}.
// Off ({"on": false}) gives the walk or the film back.
EMSCRIPTEN_KEEPALIVE int loop_edit_camera(const char* json) {
  Json o;
  if (!Json::parse(json ? json : "{}", o) || !o.isObject()) return 0;
  auto vec = [&](const char* k, Vec3 fallback) {
    if (!o.has(k)) return fallback;
    const Json& a = o[k];
    if (!a.isArray() || a.size() < 3) return fallback;
    return Vec3(static_cast<float>(a[0].number()), static_cast<float>(a[1].number()), static_cast<float>(a[2].number()));
  };
  bool wasOn = g.editCam;
  if (o.has("on")) g.editCam = o["on"].boolean();
  g.editAt = vec("at", g.editAt), g.editLook = vec("look_at", g.editLook);
  if (o.has("lens")) g.editLens = static_cast<float>(o["lens"].number());
  if (g.editCam) g.move = Vec2(0);
  if (wasOn && !g.editCam && g.stage && !g.movie) {
    // walking carries on from where the editor camera was
    g.pos = g.prevPos = g.editAt;
    Vec3 f = normalize(g.editLook - g.editAt);
    g.yaw = g.yawTarget = std::atan2(f.x, -f.z);
    g.pitch = g.pitchTarget = std::asin(clamp(f.y, -1.0f, 1.0f));
    g.vel = Vec3(0);
  }
  return 1;
}


// What's under a point of the view (x, y from 0 to 1, from the top left):
// the placed thing's index, or -1. The ray leaves the camera shown last.
EMSCRIPTEN_KEEPALIVE int loop_pick(float x, float y) {
  if (!g.stage) return -1;
  const Camera& c = g.lastCam;
  float aspect = viewAspect();
  float ty = std::tan(c.fovY() * 0.5f), tx = ty * aspect;
  Vec3 f = normalize(c.target - c.position), r = normalize(cross(f, c.up)), u = cross(r, f);
  Vec3 d = normalize(f + r * ((2 * x - 1) * tx) + u * ((1 - 2 * y) * ty));
  return g.stage->pick(Ray{c.position, d});
}

// The view as last drawn, light enough to ask every frame: where 3D points
// land on screen (viewProj, column by column) and the camera.
EMSCRIPTEN_KEEPALIVE const char* loop_view() {
  static std::string out;
  const Camera& c = g.lastCam;
  float aspect = viewAspect();
  Json o = Json::object();
  o.set("viewProj", matJson(Mat4::perspective(c.fovY(), aspect, 0.05f, std::max(config().world.drawDistance, 1.0f)) * Mat4::lookAt(c.position, c.target, c.up)));
  o.set("at", vec3Json(c.position)), o.set("look_at", vec3Json(c.target));
  o.set("fovY", Json(static_cast<double>(c.fovY()))), o.set("aspect", Json(static_cast<double>(aspect)));
  o.set("lens", Json(static_cast<double>(c.focalLength)));
  o.set("edit", Json(g.editCam));
  out = o.dump();
  return out.c_str();
}

// One thing in full: where it is, its line in the script, and its box in its
// own space with the matrix that places it (the editor draws its outline).
EMSCRIPTEN_KEEPALIVE const char* loop_thing(int index) {
  static std::string out;
  if (!g.stage || index < 0 || index >= static_cast<int>(g.stage->placed.size())) return "null";
  const Stage::Placed& p = g.stage->placed[index];
  const Scene& sc = g.stage->scene;
  AABB box;
  auto grow = [&](int inst) {
    if (inst < 0 || inst >= static_cast<int>(sc.instances.size())) return;
    int m = sc.instances[inst].mesh;
    if (m >= 0 && m < static_cast<int>(sc.meshes.size())) box.grow(sc.meshes[m].bounds());
  };
  grow(p.instance);
  for (int more : p.moreInstances) grow(more);
  if (!box.valid()) box = AABB(Vec3(-0.5f), Vec3(0.5f));
  Json o = Json::object();
  o.set("index", Json(index)), o.set("name", Json(p.name)), o.set("line", Json(p.line));
  o.set("kind", Json(std::string(1, p.kind)));
  o.set("position", vec3Json(p.position)), o.set("rotation", vec3Json(p.rotation)), o.set("scale", vec3Json(p.scale));
  o.set("visible", Json(p.visible)), o.set("solid", Json(p.body >= 0));
  o.set("lo", vec3Json(box.lo)), o.set("hi", vec3Json(box.hi));
  Quat q = p.useQuat ? p.quat : Quat::euler(p.rotation.x, p.rotation.y, p.rotation.z);
  o.set("matrix", matJson(Mat4::trs(p.position, q, p.scale)));
  if (p.instance >= 0 && p.instance < static_cast<int>(sc.instances.size())) {
    int m = sc.instances[p.instance].material;
    if (m >= 0 && m < static_cast<int>(sc.materials.size())) o.set("material", Json(sc.materials[m].name));
  }
  int sameLine = 0;
  if (p.line > 0)
    for (const Stage::Placed& q2 : g.stage->placed) sameLine += q2.line == p.line ? 1 : 0;
  o.set("lineCount", Json(sameLine));  // more than 1: made by a repeat
  out = o.dump();
  return out.c_str();
}

// Moves a thing at once, without running the script again (the editor
// writes the same change into the script): {"at": [..], "rotation": [yaw,
// pitch, roll], "scale": [..], "visible": true}. Solid things keep falling
// from where they're put.
EMSCRIPTEN_KEEPALIVE int loop_thing_set(int index, const char* json) {
  if (!g.stage || index < 0 || index >= static_cast<int>(g.stage->placed.size())) return 0;
  Json o;
  if (!Json::parse(json ? json : "{}", o) || !o.isObject()) return 0;
  Stage::Placed& p = g.stage->placed[index];
  auto vec = [&](const char* k, Vec3& into) {
    if (!o.has(k)) return;
    const Json& a = o[k];
    if (a.isArray() && a.size() >= 3) into = Vec3(static_cast<float>(a[0].number()), static_cast<float>(a[1].number()), static_cast<float>(a[2].number()));
  };
  vec("at", p.position), vec("rotation", p.rotation), vec("scale", p.scale);
  if (o.has("visible")) p.visible = o["visible"].boolean(true);
  p.useQuat = false;
  if (p.body >= 0 && p.body < static_cast<int>(g.stage->world.bodyCount())) {
    // a solid thing starts again from where it's put, still
    physics::Body& b = g.stage->world.body(p.body);
    b.x = p.position, b.q = Quat::euler(p.rotation.x, p.rotation.y, p.rotation.z);
    b.v = Vec3(0), b.w = Vec3(0), b.asleep = false, b.sleepTimer = 0;
  }
  g.stage->updateTransform(index);
  g.stage->collidersDirty = true;
  return 1;
}

// The music changed by the editor without running the script again (the
// song keeps its place): {"tempo": 100, "swing": 0, "tracks": [{"instrument":
// "kick", "pattern": "x...", "volume": 1}]}. 0, and nothing changed, if a
// track can't be read.
EMSCRIPTEN_KEEPALIVE int loop_music_set(const char* json) {
  if (!g.stage) return 0;
  Json o;
  if (!Json::parse(json ? json : "{}", o) || !o.isObject()) return 0;
  std::vector<audio::Track> tracks;
  if (o.has("tracks"))
    for (const Json& t : o["tracks"].items()) {
      audio::Track tr;
      if (!audio::parseTrack(t["instrument"].string(), t["pattern"].string(), tr)) return 0;
      tr.volume = std::clamp(static_cast<float>(t.has("volume") ? t["volume"].number(1) : 1), 0.0f, 2.0f);
      tracks.push_back(std::move(tr));
    }
  audio::Sequencer& m = g.stage->music;
  if (o.has("tempo")) m.tempo = std::clamp(static_cast<float>(o["tempo"].number(100)), 20.0f, 300.0f);
  if (o.has("swing")) m.swing = std::clamp(static_cast<float>(o["swing"].number(0)), 0.0f, 0.5f);
  m.tracks = std::move(tracks);
  return 1;
}
// The music's step playing now (for the editor's grid), or -1.
EMSCRIPTEN_KEEPALIVE int loop_music_step() { return g.stage ? static_cast<int>(g.stage->music.step() % 1000000) : -1; }

// The engine's messages since the last call (scene starts, problems, settings).
EMSCRIPTEN_KEEPALIVE const char* loop_log() {
  static std::string out;
  out.swap(g.log);
  g.log.clear();
  return out.c_str();
}
// The meter: each part's CPU time a frame (smoothed), and memory in MB: the
// engine's own (in use and reserved) and what the GPU holds.
EMSCRIPTEN_KEEPALIVE const char* loop_meter() {
  static char buf[512];
  static std::string out;
  const Meter& m = Meter::get();
  struct mallinfo mi = mallinfo();
  const double mb = 1.0 / (1024 * 1024);
  std::snprintf(buf, sizeof buf,
                "{\"scripts\":%.2f,\"physics\":%.2f,\"people\":%.2f,\"sending\":%.2f,\"drawing\":%.2f,\"cpu\":%.2f,\"gpu\":%.2f,"
                "\"memUsed\":%.1f,\"memReserved\":%.1f,\"gpuMem\":%.1f,\"cores\":%d,\"clustered\":%d}",
                m.ms(Part::kScript), m.ms(Part::kPhysics), m.ms(Part::kPeople), m.ms(Part::kSend), m.ms(Part::kDraw), g.cpuMs,
                g.renderer ? g.renderer->gpuMs() : 0.0f, mi.uordblks * mb, emscripten_get_heap_size() * mb, g.renderer ? g.renderer->gpuBytes() * mb : 0.0,
                JobSystem::shared().threads(), g.renderer && g.renderer->clustered() ? 1 : 0);
  out = buf;
  addPieces(out);
  if (g.renderer && g.renderer->lightCacheWaiting()) out.insert(out.size() - 1, ",\"cacheWaiting\":" + std::to_string(g.renderer->lightCacheWaiting()));
  // each step of the frame on the graphics chip, when it has its own timers
  if (g.renderer && g.renderer->timers()) addSteps(out, g.renderer->stepMs());
  return out.c_str();
}
EMSCRIPTEN_KEEPALIVE const char* loop_stats() {
  static char buf[768];
  if (!g.stage) return "{}";
  std::snprintf(buf, sizeof buf, "{\"fps\":%.1f,\"cpu\":%.1f,\"gpu\":%.1f,\"scale\":%.2f,\"things\":%zu,\"lights\":%zu,\"draws\":%zu,\"ratio\":%.2f,\"time\":%.2f,\"movie\":%d,\"film\":%.2f,\"inflight\":%d,\"gpuLast\":%.1f,\"frames\":%llu,\"exposure\":%.6g,\"clustered\":%d,\"shadowDraws\":%llu,\"lampShadowDraws\":%llu,\"cacheWaiting\":%d,\"cacheRelit\":%llu,\"cacheProbes\":%d,\"farWaiting\":%d}", g.fps, g.cpuMs, g.renderer ? g.renderer->gpuMs() : 0.0f,
                g.renderer ? g.renderer->renderScale() : 1.0f, g.stage->scene.instances.size(), g.stage->scene.lights.size(), g.renderer ? g.renderer->drawCalls() : 0,
                g.renderer ? g.renderer->pixelRatio() : 1.0f, g.vm ? g.vm->time() : 0.0, g.movie ? 1 : 0, g.filmTime, g.renderer ? g.renderer->framesInFlight() : 0, g.renderer ? g.renderer->lastGpuMs() : 0.0f,
                static_cast<unsigned long long>(g.renderer ? g.renderer->presentedFrames() : 0), g.renderer ? g.renderer->currentExposure() : 0.0f, g.renderer && g.renderer->clustered() ? 1 : 0,
                static_cast<unsigned long long>(g.renderer ? g.renderer->shadowDraws() : 0),
                static_cast<unsigned long long>(g.renderer ? g.renderer->lampShadowDraws() : 0), g.renderer ? g.renderer->lightCacheWaiting() : 0,
                static_cast<unsigned long long>(g.renderer ? g.renderer->lightCacheRelit() : 0), g.renderer ? g.renderer->lightCacheProbes() : 0,
                g.renderer ? g.renderer->farShadowWaiting() : 0);
  return buf;
}
}

int main() {
  gpu::g_skipPasses = EM_ASM_INT({ return +(new URLSearchParams(location.search).get('skip') || 0); });
  // the page's testing switches (?view= ?res= ?msaa= ?clusters= ?lamps= ?rays= ?prepass= ?cull= ?lod=) are settings too
  for (auto [param, id] : {std::pair{"view", "render.view"}, {"res", "render.res"}, {"msaa", "render.msaa"}, {"clusters", "render.clusters"}, {"lightgrid", "render.lightGrid"}, {"lampgrouping", "render.lampGrouping"}, {"lampmaps", "render.lampMaps"}, {"shadowdistance", "render.shadowDistance"}, {"cachems", "render.lightCacheMs"}, {"lightcache", "render.lightCache"}, {"ao", "render.ao"}, {"ssr", "render.ssr"}, {"alllampshadows", "render.allLampShadows"}, {"ratio", "render.ratioPhone"}, {"clustererror", "render.clusterError"}, {"lamps", "render.lamps"}, {"rays", "render.rays"},
                           {"prepass", "render.prepass"}, {"cull", "render.cull"}, {"lod", "world.lodBias"}, {"reuse", "ultra.reuse"}, {"denoise", "ultra.denoise"}}) {
    double v = EM_ASM_DOUBLE({ const v = new URLSearchParams(location.search).get(UTF8ToString($0)); return v === null ? NaN : +v; }, param);
    if (!std::isnan(v)) Settings::get().set(Settings::kEditor, id, Json(v));
  }
  // render settings change what the renderer has made (shadow maps, pipelines, pixel density)
  Settings::get().watch("render.", [](const std::string&) { if (g.renderer) g.renderer->applySettings(); });
  Settings::get().watch("ultra.", [](const std::string&) { if (g.renderer) g.renderer->applySettings(); });
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
