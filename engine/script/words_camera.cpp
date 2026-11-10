// The world words: camera, look, shots (registered by installSceneApi, scene_api.cpp).
#include "engine/script/words_internal.h"

namespace loop::script::words {

void registerCamera(Vm& vm) {
  // ---- camera, look, shots
  vm.native("camera", [](Args& a) {
    Stage& s = stageOf(a.vm);
    Camera& c = s.scene.camera;
    c.position = a.vec(0, "at", c.position);
    c.target = a.vec(1, "look_at", c.target);
    double lens = a.num(-1, "lens", c.focalLength);
    c.focalLength = f32(lens < 1 ? lens * 1000 : lens);
    c.fStop = f32(a.num(-1, "f_stop", a.num(-1, "aperture", c.fStop)));
    c.focusDistance = f32(a.num(-1, "focus", c.focusDistance));
    c.shutter = f32(a.num(-1, "shutter", c.shutter));
    c.iso = f32(a.num(-1, "iso", c.iso));
    c.exposureCompensation = f32(a.num(-1, "exposure", c.exposureCompensation));
    if (a.has(-1, "auto_exposure")) c.autoExposure = a.flag(-1, "auto_exposure", true);
    c.blades = static_cast<int>(a.num(-1, "blades", c.blades));
    if (a.has(-1, "sensor")) {
      std::string k = a.text(-1, "sensor", "ff");
      if (!sensorByName(k, c.sensor)) a.vm.fail("there's no sensor called \"" + k + "\"", "sensors: ff (full frame), s35, apsc, m43, imax");
    }
    ++s.version;
    return Value();
  }, "camera(at, look_at, lens: 35 mm, f_stop: 2.8, focus, shutter, iso, exposure, auto_exposure, sensor: \"s35\", blades)");
  vm.native("look", [](Args& a) {
    Stage& s = stageOf(a.vm);
    std::string n = a.text(0, "name", "natural");
    if (!lookByName(n, s.scene.look)) {
      std::string c = closest(n, lookNames());
      a.vm.fail("there's no look called \"" + n + "\"", c.empty() ? "looks: natural, blockbuster, noir, golden, bleach, seventies, neon, dream, documentary, horror, arctic, western" : "did you mean \"" + c + "\"?");
    }
    Look& l = s.scene.look;
    l.contrast = f32(a.num(-1, "contrast", l.contrast));
    l.saturation = f32(a.num(-1, "saturation", l.saturation));
    l.temperature = f32(a.num(-1, "warmth", l.temperature));
    l.tint = f32(a.num(-1, "tint", l.tint));
    l.bloom = f32(a.num(-1, "bloom", l.bloom));
    l.grain = f32(a.num(-1, "grain", l.grain));
    l.vignette = f32(a.num(-1, "vignette", l.vignette));
    l.letterbox = f32(a.num(-1, "bars", l.letterbox));
    if (a.has(-1, "tone")) {
      std::string t = a.text(-1, "tone", "agx");
      if (t == "agx") l.tonemap = Look::Tonemap::kAgX;
      else if (t == "aces") l.tonemap = Look::Tonemap::kAces;
      else if (t == "filmic") l.tonemap = Look::Tonemap::kFilmic;
      else if (t == "linear") l.tonemap = Look::Tonemap::kLinear;
      else a.vm.fail("a tone curve is \"agx\", \"aces\", \"filmic\" or \"linear\"");
    }
    l.lift = a.vec(-1, "lift", l.lift), l.gamma = a.vec(-1, "gamma", l.gamma), l.gain = a.vec(-1, "gain", l.gain);
    l.shadowTint = a.vec(-1, "shadow_tint", l.shadowTint), l.highlightTint = a.vec(-1, "highlight_tint", l.highlightTint);
    l.blackLevel = f32(a.num(-1, "black_level", l.blackLevel));
    l.bloomRadius = f32(a.num(-1, "bloom_size", l.bloomRadius));
    l.halation = f32(a.num(-1, "halation", l.halation));
    l.chromaticAberration = f32(a.num(-1, "fringes", l.chromaticAberration));
    l.distortion = f32(a.num(-1, "distortion", l.distortion));
    l.sharpen = f32(a.num(-1, "sharpen", l.sharpen));
    l.grainSize = f32(a.num(-1, "grain_size", l.grainSize));
    if (a.has(-1, "monochrome")) l.monochrome = a.flag(-1, "monochrome", false);
    ++s.version;
    return Value();
  }, "look(\"noir\", contrast, saturation, warmth, tint, bloom, bloom_size, halation, grain, grain_size, vignette, bars, tone, lift, gamma, gain, shadow_tint, highlight_tint, black_level, fringes, distortion, sharpen, monochrome)");
  auto framing = [](Args& a, const Value* v, Framing base) {
    if (!v) return base;
    Table* t = v->as<Table>();
    if (!t) a.vm.fail("a shot's from: and to: are tables, like {at: vec(0, 2, 6), look_at: vec(0, 1, 0), lens: 35 mm}");
    auto get = [&](const char* k) { return t->find(k); };
    if (Value* x = get("at")) base.position = x->asVec();
    if (Value* x = get("look_at")) base.target = x->asVec();
    if (Value* x = get("lens")) base.focalLength = static_cast<float>(x->n < 1 ? x->n * 1000 : x->n);
    if (Value* x = get("f_stop")) base.fStop = static_cast<float>(x->n);
    if (Value* x = get("focus")) base.focus = static_cast<float>(x->n);
    return base;
  };
  static decltype(framing) framingFn = framing;
  vm.native("shot", [](Args& a) {
    Stage& s = stageOf(a.vm);
    Shot sh;
    sh.name = a.text(0, "name", "shot " + std::to_string(s.film.shots.size() + 1));
    sh.duration = f32(a.num(1, "length", 4));
    Framing base;
    base.position = s.scene.camera.position, base.target = s.scene.camera.target, base.focalLength = s.scene.camera.focalLength, base.fStop = s.scene.camera.fStop;
    sh.from = framingFn(a, a.get(-1, "from"), base);
    sh.to = framingFn(a, a.get(-1, "to"), sh.from);
    sh.moves = a.has(-1, "to");
    std::string e = a.text(-1, "ease", "smooth");
    if (!easeByName(e, sh.ease)) a.vm.fail("there's no easing called `" + e + "`");
    sh.shake = f32(a.num(-1, "shake", 0));
    sh.fadeIn = f32(a.num(-1, "fade_in", 0));
    sh.fadeOut = f32(a.num(-1, "fade_out", 0));
    sh.look = a.text(-1, "look", "");
    s.film.shots.push_back(sh);
    return Value();
  }, "shot(name, length, from: {at, look_at, lens, f_stop, focus}, to: {…}, ease, shake, fade_in, fade_out)");
  vm.native("orbit", [](Args& a) {
    Stage& s = stageOf(a.vm);
    Shot sh;
    sh.name = a.text(0, "name", "orbit");
    sh.duration = f32(a.num(1, "length", 8));
    sh.orbit = true;
    sh.center = a.vec(-1, "around", {});
    sh.radius = f32(a.num(-1, "radius", 5));
    sh.height = f32(a.num(-1, "height", 1.6));
    sh.angleFrom = f32(a.num(-1, "from", 0));
    sh.angleTo = f32(a.num(-1, "to", kPi / 2));
    sh.from.focalLength = sh.to.focalLength = f32(a.num(-1, "lens", 35) < 1 ? a.num(-1, "lens", 0.035) * 1000 : a.num(-1, "lens", 35));
    sh.from.fStop = sh.to.fStop = f32(a.num(-1, "f_stop", 4));
    sh.from.target = sh.to.target = a.vec(-1, "look_at", sh.center);
    sh.moves = a.has(-1, "look_at");
    std::string e = a.text(-1, "ease", "smooth");
    if (!easeByName(e, sh.ease)) a.vm.fail("there's no easing called `" + e + "`");
    sh.shake = f32(a.num(-1, "shake", 0));
    sh.fadeIn = f32(a.num(-1, "fade_in", 0));
    sh.fadeOut = f32(a.num(-1, "fade_out", 0));
    s.film.shots.push_back(sh);
    return Value();
  }, "orbit(name, length, around, radius, height, from: 0 deg, to: 90 deg, lens)");
}

}  // namespace loop::script::words
