// The world words: shapes (registered by installSceneApi, scene_api.cpp).
#include "engine/script/words_internal.h"

namespace loop::script::words {

void registerShapes(Vm& vm) {
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
}

}  // namespace loop::script::words
