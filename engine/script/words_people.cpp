// The world words: people (registered by installSceneApi, scene_api.cpp).
#include "engine/script/words_internal.h"

namespace loop::script::words {

void registerPeople(Vm& vm) {
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
}

}  // namespace loop::script::words
