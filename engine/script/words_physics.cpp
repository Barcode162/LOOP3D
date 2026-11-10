// The world words: physics (registered by installSceneApi, scene_api.cpp).
#include "engine/script/words_internal.h"

namespace loop::script::words {

void registerPhysics(Vm& vm) {
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
}

}  // namespace loop::script::words
