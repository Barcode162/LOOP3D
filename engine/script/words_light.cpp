// The world words: light (registered by installSceneApi, scene_api.cpp).
#include "engine/script/words_internal.h"

namespace loop::script::words {

void registerLight(Vm& vm) {
  // ---- light
  vm.native("light", [](Args& a) {
    Stage& s = stageOf(a.vm);
    Light l;
    l.kind = Light::Kind::kPoint;
    l.position = a.vec(0, "at", {0, 2, 0});
    l.lumens = f32(a.num(-1, "lumens", a.num(-1, "brightness", 800)));
    l.color = a.has(-1, "color") ? unitLuminanceOr(a.vec(-1, "color", Vec3(1))) : kelvin(f32(a.num(-1, "kelvin", 2700)));
    l.radius = f32(a.num(-1, "radius", 0.03));
    if (a.has(-1, "aim")) {
      l.kind = Light::Kind::kSpot;
      l.direction = normalize(a.vec(-1, "aim", {0, -1, 0}) - (a.flag(-1, "aim_is_direction", false) ? Vec3(0) : l.position));
      float ang = f32(a.num(-1, "angle", 0.6));
      l.innerAngle = ang * f32(a.num(-1, "softness", 0.7)), l.outerAngle = ang;
    }
    s.scene.lights.push_back(l);
    ++s.version;
    return Value::object(a.vm.newThing(&kLightClass, static_cast<int>(s.scene.lights.size() - 1)));
  }, "light(at, lumens, kelvin | color, radius) — add aim: and angle: for a spotlight");
  vm.native("panel_light", [](Args& a) {
    Stage& s = stageOf(a.vm);
    Light l;
    l.kind = Light::Kind::kRect;
    l.position = a.vec(0, "at", {0, 3, 0});
    l.direction = normalize(a.vec(-1, "facing", {0, -1, 0}));
    l.size = {f32(a.vec(-1, "size", Vec3(1)).x), f32(a.vec(-1, "size", Vec3(1)).y)};
    l.lumens = f32(a.num(-1, "lumens", 3000));
    l.color = a.has(-1, "color") ? unitLuminanceOr(a.vec(-1, "color", Vec3(1))) : kelvin(f32(a.num(-1, "kelvin", 5000)));
    s.scene.lights.push_back(l);
    ++s.version;
    return Value::object(a.vm.newThing(&kLightClass, static_cast<int>(s.scene.lights.size() - 1)));
  }, "panel_light(at, facing, size, lumens): a soft rectangle of light (a window, a softbox)");
  vm.native("sun", [](Args& a) {
    Stage& s = stageOf(a.vm);
    float elev = f32(a.num(0, "elevation", 0.8)), az = f32(a.num(1, "toward", a.num(-1, "azimuth", 2.4)));
    Vec3 toSun{std::cos(elev) * std::sin(az), std::sin(elev), -std::cos(elev) * std::cos(az)};
    Light* sun = nullptr;
    for (auto& l : s.scene.lights)
      if (l.kind == Light::Kind::kSun) sun = &l;
    if (!sun) {
      s.scene.lights.push_back(Light());
      sun = &s.scene.lights.back();
      sun->kind = Light::Kind::kSun;
    }
    sun->direction = -toSun;
    sun->lux = f32(a.num(-1, "lux", 100000));
    sun->color = a.has(-1, "kelvin") ? kelvin(f32(a.num(-1, "kelvin", 5800))) : Vec3(1);
    // size: the sun's width in the sky (0.53 deg is our sun); wider = softer shadows
    sun->radius = f32(a.num(-1, "size", 0.0093)) * 0.5f;
    ++s.version;
    return Value();
  }, "sun(elevation: 20 deg, toward: 135 deg, lux, kelvin, size: 0.53 deg)");
  vm.native("sky", [](Args& a) {
    Stage& s = stageOf(a.vm);
    std::string k = a.text(0, "kind", "physical");
    if (k == "physical" || k == "real") s.scene.sky.kind = Sky::Kind::kPhysical;
    else if (k == "color" || k == "colour") s.scene.sky.kind = Sky::Kind::kColor;
    else if (k == "none" || k == "black") s.scene.sky.kind = Sky::Kind::kNone;
    else a.vm.fail("a sky is \"physical\", \"color\" or \"none\"");
    s.scene.sky.turbidity = f32(a.num(-1, "haze", s.scene.sky.turbidity));
    s.scene.sky.starNits = f32(a.num(-1, "stars", s.scene.sky.starNits));
    s.scene.sky.intensity = f32(a.num(-1, "brightness", s.scene.sky.intensity));
    if (a.has(-1, "color")) s.scene.sky.color = a.vec(-1, "color", Vec3(1));
    s.scene.sky.colorNits = f32(a.num(-1, "nits", s.scene.sky.colorNits));
    if (a.has(-1, "ground")) s.scene.sky.groundAlbedo = a.vec(-1, "ground", s.scene.sky.groundAlbedo);
    ++s.version;
    return Value();
  }, "sky(\"physical\", haze, stars, brightness, ground) or sky(\"color\", color, nits)");
  vm.native("fog", [](Args& a) {
    Stage& s = stageOf(a.vm);
    s.scene.fog.density = f32(a.num(0, "density", 0.01));
    s.scene.fog.top = f32(a.num(-1, "top", 1e6));
    s.scene.fog.anisotropy = f32(a.num(-1, "glow", 0.6));
    if (a.has(-1, "color")) s.scene.fog.albedo = a.vec(-1, "color", Vec3(0.9f));
    if (a.has(-1, "from") && a.has(-1, "to")) s.scene.fog.box = AABB(a.vec(-1, "from", {}), a.vec(-1, "to", {}));
    ++s.version;
    return Value();
  }, "fog(density, top, glow, color, from, to)");
  vm.native("haze", [](Args& a) {
    Stage& s = stageOf(a.vm);
    s.scene.fog.haze.visibility = f32(a.num(0, "visibility", 60000));
    s.scene.fog.haze.height = f32(a.num(1, "height", 1500));
    if (s.scene.fog.haze.visibility < 0 || s.scene.fog.haze.height <= 0) a.vm.fail("haze needs a visibility of 0 or more and a height above 0");
    ++s.version;
    return Value();
  }, "haze(visibility: 60 km, height: 1.5 km): far things fade to the horizon's colour, blue first; the air thins with height");
}

}  // namespace loop::script::words
