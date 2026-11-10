#include "engine/script/scene_api.h"

#include <algorithm>
#include <map>
#include <unordered_map>

#include "engine/core/meter.h"
#include "engine/core/random.h"
#include "engine/core/file.h"
#include "engine/core/settings.h"
#include "engine/geometry/gltf.h"
#include "engine/geometry/plants.h"
#include "engine/geometry/terrain.h"
#include "engine/geometry/world_gen.h"
#include "engine/geometry/world_stream.h"
#include "engine/geometry/noise.h"
#include "engine/render/materials.h"

namespace loop::script {

Vec3 unitLuminanceOr(Vec3 c);

void Stage::updateTransform(int i) {
  Placed& p = placed[i];
  Quat q = p.useQuat ? p.quat : Quat::euler(p.rotation.x, p.rotation.y, p.rotation.z);
  Vec3 s = p.visible ? p.scale : Vec3(0);
  // the first move in this step: where it was is where it was last drawn
  // (before its first move ever, there's nothing to blend from)
  if (p.movedAt != steps) {
    if (p.movedAt == ~0ull) p.prevPos = p.position, p.prevQuat = q, p.prevScale = s;
    else p.prevPos = p.drawnPos, p.prevQuat = p.drawnQuat, p.prevScale = p.drawnScale;
    p.movedAt = steps;
  }
  p.drawnPos = p.position, p.drawnQuat = q, p.drawnScale = s;
  if (!p.queued) p.queued = true, moved.push_back(i);
  Mat4 xf = Mat4::trs(p.position, q, s);
  scene.instances[p.instance].transform = xf;
  for (int k : p.moreInstances) scene.instances[k].transform = xf;
  ++transformsVersion;
}

void Stage::takeMoves(float alpha, std::vector<int>& instances, std::vector<Mat4>& transforms) {
  instances.clear(), transforms.clear();
  for (int i : blending_)
    if (!placed[i].queued) placed[i].queued = true, moved.push_back(i);
  blending_.clear();
  alpha = clamp(alpha, 0.0f, 1.0f);
  for (int i : moved) {
    Placed& p = placed[i];
    p.queued = false;
    // moved in the latest step: part-way from where it was
    bool blend = p.movedAt + 1 == steps && alpha < 1;
    Mat4 xf = blend ? Mat4::trs(vlerp(p.prevPos, p.drawnPos, alpha), slerp(p.prevQuat, p.drawnQuat, alpha), vlerp(p.prevScale, p.drawnScale, alpha))
                    : scene.instances[p.instance].transform;
    if (blend) blending_.push_back(i);
    instances.push_back(p.instance), transforms.push_back(xf);
    for (int k : p.moreInstances) instances.push_back(k), transforms.push_back(xf);
  }
  moved.clear();
}

int Stage::pick(const Ray& ray, float* distance) const {
  // Each thing's triangles in its own space: the ray goes there (no copies of
  // the mesh), its box first so most things are skipped at once.
  float best = kInf;
  int hit = -1;
  std::unordered_map<int, AABB> boxes;  // mesh bounds, once per pick
  auto meet = [&](int instance) -> float {
    const MeshInstance& in = scene.instances[instance];
    if (in.mesh < 0 || in.mesh >= static_cast<int>(scene.meshes.size())) return kInf;
    const Mesh& m = scene.meshes[in.mesh];
    auto b = boxes.find(in.mesh);
    if (b == boxes.end()) b = boxes.emplace(in.mesh, m.bounds()).first;
    Mat4 inv = in.transform.inverse();
    Vec3 o = inv.point(ray.o), d = inv.vector(ray.d);
    float len = length(d);
    if (len < 1e-12f) return kInf;
    d = d / len;  // a unit ray in the thing's space: distances there are scaled by len
    Vec3 invD{1 / d.x, 1 / d.y, 1 / d.z};
    if (intersect(b->second, o, invD, 0, kInf) == kInf) return kInf;
    float near = kInf;
    for (size_t t = 0; t + 2 < m.indices.size(); t += 3) {
      Vec3 a = m.positions[m.indices[t]], e1 = m.positions[m.indices[t + 1]] - a, e2 = m.positions[m.indices[t + 2]] - a;
      Vec3 pv = cross(d, e2);
      float det = dot(e1, pv);
      if (std::fabs(det) < 1e-12f) continue;
      float k = 1 / det;
      Vec3 tv = o - a;
      float u = dot(tv, pv) * k;
      if (u < 0 || u > 1) continue;
      Vec3 qv = cross(tv, e1);
      float v = dot(d, qv) * k;
      if (v < 0 || u + v > 1) continue;
      float tt = dot(e2, qv) * k;
      if (tt > 1e-5f && tt < near) near = tt;
    }
    return near == kInf ? kInf : near / len;
  };
  for (size_t i = 0; i < placed.size(); ++i) {
    const Placed& p = placed[i];
    if (!p.visible || p.instance < 0 || p.instance >= static_cast<int>(scene.instances.size())) continue;
    float t = meet(p.instance);
    for (int more : p.moreInstances)
      if (more >= 0 && more < static_cast<int>(scene.instances.size())) t = std::min(t, meet(more));
    if (t < best) best = t, hit = static_cast<int>(i);
  }
  if (distance) *distance = best;
  return hit;
}

void Stage::setListener(const Camera& cam) {
  mixer.listener.position = cam.position;
  mixer.listener.forward = normalize(cam.target - cam.position);
}

void Stage::update(float dt) {
  if (dt <= 0) return;
  if (streamed) streamWorld();
  // the music's notes that start now
  music.advance(dt, [this](const audio::Track& t, const audio::Track::Step& st, float seconds) {
    mixer.play(audio::makeNote(t.instrument, static_cast<float>(st.midi), seconds, st.velocity, ++musicSeed), Vec3(0), t.volume, false, false, audio::Category::kMusic);
  });
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
    MeterScope collidersTime(Part::kPhysics);
    world.clearStatic();
    for (const Placed& p : placed)
      if (p.collide && p.body < 0 && p.visible) {
        world.addStaticMesh(scene.meshes[scene.instances[p.instance].mesh], scene.instances[p.instance].transform);
        for (int i : p.moreInstances) world.addStaticMesh(scene.meshes[scene.instances[i].mesh], scene.instances[i].transform);
      }
    for (const auto& [mesh, xf] : blockers) world.addStaticMesh(scene.meshes[mesh], xf);
    collidersDirty = false;
  }
  // physics: solid shapes follow their bodies
  {
    MeterScope physicsTime(Part::kPhysics);
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
  }
  // critters and people: steer, animate, and lay the body's shapes along the bones
  MeterScope peopleTime(Part::kPeople);
  updateCritters(dt);
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
  ++steps;  // this step is done: what moved in it blends from here
}

// Butterflies wander from flower to flower, pausing to sit with wings open;
// fireflies drift on slow curves and twinkle (their size breathes).
void Stage::updateCritters(float dt) {
  if (critters.empty()) return;
  for (Critter& c : critters) {
    if (!c.visible) continue;
    c.phase += dt;
    Rng rng(c.seed * 2654435761u + static_cast<uint32_t>(c.phase * 3));
    auto newTarget = [&] {
      float a = rng.uniform() * kTwoPi, r = c.radius * std::sqrt(rng.uniform());
      c.target = c.home + Vec3(std::cos(a) * r, (c.kind == 'b' ? 0.15f : 0.4f) + c.height * rng.uniform(), std::sin(a) * r);
    };
    if (length(c.target - c.pos) < 0.15f || length(c.target) == 0) {
      newTarget();
      if (c.kind == 'b' && rng.uniform() < 0.4f) c.wait = 1.0f + 2.5f * rng.uniform();  // sit a moment
    }
    if (c.wait > 0) c.wait -= dt;
    Vec3 want = c.wait > 0 ? Vec3(0) : normalize(c.target - c.pos) * c.speed;
    // butterflies flutter: an up-down wobble on top of the way they're going
    if (c.kind == 'b' && c.wait <= 0) want.y += std::sin(c.phase * 9 + c.seed) * 0.6f;
    float k = 1 - std::exp(-(c.kind == 'b' ? 3.0f : 0.8f) * dt);
    c.vel += (want - c.vel) * k;
    c.pos += c.vel * dt;
    float yaw = std::atan2(c.vel.x, c.vel.z);
    if (c.kind == 'f') {
      Placed& m = placed[c.parts[0]];
      float tw = 0.55f + 0.45f * std::sin(c.phase * (2.3f + (c.seed % 7) * 0.3f) + c.seed);
      m.position = c.pos, m.scale = Vec3(c.size * (0.5f + tw)), m.useQuat = true, m.quat = Quat();
      updateTransform(c.parts[0]);
      continue;
    }
    // wings: fast beats in flight, a slow fan when sitting
    float flap = c.wait > 0 ? 0.25f + 0.25f * std::sin(c.phase * 2) : 0.15f + 1.1f * (0.5f + 0.5f * std::sin(c.phase * 22 + c.seed));
    Quat body = Quat::axisAngle({0, 1, 0}, yaw);
    for (int i = 0; i < 3; ++i) {
      Placed& p = placed[c.parts[i]];
      p.position = c.pos, p.scale = Vec3(c.size), p.useQuat = true;
      if (i == 0) p.quat = body * Quat::axisAngle({1, 0, 0}, kPi / 2);  // the body lies along the way it flies
      // a wing lies along +X (sideways when flying toward +Z); the right one is the
      // same card turned round; both rise about the body's long axis
      else p.quat = body * Quat::axisAngle({0, 1, 0}, i == 1 ? 0.0f : kPi) * Quat::axisAngle({0, 0, 1}, flap);
      updateTransform(c.parts[i]);
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
Value getGroup(Vm& vm, Thing& t, const std::string& f, bool& found);
bool setGroup(Vm& vm, Thing& t, const std::string& f, const Value& v);
const ThingClass kGroupClass{"model", getGroup, setGroup, {"position", "at", "visible", "parts"}};
Value getCritter(Vm& vm, Thing& t, const std::string& f, bool& found);
bool setCritter(Vm& vm, Thing& t, const std::string& f, const Value& v);
const ThingClass kCritterClass{"creature", getCritter, setCritter, {"position", "around", "radius", "visible", "speed"}};

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
        {"planks", Pattern::Kind::kPlanks}, {"grid", Pattern::Kind::kGrid}};
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
    int m = materialFrom(vm, v);
    s.scene.instances[p.instance].material = m;
    for (int i : p.moreInstances) s.scene.instances[i].material = m;
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

Stage::Group& groupOf(Vm& vm, Thing& t) {
  Stage& s = stageOf(vm);
  if (t.id < 0 || t.id >= static_cast<int>(s.groups.size())) vm.fail("that model is gone");
  return s.groups[t.id];
}
Value getGroup(Vm& vm, Thing& t, const std::string& f, bool& found) {
  Stage::Group& g = groupOf(vm, t);
  found = true;
  if (f == "position" || f == "at") return Value::vec(g.position);
  if (f == "visible") return Value::boolean(g.visible);
  if (f == "parts") return Value::number(static_cast<double>(g.parts.size()));
  found = false;
  return {};
}
bool setGroup(Vm& vm, Thing& t, const std::string& f, const Value& v) {
  Stage& s = stageOf(vm);
  Stage::Group& g = groupOf(vm, t);
  if (f == "position" || f == "at") {
    Vec3 to = asVec(vm, v, f), d = to - g.position;
    g.position = to;
    for (int i : g.parts) s.placed[i].position += d, s.updateTransform(i);
  } else if (f == "visible") {
    g.visible = v.truthy();
    for (int i : g.parts) s.placed[i].visible = g.visible, s.updateTransform(i);
    // a creature hidden with its group stops moving too
    for (auto& c : s.critters)
      for (int i : c.parts)
        if (std::find(g.parts.begin(), g.parts.end(), i) != g.parts.end()) c.visible = g.visible;
  } else return false;
  return true;
}
Stage::Critter& critterOf(Vm& vm, Thing& t) {
  Stage& s = stageOf(vm);
  if (t.id < 0 || t.id >= static_cast<int>(s.critters.size())) vm.fail("that creature is gone");
  return s.critters[t.id];
}
Value getCritter(Vm& vm, Thing& t, const std::string& f, bool& found) {
  Stage::Critter& c = critterOf(vm, t);
  found = true;
  if (f == "position" || f == "at") return Value::vec(c.pos);
  if (f == "around") return Value::vec(c.home);
  if (f == "radius") return Value::number(c.radius);
  if (f == "speed") return Value::number(c.speed);
  if (f == "visible") return Value::boolean(c.visible);
  found = false;
  return {};
}
bool setCritter(Vm& vm, Thing& t, const std::string& f, const Value& v) {
  Stage& s = stageOf(vm);
  Stage::Critter& c = critterOf(vm, t);
  if (f == "position" || f == "at") c.pos = asVec(vm, v, f), c.target = Vec3(0);
  else if (f == "around") c.home = asVec(vm, v, f), c.target = Vec3(0);
  else if (f == "radius") c.radius = static_cast<float>(v.n);
  else if (f == "speed") c.speed = static_cast<float>(v.n);
  else if (f == "visible") {
    c.visible = v.truthy();
    for (int i : c.parts) s.placed[i].visible = c.visible, s.updateTransform(i);
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
  p.line = a.line;
  // simple shapes are things you bump into (walls, tables), unless told not
  p.collide = a.flag(-1, "collide", true) && kind != 'm';
  s.collidersDirty = true;
  s.placed.push_back(p);
  s.updateTransform(static_cast<int>(s.placed.size() - 1));
  return Value::object(a.vm.newThing(&kShapeClass, static_cast<int>(s.placed.size() - 1)));
}

float f32(double d) { return static_cast<float>(d); }

// One part of a model: a mesh placed with a turn (yaw) and scale; never
// something you bump into. Returns its placed index.
int placePart(Stage& s, int mesh, int mat, Vec3 at, float yaw, Vec3 scale, float tilt = 0) {
  s.scene.add(mesh, mat);
  Stage::Placed p;
  p.instance = static_cast<int>(s.scene.instances.size() - 1);
  p.position = at, p.scale = scale, p.rotation = Vec3(yaw, tilt, 0);
  p.kind = 'm';
  s.placed.push_back(p);
  s.updateTransform(static_cast<int>(s.placed.size() - 1));
  return static_cast<int>(s.placed.size() - 1);
}
Value newGroup(Vm& vm, Stage& s, std::vector<int> parts, Vec3 at) {
  Stage::Group g;
  g.parts = std::move(parts), g.position = at;
  s.groups.push_back(std::move(g));
  return Value::object(vm.newThing(&kGroupClass, static_cast<int>(s.groups.size() - 1)));
}
// the ground's height here (the last terrain, or the ground plane)
float groundAt(Stage& s, float x, float z) { return s.terrains.empty() ? s.world.groundHeight : s.terrains.back().height(x, z); }
// glTF's glow has no unit; this many nits per unit of emissive x strength
// (the bar's models were tuned for 8 in the original engine). `glow:` on the
// model word changes it.
constexpr float kGltfNitsPerUnit = 8;

// A glTF material as ours. See-through (BLEND) and nearly clear with no metal
// becomes real glass; the rest keeps its colour, metal, roughness and glow.
Material materialFromGltf(const GltfMaterial& g, const std::vector<int>& textures, float nitsPerUnit) {
  Material m;
  m.name = g.name.empty() ? "model" : g.name;
  m.baseColor = g.baseColor.xyz();
  m.metallic = saturate(g.metallic), m.roughness = saturate(g.roughness);
  m.emission = g.emissive * nitsPerUnit;
  auto tex = [&](int i) { return i >= 0 && i < static_cast<int>(textures.size()) ? textures[i] : -1; };
  m.baseTexture = tex(g.baseTexture), m.metalRoughTexture = tex(g.metalRoughTexture);
  m.normalTexture = tex(g.normalTexture), m.emissiveTexture = tex(g.emissiveTexture);
  m.roughnessMin = g.roughnessMin;
  if (g.alpha == GltfMaterial::Alpha::kMask) m.alphaCutoff = g.alphaCutoff;
  if (g.alpha == GltfMaterial::Alpha::kBlend) {
    if (g.baseColor.w < 0.6f && g.metallic < 0.5f) {
      m.transmission = 1, m.ior = 1.5f;
      // the colour tints the light passing through (about 1 cm of glass gives it)
      Vec3 c = vmax(m.baseColor, Vec3(0.02f));
      m.absorption = Vec3(-std::log(c.x), -std::log(c.y), -std::log(c.z)) * 20;
      m.baseColor = Vec3(1);
    } else {
      m.opacity = saturate(g.baseColor.w);
    }
  }
  return m;
}

// Reads a model once per scene: its parts as (mesh, material) in the scene.
const std::vector<std::pair<int, int>>& modelParts(Args& a, const std::string& file, float nitsPerUnit) {
  Stage& s = stageOf(a.vm);
  std::string key = file + " " + std::to_string(nitsPerUnit);
  auto it = s.modelCache.find(key);
  if (it != s.modelCache.end()) return it->second;
  // the scene's folder, then its models/ folder
  std::vector<std::string> tries = {joinPath(s.folder, file), joinPath(joinPath(s.folder, "models"), file)};
  GltfModel g;
  std::string err, used;
  bool ok = false;
  for (const std::string& path : tries) {
    std::vector<uint8_t> bytes;
    if (!readFile(path, bytes)) continue;
    used = path;
    ok = loadGlb(bytes, g, &err, false);  // its pictures wait, to be decoded with the scene's others
    break;
  }
  if (used.empty()) a.vm.fail("there's no model called \"" + file + "\"", "put the .glb next to the scene or in its models/ folder");
  if (!ok) a.vm.fail("the model \"" + file + "\" can't be read: " + err);
  // real values for its materials, if its folder has them (materials.json:
  // {"model.glb": {"Material name": {"fix": colour times this, "rough": never smoother}}})
  std::string table = joinPath(directoryOf(used), "materials.json");
  auto known = s.materialTables.find(table);
  if (known == s.materialTables.end()) {
    Json j;
    std::string text;
    if (readText(table, text)) Json::parse(text, j);
    known = s.materialTables.emplace(table, std::move(j)).first;
  }
  const Json& fixes = known->second[file];
  for (GltfMaterial& gm : g.materials) {
    const Json& f = fixes[gm.name];
    if (!f.isObject()) continue;
    float k = static_cast<float>(f["fix"].number(1));
    gm.baseColor = Vec4(gm.baseColor.xyz() * k, gm.baseColor.w);
    gm.roughnessMin = static_cast<float>(f["rough"].number(0));
  }
  // each picture once per scene, however many models carry it
  std::vector<int> texIndex;
  for (Texture& t : g.textures) {
    uint64_t h = 1469598103934665603ull ^ (static_cast<uint64_t>(t.width) << 32 | static_cast<uint32_t>(t.height));
    for (uint8_t b : t.pending() ? t.packed : t.rgba) h = (h ^ b) * 1099511628211ull;
    auto found = s.textureByContent.find(h);
    if (found != s.textureByContent.end()) {
      texIndex.push_back(found->second);
      continue;
    }
    int idx = static_cast<int>(s.scene.textures.size());
    s.scene.textures.push_back(std::move(t));
    s.textureByContent[h] = idx;
    texIndex.push_back(idx);
  }
  std::vector<int> mats;
  for (const GltfMaterial& gm : g.materials) mats.push_back(s.scene.addMaterial(materialFromGltf(gm, texIndex, nitsPerUnit)));
  std::vector<std::pair<int, int>> parts;
  for (GltfModel::Part& p : g.parts) {
    int mat = p.material >= 0 && p.material < static_cast<int>(mats.size()) ? mats[p.material] : 0;
    parts.push_back({s.scene.addMesh(std::move(p.mesh)), mat});
  }
  ++s.version;
  return s.modelCache[key] = std::move(parts);
}

// Trees, kept as a few shapes per kind and placed many times.
// lod 0: the tree as asked; 1: simpler (for further away); 2: the farthest
// (no twig bark, a quarter of the leaf cards, bigger).
std::pair<int, int> treeMeshes(Stage& st, const std::string& kind, int variant, float leafScale = 1, float detail = 1, int lod = 0) {
  auto& c = meshCache(st);
  std::string key = "tree " + kind + " " + std::to_string(variant) + (leafScale == 1 ? "" : " leaves " + std::to_string(leafScale)) +
                    (detail == 1 ? "" : " detail " + std::to_string(detail)) + (lod == 2 ? " far" : "");
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
  // smaller leaves come in greater numbers, so the crown keeps its fullness
  if (leafScale != 1) {
    ts.leafSize *= leafScale;
    ts.leavesPerTip = std::min(48, static_cast<int>(std::lround(ts.leavesPerTip / std::max(leafScale, 0.2f))));
  }
  ts.detail = std::clamp(detail, 0.2f, 1.0f);
  if (lod == 2) ts.barkLevels = 1, ts.leafShare = 0.25f;
  TreeMeshes t = growTree(ts);
  int b = st.scene.addMesh(std::move(t.bark)), l = st.scene.addMesh(std::move(t.leaves));
  c[key + " bark"] = b;
  c[key + " leaves"] = l;
  // simpler versions of the same tree for far away (levels of detail): the same
  // branches, thinner tubes, fewer and bigger leaf cards
  if (lod == 0) {
    float d1 = std::max(0.2f, ts.detail * 0.45f);
    auto [b1, l1] = d1 < ts.detail - 1e-3f ? treeMeshes(st, kind, variant, leafScale, d1, 1) : std::pair<int, int>{-1, -1};
    auto [b2, l2] = treeMeshes(st, kind, variant, leafScale, 0.2f, 2);
    st.scene.setLods(b, b1, b2), st.scene.setLods(l, l1, l2);
  }
  return {b, l};
}

}  // namespace

Vec3 unitLuminanceOr(Vec3 c) {
  float l = luminance(c);
  return l > 1e-6f ? c / l : Vec3(1);
}

// ---- world(): a generated world (engine/geometry/world_gen.h), streamed
struct StreamedWorld {
  WorldMap map;
  WorldStream land, trees;
  const HeightMap* ground = nullptr;  // the walkable ground, 4 m a sample (in stage.terrains)
  std::map<std::pair<int, int>, std::vector<int>> landParts, treeParts;  // instances per chunk
  std::vector<Mat4> shownAt;  // per instance: where it stands when shown (by instance index, sparse)
  int mat[7] = {0};           // a material per ground (the sea's and lakes' beds: soil)
  int water = 0, bark = 0, needles = 0, leaves = 0;
};

namespace {
// Land quads over [x0, x1] x [z0, z1] every `step` m, one mesh per kind of
// ground (each quad takes the ground at its middle), heights from `h`.
std::map<int, Mesh> landMeshes(const StreamedWorld& w, float x0, float z0, float x1, float z1, float step, const std::function<float(float, float)>& h) {
  std::map<int, Mesh> out;
  const int nx = std::max(1, static_cast<int>(std::lround((x1 - x0) / step))), nz = std::max(1, static_cast<int>(std::lround((z1 - z0) / step)));
  for (int j = 0; j < nz; ++j)
    for (int i = 0; i < nx; ++i) {
      const float ax = x0 + i * step, az = z0 + j * step, bx = ax + step, bz = az + step;
      uint8_t g = w.map.groundAt((ax + bx) * 0.5f, (az + bz) * 0.5f);
      if (g == WorldMap::kSea || g == WorldMap::kLake) g = WorldMap::kSea;  // under water: its bed
      Mesh& m = out[g];
      auto v = [&](float x, float z) {
        const float y = h(x, z);
        const float e = step;
        const Vec3 n = normalize(Vec3(h(x - e, z) - h(x + e, z), 2 * e, h(x, z - e) - h(x, z + e)));
        return m.addVertex({x, y, z}, n, {x * 0.25f, z * 0.25f});
      };
      const uint32_t a = v(ax, az), b = v(ax, bz), c = v(bx, bz), d = v(bx, az);
      m.addQuad(a, b, c, d);
    }
  return out;
}
}  // namespace

Vec3 makeStreamedWorld(Vm& vm, Stage& s, const WorldSettings& ws) {
  auto w = std::make_shared<StreamedWorld>();
  w->map = generateWorld(ws);
  const WorldMap& m = w->map;
  // the walkable ground: the map's land, 4 m a sample, with rivers cut into it
  // and small bumps (rougher on rock)
  const int res = std::clamp(static_cast<int>(ws.size / 4 * config().world.terrainRes) + 1, 257, 2049);
  HeightMap g(res, ws.size);
  noise::Fractal f;
  f.octaves = 3, f.seed = ws.seed * 13 + 1;
  for (int z = 0; z < res; ++z)
    for (int x = 0; x < res; ++x) {
      const float wx = (static_cast<float>(x) / (res - 1) - 0.5f) * ws.size, wz = (static_cast<float>(z) / (res - 1) - 0.5f) * ws.size;
      float y = m.heightAt(wx, wz);
      const float drain = m.sample(m.flow, wx, wz) / (m.cell * m.cell);
      if (y > 0) y -= std::min({5.0f, 0.15f * std::sqrt(std::max(drain - 20, 0.0f)), std::max(y - 0.5f, 0.0f)});  // a river's bed (never below the sea)
      const uint8_t gr = m.groundAt(wx, wz);
      y += noise::fbm(Vec2(wx, wz) / 40.0f, f) * (gr == WorldMap::kRock ? 6.0f : gr == WorldMap::kSand ? 0.4f : 1.5f);
      g.at(x, z) = y;
    }
  s.terrains.push_back(std::move(g));
  w->ground = &s.terrains.back();
  s.world.setTerrain(&s.terrains.back());
  s.world.groundPlane = false;
  s.groundSurface = "grass";
  // the materials
  auto mat = [&](const char* name) { return materialFrom(vm, vm.text(name)); };
  w->mat[WorldMap::kSea] = mat("soil"), w->mat[WorldMap::kLake] = w->mat[WorldMap::kSea];
  w->mat[WorldMap::kSand] = mat("sand"), w->mat[WorldMap::kGrass] = mat("grass"), w->mat[WorldMap::kForest] = mat("moss");
  w->mat[WorldMap::kRock] = mat("granite"), w->mat[WorldMap::kSnow] = mat("snow");
  w->water = mat("water"), w->bark = mat("bark"), w->needles = mat("needles"), w->leaves = mat("leaves");
  // the sea, out to the horizon
  s.scene.add(s.scene.addMesh(shapes::plane({ws.size * 3, ws.size * 3})), w->water);
  // the whole land seen from afar: the map itself, 32 m a quad, a little lower
  // so the near chunks always cover it
  for (auto& [kind, mesh] : landMeshes(*w, -ws.size / 2, -ws.size / 2, ws.size / 2, ws.size / 2, m.cell,
                                       [&](float x, float z) { return m.heightAt(x, z) - 8; }))
    s.scene.add(s.scene.addMesh(std::move(mesh)), w->mat[kind]);
  w->land.half = w->trees.half = ws.size / 2;
  w->trees.chunk = 128, w->trees.show = 320, w->trees.hide = 400;
  // a good place to start: the nearest gentle, dry land to the middle
  Vec3 start(0, 0, 0);
  float best = 1e30f;
  for (int z = 0; z < m.n; ++z)
    for (int x = 0; x < m.n; ++x) {
      const size_t i = m.at(x, z);
      const uint8_t gr = m.ground[i];
      if ((gr != WorldMap::kGrass && gr != WorldMap::kForest) || m.slopeAt(x, z) > 0.15f || m.height[i] < 5) continue;  // up off the shore
      const Vec2 p = m.place(x, z);
      if (length(p) < best) best = length(p), start = Vec3(p.x, 0, p.y);
    }
  start.y = s.terrains.back().height(start.x, start.z);
  // facing the mountains: the way the land rises most, 1.5 km out
  Vec3 look(0, 0, -1);
  float highest = -1e30f;
  for (int k = 0; k < 32; ++k) {
    const float ang = k * kTwoPi / 32;
    const Vec3 d(std::cos(ang), 0, std::sin(ang));
    const float y = m.heightAt(start.x + d.x * 1500, start.z + d.z * 1500);
    if (y > highest) highest = y, look = d;
  }
  s.streamed = w;
  s.playerPos = start;
  s.teleportTo = start, s.teleportLook = start + look * 40 + Vec3(0, 1.2f, 0), s.teleport = true;
  s.scene.camera.position = start + Vec3(0, 1.7f, 0);
  s.scene.camera.target = start + look * 40 + Vec3(0, 1.2f, 0);
  s.streamWorld();  // the first look round: all the land near the start, at once
  return start;
}

void Stage::streamWorld() {
  StreamedWorld& w = *streamed;
  const bool first = w.land.chunks.empty();
  bool changed = false;
  auto shownAt = [&](int inst, const Mat4& xf) {
    if (w.shownAt.size() <= static_cast<size_t>(inst)) w.shownAt.resize(static_cast<size_t>(inst) + 1);
    w.shownAt[static_cast<size_t>(inst)] = xf;
  };
  auto showParts = [&](const std::vector<int>& parts, bool on) {
    for (int i : parts) scene.instances[static_cast<size_t>(i)].transform = on ? w.shownAt[static_cast<size_t>(i)] : Mat4::scale(Vec3(0));
    changed = true;
  };
  const Vec2 at(playerPos.x, playerPos.z);
  // the land: 256 m chunks, 4 m a quad, out to 1 km
  WorldStream::Work lw = w.land.update(at, first);
  for (const auto& c : lw.make) {
    const float x0 = c.first * w.land.chunk, z0 = c.second * w.land.chunk;
    std::vector<int>& parts = w.landParts[c];
    const HeightMap& g = *w.ground;
    for (auto& [kind, mesh] : landMeshes(w, x0, z0, x0 + w.land.chunk, z0 + w.land.chunk, 4, [&](float x, float z) { return g.height(x, z); })) {
      scene.add(scene.addMesh(std::move(mesh)), w.mat[kind]);
      parts.push_back(static_cast<int>(scene.instances.size() - 1));
      shownAt(parts.back(), Mat4());
    }
    // its lakes: water at their level where the map has a lake
    Mesh lake;
    for (int j = 0; j < 32; ++j)
      for (int i = 0; i < 32; ++i) {
        const float x = x0 + i * 8, z = z0 + j * 8;
        if (w.map.groundAt(x + 4, z + 4) != WorldMap::kLake) continue;
        const float y = w.map.sample(w.map.water, x + 4, z + 4);
        const uint32_t a = lake.addVertex({x, y, z}, {0, 1, 0}, {x, z}), b = lake.addVertex({x, y, z + 8}, {0, 1, 0}, {x, z + 8}),
                       cc = lake.addVertex({x + 8, y, z + 8}, {0, 1, 0}, {x + 8, z + 8}), d = lake.addVertex({x + 8, y, z}, {0, 1, 0}, {x + 8, z});
        lake.addQuad(a, b, cc, d);
      }
    if (!lake.indices.empty()) {
      scene.add(scene.addMesh(std::move(lake)), w.water);
      parts.push_back(static_cast<int>(scene.instances.size() - 1));
      shownAt(parts.back(), Mat4());
    }
    changed = true;
  }
  for (const auto& c : lw.show) showParts(w.landParts[c], true);
  for (const auto& c : lw.hide) showParts(w.landParts[c], false);
  // the trees: 128 m chunks out to 320 m, grown where the map says forest
  // (pines where it is cold or high, broad trees where it is warm)
  WorldStream::Work tw = w.trees.update(at, first);
  const float density = config().world.foliageDensity;
  for (const auto& c : tw.make) {
    std::vector<int>& parts = w.treeParts[c];
    Rng rng(static_cast<uint64_t>(c.first * 92821 + c.second * 68917 + 7));
    const float x0 = c.first * w.trees.chunk, z0 = c.second * w.trees.chunk, spacing = 14 / std::sqrt(std::max(density, 0.05f));
    for (float z = z0; z < z0 + w.trees.chunk; z += spacing)
      for (float x = x0; x < x0 + w.trees.chunk; x += spacing) {
        const float px = x + rng.uniform() * spacing, pz = z + rng.uniform() * spacing;
        const float grow = w.map.sample(w.map.forest, px, pz);
        if (density <= 0.001f || rng.uniform() > grow * 0.9f) continue;
        const uint8_t gr = w.map.groundAt(px, pz);
        if (gr != WorldMap::kForest && gr != WorldMap::kGrass) continue;
        if (w.ground->slope(px, pz) > 0.35f) continue;
        const bool cold = w.map.sample(w.map.warmth, px, pz) < 9 || w.ground->height(px, pz) > 220;
        auto [b, l] = treeMeshes(*this, cold ? "pine" : (rng.uniform() < 0.5f ? "oak" : "birch"), static_cast<int>(rng.uniform() * 4));
        const Mat4 xf = Mat4::trs({px, w.ground->height(px, pz) - 0.2f, pz}, Quat::axisAngle({0, 1, 0}, rng.uniform() * kTwoPi), Vec3(0.7f + 0.6f * rng.uniform()));
        scene.add(b, w.bark, xf);
        parts.push_back(static_cast<int>(scene.instances.size() - 1)), shownAt(parts.back(), xf);
        scene.add(l, cold ? w.needles : w.leaves, xf);
        parts.push_back(static_cast<int>(scene.instances.size() - 1)), shownAt(parts.back(), xf);
      }
    changed = true;
  }
  for (const auto& c : tw.show) showParts(w.treeParts[c], true);
  for (const auto& c : tw.hide) showParts(w.treeParts[c], false);
  if (changed) ++version;
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
  vm.native("model", [](Args& a) {
    std::string file = a.text(0, "file", "");
    if (file.empty()) a.vm.fail("model needs a file, like model \"stool.glb\", at: vec(0, 0, 0)");
    const auto& parts = modelParts(a, file, f32(a.num(-1, "glow", kGltfNitsPerUnit)));
    if (parts.empty()) a.vm.fail("the model \"" + file + "\" has no triangles in it");
    Stage& s = stageOf(a.vm);
    const Value* override = a.get(-1, "material") ? a.get(-1, "material") : a.get(-1, "made_of");
    int forced = override ? materialFrom(a.vm, *override) : -1;
    Value thing = place(a, parts[0].first, 'm', Vec3(1));
    Stage::Placed& pl = s.placed.back();
    if (forced < 0) s.scene.instances[pl.instance].material = parts[0].second;
    for (size_t i = 1; i < parts.size(); ++i) {
      s.scene.add(parts[i].first, forced >= 0 ? forced : parts[i].second);
      pl.moreInstances.push_back(static_cast<int>(s.scene.instances.size() - 1));
    }
    pl.collide = a.flag(-1, "collide", false);
    s.updateTransform(static_cast<int>(s.placed.size() - 1));
    return thing;
  }, "model(file, at, turn, scale, name, glow, collide): a .glb model from the scene's folder or its models/ folder");
  vm.native("blocker", [](Args& a) {
    Stage& s = stageOf(a.vm);
    Vec3 sz = a.vec(0, "size", Vec3(1));
    int m = cachedMesh(s, keyOf("box", {sz.x, sz.y, sz.z, 0}), [&] { return shapes::box(sz); });
    float turn = f32(a.num(-1, "turn", 0));
    s.blockers.push_back({m, Mat4::trs(a.vec(-1, "at", {}), Quat::axisAngle({0, 1, 0}, turn), Vec3(1))});
    s.collidersDirty = true;
    return Value();
  }, "blocker(size: vec(w, h, d), at, turn): an invisible box you can't walk through (walls of a room made of one model)");
  vm.native("ring", [](Args& a) {
    float r = f32(a.num(0, "radius", 0.5)), t = f32(a.num(1, "thickness", 0.1));
    return place(a, cachedMesh(stageOf(a.vm), keyOf("torus", {r, t}), [&] { return shapes::torus(r, t, 96, 48); }));
  }, "ring(radius, thickness, at, material): a torus");
  vm.native("ground", [](Args& a) {
    float sz = f32(a.num(0, "size", 100));
    Stage& st = stageOf(a.vm);
    // collide: no (water, a painted floor) is seen but never stood on
    if (a.flag(-1, "collide", true)) {
      st.world.groundPlane = true;
      st.world.groundHeight = a.vec(-1, "at", {}).y;
    }
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
    auto [b, l] = treeMeshes(s, kind, static_cast<int>(a.num(-1, "variant", 0)) % 4, f32(a.num(-1, "leaf_size", 1)), f32(a.num(-1, "detail", 1)));
    bool conifer = kind == "pine" || kind == "fir" || kind == "spruce";
    Vec3 at = a.vec(-1, "at", {});
    float scale = f32(a.num(-1, "height", conifer ? 14 : 10) / (conifer ? 14 : 10));
    int bark = materialFrom(a.vm, a.vm.text("bark"));
    int leaf = materialFrom(a.vm, a.vm.text(a.text(-1, "leaves", conifer ? "needles" : "leaves")));
    float turn = f32(a.num(-1, "turn", 0));
    std::vector<int> parts = {placePart(s, b, bark, at, turn, Vec3(scale)), placePart(s, l, leaf, at, turn, Vec3(scale))};
    ++s.version;
    return newGroup(a.vm, s, parts, at);
  }, "tree(kind: \"oak\" | \"pine\" | \"birch\" | \"bush\", at, height, turn, variant, leaves, leaf_size: 1, detail: 1): gives back the tree (.position, .visible); far away it is drawn as a simpler version of itself on its own");

  // ---- undergrowth, flowers, ruins and creatures (the model library)
  vm.native("grass", [](Args& a) {
    Stage& s = stageOf(a.vm);
    Vec3 c = a.vec(-1, "around", {});
    float radius = f32(a.num(-1, "radius", 10)), height = f32(a.num(-1, "height", 0.45));
    // tufts per square metre, times world.foliageDensity
    float density = f32(a.num(-1, "density", 6)) * config().world.foliageDensity;
    if (density <= 0.001f) return newGroup(a.vm, s, {}, c);
    float clearing = f32(a.num(-1, "clearing", 0));
    Vec3 clearAt = a.vec(-1, "clear_around", c);
    uint32_t seed = static_cast<uint32_t>(a.num(-1, "seed", 7));
    int green = materialFrom(a.vm, a.get(-1, "material") ? *a.get(-1, "material") : a.vm.text("blades"));
    int dry = materialFrom(a.vm, a.get(-1, "dry") ? *a.get(-1, "dry") : a.vm.text("dry blades"));
    float dryShare = f32(a.num(-1, "dry_share", 0.15));
    int meshes[4];
    for (int v = 0; v < 4; ++v)
      meshes[v] = cachedMesh(s, keyOf("grass tuft", {static_cast<float>(v)}), [v] { return makeGrassTuft(14, 1.0f, 101 + v * 31); });
    auto pts = poissonDisk(radius * 2, 1.0f / std::sqrt(density), seed);
    Rng rng(seed);
    std::vector<int> parts;
    for (Vec2 p : pts) {
      if (parts.size() >= 20000) break;
      if (length(p) > radius) continue;
      Vec3 w{c.x + p.x, 0, c.z + p.y};
      if (clearing > 0 && length(Vec2(w.x - clearAt.x, w.z - clearAt.z)) < clearing * (0.8f + 0.4f * rng.uniform())) continue;
      w.y = groundAt(s, w.x, w.z) - 0.02f;
      // taller and denser toward the middle of the patch, shorter at its edge
      float edge = 1 - 0.5f * smoothstep(radius * 0.6f, radius, length(p));
      float h = height * edge * (0.65f + 0.7f * rng.uniform());
      parts.push_back(placePart(s, meshes[rng.below(4)], rng.uniform() < dryShare ? dry : green, w, rng.uniform() * kTwoPi, Vec3(1.1f, h, 1.1f)));
    }
    ++s.version;
    return newGroup(a.vm, s, parts, c);
  }, "grass(around, radius, density: tufts per m², height, clearing, clear_around, dry_share, material, seed): a meadow (.visible)");
  vm.native("fern", [](Args& a) {
    Stage& s = stageOf(a.vm);
    Vec3 at = a.vec(0, "at", {});
    int v = static_cast<int>(a.num(-1, "seed", 1)) % 4;
    int mesh = cachedMesh(s, keyOf("fern", {static_cast<float>(v)}), [v] { return makeFern(1.0f, 9 + v, 17 + v * 13); });
    int mat = materialFrom(a.vm, a.get(-1, "material") ? *a.get(-1, "material") : a.vm.text("fern"));
    if (!a.has(-1, "at") && a.count == 0) at = Vec3(0);
    at.y = a.has(-1, "on_ground") && !a.flag(-1, "on_ground", true) ? at.y : groundAt(s, at.x, at.z) + at.y;
    float size = f32(a.num(-1, "size", 1));
    ++s.version;
    return newGroup(a.vm, s, {placePart(s, mesh, mat, at, f32(a.num(-1, "turn", v * 1.3)), Vec3(size))}, at);
  }, "fern(at, size: 1 m, turn, seed, material): arching fronds of leaflets, set on the ground");
  auto flowerAt = [](Args& a, Stage& s, Vec3 at, const std::string& kind, float size, uint32_t seed, float turn) {
    FlowerShape fs;
    std::string petal = "daisy";
    if (kind == "violet") fs.petals = 5, fs.cup = 0.25f, fs.headSize = 0.022f, fs.height = 0.18f, petal = "violet";
    else if (kind == "poppy") fs.petals = 4, fs.cup = 0.45f, fs.headSize = 0.045f, fs.height = 0.45f, petal = "poppy";
    else if (kind == "bluebell") fs.petals = 6, fs.cup = 0.9f, fs.headSize = 0.02f, fs.height = 0.28f, petal = "bluebell";
    else if (kind != "daisy") a.vm.fail("there's no flower called \"" + kind + "\"", "flowers: daisy, violet, poppy, bluebell");
    else fs.petals = 12, fs.cup = 0.08f;
    int v = static_cast<int>(seed % 3);
    fs.seed = 31 + v * 7;
    std::string key = "flower " + kind + " " + std::to_string(v);
    auto& cache = meshCache(s);
    if (!cache.count(key + " stem")) {
      FlowerMeshes m = makeFlower(fs);
      cache[key + " stem"] = s.scene.addMesh(std::move(m.stem));
      cache[key + " petals"] = s.scene.addMesh(std::move(m.petals));
      cache[key + " centre"] = s.scene.addMesh(std::move(m.centre));
    }
    int stem = materialFrom(a.vm, a.vm.text("stem")), pet = materialFrom(a.vm, a.vm.text(petal)), pol = materialFrom(a.vm, a.vm.text("pollen"));
    Vec3 sc(size);
    return std::vector<int>{placePart(s, cache[key + " stem"], stem, at, turn, sc), placePart(s, cache[key + " petals"], pet, at, turn, sc),
                            placePart(s, cache[key + " centre"], pol, at, turn, sc)};
  };
  static decltype(flowerAt) flowerFn = flowerAt;
  vm.native("flower", [](Args& a) {
    Stage& s = stageOf(a.vm);
    Vec3 at = a.vec(-1, "at", {});
    at.y += groundAt(s, at.x, at.z);
    auto parts = flowerFn(a, s, at, a.text(0, "kind", "daisy"), f32(a.num(-1, "size", 1)), static_cast<uint32_t>(a.num(-1, "seed", 1)), f32(a.num(-1, "turn", 0)));
    ++s.version;
    return newGroup(a.vm, s, parts, at);
  }, "flower(\"daisy\" | \"violet\" | \"poppy\" | \"bluebell\", at, size, turn): one wild flower");
  vm.native("flowers", [](Args& a) {
    Stage& s = stageOf(a.vm);
    Vec3 c = a.vec(-1, "around", {});
    float radius = f32(a.num(-1, "radius", 4));
    int count = static_cast<int>(a.num(-1, "count", 40) * std::max(config().world.foliageDensity, 0.0f));
    std::vector<std::string> kinds;
    if (const Value* k = a.get(-1, "kinds"))
      if (List* l = k->as<List>())
        for (auto& x : l->items) kinds.push_back(a.vm.show(x));
    if (kinds.empty()) kinds = {"daisy", "violet"};
    Rng rng(static_cast<uint32_t>(a.num(-1, "seed", 5)));
    // flowers grow in drifts: a few clumps, each one kind
    int clumps = std::max(1, count / 12);
    std::vector<int> parts;
    for (int k = 0; k < clumps; ++k) {
      float ca = rng.uniform() * kTwoPi, cr = radius * std::sqrt(rng.uniform());
      Vec3 cc = c + Vec3(std::cos(ca) * cr, 0, std::sin(ca) * cr);
      const std::string& kind = kinds[rng.below(static_cast<uint32_t>(kinds.size()))];
      int n = count / clumps;
      for (int i = 0; i < n; ++i) {
        float a2 = rng.uniform() * kTwoPi, r2 = radius * 0.25f * std::sqrt(rng.uniform());
        Vec3 at = cc + Vec3(std::cos(a2) * r2, 0, std::sin(a2) * r2);
        at.y = groundAt(s, at.x, at.z);
        auto p = flowerFn(a, s, at, kind, f32(a.num(-1, "size", 1)) * (0.75f + 0.5f * rng.uniform()), rng.next(), rng.uniform() * kTwoPi);
        parts.insert(parts.end(), p.begin(), p.end());
      }
    }
    ++s.version;
    return newGroup(a.vm, s, parts, c);
  }, "flowers(around, radius, count, kinds: [\"daisy\", \"violet\"], size, seed): drifts of wild flowers (.visible)");
  vm.native("column", [](Args& a) {
    Stage& s = stageOf(a.vm);
    ColumnShape cs;
    cs.height = f32(a.num(-1, "height", 3)), cs.radius = f32(a.num(-1, "radius", 0.28));
    cs.flutes = static_cast<int>(a.num(-1, "flutes", 16)), cs.broken = a.flag(-1, "broken", false);
    cs.seed = static_cast<uint32_t>(a.num(-1, "seed", 1));
    int mesh = cachedMesh(s, keyOf(cs.broken ? "column broken" : "column", {cs.height, cs.radius, static_cast<float>(cs.flutes), static_cast<float>(cs.seed)}),
                          [cs] { return makeColumn(cs); });
    if (!a.get(-1, "material") && !a.get(-1, "made_of")) {
      // default: weathered stone
      Value v = place(a, mesh, 'b', Vec3(cs.radius * 2.6f, cs.height, cs.radius * 2.6f));
      Stage::Placed& p = s.placed.back();
      s.scene.instances[p.instance].material = materialFrom(a.vm, a.vm.text("ruin stone"));
      ++s.version;
      return v;
    }
    return place(a, mesh, 'b', Vec3(cs.radius * 2.6f, cs.height, cs.radius * 2.6f));
  }, "column(at, height: 3 m, radius, flutes: 16, broken: no, seed, material): a fluted stone column (a broken one has a ragged top)");
  vm.native("log", [](Args& a) {
    Stage& s = stageOf(a.vm);
    float len = f32(a.num(-1, "length", 3)), r = f32(a.num(-1, "radius", 0.2));
    int v = static_cast<int>(a.num(-1, "seed", 1)) % 4;
    std::string key = "log " + std::to_string(v);
    auto& cache = meshCache(s);
    if (!cache.count(key + " bark")) {
      LogMeshes m = makeLog(1.0f, 0.1f, 11 + v * 5);
      cache[key + " bark"] = s.scene.addMesh(std::move(m.bark));
      cache[key + " ends"] = s.scene.addMesh(std::move(m.ends));
    }
    Vec3 at = a.vec(-1, "at", {});
    at.y += groundAt(s, at.x, at.z) - r * 0.35f;
    float turn = f32(a.num(-1, "turn", 0)), tilt = f32(a.num(-1, "tilt", 0));
    Vec3 sc(len, r / 0.1f, r / 0.1f);
    int bark = materialFrom(a.vm, a.get(-1, "material") ? *a.get(-1, "material") : a.vm.text("bark"));
    std::vector<int> parts = {placePart(s, cache[key + " bark"], bark, at, turn, sc, tilt),
                              placePart(s, cache[key + " ends"], materialFrom(a.vm, a.vm.text("old wood")), at, turn, sc, tilt)};
    ++s.version;
    return newGroup(a.vm, s, parts, at);
  }, "log(at, length: 3 m, radius: 0.2 m, turn, tilt, seed, material): a fallen log, its cut ends showing");
  vm.native("butterfly", [](Args& a) {
    Stage& s = stageOf(a.vm);
    Stage::Critter c;
    c.kind = 'b';
    c.home = a.vec(0, "around", a.vec(-1, "at", {}));
    c.radius = f32(a.num(-1, "radius", 2.5)), c.size = f32(a.num(-1, "size", 1)), c.speed = f32(a.num(-1, "speed", 1.2));
    c.height = f32(a.num(-1, "height", 1));
    c.seed = static_cast<uint32_t>(a.num(-1, "seed", s.critters.size() * 7 + 3));
    c.pos = c.home + Vec3(0, 0.5f, 0);
    std::string col = a.text(-1, "color", "orange");
    int wing = a.get(-1, "material") ? materialFrom(a.vm, *a.get(-1, "material")) : materialFrom(a.vm, a.vm.text("wing " + col));
    int body = materialFrom(a.vm, a.vm.text("black"));
    int bm = cachedMesh(s, "butterfly body", [] { return shapes::capsule(0.006f, 0.035f, 8, 3); });
    int wm = cachedMesh(s, "butterfly wing", [] { return makeWing(0.045f); });
    c.parts = {placePart(s, bm, body, c.pos, 0, Vec3(1)), placePart(s, wm, wing, c.pos, 0, Vec3(1)), placePart(s, wm, wing, c.pos, 0, Vec3(1))};
    s.critters.push_back(c);
    ++s.version;
    return Value::object(a.vm.newThing(&kCritterClass, static_cast<int>(s.critters.size() - 1)));
  }, "butterfly(around, radius: 2.5 m, color: \"orange\" | \"blue\" | \"white\", size, speed, height): flutters about, resting now and then");
  vm.native("fireflies", [](Args& a) {
    Stage& s = stageOf(a.vm);
    Vec3 c = a.vec(-1, "around", {});
    float radius = f32(a.num(-1, "radius", 6)), height = f32(a.num(-1, "height", 2.5)), size = f32(a.num(-1, "size", 1));
    int count = static_cast<int>(a.num(-1, "count", 40));
    int mat = materialFrom(a.vm, a.get(-1, "material") ? *a.get(-1, "material") : a.vm.text("firefly"));
    int mesh = cachedMesh(s, "mote", [] { return makeMote(0.012f); });
    Rng rng(static_cast<uint32_t>(a.num(-1, "seed", 9)));
    std::vector<int> parts;
    for (int i = 0; i < count; ++i) {
      Stage::Critter f;
      f.kind = 'f';
      f.home = c, f.radius = radius, f.height = height, f.size = size * (0.6f + 0.8f * rng.uniform());
      f.speed = 0.25f + 0.3f * rng.uniform();
      f.seed = rng.next();
      float a2 = rng.uniform() * kTwoPi, r2 = radius * std::sqrt(rng.uniform());
      f.pos = c + Vec3(std::cos(a2) * r2, 0.3f + height * rng.uniform(), std::sin(a2) * r2);
      f.phase = rng.uniform() * 10;
      f.parts = {placePart(s, mesh, mat, f.pos, 0, Vec3(f.size))};
      parts.push_back(f.parts[0]);
      s.critters.push_back(f);
    }
    ++s.version;
    return newGroup(a.vm, s, parts, c);
  }, "fireflies(around, radius, count, height, size, material): glowing motes drifting and twinkling (.visible)");

  // ---- a whole world, made the way nature makes it, streamed round the player
  vm.native("world", [](Args& a) {
    Stage& s = stageOf(a.vm);
    WorldSettings ws;
    ws.size = f32(a.num(-1, "size", 8192));
    ws.seed = static_cast<uint32_t>(a.num(-1, "seed", 1));
    ws.mountains = f32(a.num(-1, "mountains", 1));
    ws.warmth = f32(a.num(-1, "warmth", 14));
    ws.wind = f32(a.num(-1, "wind", 0));
    ws.cells = std::clamp(static_cast<int>(a.num(-1, "cells", 257)), 33, 513);
    Vec3 start = makeStreamedWorld(a.vm, s, ws);
    ++s.version;
    return Value::vec(start);
  }, "world(seed, size: 8192 m, mountains: 1, warmth: 14 C, wind: 0 (from the west)): a world grown like nature (plates lift mountains, rivers carve, lakes fill, rain falls on the windward side; sand, grass, forest, rock, snow), its land and trees made round the player as they go; gives back a good place to start (for move_player)");

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
    // island: yes: the land sinks into the sea towards the edges (a smooth
    // ring from 40% to 90% of the way out), so the middle is an island
    bool island = a.flag(-1, "island", false);
    if (island) {
      float lo = h.minHeight(), hi = h.maxHeight(), floor = lo - (hi - lo) * 0.3f;
      int n = h.resolution();
      for (int z = 0; z < n; ++z)
        for (int x = 0; x < n; ++x) {
          float wx = (static_cast<float>(x) / (n - 1) - 0.5f) * size, wz = (static_cast<float>(z) / (n - 1) - 0.5f) * size;
          float r = std::sqrt(wx * wx + wz * wz) / (size * 0.5f);
          float f = smoothstep(0.4f, 0.9f, r);
          h.at(x, z) = h.at(x, z) * (1 - f) + floor * f;
        }
    }
    if (a.flag(-1, "erosion", true)) {
      HeightMap::Erosion e;
      e.droplets = static_cast<int>(a.num(-1, "rain", res * res * 2.5));
      h.erodeHydraulic(e);
      h.erodeThermal(6);
    }
    // keep the middle near zero so things placed at y 0 sit on the land; an
    // island's sea is at 0 instead: the land is raised so its shore (where the
    // sinking ring is half way, 65% of the way out) is at 0 on average
    float mid = h.height(0, 0);
    if (island) {
      float sum = 0;
      for (int k = 0; k < 64; ++k) {
        float ang = k * kTwoPi / 64, rr = size * 0.5f * 0.65f;
        sum += h.height(std::cos(ang) * rr, std::sin(ang) * rr);
      }
      mid = sum / 64;
    }
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
  }, "terrain(size, height, ridges, seed, erosion, island, material): land carved by rain; island: yes sinks its edges into a sea at height 0");
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
      auto [b, l] = treeMeshes(s, kind, rng.below(4), f32(a.num(-1, "leaf_size", 1)), f32(a.num(-1, "detail", 1)));
      Mat4 xf = Mat4::trs(w, Quat::axisAngle({0, 1, 0}, rng.uniform() * kTwoPi), Vec3(0.7f + 0.6f * rng.uniform()));
      s.scene.add(b, bark, xf);
      s.scene.add(l, leaf, xf);
      ++count;
    }
    ++s.version;
    return Value::number(count);
  }, "forest(kind, around, radius, spacing, clearing, clear_around, steepest, seed, leaves, leaf_size, detail: 1): plants trees on the land; trees far from the camera are drawn as simpler versions of themselves on their own (world.lodBias), and detail below 1 lightens even the nearest");

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

  // ---- music: the engine's own instruments, in time with the world (engine/audio/music.h)
  vm.native("tempo", [](Args& a) {
    Stage& s = stageOf(a.vm);
    s.music.tempo = std::clamp(f32(a.num(0, "bpm", 100)), 20.0f, 300.0f);
    s.music.swing = std::clamp(f32(a.num(-1, "swing", 0)), 0.0f, 0.5f);
    return Value();
  }, "tempo(bpm, swing): how fast the music goes (beats a minute, 4 steps a beat) and its shuffle (0 straight .. 0.5)");
  vm.native("track", [](Args& a) {
    Stage& s = stageOf(a.vm);
    audio::Track t;
    std::string err, ins = a.text(0, "instrument", ""), pat = a.text(1, "pattern", "");
    if (!audio::parseTrack(ins, pat, t, &err)) {
      std::string meant = audio::isInstrument(ins) ? "" : closest(ins, audio::instrumentNames());
      a.vm.fail(err + (meant.empty() ? "" : " (did you mean \"" + meant + "\"?)") + "; instruments: kick, snare, hat, open_hat, clap, tom, bass, lead, pad, pluck, keys, bell");
    }
    t.volume = std::clamp(f32(a.num(-1, "volume", 1)), 0.0f, 2.0f);
    s.music.tracks.push_back(std::move(t));
    return Value();
  }, "track(instrument, pattern, volume): a line of music that loops: drums \"x...X...\" (x a hit, X a loud one), notes \"C2 . - G1\" (. rest, - hold)");

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
