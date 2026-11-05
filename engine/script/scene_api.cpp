#include "engine/script/scene_api.h"

#include <algorithm>
#include <map>
#include <unordered_map>

#include "engine/core/random.h"
#include "engine/core/settings.h"
#include "engine/geometry/plants.h"
#include "engine/render/materials.h"

namespace loop::script {

Vec3 unitLuminanceOr(Vec3 c);

void Stage::updateTransform(int i) {
  Placed& p = placed[i];
  Quat q = p.useQuat ? p.quat : Quat::euler(p.rotation.x, p.rotation.y, p.rotation.z);
  Vec3 s = p.visible ? p.scale : Vec3(0);
  scene.instances[p.instance].transform = Mat4::trs(p.position, q, s);
  ++transformsVersion;
}

void Stage::setListener(const Camera& cam) {
  mixer.listener.position = cam.position;
  mixer.listener.forward = normalize(cam.target - cam.position);
}

void Stage::update(float dt) {
  if (dt <= 0) return;
  // the physics settings (project, scene and editor) in effect now
  const Config::Physics& pc = config().physics;
  world.gravity = pc.gravity;
  world.stepRate = static_cast<float>(pc.rate);
  world.velocityIterations = pc.iterations;
  world.sleepSpeed = pc.sleep;
  world.staticFriction = pc.friction, world.staticBounce = pc.bounce;
  world.walkableCos = std::cos(pc.charSlope * kPi / 180);
  world.clothIterations = pc.clothIterations;
  world.wind = pc.wind;
  // fixed things to bump into: every simple shape that isn't solid
  if (collidersDirty) {
    world.clearStatic();
    for (const Placed& p : placed)
      if (p.collide && p.body < 0 && p.visible) world.addStaticMesh(scene.meshes[scene.instances[p.instance].mesh], scene.instances[p.instance].transform);
    collidersDirty = false;
  }
  // physics: solid shapes follow their bodies
  world.step(dt);
  for (size_t i = 0; i < placed.size(); ++i) {
    Placed& p = placed[i];
    if (p.body < 0) continue;
    const physics::Body& b = world.body(p.body);
    // a sudden stop is a knock: play the material's sound, as hard as it hit
    float dv = length(b.v - p.lastVelocity);
    if (dv > 1.2f && !b.asleep) {
      const Material& m = scene.materials[scene.instances[p.instance].material];
      mixer.play(audio::makeSound("impact", audio::surfaceOf(m.name), clamp(dv / 6, 0.1f, 1.0f), static_cast<uint32_t>(i * 7919 + static_cast<int>(dv * 100))), b.x,
                 clamp(dv / 5, 0.2f, 1.0f), true, false, audio::Category::kEffects);
    }
    p.lastVelocity = b.v;
    if (b.asleep && !p.useQuat) continue;
    p.position = b.x, p.quat = b.q, p.useQuat = true;
    updateTransform(static_cast<int>(i));
  }
  // people: steer, animate, and lay the body's shapes along the bones
  for (Person& per : people) {
    anim::Animator& a = *per.animator;
    if (per.walking) {
      Vec3 d = per.walkTo - a.position;
      d.y = 0;
      float dist = length(d);
      // slow down as they arrive (no sudden stop)
      float want = per.speed * std::max(0.12f, smoothstep(0.0f, 1.2f, dist));
      a.velocity = dist > 0.04f ? d / dist * std::min(want, dist / std::max(dt, 1e-4f)) : Vec3(0);
      if (dist <= 0.04f) per.walking = false, a.velocity = Vec3(0);
    }
    a.update(dt);
    auto w = a.world();
    for (auto& [pi, bones] : per.parts) {
      Placed& p = placed[pi];
      Vec3 from = w[bones.first].position, to = bones.second >= 0 ? w[bones.second].position : from + w[bones.first].rotation.rotate({0, 0.12f, 0});
      p.position = (from + to) * 0.5f;
      Vec3 dir = to - from;
      p.quat = lengthSq(dir) > 1e-8f ? Quat::fromTo({0, 1, 0}, normalize(dir)) * Quat::axisAngle({0, 1, 0}, 0) : w[bones.first].rotation;
      if (bones.second < 0) p.quat = w[bones.first].rotation;
      p.useQuat = true;
      updateTransform(pi);
    }
  }
}

namespace {

struct StoryValue {
  int kind = 0;  // 0 nothing, 1 number, 2 true/false, 3 text
  double n = 0;
  std::string s;
};
std::map<std::string, StoryValue>& storyMemory() {
  static std::map<std::string, StoryValue> m;
  return m;
}

std::unordered_map<Vm*, Stage*>& stages() {
  static std::unordered_map<Vm*, Stage*> m;
  return m;
}
Stage& stageOf(Vm& vm) { return *stages().at(&vm); }

std::map<std::string, int>& meshCache(Stage& s) { return s.meshCache; }

// A script value as a setting's value: numbers, yes/no, text, vec(…) or [a, b, c].
Json settingValue(Vm& vm, const Value& v) {
  if (v.isNumber()) return Json(v.n);
  if (v.isBool()) return Json(v.b);
  if (v.isVec()) {
    Json a = Json::array();
    for (int i = 0; i < 3; ++i) a.push(Json(static_cast<double>(v.v[i])));
    return a;
  }
  if (List* l = v.as<List>()) {
    Json a = Json::array();
    for (const Value& x : l->items) a.push(x.isNumber() ? Json(x.n) : Json(vm.show(x)));
    return a;
  }
  if (v.isNothing()) return Json();
  return Json(vm.show(v));
}
void setSetting(Vm& vm, const std::string& id, const Value& v) {
  std::string err;
  // names may be written with _ for the dot: physics_iterations
  std::string name = id;
  if (!Settings::get().find(name) && name.find('.') == std::string::npos) {
    size_t u = name.find('_');
    if (u != std::string::npos) name[u] = '.';
  }
  if (!Settings::get().set(Settings::kScene, name, settingValue(vm, v), &err)) vm.fail(err, "every setting is listed in docs/SETTINGS.md");
}
int cachedMesh(Stage& st, const std::string& key, const std::function<Mesh()>& make) {
  auto& c = meshCache(st);
  auto it = c.find(key);
  if (it != c.end()) return it->second;
  int m = st.scene.addMesh(make());
  c[key] = m;
  return m;
}
std::string keyOf(const char* kind, std::initializer_list<float> v) {
  std::string k = kind;
  for (float x : v) k += " " + std::to_string(x);
  return k;
}

// ---- thing classes -------------------------------------------------------
Value getMaterial(Vm& vm, Thing& t, const std::string& f, bool& found);
bool setMaterial(Vm& vm, Thing& t, const std::string& f, const Value& v);
const ThingClass kMaterialClass{"material", getMaterial, setMaterial,
                                {"name", "color", "roughness", "metal", "glass", "glow", "ior", "clearcoat", "sheen", "subsurface", "translucent", "opacity"}};
Value getShape(Vm& vm, Thing& t, const std::string& f, bool& found);
bool setShape(Vm& vm, Thing& t, const std::string& f, const Value& v);
const ThingClass kShapeClass{"shape", getShape, setShape, {"position", "at", "turn", "rotation", "scale", "material", "visible", "name"}};
Value getLight(Vm& vm, Thing& t, const std::string& f, bool& found);
bool setLight(Vm& vm, Thing& t, const std::string& f, const Value& v);
const ThingClass kLightClass{"light", getLight, setLight, {"position", "at", "lumens", "brightness", "color", "kelvin", "radius", "aim", "on"}};
Value getPerson(Vm& vm, Thing& t, const std::string& f, bool& found);
bool setPerson(Vm& vm, Thing& t, const std::string& f, const Value& v);
const ThingClass kPersonClass{"person", getPerson, setPerson, {"position", "at", "walk_to", "look_at", "sitting", "speed", "facing", "walking", "visible", "lift"}};
Value getSound(Vm& vm, Thing& t, const std::string& f, bool& found);
bool setSound(Vm& vm, Thing& t, const std::string& f, const Value& v);
const ThingClass kSoundClass{"sound", getSound, setSound, {"volume", "position", "at", "playing"}};
Value getTerrain(Vm& vm, Thing& t, const std::string& f, bool& found);
const ThingClass kTerrainClass{"terrain", getTerrain, nullptr, {"size", "lowest", "highest"}};

Material* matOf(Vm& vm, Thing& t) {
  Stage& s = stageOf(vm);
  if (t.id < 0 || t.id >= static_cast<int>(s.scene.materials.size())) vm.fail("that material is gone");
  return &s.scene.materials[t.id];
}
Value getMaterial(Vm& vm, Thing& t, const std::string& f, bool& found) {
  Material* m = matOf(vm, t);
  found = true;
  if (f == "name") return vm.text(m->name);
  if (f == "color") return Value::vec(m->baseColor);
  if (f == "roughness") return Value::number(m->roughness);
  if (f == "metal") return Value::number(m->metallic);
  if (f == "glass") return Value::number(m->transmission);
  if (f == "glow") return Value::vec(m->emission);
  found = false;
  return {};
}
// Settings shared by material() and setting fields.
bool applyMaterialSetting(Vm& vm, Material& m, const std::string& f, const Value& v) {
  auto n = [&] {
    if (!v.isNumber()) vm.fail("`" + f + "` should be a number");
    return static_cast<float>(v.n);
  };
  auto c = [&] {
    if (v.isVec()) return v.asVec();
    if (v.isNumber()) return Vec3(static_cast<float>(v.n));
    vm.fail("`" + f + "` should be a colour, like rgb(0.8, 0.2, 0.1)");
  };
  if (f == "color" || f == "colour") m.baseColor = c();
  else if (f == "roughness") m.roughness = n();
  else if (f == "metal" || f == "metallic") m.metallic = n();
  else if (f == "glass" || f == "transmission") m.transmission = n();
  else if (f == "ior") m.ior = n();
  else if (f == "dispersion") m.dispersion = n();
  else if (f == "glow" || f == "emission") m.emission = v.isNumber() ? Vec3(n()) : c();
  else if (f == "glow_color") m.emission = c() * std::max(luminance(m.emission), 1.0f);
  else if (f == "clearcoat") m.clearcoat = n();
  else if (f == "sheen") m.sheen = n();
  else if (f == "subsurface") m.subsurface = n();
  else if (f == "subsurface_color") m.subsurfaceColor = c();
  else if (f == "translucent" || f == "translucency") m.translucency = n();
  else if (f == "opacity") m.opacity = n();
  else if (f == "leaf") m.leafShape = v.truthy();
  else if (f == "pattern") {
    static const std::pair<const char*, Pattern::Kind> kinds[] = {
        {"none", Pattern::Kind::kNone}, {"checker", Pattern::Kind::kChecker}, {"tiles", Pattern::Kind::kTiles}, {"bricks", Pattern::Kind::kBricks},
        {"wood", Pattern::Kind::kWood}, {"marble", Pattern::Kind::kMarble}, {"noise", Pattern::Kind::kNoise}, {"ground", Pattern::Kind::kGround},
        {"planks", Pattern::Kind::kPlanks}};
    std::string p = vm.show(v);
    std::vector<std::string> names;
    for (auto& [k, kind] : kinds) {
      names.push_back(k);
      if (p == k) { m.pattern.kind = kind; return true; }
    }
    std::string cl = closest(p, names);
    vm.fail("there's no pattern `" + p + "`", cl.empty() ? "patterns: checker, tiles, bricks, wood, planks, marble, noise, ground" : "did you mean `" + cl + "`?");
  } else if (f == "pattern_color" || f == "pattern_colour") m.pattern.colorB = c();
  else if (f == "pattern_scale") m.pattern.scale = n();
  else if (f == "bump") m.pattern.bump = n() * 1000;  // written in mm: `bump: 1 mm`
  else return false;
  return true;
}
bool setMaterial(Vm& vm, Thing& t, const std::string& f, const Value& v) {
  bool ok = applyMaterialSetting(vm, *matOf(vm, t), f, v);
  if (ok) ++stageOf(vm).version;
  return ok;
}

Stage::Placed& placedOf(Vm& vm, Thing& t) {
  Stage& s = stageOf(vm);
  if (t.id < 0 || t.id >= static_cast<int>(s.placed.size())) vm.fail("that shape is gone");
  return s.placed[t.id];
}
Value getShape(Vm& vm, Thing& t, const std::string& f, bool& found) {
  Stage::Placed& p = placedOf(vm, t);
  found = true;
  if (f == "position" || f == "at") return Value::vec(p.position);
  if (f == "turn") return Value::number(p.rotation.x);
  if (f == "rotation") return Value::vec(p.rotation);
  if (f == "scale") return Value::vec(p.scale);
  if (f == "visible") return Value::boolean(p.visible);
  if (f == "name") return vm.text(p.name);
  if (f == "material") {
    Stage& s = stageOf(vm);
    return Value::object(vm.newThing(&kMaterialClass, s.scene.instances[p.instance].material));
  }
  found = false;
  return {};
}
Vec3 asVec(Vm& vm, const Value& v, const std::string& f) {
  if (v.isVec()) return v.asVec();
  if (v.isNumber()) return Vec3(static_cast<float>(v.n));
  vm.fail("`" + f + "` should be a vector, like vec(1, 2, 3)");
}
int materialFrom(Vm& vm, const Value& v);
bool setShape(Vm& vm, Thing& t, const std::string& f, const Value& v) {
  Stage& s = stageOf(vm);
  Stage::Placed& p = placedOf(vm, t);
  if (f == "position" || f == "at") p.position = asVec(vm, v, f);
  else if (f == "turn") {
    if (!v.isNumber()) vm.fail("`turn` is an angle, like 30 deg");
    p.rotation.x = static_cast<float>(v.n);
  } else if (f == "rotation") p.rotation = asVec(vm, v, f);
  else if (f == "scale") p.scale = asVec(vm, v, f);
  else if (f == "visible") p.visible = v.truthy();
  else if (f == "name") p.name = vm.show(v);
  else if (f == "material") {
    s.scene.instances[p.instance].material = materialFrom(vm, v);
    ++s.version;
    return true;
  } else return false;
  s.updateTransform(t.id);
  return true;
}

Light& lightOf(Vm& vm, Thing& t) {
  Stage& s = stageOf(vm);
  if (t.id < 0 || t.id >= static_cast<int>(s.scene.lights.size())) vm.fail("that light is gone");
  return s.scene.lights[t.id];
}
Value getLight(Vm& vm, Thing& t, const std::string& f, bool& found) {
  Light& l = lightOf(vm, t);
  found = true;
  if (f == "position" || f == "at") return Value::vec(l.position);
  if (f == "lumens" || f == "brightness") return Value::number(l.kind == Light::Kind::kSun ? l.lux : l.lumens);
  if (f == "color") return Value::vec(l.color);
  if (f == "radius") return Value::number(l.radius);
  if (f == "aim") return Value::vec(l.direction);
  found = false;
  return {};
}
bool setLight(Vm& vm, Thing& t, const std::string& f, const Value& v) {
  Light& l = lightOf(vm, t);
  if (f == "position" || f == "at") l.position = asVec(vm, v, f);
  else if (f == "lumens" || f == "brightness") {
    if (!v.isNumber()) vm.fail("`" + f + "` is a number of lumens");
    (l.kind == Light::Kind::kSun ? l.lux : l.lumens) = static_cast<float>(std::max(0.0, v.n));
  } else if (f == "color") l.color = unitLuminanceOr(asVec(vm, v, f));
  else if (f == "kelvin") l.color = kelvin(static_cast<float>(v.n));
  else if (f == "radius") l.radius = static_cast<float>(v.n);
  else if (f == "aim") l.direction = normalize(asVec(vm, v, f));
  else return false;
  // a sun changes the sky too; a lamp only itself
  if (l.kind == Light::Kind::kSun) ++stageOf(vm).version;
  else ++stageOf(vm).lightsVersion;
  return true;
}

Value getTerrain(Vm& vm, Thing& t, const std::string& f, bool& found) {
  HeightMap& h = stageOf(vm).terrains[t.id];
  found = true;
  if (f == "size") return Value::number(h.size());
  if (f == "lowest") return Value::number(h.minHeight());
  if (f == "highest") return Value::number(h.maxHeight());
  found = false;
  return {};
}

Stage::Person& personOf(Vm& vm, Thing& t) {
  Stage& s = stageOf(vm);
  if (t.id < 0 || t.id >= static_cast<int>(s.people.size())) vm.fail("that person is gone");
  return s.people[t.id];
}
Value getPerson(Vm& vm, Thing& t, const std::string& f, bool& found) {
  Stage::Person& p = personOf(vm, t);
  found = true;
  if (f == "position" || f == "at") return Value::vec(p.animator->position);
  if (f == "walk_to") return Value::vec(p.walkTo);
  if (f == "look_at") return Value::vec(p.animator->lookAt);
  if (f == "sitting") return Value::boolean(p.animator->sitting);
  if (f == "walking") return Value::boolean(p.walking);
  if (f == "speed") return Value::number(p.speed);
  if (f == "facing") return Value::number(p.animator->facing);
  if (f == "visible") return Value::boolean(p.visible);
  if (f == "lift") return Value::number(p.animator->lift);
  found = false;
  return {};
}
bool setPerson(Vm& vm, Thing& t, const std::string& f, const Value& v) {
  Stage::Person& p = personOf(vm, t);
  if (f == "position" || f == "at") p.animator->position = asVec(vm, v, f), p.walking = false;
  else if (f == "walk_to") p.walkTo = asVec(vm, v, f), p.walking = true;
  else if (f == "look_at") {
    if (v.isNothing()) p.animator->looking = false;
    else p.animator->lookAt = asVec(vm, v, f), p.animator->looking = true;
  } else if (f == "sitting") p.animator->sitting = v.truthy();
  else if (f == "speed") p.speed = static_cast<float>(v.n);
  else if (f == "facing") p.animator->facing = static_cast<float>(v.n);
  else if (f == "lift") p.animator->lift = static_cast<float>(v.n);
  else if (f == "visible") {
    Stage& s = stageOf(vm);
    p.visible = v.truthy();
    for (auto& part : p.parts) {
      s.placed[part.first].visible = p.visible;
      s.updateTransform(part.first);
    }
  } else return false;
  return true;
}
Value getSound(Vm& vm, Thing& t, const std::string& f, bool& found) {
  Stage& s = stageOf(vm);
  Stage::SoundRef& r = s.sounds[t.id];
  found = true;
  if (f == "volume") return Value::number(r.volume);
  if (f == "position" || f == "at") return Value::vec(r.at);
  if (f == "playing") return Value::boolean(s.mixer.playing(r.mixerId));
  found = false;
  return {};
}
bool setSound(Vm& vm, Thing& t, const std::string& f, const Value& v) {
  Stage& s = stageOf(vm);
  Stage::SoundRef& r = s.sounds[t.id];
  if (f == "volume") r.volume = static_cast<float>(v.n), s.mixer.setVolume(r.mixerId, r.volume);
  else if (f == "position" || f == "at") r.at = asVec(vm, v, f), s.mixer.move(r.mixerId, r.at);
  else if (f == "playing") {
    if (!v.truthy()) s.mixer.stop(r.mixerId, 0.5f);
  } else return false;
  return true;
}

int materialFrom(Vm& vm, const Value& v) {
  Stage& s = stageOf(vm);
  if (Thing* t = v.as<Thing>(); t && t->cls == &kMaterialClass) return t->id;
  if (v.isString()) {
    int i = s.scene.findMaterial(v.str());
    if (i >= 0) return i;
    Material m;
    if (!materialByName(v.str(), m)) {
      std::string c = closest(v.str(), materialNames());
      vm.fail("there's no material called \"" + v.str() + "\"", c.empty() ? "make one with material(color: …), or use a name like \"oak\", \"brass\", \"glass\"" : "did you mean \"" + c + "\"?");
    }
    return s.scene.addMaterial(m);
  }
  if (v.isVec()) {  // a bare colour
    Material m;
    m.name = "colour " + vm.show(v);
    m.baseColor = v.asVec();
    return s.scene.addMaterial(m);
  }
  if (v.isNothing()) {
    int i = s.scene.findMaterial("default");
    return i >= 0 ? i : s.scene.addMaterial(Material());
  }
  vm.fail("a material is a name like \"oak\", a material(...) or a colour");
}

// A shape placed in the world, with the common settings: at, turn,
// rotation, scale, material, name.
Value place(Args& a, int mesh, char kind = 'm', Vec3 size = Vec3(1)) {
  Stage& s = stageOf(a.vm);
  int mat = materialFrom(a.vm, a.get(-1, "material") ? *a.get(-1, "material") : (a.get(-1, "made_of") ? *a.get(-1, "made_of") : Value()));
  s.scene.add(mesh, mat);
  Stage::Placed p;
  p.instance = static_cast<int>(s.scene.instances.size() - 1);
  p.position = a.vec(-1, "at", {});
  p.scale = a.vec(-1, "scale", Vec3(1));
  p.rotation = a.vec(-1, "rotation", Vec3(0));
  if (a.has(-1, "turn")) p.rotation.x = static_cast<float>(a.num(-1, "turn", 0));
  if (a.has(-1, "tilt")) p.rotation.y = static_cast<float>(a.num(-1, "tilt", 0));
  p.name = a.text(-1, "name", "");
  p.kind = kind, p.size = size;
  // simple shapes are things you bump into (walls, tables), unless told not
  p.collide = a.flag(-1, "collide", true) && kind != 'm';
  s.collidersDirty = true;
  s.placed.push_back(p);
  s.updateTransform(static_cast<int>(s.placed.size() - 1));
  return Value::object(a.vm.newThing(&kShapeClass, static_cast<int>(s.placed.size() - 1)));
}

float f32(double d) { return static_cast<float>(d); }

// Trees, kept as a few shapes per kind and placed many times.
std::pair<int, int> treeMeshes(Stage& st, const std::string& kind, int variant) {
  auto& c = meshCache(st);
  std::string key = "tree " + kind + " " + std::to_string(variant);
  auto it = c.find(key + " bark");
  if (it != c.end()) return {it->second, c[key + " leaves"]};
  TreeShape ts;
  ts.seed = static_cast<uint32_t>(variant * 7919 + 13);
  if (kind == "pine" || kind == "fir" || kind == "spruce") {
    ts.conifer = true, ts.height = 14, ts.levels = 2, ts.branches = 10, ts.leavesPerTip = 8, ts.leafSize = 0.45f, ts.trunkRadius = 0.25f;
  } else if (kind == "birch") {
    ts.height = 11, ts.trunkRadius = 0.14f, ts.spread = 0.5f, ts.upward = 0.6f, ts.leafSize = 0.18f, ts.leavesPerTip = 16;
  } else if (kind == "bush") {
    ts.height = 2, ts.trunkRadius = 0.04f, ts.levels = 3, ts.branches = 6, ts.spread = 1.0f, ts.leafSize = 0.12f;
  } else {  // oak and anything else broad
    ts.height = 10, ts.trunkRadius = 0.32f, ts.spread = 0.9f, ts.leafSize = 0.26f, ts.leavesPerTip = 16, ts.gnarl = 0.4f;
  }
  TreeMeshes t = growTree(ts);
  int b = st.scene.addMesh(std::move(t.bark)), l = st.scene.addMesh(std::move(t.leaves));
  c[key + " bark"] = b;
  c[key + " leaves"] = l;
  return {b, l};
}

}  // namespace

Vec3 unitLuminanceOr(Vec3 c) {
  float l = luminance(c);
  return l > 1e-6f ? c / l : Vec3(1);
}

void installSceneApi(Vm& vm, Stage& stage) {
  stages()[&vm] = &stage;
  if (stage.scene.findMaterial("default") < 0) stage.scene.addMaterial(Material());
  // a new scene starts from the project's settings: forget the last scene's
  Settings::get().clear(Settings::kScene);
  if (!config().project.mode.empty()) stage.mode = config().project.mode;

  // ---- settings: the same names as the editor and project.json
  vm.native("setting", [](Args& a) {
    std::string id = a.text(0, "name", "");
    const Value* v = a.get(1, "value");
    if (id.empty() || !v) a.vm.fail("setting needs a name and a value, like setting \"render.lamps\", 4");
    setSetting(a.vm, id, *v);
    return Value();
  }, "setting \"physics.iterations\", 24: changes an engine setting for this scene (names as in the editor)");
  vm.native("settings", [](Args& a) {
    Table* t = a.count ? a.pos[0].as<Table>() : nullptr;
    if (!t) a.vm.fail("settings needs a table, like settings {render_lamps: 4, story_textSpeed: 20}");
    for (auto& [k, v] : t->entries) setSetting(a.vm, k, v);
    return Value();
  }, "settings {story_textSpeed: 20, audio_reverb: 1.5}: several settings at once (_ or . between the parts)");
  vm.native("get_setting", [](Args& a) {
    std::string id = a.text(0, "name", "");
    if (!Settings::get().find(id)) {
      std::string s = Settings::get().suggest(id);
      a.vm.fail("there's no setting \"" + id + "\"", s.empty() ? "" : "did you mean \"" + s + "\"?");
    }
    Json j = Settings::get().value(id);
    if (j.isNumber()) return Value::number(j.number());
    if (j.type() == Json::Type::kBool) return Value::boolean(j.boolean());
    if (j.isArray()) return Value::vec(Vec3(static_cast<float>(j[0].number()), static_cast<float>(j[1].number()), static_cast<float>(j[2].number())));
    return a.vm.text(j.string());
  }, "get_setting(\"player.walk\"): a setting's value in effect now");

  vm.native("title", [](Args& a) {
    stageOf(a.vm).title = a.text(0, "name", "Untitled");
    return Value();
  }, "title \"The Last Ember\"");
  vm.native("mode", [](Args& a) {
    std::string m = a.text(0, "kind", "game");
    if (m != "game" && m != "movie") a.vm.fail("mode is \"game\" or \"movie\"");
    stageOf(a.vm).mode = m;
    return Value();
  }, "mode \"movie\": plays its shots; \"game\": you walk around");

  // ---- materials
  vm.native("material", [](Args& a) {
    Stage& s = stageOf(a.vm);
    Material m;
    std::string base = a.text(0, "like", "");
    if (!base.empty() && !materialByName(base, m)) {
      std::string c = closest(base, materialNames());
      a.vm.fail("there's no material called \"" + base + "\"", c.empty() ? "" : "did you mean \"" + c + "\"?");
    }
    m.name = a.text(-1, "name", base.empty() ? "material " + std::to_string(s.scene.materials.size()) : base);
    for (auto& [k, v] : a.named)
      if (k != "like" && k != "name" && !applyMaterialSetting(a.vm, m, k, v)) {
        std::vector<std::string> opts = {"color", "roughness", "metal", "glass", "ior", "dispersion", "glow", "clearcoat", "sheen", "subsurface",
                                         "translucent", "opacity", "pattern", "pattern_color", "pattern_scale", "bump", "leaf"};
        std::string c = closest(k, opts);
        a.vm.fail("a material has no setting `" + k + "`", c.empty() ? "" : "did you mean `" + c + "`?");
      }
    return Value::object(a.vm.newThing(&kMaterialClass, s.scene.addMaterial(m)));
  }, "material(\"oak\", roughness: 0.3) or material(color: rgb(…), metal: 1, …)");
  vm.native("materials", [](Args& a) {
    List* l = a.vm.newList();
    for (auto& n : materialNames()) l->items.push_back(a.vm.text(n));
    return Value::object(l);
  }, "materials(): every material name");

  // ---- shapes
  vm.native("box", [](Args& a) {
    Vec3 sz = a.vec(0, "size", Vec3(1));
    float r = f32(a.num(-1, "round", 0));
    int m = cachedMesh(stageOf(a.vm), keyOf("box", {sz.x, sz.y, sz.z, r}), [&] { return r > 0 ? shapes::roundedBox(sz, r, 4) : shapes::box(sz); });
    return place(a, m, 'b', sz);
  }, "box(size: vec(w, h, d), at, turn, material, round)");
  vm.native("sphere", [](Args& a) {
    float r = f32(a.num(0, "radius", a.num(-1, "size", 1) * 0.5));
    return place(a, cachedMesh(stageOf(a.vm), keyOf("sphere", {r}), [&] { return shapes::sphere(r, 96, 48); }), 's', Vec3(r));
  }, "sphere(radius, at, material)");
  vm.native("cylinder", [](Args& a) {
    float r = f32(a.num(0, "radius", 0.5)), h = f32(a.num(1, "height", 1));
    return place(a, cachedMesh(stageOf(a.vm), keyOf("cyl", {r, h}), [&] { return shapes::cylinder(r, h, 64); }), 'y', Vec3(r, h, r));
  }, "cylinder(radius, height, at, material)");
  vm.native("cone", [](Args& a) {
    float r = f32(a.num(0, "radius", 0.5)), h = f32(a.num(1, "height", 1));
    return place(a, cachedMesh(stageOf(a.vm), keyOf("cone", {r, h}), [&] { return shapes::cone(r, h, 64); }));
  }, "cone(radius, height, at, material)");
  vm.native("capsule", [](Args& a) {
    float r = f32(a.num(0, "radius", 0.3)), h = f32(a.num(1, "height", 1.8));
    return place(a, cachedMesh(stageOf(a.vm), keyOf("capsule", {r, h}), [&] { return shapes::capsule(r, h); }), 'c', Vec3(r, h, r));
  }, "capsule(radius, height, at, material)");
  vm.native("ring", [](Args& a) {
    float r = f32(a.num(0, "radius", 0.5)), t = f32(a.num(1, "thickness", 0.1));
    return place(a, cachedMesh(stageOf(a.vm), keyOf("torus", {r, t}), [&] { return shapes::torus(r, t, 96, 48); }));
  }, "ring(radius, thickness, at, material): a torus");
  vm.native("ground", [](Args& a) {
    float sz = f32(a.num(0, "size", 100));
    Stage& st = stageOf(a.vm);
    st.world.groundPlane = true;
    st.world.groundHeight = a.vec(-1, "at", {}).y;
    Value v = place(a, cachedMesh(st, keyOf("plane", {sz}), [&] { return shapes::plane({sz, sz}); }));
    const Material& gm = st.scene.materials[st.scene.instances.back().material];
    st.groundSurface = audio::surfaceOf(gm.name);
    return v;
  }, "ground(size, material): a flat floor");
  vm.native("rock", [](Args& a) {
    float sz = f32(a.num(0, "size", 1));
    int seed = static_cast<int>(a.num(-1, "seed", 1));
    return place(a, cachedMesh(stageOf(a.vm), keyOf("rock", {sz, static_cast<float>(seed)}), [&] {
                   RockShape r;
                   r.size = sz, r.seed = static_cast<uint32_t>(seed);
                   return makeRock(r);
                 }));
  }, "rock(size, at, seed, material)");
  vm.native("tree", [](Args& a) {
    Stage& s = stageOf(a.vm);
    std::string kind = a.text(0, "kind", "oak");
    auto [b, l] = treeMeshes(s, kind, static_cast<int>(a.num(-1, "variant", 0)) % 4);
    bool conifer = kind == "pine" || kind == "fir" || kind == "spruce";
    Vec3 at = a.vec(-1, "at", {});
    float scale = f32(a.num(-1, "height", conifer ? 14 : 10) / (conifer ? 14 : 10));
    int bark = materialFrom(a.vm, a.vm.text("bark"));
    int leaf = materialFrom(a.vm, a.vm.text(a.text(-1, "leaves", conifer ? "needles" : "leaves")));
    Mat4 xf = Mat4::trs(at, Quat::axisAngle({0, 1, 0}, f32(a.num(-1, "turn", 0))), Vec3(scale));
    s.scene.add(b, bark, xf);
    s.scene.add(l, leaf, xf);
    ++s.version;
    return Value();
  }, "tree(kind: \"oak\" | \"pine\" | \"birch\" | \"bush\", at, height)");

  // ---- land
  vm.native("terrain", [](Args& a) {
    Stage& s = stageOf(a.vm);
    float size = f32(a.num(-1, "size", 1000));
    int res = static_cast<int>(a.num(-1, "detail", 257) * config().world.terrainRes);
    HeightMap h(std::clamp(res, 17, 1025), size);
    HeightMap::Shape sh;
    sh.height = f32(a.num(-1, "height", 120));
    sh.featureSize = f32(a.num(-1, "feature", size * 0.4));
    sh.ridges = f32(a.num(-1, "ridges", 0.5));
    sh.seed = static_cast<uint32_t>(a.num(-1, "seed", 1));
    h.generate(sh);
    if (a.flag(-1, "erosion", true)) {
      HeightMap::Erosion e;
      e.droplets = static_cast<int>(a.num(-1, "rain", res * res * 2.5));
      h.erodeHydraulic(e);
      h.erodeThermal(6);
    }
    // keep the middle near zero so things placed at y 0 sit on the land
    float mid = h.height(0, 0);
    for (int z = 0; z < h.resolution(); ++z)
      for (int x = 0; x < h.resolution(); ++x) h.at(x, z) -= mid;
    s.terrains.push_back(h);
    s.world.setTerrain(&s.terrains.back());
    s.world.groundPlane = false;
    int mesh = s.scene.addMesh(h.toMesh(1));
    int mat = materialFrom(a.vm, a.get(-1, "material") ? *a.get(-1, "material") : a.vm.text("grass"));
    s.scene.add(mesh, mat);
    ++s.version;
    return Value::object(a.vm.newThing(&kTerrainClass, static_cast<int>(s.terrains.size() - 1)));
  }, "terrain(size, height, ridges, seed, erosion, material)");
  vm.native("height_at", [](Args& a) {
    Stage& s = stageOf(a.vm);
    Thing* t = a.get(-1, "on") ? a.get(-1, "on")->as<Thing>() : nullptr;
    const HeightMap* h = t && t->cls == &kTerrainClass ? &s.terrains[t->id] : (s.terrains.empty() ? nullptr : &s.terrains.back());
    if (!h) return Value::number(0);
    Vec3 p = a.vec(0, "at", {});
    return Value::number(h->height(p.x, p.z));
  }, "height_at(vec(x, 0, z), on: land): the ground's height there");
  vm.native("forest", [](Args& a) {
    Stage& s = stageOf(a.vm);
    std::string kind = a.text(-1, "kind", "pine");
    Vec3 c = a.vec(-1, "around", {});
    float radius = f32(a.num(-1, "radius", 60)), spacing = f32(a.num(-1, "spacing", 7));
    // world.foliageDensity: trees per area (spacing shrinks as density grows)
    float density = config().world.foliageDensity;
    if (density <= 0.001f) return Value::number(0);
    spacing /= std::sqrt(density);
    float maxSlope = f32(a.num(-1, "steepest", 0.3));
    float clearing = f32(a.num(-1, "clearing", 0));
    Vec3 clearAt = a.vec(-1, "clear_around", c);
    const HeightMap* h = s.terrains.empty() ? nullptr : &s.terrains.back();
    Rng rng(static_cast<uint64_t>(a.num(-1, "seed", 3)));
    auto pts = poissonDisk(radius * 2, spacing, static_cast<uint32_t>(a.num(-1, "seed", 3)));
    bool conifer = kind == "pine" || kind == "fir" || kind == "spruce";
    int bark = materialFrom(a.vm, a.vm.text("bark"));
    int leaf = materialFrom(a.vm, a.vm.text(a.text(-1, "leaves", conifer ? "needles" : "leaves")));
    int count = 0;
    for (Vec2 p : pts) {
      Vec3 w{c.x + p.x, 0, c.z + p.y};
      if (length(Vec2(p.x, p.y)) > radius) continue;
      if (clearing > 0 && length(Vec2(w.x - clearAt.x, w.z - clearAt.z)) < clearing) continue;
      if (h && h->slope(w.x, w.z) > maxSlope) continue;
      w.y = h ? h->height(w.x, w.z) - 0.2f : c.y;
      auto [b, l] = treeMeshes(s, kind, rng.below(4));
      Mat4 xf = Mat4::trs(w, Quat::axisAngle({0, 1, 0}, rng.uniform() * kTwoPi), Vec3(0.7f + 0.6f * rng.uniform()));
      s.scene.add(b, bark, xf);
      s.scene.add(l, leaf, xf);
      ++count;
    }
    ++s.version;
    return Value::number(count);
  }, "forest(kind, around, radius, spacing, clearing, steepest): plants trees on the land");

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

  // ---- physics: things that fall, roll and knock
  vm.native("solid", [](Args& a) {
    Stage& s = stageOf(a.vm);
    Thing* t = a.count ? a.pos[0].as<Thing>() : nullptr;
    if (!t || t->cls != &kShapeClass) a.vm.fail("solid() needs a shape, like solid(ball, mass: 2)");
    Stage::Placed& p = s.placed[t->id];
    if (p.body >= 0) return a.pos[0];
    physics::BodyDesc d;
    switch (p.kind) {
      case 's': d.shape = physics::ShapeKind::kSphere, d.radius = p.size.x * p.scale.x; break;
      case 'c': d.shape = physics::ShapeKind::kCapsule, d.radius = p.size.x * p.scale.x, d.halfHeight = std::max(0.0f, p.size.y * 0.5f - p.size.x); break;
      case 'y':  // a cylinder moves as a capsule of its size
        d.shape = physics::ShapeKind::kCapsule, d.radius = p.size.x * p.scale.x, d.halfHeight = std::max(0.0f, p.size.y * 0.5f - p.size.x);
        break;
      default: d.shape = physics::ShapeKind::kBox, d.halfExtents = p.size * p.scale * 0.5f; break;
    }
    d.mass = f32(a.num(-1, "mass", 1));
    d.restitution = f32(a.num(-1, "bounce", config().physics.bounce));
    d.friction = f32(a.num(-1, "friction", config().physics.friction));
    d.position = p.position;
    d.rotation = Quat::euler(p.rotation.x, p.rotation.y, p.rotation.z);
    d.velocity = a.vec(-1, "velocity", {});
    p.body = s.world.addBody(d);
    return a.pos[0];
  }, "solid(shape, mass: 1 kg, bounce: 0.2, friction: 0.6): it falls, rolls and knocks");
  vm.native("push", [](Args& a) {
    Stage& s = stageOf(a.vm);
    Thing* t = a.count ? a.pos[0].as<Thing>() : nullptr;
    if (!t || t->cls != &kShapeClass || s.placed[t->id].body < 0) a.vm.fail("push() needs a solid shape");
    physics::Body& b = s.world.body(s.placed[t->id].body);
    s.world.applyImpulse(b.id, a.vec(1, "by", {}), a.vec(-1, "at", b.x));
    return Value();
  }, "push(shape, vec(0, 5, 0)): a shove, in kg·m/s");
  vm.native("gravity", [](Args& a) {
    // the scene's physics.gravity setting (the editor can still change it live)
    Vec3 g = a.vec(0, "pull", {0, -9.81f, 0});
    setSetting(a.vm, "physics.gravity", Value::vec(g));
    stageOf(a.vm).world.gravity = config().physics.gravity;
    return Value();
  }, "gravity(vec(0, -1.62, 0)): the pull on everything (m/s²); the Moon is -1.62");

  // ---- people
  vm.native("person", [](Args& a) {
    Stage& s = stageOf(a.vm);
    if (static_cast<int>(s.people.size()) >= config().anim.maxPeople)
      a.vm.fail("this scene already has " + std::to_string(s.people.size()) + " people, the most allowed", "raise the setting anim.maxPeople");
    Stage::Person per;
    float h = f32(a.num(-1, "height", 1.75));
    per.skeleton = std::make_unique<anim::Skeleton>(anim::humanSkeleton(h));
    per.animator = std::make_unique<anim::Animator>(*per.skeleton);
    per.animator->position = a.vec(0, "at", {});
    per.animator->facing = f32(a.num(-1, "facing", 0));
    per.animator->lift = f32(a.num(-1, "lift", 0));
    Stage* sp = &s;
    per.animator->ground = [sp](float x, float z) { return sp->terrains.empty() ? sp->world.groundHeight : sp->terrains.back().height(x, z); };
    int index = static_cast<int>(s.people.size());
    per.animator->onFootstep = [sp, index](int foot, Vec3 at, float strength) {
      std::string surface = sp->people[index].footSurface.empty() ? (sp->terrains.empty() ? sp->groundSurface : std::string("grass")) : sp->people[index].footSurface;
      sp->mixer.play(audio::makeSound("footstep", surface, strength, static_cast<uint32_t>(foot * 131 + at.x * 1000 + at.z * 977)), at, 0.7f, true, false,
                     audio::Category::kFootsteps);
    };
    // the body: limbs as capsules between joints, a head, a torso
    const anim::Skeleton& sk = *per.skeleton;
    int skin = materialFrom(a.vm, a.get(-1, "skin") ? *a.get(-1, "skin") : a.vm.text("skin"));
    int shirt = materialFrom(a.vm, a.get(-1, "shirt") ? *a.get(-1, "shirt") : a.vm.text("fabric"));
    int trousers = materialFrom(a.vm, a.get(-1, "trousers") ? *a.get(-1, "trousers") : a.vm.text("wool"));
    float k = h / 1.75f;
    auto part = [&](const char* from, const char* to, float radius, int mat) {
      int fb = sk.find(from), tb = to ? sk.find(to) : -1;
      float len = tb >= 0 ? length(sk.bones[tb].rest.position) : 0.24f * k;
      int mesh = cachedMesh(s, keyOf("limb", {radius, len}), [&] { return shapes::capsule(radius, len + radius * 2); });
      s.scene.add(mesh, mat);
      Stage::Placed pl;
      pl.instance = static_cast<int>(s.scene.instances.size() - 1);
      pl.useQuat = true;
      s.placed.push_back(pl);
      per.parts.push_back({static_cast<int>(s.placed.size() - 1), {fb, tb}});
    };
    part("pelvis", "chest", 0.16f * k, shirt);
    part("chest", "neck", 0.17f * k, shirt);
    part("neck", "head", 0.05f * k, skin);
    part("head", nullptr, 0.11f * k, skin);
    for (const char* side : {"left ", "right "}) {
      std::string sd = side;
      part((sd + "shoulder").c_str(), (sd + "elbow").c_str(), 0.05f * k, shirt);
      part((sd + "elbow").c_str(), (sd + "wrist").c_str(), 0.042f * k, skin);
      part((sd + "hip").c_str(), (sd + "knee").c_str(), 0.075f * k, trousers);
      part((sd + "knee").c_str(), (sd + "ankle").c_str(), 0.06f * k, trousers);
    }
    s.people.push_back(std::move(per));
    s.update(1e-4f);
    ++s.version;
    return Value::object(a.vm.newThing(&kPersonClass, index));
  }, "person(at, height, skin, shirt, trousers, facing): someone who breathes, walks, looks and sits");

  // ---- sound
  vm.native("sound", [](Args& a) {
    Stage& s = stageOf(a.vm);
    std::string name = a.text(0, "name", "fire");
    std::unique_ptr<audio::Voice> v;
    auto amb = audio::ambienceNames();
    bool endless = std::find(amb.begin(), amb.end(), name) != amb.end();
    if (endless) v = audio::makeAmbience(name, f32(a.num(-1, "intensity", 1)), static_cast<uint32_t>(s.sounds.size() + 1));
    else v = audio::makeSound(name, a.text(1, "variant", "stone"), f32(a.num(-1, "strength", 0.7)), static_cast<uint32_t>(s.sounds.size() + 1));
    if (!v) {
      std::vector<std::string> all = amb;
      for (auto& n : audio::soundNames()) all.push_back(n);
      std::string c = closest(name, all);
      a.vm.fail("there's no sound called \"" + name + "\"", c.empty() ? "" : "did you mean \"" + c + "\"?");
    }
    Stage::SoundRef r;
    r.at = a.vec(-1, "at", {});
    r.volume = f32(a.num(-1, "volume", 1));
    bool everywhere = !a.has(-1, "at");
    r.mixerId = s.mixer.play(std::move(v), r.at, r.volume, !everywhere, endless, endless ? audio::Category::kAmbience : audio::Category::kEffects);
    s.sounds.push_back(r);
    return Value::object(a.vm.newThing(&kSoundClass, static_cast<int>(s.sounds.size() - 1)));
  }, "sound(\"fire\", at: vec(…), volume) for endless sounds; sound(\"impact\", \"glass\", at: …) for one-offs");
  vm.native("room", [](Args& a) {
    Stage& s = stageOf(a.vm);
    s.mixer.roomSize = f32(a.num(0, "size", 0.3));
    s.mixer.roomWet = f32(a.num(-1, "echo", 0.15));
    return Value();
  }, "room(size: 0 outdoors .. 1 a cathedral, echo)");

  // ---- story: lines on screen, questions, the next scene
  vm.native("line", [](Args& a) {
    Stage& s = stageOf(a.vm);
    Stage::UiMessage m;
    m.kind = "line";
    s.story = true;
    if (a.count >= 2) m.speaker = a.vm.show(a.pos[0]), m.text = a.vm.show(a.pos[1]);
    else m.text = a.text(0, "text", "");
    m.speaker = a.text(-1, "speaker", m.speaker);
    // long enough to read: story.textSpeed characters a second (15), at least story.lineMin (2 s)
    const Config::Story& st = config().story;
    size_t chars = 0;
    for (unsigned char ch : m.text) chars += (ch & 0xC0) != 0x80;  // letters, not bytes
    m.seconds = f32(a.num(-1, "seconds", std::max<double>(st.lineMin, chars / std::max(st.textSpeed, 1.0f) + 1)));
    s.ui.push_back(m);
    return Value::number(m.seconds);
  }, "line(\"Mira\", \"The ember has gone out.\"): a line of dialogue on screen; gives back how long it shows");
  vm.native("ask", [](Args& a) {
    Stage& s = stageOf(a.vm);
    Stage::UiMessage m;
    m.kind = "ask";
    s.story = true;
    m.text = a.text(0, "question", "");
    const Value* o = a.get(1, "options");
    List* l = o ? o->as<List>() : nullptr;
    if (!l || l->items.empty()) a.vm.fail("ask() needs a list of options, like ask(\"Which way?\", [\"The woods\", \"The river\"])");
    for (auto& v : l->items) m.options.push_back(a.vm.show(v));
    s.ui.push_back(m);
    return Value();
  }, "ask(question, [options]): buttons appear; `when \"choice\"` gets the number picked (0 = the first)");
  // what the story remembers from scene to scene (survives go_to)
  vm.native("remember", [](Args& a) {
    StoryValue v;
    const Value* x = a.get(1, "value");
    if (!x || x->isNothing()) v.kind = 0;
    else if (x->isNumber()) v.kind = 1, v.n = x->n;
    else if (x->isBool()) v.kind = 2, v.n = x->b ? 1 : 0;
    else v.kind = 3, v.s = a.vm.show(*x);
    storyMemory()[a.text(0, "key", "")] = v;
    return Value();
  }, "remember(\"helped nia\", yes): kept for the rest of the story");
  vm.native("recall", [](Args& a) {
    auto& m = storyMemory();
    auto it = m.find(a.text(0, "key", ""));
    if (it == m.end()) return a.get(1, "otherwise") ? *a.get(1, "otherwise") : Value();
    const StoryValue& v = it->second;
    if (v.kind == 1) return Value::number(v.n);
    if (v.kind == 2) return Value::boolean(v.n != 0);
    if (v.kind == 3) return a.vm.text(v.s);
    return Value();
  }, "recall(\"helped nia\", otherwise: no): what the story remembered");
  vm.native("forget_story", [](Args&) {
    storyMemory().clear();
    return Value();
  }, "forget_story(): start the story's memory afresh");
  vm.native("player", [](Args& a) { return Value::vec(stageOf(a.vm).playerPos); }, "player(): where the player is");
  vm.native("near", [](Args& a) {
    if (stageOf(a.vm).autoplay) return Value::boolean(true);
    Vec3 p = stageOf(a.vm).playerPos, q = a.vec(0, "place", {});
    float r = f32(a.num(1, "within", 2));
    return Value::boolean(sqr(p.x - q.x) + sqr(p.z - q.z) < r * r);
  }, "near(vec(…), within: 2 m): is the player that close (on the ground)?");
  vm.native("move_player", [](Args& a) {
    Stage& s = stageOf(a.vm);
    s.teleportTo = a.vec(0, "at", s.playerPos);
    s.teleportLook = a.vec(1, "look_at", s.teleportTo + Vec3(0, 1.6f, -1));
    s.teleport = true;
    s.playerPos = s.teleportTo;
    return Value();
  }, "move_player(at: vec(…), look_at: vec(…)): carry the player somewhere new (feet at `at`)");
  vm.native("go_to", [](Args& a) {
    stageOf(a.vm).nextScene = a.text(0, "scene", "");
    return Value();
  }, "go_to(\"the woods\"): load the scene file scenes/the woods.loop next");
  vm.native("clear_lines", [](Args& a) {
    Stage::UiMessage m;
    m.kind = "clear";
    stageOf(a.vm).ui.push_back(m);
    return Value();
  }, "clear_lines(): takes every line of dialogue off the screen");

  // ---- renders (made by the `loop` command with the cinema renderer)
  vm.native("render", [](Args& a) {
    Stage& s = stageOf(a.vm);
    RenderRequest r;
    r.file = a.text(0, "file", s.title + ".png");
    const Config::Cinema& cc = config().cinema;
    std::string q = a.text(-1, "quality", cc.quality.empty() ? "good" : cc.quality);
    r.samples = q == "draft" ? 16 : q == "good" ? 128 : q == "cinema" ? 1024 : q == "ultra" ? 4096 : 128;
    if (cc.samples > 0) r.samples = cc.samples;
    r.samples = static_cast<int>(a.num(-1, "samples", r.samples));
    r.width = static_cast<int>(a.num(-1, "width", cc.width > 0 ? cc.width : 1920));
    r.height = static_cast<int>(a.num(-1, "height", cc.height > 0 ? cc.height : r.width * 9 / 16));
    r.bounces = cc.bounces, r.denoise = cc.denoise, r.dispersion = cc.dispersion, r.motionBlur = cc.motionBlur;
    r.at = f32(a.num(-1, "at", a.vm.time()));
    s.renders.push_back(r);
    return Value();
  }, "render(file, quality: \"draft\" | \"good\" | \"cinema\" | \"ultra\", width, height, samples)");
  vm.native("film", [](Args& a) {
    Stage& s = stageOf(a.vm);
    RenderRequest r;
    r.film = true;
    const Config::Cinema& cc = config().cinema;
    r.file = a.text(0, "folder", cc.folder);
    std::string q = a.text(-1, "quality", cc.quality.empty() ? "good" : cc.quality);
    r.samples = q == "draft" ? 8 : q == "good" ? 64 : q == "cinema" ? 512 : 64;
    if (cc.samples > 0) r.samples = cc.samples;
    r.samples = static_cast<int>(a.num(-1, "samples", r.samples));
    r.width = static_cast<int>(a.num(-1, "width", cc.width > 0 ? cc.width : 1280));
    r.height = static_cast<int>(a.num(-1, "height", cc.height > 0 ? cc.height : r.width * 9 / 16));
    r.bounces = cc.bounces, r.denoise = cc.denoise, r.dispersion = cc.dispersion, r.motionBlur = cc.motionBlur;
    s.film.fps = f32(a.num(-1, "fps", cc.fps));
    s.renders.push_back(r);
    return Value();
  }, "film(folder, quality, width, fps): renders every frame of the shots");
}

}  // namespace loop::script
