// The Loop3D player: runs a Looped Script world in the browser and draws it
// live with WebGPU. In "movie" mode it plays the script's shots; in "game"
// mode you walk (left thumb / WASD) and look (right thumb / mouse).
//
// Motion is smooth at any frame rate: scripts run on a fixed 120 Hz clock,
// walking and looking ease toward where the controls point (no snapping),
// and the camera never jumps between frames.
#include <emscripten/emscripten.h>
#include <emscripten/html5.h>

#include <cstdio>
#include <cstring>
#include <memory>
#include <string>

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
  uint64_t seenVersion = ~0ull, seenTransforms = ~0ull;
  size_t seenMeshes = 0;
  uint64_t skyKey = 0;
  double lastTime = 0, accumulator = 0, filmTime = 0;
  bool movie = true;
  // walking
  Vec3 pos{0, 1.6f, 5}, vel{0};
  float yaw = 0, pitch = 0, yawTarget = 0, pitchTarget = 0;
  Vec2 move{0};  // -1..1 from keys or the thumb
  bool keys[8] = {};
  std::string pendingScript, problems;
  int frames = 0;
  double fpsTime = 0;
  float fps = 0;
};
Player g;

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

void startScript(const std::string& src) {
  if (g.renderer) g.renderer->reset();
  g.vm = std::make_unique<Vm>();
  g.stage = std::make_unique<Stage>();
  installStandardLibrary(*g.vm);
  installSceneApi(*g.vm, *g.stage);
  g.vm->onSay = [](const std::string& s) { EM_ASM({ if (window.loopSay) window.loopSay(UTF8ToString($0)); }, s.c_str()); };
  g.problems.clear();
  g.vm->load(src, "scene.loop");
  for (auto& p : g.vm->problems()) g.problems += p.text() + "\n";
  if (!g.problems.empty()) EM_ASM({ if (window.loopProblems) window.loopProblems(UTF8ToString($0)); }, g.problems.c_str());
  g.seenVersion = ~0ull, g.seenTransforms = ~0ull, g.seenMeshes = 0;
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
  if (s.version != g.seenVersion) {
    uint64_t sk = skySignature(s.scene);
    bool geometry = s.scene.meshes.size() != g.seenMeshes;
    g.renderer->sync(s.scene, geometry, sk != g.skyKey);
    g.skyKey = sk;
    g.seenMeshes = s.scene.meshes.size();
    g.seenVersion = s.version;
    g.seenTransforms = s.transformsVersion;
  } else if (s.transformsVersion != g.seenTransforms) {
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
  float speed = g.keys[4] ? 4.5f : 1.6f;
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

bool frame(double now, void*) {
  if (!g.pendingScript.empty()) {
    startScript(g.pendingScript);
    g.pendingScript.clear();
  }
  if (!g.renderer || !g.vm) return true;
  double dt = g.lastTime > 0 ? std::min(0.1, (now - g.lastTime) / 1000.0) : 1.0 / 60;
  g.lastTime = now;
  // scripts tick on a fixed clock
  g.accumulator += dt;
  int steps = 0;
  while (g.accumulator >= 1.0 / 120 && steps++ < 12) {
    g.vm->update(1.0 / 120);
    g.stage->update(1.0f / 120);
    g.accumulator -= 1.0 / 120;
  }
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
    g.filmTime += dt;
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
  g.stage->setListener(cam);
  g.stage->playerPos = g.movie ? cam.position : g.pos - Vec3(0, 1.6f, 0);
  g.renderer->draw(cam, look, static_cast<float>(dt), static_cast<float>(g.vm->time()), fade);
  ++g.frames;
  g.fpsTime += dt;
  if (g.fpsTime > 0.5) g.fps = static_cast<float>(g.frames / g.fpsTime), g.frames = 0, g.fpsTime = 0;
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
  g.yawTarget += dx * 0.0045f;
  g.pitchTarget = clamp(g.pitchTarget - dy * 0.0045f, -1.45f, 1.45f);
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
  if (g.renderer) g.renderer->setQuality(q == 0 ? gpu::Quality::kPhone : q == 1 ? gpu::Quality::kHigh : gpu::Quality::kUltra);
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
EMSCRIPTEN_KEEPALIVE const char* loop_stats() {
  static char buf[512];
  if (!g.stage) return "{}";
  std::snprintf(buf, sizeof buf, "{\"fps\":%.1f,\"things\":%zu,\"lights\":%zu,\"draws\":%zu,\"ratio\":%.2f,\"time\":%.2f,\"movie\":%d,\"film\":%.2f}", g.fps,
                g.stage->scene.instances.size(), g.stage->scene.lights.size(), g.renderer ? g.renderer->drawCalls() : 0,
                g.renderer ? g.renderer->pixelRatio() : 1.0f, g.vm ? g.vm->time() : 0.0, g.movie ? 1 : 0, g.filmTime);
  return buf;
}
}

int main() {
  gpu::g_debugView = EM_ASM_INT({ return +(new URLSearchParams(location.search).get('view') || 0); });
  gpu::g_skipPasses = EM_ASM_INT({ return +(new URLSearchParams(location.search).get('skip') || 0); });
  gpu::Renderer::create("#view", [](std::unique_ptr<gpu::Renderer> r) {
    if (!r) {
      EM_ASM({ if (window.loopNoGpu) window.loopNoGpu(); });
      return;
    }
    g.renderer = std::move(r);
    bool phone = EM_ASM_INT({ return /iPhone|iPad|Android/i.test(navigator.userAgent) ? 1 : 0; });
    g.renderer->setQuality(phone ? gpu::Quality::kPhone : gpu::Quality::kHigh);
    EM_ASM({ if (window.loopReady) window.loopReady(); });
    emscripten_request_animation_frame_loop(frame, nullptr);
  });
  return 0;
}
