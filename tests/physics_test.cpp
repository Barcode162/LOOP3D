#include "engine/physics/physics.h"
#include "tests/test.h"

using namespace loop;
using namespace loop::physics;

TEST(physics_free_fall_matches_gravity) {
  World w;
  w.groundPlane = false;
  BodyDesc d;
  d.shape = ShapeKind::kSphere;
  d.radius = 0.1f;
  d.position = {0, 10, 0};
  d.linearDamping = 0;
  int b = w.addBody(d);
  for (int i = 0; i < 120; ++i) w.step(1.0f / 120);
  // 1 s of fall: y = 10 - g/2 (semi-implicit Euler is within a step of it)
  CHECK_NEAR(w.body(b).x.y, 10 - 9.81 / 2, 0.06);
  CHECK_NEAR(w.body(b).v.y, -9.81, 0.01);
}

TEST(physics_ball_comes_to_rest_on_ground) {
  World w;
  BodyDesc d;
  d.shape = ShapeKind::kSphere;
  d.radius = 0.25f;
  d.position = {0, 2, 0};
  d.restitution = 0.5f;
  int b = w.addBody(d);
  float maxAfterBounce = 0;
  bool bounced = false;
  for (int i = 0; i < 600; ++i) {
    w.step(1.0f / 120);
    if (w.body(b).v.y > 0.5f) bounced = true;
    if (bounced) maxAfterBounce = std::max(maxAfterBounce, w.body(b).x.y);
  }
  CHECK(bounced);
  // drop 1.75 m to the touch point; a 0.5 bounce rises about 0.25 of that
  CHECK(maxAfterBounce > 0.25f + 0.25f && maxAfterBounce < 0.25f + 0.9f);
  CHECK_NEAR(w.body(b).x.y, 0.25, 0.02);
}

TEST(physics_friction_stopping_distance) {
  // a box sliding at 4 m/s with friction 0.5 stops after v²/(2 μ g) = 1.63 m
  World w;
  BodyDesc d;
  d.shape = ShapeKind::kBox;
  d.halfExtents = {0.25f, 0.25f, 0.25f};
  d.position = {0, 0.25f, 0};
  d.velocity = {4, 0, 0};
  d.friction = 0.5f;
  d.linearDamping = 0;
  d.restitution = 0;
  int b = w.addBody(d);
  w.body(b).d.friction = 0.5f;
  for (int i = 0; i < 360; ++i) w.step(1.0f / 120);
  // the ground's friction is 0.6: combined sqrt(0.5 * 0.6) = 0.548 → 1.49 m
  CHECK_NEAR(w.body(b).x.x, 16.0 / (2 * 0.5477 * 9.81), 0.2);
  CHECK(std::fabs(w.body(b).v.x) < 0.05f);
}

TEST(physics_box_stack_stands) {
  World w;
  std::vector<int> ids;
  for (int i = 0; i < 5; ++i) {
    BodyDesc d;
    d.shape = ShapeKind::kBox;
    d.halfExtents = Vec3(0.5f);
    d.position = {0, 0.5f + i * 1.0f, 0};
    ids.push_back(w.addBody(d));
  }
  for (int i = 0; i < 600; ++i) w.step(1.0f / 120);
  for (int i = 0; i < 5; ++i) {
    CHECK_NEAR(w.body(ids[i]).x.y, 0.5 + i, 0.06);
    CHECK(std::fabs(w.body(ids[i]).x.x) < 0.05f);
  }
}

TEST(physics_pendulum_keeps_its_energy) {
  // a ball on a rod (distance joint) swinging without damping: the
  // symplectic steps keep the energy from creeping up
  World w;
  w.groundPlane = false;
  BodyDesc d;
  d.shape = ShapeKind::kSphere;
  d.radius = 0.05f;
  d.position = {1, 2, 0};
  d.linearDamping = d.angularDamping = 0;
  int b = w.addBody(d);
  Joint j;
  j.kind = Joint::Kind::kDistance;
  j.a = b, j.b = -1, j.localB = {0, 2, 0}, j.length = 1;
  w.addJoint(j);
  float e0 = w.totalEnergy();
  float lowest = 10;
  for (int i = 0; i < 1200; ++i) {
    w.step(1.0f / 120);
    lowest = std::min(lowest, w.body(b).x.y);
  }
  CHECK_NEAR(lowest, 1.0, 0.03);  // swings through the bottom
  CHECK(w.totalEnergy() <= e0 + 0.05f);
  CHECK(w.totalEnergy() > e0 - 0.6f);  // loses a little to the joint's correction, never gains
  CHECK_NEAR(length(w.body(b).x - Vec3(0, 2, 0)), 1.0, 0.02);
}

TEST(physics_mixed_shapes_settle_on_a_mesh) {
  World w;
  w.groundPlane = false;
  Mesh ramp = shapes::box({10, 0.4f, 10});
  w.addStaticMesh(ramp, Mat4::trs({0, 0, 0}, Quat::axisAngle({0, 0, 1}, 0.0f), Vec3(1)));
  std::vector<int> ids;
  for (int i = 0; i < 6; ++i) {
    BodyDesc d;
    d.shape = i % 3 == 0 ? ShapeKind::kSphere : i % 3 == 1 ? ShapeKind::kBox : ShapeKind::kCapsule;
    d.radius = 0.2f, d.halfHeight = 0.3f, d.halfExtents = Vec3(0.2f);
    d.position = {(i - 3) * 0.7f, 1.5f + i * 0.3f, 0};
    ids.push_back(w.addBody(d));
  }
  for (int i = 0; i < 720; ++i) w.step(1.0f / 120);
  for (int id : ids) {
    CHECK(w.body(id).x.y > 0.2f && w.body(id).x.y < 1.0f);
    CHECK(isFinite(w.body(id).x));
  }
  RayHit h = w.raycast({{0, 5, 3}, {0, -1, 0}});
  CHECK(h.hit());
  CHECK_NEAR(h.point.y, 0.2, 0.01);
}

TEST(physics_cloth_drapes_and_holds) {
  World w;
  Cloth& c = w.addCloth({-1, 2, 0}, {2, 0, 0}, {0, 0, 2}, 16, 16, 0.5f);
  for (int i = 0; i < 16; ++i) w.pin(c, i);  // hang it by one edge
  for (int i = 0; i < 360; ++i) w.step(1.0f / 120);
  Cloth& cl = w.cloths()[0];
  float lowest = 10;
  for (auto& p : cl.x) {
    lowest = std::min(lowest, p.y);
    CHECK(isFinite(p));
  }
  CHECK(lowest < 0.6f);  // it fell and hangs down about its length
  CHECK(lowest > -0.01f);  // but not through the floor
  // links stay near their length (no tearing or blowing up)
  float worst = 0;
  for (auto& l : cl.links) worst = std::max(worst, std::fabs(length(cl.x[l.i] - cl.x[l.j]) - l.rest) / l.rest);
  CHECK(worst < 0.15f);
  CHECK(cl.mesh().triangleCount() == 15 * 15 * 2);
}

TEST(physics_character_walks_and_steps_up) {
  World w;
  w.addStaticMesh(shapes::box({2, 0.2f, 2}), Mat4::translation({2, 0.1f, 0}));  // a 20 cm step
  w.addStaticMesh(shapes::box({0.2f, 3, 4}), Mat4::translation({5, 1.5f, 0}));  // a wall
  w.step(0);
  Vec3 pos{0, 0.9f, 0};
  bool grounded = false;
  for (int i = 0; i < 300; ++i) grounded = w.moveCharacter(pos, Vec3(0.02f, -0.05f, 0), 0.3f, 0.6f, 0.3f);
  CHECK(grounded);
  CHECK(pos.x > 4.0f && pos.x < 4.75f);  // walked over the step, stopped at the wall
  CHECK_NEAR(pos.y, 0.9, 0.05);
}
