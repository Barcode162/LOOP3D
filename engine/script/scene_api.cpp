#include "engine/script/scene_api.h"
#include "engine/script/words_internal.h"

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

void Stage::placeGroup(int gi, Vec3 to, float turn) {
  Group& g = groups[gi];
  const float d = turn - g.turn;
  const Quat r = Quat::axisAngle({0, 1, 0}, d);
  const Vec3 from = g.position;
  for (int i : g.parts) {
    Placed& p = placed[i];
    p.position = to + r.rotate(p.position - from);
    if (p.useQuat) p.quat = r * p.quat;
    else p.rotation.x += d;  // yaw is the outermost turn (Quat::euler), so adding is exact
    updateTransform(i);
  }
  for (int i : g.lights) {
    Light& l = scene.lights[i];
    l.position = to + r.rotate(l.position - from);
    l.direction = r.rotate(l.direction);
  }
  for (int c : g.children) {
    groups[c].position = to + r.rotate(groups[c].position - from);
    groups[c].turn += d;
  }
  g.position = to, g.turn = turn;
  if (!g.parts.empty()) collidersDirty = true;
  if (!g.lights.empty()) ++lightsVersion;
}

void Stage::showGroup(int gi, bool visible) {
  Group& g = groups[gi];
  if (g.visible == visible) return;
  g.visible = visible;
  for (int i : g.parts) placed[i].visible = visible, updateTransform(i);
  // a hidden group's lamps go dark, and come back as bright as they were
  if (!visible) {
    g.litLumens.clear();
    for (int i : g.lights) g.litLumens.push_back(scene.lights[i].lumens), scene.lights[i].lumens = 0;
  } else {
    for (size_t k = 0; k < g.lights.size() && k < g.litLumens.size(); ++k) scene.lights[g.lights[k]].lumens = g.litLumens[k];
  }
  for (int c : g.children) groups[c].visible = visible;
  if (!g.lights.empty()) ++lightsVersion;
  // a creature hidden with its group stops moving too
  for (auto& c : critters)
    for (int i : c.parts)
      if (std::find(g.parts.begin(), g.parts.end(), i) != g.parts.end()) c.visible = visible;
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

namespace words {

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
const ThingClass kGroupClass{"model", getGroup, setGroup, {"position", "at", "turn", "visible", "parts"}};
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
  if (f == "turn") return Value::number(g.turn);
  if (f == "parts") return Value::number(static_cast<double>(g.parts.size()));
  found = false;
  return {};
}
bool setGroup(Vm& vm, Thing& t, const std::string& f, const Value& v) {
  Stage& s = stageOf(vm);
  Stage::Group& g = groupOf(vm, t);
  if (f == "position" || f == "at") s.placeGroup(t.id, asVec(vm, v, f), g.turn);
  else if (f == "turn") {
    if (!v.isNumber()) vm.fail("`turn` is an angle, like 90 deg");
    s.placeGroup(t.id, g.position, static_cast<float>(v.n));
  } else if (f == "visible") s.showGroup(t.id, v.truthy());
  else return false;
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
Value place(Args& a, int mesh, char kind, Vec3 size) {
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
int placePart(Stage& s, int mesh, int mat, Vec3 at, float yaw, Vec3 scale, float tilt) {
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
std::pair<int, int> treeMeshes(Stage& st, const std::string& kind, int variant, float leafScale, float detail, int lod) {
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

}  // namespace words
using namespace words;

Vec3 unitLuminanceOr(Vec3 c) {
  float l = luminance(c);
  return l > 1e-6f ? c / l : Vec3(1);
}

// ---- world(): a generated world (engine/geometry/world_gen.h), streamed
struct StreamedWorld {
  WorldMap map;
  WorldStream land, trees;
  const HeightMap* ground = nullptr;  // the walkable ground, 4 m a sample (in stage.terrains)
  std::map<std::pair<int, int>, std::vector<int>> landParts, treeParts;  // placed things per chunk
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
  // each part a placed thing, so showing and hiding it is a move (the drawing
  // takes only what moved), not a new world to read
  auto place = [&](int mesh, int material, Vec3 at, float yaw, float size) {
    scene.add(mesh, material, Mat4::trs(at, Quat::euler(yaw, 0, 0), Vec3(size)));
    Placed p;
    p.instance = static_cast<int>(scene.instances.size() - 1);
    p.position = at, p.rotation = Vec3(yaw, 0, 0), p.scale = Vec3(size);
    placed.push_back(p);
    return static_cast<int>(placed.size() - 1);
  };
  auto showParts = [&](const std::vector<int>& parts, bool on) {
    for (int i : parts) {
      placed[static_cast<size_t>(i)].visible = on;
      updateTransform(i);
    }
  };
  const Vec2 at(playerPos.x, playerPos.z);
  // the land: 256 m chunks, 4 m a quad, out to 1 km
  WorldStream::Work lw = w.land.update(at, first);
  for (const auto& c : lw.make) {
    const float x0 = c.first * w.land.chunk, z0 = c.second * w.land.chunk;
    std::vector<int>& parts = w.landParts[c];
    const HeightMap& g = *w.ground;
    for (auto& [kind, mesh] : landMeshes(w, x0, z0, x0 + w.land.chunk, z0 + w.land.chunk, 4, [&](float x, float z) { return g.height(x, z); }))
      parts.push_back(place(scene.addMesh(std::move(mesh)), w.mat[kind], Vec3(0), 0, 1));
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
    if (!lake.indices.empty()) parts.push_back(place(scene.addMesh(std::move(lake)), w.water, Vec3(0), 0, 1));
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
        const Vec3 at(px, w.ground->height(px, pz) - 0.2f, pz);
        const float yaw = rng.uniform() * kTwoPi, size = 0.7f + 0.6f * rng.uniform();
        parts.push_back(place(b, w.bark, at, yaw, size));
        parts.push_back(place(l, cold ? w.needles : w.leaves, at, yaw, size));
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

  words::registerSettings(vm);
  words::registerMaterials(vm);
  words::registerShapes(vm);
  words::registerNature(vm);
  words::registerWorld(vm);
  words::registerLand(vm);
  words::registerLight(vm);
  words::registerCamera(vm);
  words::registerPhysics(vm);
  words::registerPeople(vm);
  words::registerSound(vm);
  words::registerMusic(vm);
  words::registerStory(vm);
  words::registerRenders(vm);
}

}  // namespace loop::script
