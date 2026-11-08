#include "engine/core/settings.h"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdio>

namespace loop {

Config& config() {
  static Config c;
  return c;
}

namespace {

Json vecJson(Vec3 v) {
  Json a = Json::array();
  a.push(Json(static_cast<double>(v.x))), a.push(Json(static_cast<double>(v.y))), a.push(Json(static_cast<double>(v.z)));
  return a;
}

size_t editDistance(const std::string& a, const std::string& b) {
  std::vector<size_t> row(b.size() + 1);
  for (size_t j = 0; j <= b.size(); ++j) row[j] = j;
  for (size_t i = 1; i <= a.size(); ++i) {
    size_t prev = row[0];
    row[0] = i;
    for (size_t j = 1; j <= b.size(); ++j) {
      size_t cur = row[j];
      row[j] = std::min({row[j] + 1, row[j - 1] + 1, prev + (std::tolower(a[i - 1]) == std::tolower(b[j - 1]) ? 0 : 1)});
      prev = cur;
    }
  }
  return row[b.size()];
}

}  // namespace

Settings& Settings::get() {
  static Settings s;
  return s;
}

// Every engine setting: name, field, default (taken from Config), range, help.
Settings::Settings() {
  Config& c = config();
  auto B = [&](const char* id, bool& f, const char* help) { entries_.push_back({id, Type::kBool, &f, Json(f), 0, 1, help, {}}); };
  auto I = [&](const char* id, int& f, double lo, double hi, const char* help) {
    entries_.push_back({id, Type::kInt, &f, Json(f), lo, hi, help, {}});
  };
  auto F = [&](const char* id, float& f, double lo, double hi, const char* help) {
    entries_.push_back({id, Type::kFloat, &f, Json(static_cast<double>(f)), lo, hi, help, {}});
  };
  auto V = [&](const char* id, Vec3& f, double lo, double hi, const char* help) {
    entries_.push_back({id, Type::kVec3, &f, vecJson(f), lo, hi, help, {}});
  };
  auto T = [&](const char* id, std::string& f, const char* help) { entries_.push_back({id, Type::kText, &f, Json(f), 0, 0, help, {}}); };

  auto& r = c.render;
  I("render.targetFps", r.targetFps, 15, 240, "frame rate the automatic resolution aims for (never above the display's)");
  I("render.framesInFlight", r.framesInFlight, 1, 3, "frames the GPU may work on at once: more hides stalls, fewer means less delay");
  B("render.pauseHidden", r.pauseHidden, "stop drawing while the page is hidden");
  F("render.res", r.res, 0, 1, "0 automatic resolution, else held at this share of full size");
  F("render.ratioPhone", r.ratioPhone, 0.5, 3, "most pixels per screen point on Phone");
  F("render.ratioHigh", r.ratioHigh, 0.5, 3, "most pixels per screen point on High");
  F("render.ratioUltra", r.ratioUltra, 0.5, 3, "most pixels per screen point on Ultra");
  F("render.floor", r.floor, 0.2, 1, "lowest automatic resolution (Phone and High)");
  F("render.floorUltra", r.floorUltra, 0.2, 1, "lowest automatic resolution on Ultra");
  F("render.slowGpu", r.slowGpu, 50, 300, "GPU time (% of the frame budget) that counts as too slow: drop resolution");
  F("render.climbUnder", r.climbUnder, 30, 150, "climb only if the bigger size would still fit under this % of the budget");
  F("render.calm", r.calm, 0.5, 30, "seconds of steady frames before the resolution climbs");
  I("render.msaa", r.msaa, 1, 4, "multisampling of the scene pass: 1 off, 4 on");
  B("render.fxaa", r.fxaa, "edge smoothing in the final pass (cheap; helps with MSAA off)");
  B("render.lightGrid", r.lightGrid, "many lamps cheaply: each pixel works out the lamps near it alone, and far groups of lamps as one lamp each (off: every lamp alone, everywhere)");
  F("render.lampGrouping", r.lampGrouping, 1, 64, "how far (in its own sizes) a group of lamps must be to be worked out as one: more is closer to every lamp alone, and more work");
  B("render.cull", r.cull, "skip what a pass can't see: off screen, or outside the sun's shadow area (off: draw everything)");
  F("render.clusterError", r.clusterError, 0.01, 16, "with clusters: the most, in pixels, a simpler version of a shape may look different from its full detail (smaller: more detail, more work)");
  B("render.taa", r.taa, "frame blending: the view shifts a fraction of a pixel each frame and each pixel is blended with the frames before (smooth edges, no shimmer); with clusters, in place of multisampling");
  B("render.clusters", r.clusters, "draw every shape in clusters, each at the detail its size on screen needs, chosen and drawn by the GPU in one go (Nanite's way; off: one draw per kind of shape, with multisampling)");
  B("render.occlude", r.occlude, "with clusters: skip the clusters hidden behind nearer ones, tested against the last frame's depth and tried again against this frame's (nothing newly in view goes missing)");
  B("render.filmGrain", r.filmGrain, "film grain in the live picture (off: no fizz; stills keep a scene's grain)");
  B("render.sunShadows", r.sunShadows, "the sun casts shadows");
  I("render.sunMapPhone", r.sunMapPhone, 256, 4096, "sun shadow map size on Phone");
  I("render.sunMapHigh", r.sunMapHigh, 512, 8192, "sun shadow map size on High");
  F("render.shadowDistance", r.shadowDistance, 0, 500, "half-width of the sun's shadowed area in metres (0 = the quality's own)");
  F("render.shadowBias", r.shadowBias, 0, 0.02, "depth bias of sun shadows: higher hides acne, lower keeps contact");
  F("render.shadowSoftness", r.shadowSoftness, 0, 8, "blur radius of sun shadow edges (in shadow-map texels)");
  I("render.lamps", r.lamps, -1, 16, "lamps that cast ray-traced shadows (-1 = the quality's own: Phone 0, High 2)");
  I("render.rays", r.rays, -1, 16, "shadow rays per lamp (-1 = 1); soft edges need about 4");
  B("render.lampShadows", r.lampShadows, "lamps cast shadows at all");
  I("render.maxLights", r.maxLights, 1, 256, "most lamps a scene draws (the brightest are kept)");
  I("render.textureSize", r.textureSize, 16, 2048, "size of model pictures on the GPU (pixels square): sharper costs memory (256: about 0.35 MB each)");
  F("render.skyLight", r.skyLight, 0, 4, "strength of the light from the sky (bounced daylight)");
  F("render.occlusion", r.occlusion, 0, 2, "how much creases and undersides darken");
  B("render.passScene", r.passScene, "debug: the scene's lighting");
  B("render.passExposure", r.passExposure, "debug: automatic exposure");
  B("render.passBloom", r.passBloom, "debug: bloom");
  B("render.passFilm", r.passFilm, "debug: the film pass (tone curve, grading, grain)");
  B("render.passShadow", r.passShadow, "debug: the shadow pass");
  B("render.passDraw", r.passDraw, "debug: the scene pass");
  I("render.prepass", r.prepass, 0, 2, "depth pre-pass, so each pixel is lit once: 0 off, 1 on, 2 automatic (on when 6 or more lamps light the scene)");
  I("render.view", r.view, 0, 4, "debug view: 0 picture, 1 raw light, 2 exposure, 3 bloom, 4 ray work");

  auto& u = c.ultra;
  I("ultra.bounces", u.bounces, 1, 16, "light bounces per path");
  I("ultra.spp", u.spp, 1, 16, "paths per pixel per frame");
  I("ultra.history", u.history, 0, 4096, "most frames averaged while still (0 = keep sharpening)");
  F("ultra.clamp", u.clamp, 0.5, 100, "firefly clamp: brightest a single path may be");
  B("ultra.denoise", u.denoise, "noise removal");
  I("ultra.denoisePasses", u.denoisePasses, 1, 5, "noise removal strength (passes)");
  B("ultra.glossy", u.glossy, "glossy reflections");
  B("ultra.glass", u.glass, "glass and refraction");
  B("ultra.dof", u.dof, "depth of field");
  B("ultra.sunShadows", u.sunShadows, "traced sun shadows");
  B("ultra.lampShadows", u.lampShadows, "shadows from every lamp");

  auto& ci = c.cinema;
  T("cinema.quality", ci.quality, "draft | good | cinema | ultra (empty = good)");
  I("cinema.width", ci.width, 0, 8192, "width of stills and film frames (0 = render 1920, film 1280)");
  I("cinema.height", ci.height, 0, 8192, "height (0 = 16:9 of the width)");
  I("cinema.samples", ci.samples, 0, 65536, "samples per pixel (0 = from the quality)");
  I("cinema.bounces", ci.bounces, 1, 64, "light bounces");
  B("cinema.denoise", ci.denoise, "noise removal");
  B("cinema.dispersion", ci.dispersion, "rainbow dispersion in glass");
  B("cinema.motionBlur", ci.motionBlur, "motion blur");
  I("cinema.fps", ci.fps, 1, 120, "film frames per second");
  T("cinema.folder", ci.folder, "folder for film frames");
  I("cinema.threads", ci.threads, 0, 256, "render threads (0 = all)");

  auto& ca = c.camera;
  F("cam.adaptSpeed", ca.adaptSpeed, 0.1, 10, "how fast exposure follows the light (like an eye)");
  F("cam.minEv", ca.minEv, -16, 10, "darkest scene exposure will adapt to (EV)");
  F("cam.maxEv", ca.maxEv, 0, 26, "brightest scene exposure will adapt to (EV)");
  F("cam.shake", ca.shake, 0, 1, "handheld shake of the walking camera");
  F("cam.smoothing", ca.smoothing, 1, 60, "how quickly the view eases to where you point it");
  B("cam.portraitWide", ca.portraitWide, "a screen taller than wide (a phone held upright) keeps the lens's width, so you see as much side to side as on a square screen");

  auto& w = c.world;
  F("world.drawDistance", w.drawDistance, 50, 50000, "farthest the camera sees (m)");
  F("world.patternFade", w.patternFade, 0.25, 4, "how early fine pattern detail fades with distance");
  F("world.foliageDensity", w.foliageDensity, 0, 3, "trees per area in forests (applies when a scene builds)");
  F("world.terrainRes", w.terrainRes, 0.25, 4, "detail of terrain meshes (applies when a scene builds)");
  F("world.lodBias", w.lodBias, -2, 2, "levels of detail: +1 keeps full detail twice as far, -1 switches to simpler versions twice as soon");

  auto& p = c.physics;
  V("physics.gravity", p.gravity, -100, 100, "gravity (m/s²); `gravity(…)` in a script sets it for the scene");
  I("physics.rate", p.rate, 30, 480, "physics steps per second");
  I("physics.iterations", p.iterations, 1, 64, "solver iterations: more is stiffer stacks, slower");
  F("physics.sleep", p.sleep, 0, 1, "things slower than this (m/s) for half a second go to sleep");
  F("physics.friction", p.friction, 0, 2, "default friction (solid() and the world)");
  F("physics.bounce", p.bounce, 0, 1, "default bounce (solid() and the world)");
  F("physics.charRadius", p.charRadius, 0.1, 1, "walking body radius (m)");
  F("physics.charStep", p.charStep, 0, 1, "highest step the walking body climbs (m)");
  F("physics.charSlope", p.charSlope, 10, 85, "steepest slope it walks up (degrees)");
  F("physics.charHeight", p.charHeight, 0.4, 2.5, "height of the walking body's capsule (m)");
  I("physics.clothIterations", p.clothIterations, 1, 64, "cloth solver iterations");
  V("physics.wind", p.wind, -50, 50, "wind on cloth (m/s)");

  auto& pl = c.player;
  F("player.walk", pl.walk, 0.1, 20, "walking speed (m/s)");
  F("player.run", pl.run, 0.1, 40, "running speed (m/s)");
  F("player.look", pl.look, 0.05, 10, "mouse look speed");
  F("player.eye", pl.eye, 0.3, 3, "eye height (m)");
  F("player.bob", pl.bob, 0, 0.2, "head bob while walking (m)");
  F("player.accel", pl.accel, 0.5, 60, "how quickly walking starts and stops");
  B("player.invertY", pl.invertY, "invert vertical look");
  F("player.touchLook", pl.touchLook, 0.1, 10, "touch look speed");
  F("player.stickSize", pl.stickSize, 50, 300, "thumb stick size (px)");
  F("player.deadzone", pl.deadzone, 0, 0.6, "gamepad stick dead zone");
  T("keys.forward", pl.kForward, "key: forward");
  T("keys.back", pl.kBack, "key: back");
  T("keys.left", pl.kLeft, "key: left");
  T("keys.right", pl.kRight, "key: right");
  T("keys.run", pl.kRun, "key: run");
  T("keys.use", pl.kUse, "key: use / talk");

  auto& a = c.anim;
  B("anim.ik", a.ik, "feet planted on the ground (IK)");
  B("anim.breathing", a.breathing, "people breathe");
  B("anim.armSwing", a.armSwing, "arms swing when walking");
  F("anim.blend", a.blend, 0.02, 2, "how long a person takes to change speed (s)");
  I("anim.maxPeople", a.maxPeople, 1, 5000, "most people in a scene");

  auto& au = c.audio;
  F("audio.master", au.master, 0, 2, "master volume");
  F("audio.ambience", au.ambience, 0, 2, "endless sounds: fire, wind, rain, birds…");
  F("audio.effects", au.effects, 0, 2, "one-off sounds and knocks");
  F("audio.footsteps", au.footsteps, 0, 2, "footsteps");
  F("audio.dialogue", au.dialogue, 0, 2, "dialogue");
  F("audio.music", au.music, 0, 2, "music");
  F("audio.reverb", au.reverb, 0, 3, "room echo (times the scene's room())");
  F("audio.distance", au.distance, 0.1, 5, "how fast sounds fade with distance");
  B("audio.earDelay", au.earDelay, "sound reaches the nearer ear first (3D)");
  B("audio.airAbsorb", au.airAbsorb, "far sounds lose their highs");
  B("audio.doppler", au.doppler, "moving sounds change pitch");
  B("audio.limiter", au.limiter, "soft limiter: loud moments never clip");
  I("audio.buffer", au.buffer, 256, 8192, "audio buffer (frames): smaller is less delay");

  auto& s = c.script;
  I("script.tick", s.tick, 30, 480, "script steps per second");
  F("script.catchUp", s.catchUp, 1, 50, "most catch-up time per frame (ms)");
  I("script.maxSteps", s.maxSteps, 1, 60, "most script steps per frame");
  F("script.timeScale", s.timeScale, 0, 8, "world speed (walking keeps real time)");
  T("script.onError", s.onError, "continue | stop | pause");
  B("script.autoplay", s.autoplay, "the player counts as near everything (tests)");
  T("script.choices", s.choices, "story choices for tests, as a comma list");

  auto& st = c.story;
  F("story.textSpeed", st.textSpeed, 3, 60, "reading speed for line() (characters per second)");
  F("story.lineMin", st.lineMin, 0.5, 20, "shortest time a line stays (s)");
  F("story.subtitleSize", st.subtitleSize, 0.5, 3, "subtitle size");
  B("story.speakerNames", st.speakerNames, "show speaker names");
  T("story.speakerColor", st.speakerColor, "speaker name colour");
  F("story.choiceTimeout", st.choiceTimeout, 0, 120, "seconds to choose (0 = wait)");
  I("story.timeoutPick", st.timeoutPick, -1, 9, "option picked when time runs out (-1 random)");
  F("story.fade", st.fade, 0, 5, "fade between scenes (s)");
  T("story.loadingText", st.loadingText, "loading text ({scene} is the scene's title)");
  B("story.hud", st.hud, "show the HUD (buttons and stats)");

  F("perf.hitch", c.perf.hitch, 5, 200, "a hitch is a frame longer than this (ms) or twice the median");
  T("project.mode", c.project.mode, "mode for scenes that don't say: game | movie");
}

const Settings::Entry* Settings::find(const std::string& id) const {
  for (const Entry& e : entries_)
    if (e.id == id) return &e;
  return nullptr;
}

std::string Settings::suggest(const std::string& id) const {
  std::string best;
  size_t bestD = 1000;
  for (const Entry& e : entries_) {
    // compare whole names and the part after the dot
    size_t d = std::min(editDistance(id, e.id), editDistance(id, e.id.substr(e.id.find('.') + 1)) + 1);
    if (d < bestD) bestD = d, best = e.id;
  }
  return bestD <= std::max<size_t>(3, id.size() / 3) ? best : "";
}

namespace {
bool fitsType(Settings::Type t, const Json& v) {
  switch (t) {
    case Settings::Type::kBool: return v.type() == Json::Type::kBool || v.isNumber();
    case Settings::Type::kInt:
    case Settings::Type::kFloat: return v.isNumber() || v.type() == Json::Type::kBool;
    case Settings::Type::kVec3: return (v.isArray() && v.size() == 3) || v.isNumber();
    case Settings::Type::kText: return v.isString() || v.isNumber();
  }
  return false;
}
double asNumber(const Json& v) { return v.type() == Json::Type::kBool ? (v.boolean() ? 1 : 0) : v.number(); }
}  // namespace

void Settings::apply(Entry& e) {
  const Json* v = &e.def;
  for (int l = kEditor; l >= kProject; --l)
    if (!e.layers[l].isNull()) { v = &e.layers[l]; break; }
  auto clampD = [&](double x) { return std::clamp(x, e.min, e.max); };
  switch (e.type) {
    case Type::kBool: *static_cast<bool*>(e.field) = v->type() == Json::Type::kBool ? v->boolean() : v->number() != 0; break;
    case Type::kInt: *static_cast<int*>(e.field) = static_cast<int>(std::lround(clampD(asNumber(*v)))); break;
    case Type::kFloat: *static_cast<float*>(e.field) = static_cast<float>(clampD(asNumber(*v))); break;
    case Type::kVec3: {
      Vec3& f = *static_cast<Vec3*>(e.field);
      if (v->isNumber()) f = Vec3(static_cast<float>(clampD(v->number())));
      else f = Vec3(static_cast<float>(clampD((*v)[0].number())), static_cast<float>(clampD((*v)[1].number())), static_cast<float>(clampD((*v)[2].number())));
      break;
    }
    case Type::kText: {
      std::string& f = *static_cast<std::string*>(e.field);
      if (v->isString()) f = v->string();
      else {
        char buf[32];
        std::snprintf(buf, sizeof buf, "%g", v->number());
        f = buf;
      }
      break;
    }
  }
  for (const Watcher& w : watchers_)
    if (e.id.compare(0, w.prefix.size(), w.prefix) == 0) w.fn(e.id);
}

bool Settings::check(const std::string& id, const Json& value, std::string* error) const {
  const Entry* e = find(id);
  if (!e) {
    if (error) {
      std::string s = suggest(id);
      *error = "there's no setting \"" + id + "\"" + (s.empty() ? "" : "; did you mean \"" + s + "\"?");
    }
    return false;
  }
  if (value.isNull()) return true;
  if (!fitsType(e->type, value)) {
    if (error) {
      const char* kinds[] = {"yes or no", "a whole number", "a number", "three numbers (a vec)", "text"};
      *error = "the setting \"" + id + "\" takes " + kinds[static_cast<int>(e->type)];
    }
    return false;
  }
  // numbers stay inside the setting's safe range
  if (e->type == Type::kInt || e->type == Type::kFloat || e->type == Type::kVec3) {
    std::vector<double> xs;
    if (value.isNumber()) xs.push_back(value.number());
    else
      for (const Json& x : value.items()) xs.push_back(x.number());
    for (double x : xs)
      if (x < e->min || x > e->max) {
        if (error) {
          char buf[200];
          std::snprintf(buf, sizeof buf, "the setting \"%s\" takes %g to %g (%g is too %s)", id.c_str(), e->min, e->max, x, x < e->min ? "low" : "high");
          *error = buf;
        }
        return false;
      }
  }
  return true;
}

bool Settings::set(Layer layer, const std::string& id, const Json& value, std::string* error) {
  if (!check(id, value, error)) return false;
  Entry* e = nullptr;
  for (Entry& x : entries_)
    if (x.id == id) e = &x;
  e->layers[layer] = value;
  apply(*e);
  return true;
}

bool Settings::setMany(Layer layer, const Json& object, std::string* error) {
  bool ok = true;
  for (const auto& [k, v] : object.members()) {
    std::string err;
    if (!set(layer, k, v, &err)) {
      ok = false;
      if (error) *error += (error->empty() ? "" : "\n") + err;
    }
  }
  return ok;
}

void Settings::clear(Layer layer) {
  for (Entry& e : entries_)
    if (!e.layers[layer].isNull()) {
      e.layers[layer] = Json();
      apply(e);
    }
}

void Settings::resetAll() {
  for (Entry& e : entries_) {
    bool any = false;
    for (Json& l : e.layers) any |= !l.isNull(), l = Json();
    if (any) apply(e);
  }
}

Json Settings::value(const std::string& id) const {
  const Entry* e = find(id);
  if (!e) return Json();
  switch (e->type) {
    case Type::kBool: return Json(*static_cast<const bool*>(e->field));
    case Type::kInt: return Json(*static_cast<const int*>(e->field));
    case Type::kFloat: return Json(static_cast<double>(*static_cast<const float*>(e->field)));
    case Type::kVec3: return vecJson(*static_cast<const Vec3*>(e->field));
    case Type::kText: return Json(*static_cast<const std::string*>(e->field));
  }
  return Json();
}

Json Settings::list() const {
  Json out = Json::array();
  const char* kinds[] = {"bool", "int", "float", "vec3", "text"};
  const char* layerNames[] = {"project", "scene", "editor"};
  for (const Entry& e : entries_) {
    Json o = Json::object();
    o.set("id", Json(e.id));
    o.set("type", Json(kinds[static_cast<int>(e.type)]));
    o.set("default", e.def);
    o.set("value", value(e.id));
    if (e.type == Type::kInt || e.type == Type::kFloat || e.type == Type::kVec3) o.set("min", Json(e.min)), o.set("max", Json(e.max));
    o.set("help", Json(e.help));
    Json set = Json::object();
    for (int l = 0; l < 3; ++l)
      if (!e.layers[l].isNull()) set.set(layerNames[l], e.layers[l]);
    if (set.size()) o.set("layers", set);
    out.push(o);
  }
  return out;
}

int Settings::watch(const std::string& prefix, std::function<void(const std::string&)> fn) {
  watchers_.push_back({nextHandle_, prefix, std::move(fn)});
  return nextHandle_++;
}

void Settings::unwatch(int handle) {
  watchers_.erase(std::remove_if(watchers_.begin(), watchers_.end(), [&](const Watcher& w) { return w.handle == handle; }), watchers_.end());
}

}  // namespace loop
