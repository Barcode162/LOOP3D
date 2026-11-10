// The world words: land (registered by installSceneApi, scene_api.cpp).
#include "engine/script/words_internal.h"

namespace loop::script::words {

void registerLand(Vm& vm) {
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
}

}  // namespace loop::script::words
