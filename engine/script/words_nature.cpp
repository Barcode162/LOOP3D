// The world words: undergrowth, flowers, ruins and creatures (the model library) (registered by installSceneApi, scene_api.cpp).
#include "engine/script/words_internal.h"

namespace loop::script::words {

void registerNature(Vm& vm) {
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
}

}  // namespace loop::script::words
