#pragma once
// Physics: rigid bodies (spheres, boxes, capsules) that fall, slide, roll,
// bounce and stack; on a ground plane, on land (height maps) and on any
// static mesh; joined by ropes, ball joints and hinges. Cloth that drapes
// and blows in the wind. A walking body (character) that slides along walls
// and climbs steps.
//
// The maths: semi-implicit (symplectic) Euler steps, which keep energy
// from creeping up the way plain Euler does, a sequential-impulse contact
// solver with friction and warm starting (Catto), position correction so
// stacks don't sink, and sleeping for things at rest. Cloth is solved with
// XPBD (Macklin et al.), stiff and stable at any time step.
#include <cstdint>
#include <functional>
#include <memory>
#include <vector>

#include "engine/core/math.h"
#include "engine/geometry/bvh.h"
#include "engine/geometry/mesh.h"
#include "engine/geometry/terrain.h"

namespace loop::physics {

enum class ShapeKind { kSphere, kBox, kCapsule };

struct BodyDesc {
  ShapeKind shape = ShapeKind::kBox;
  Vec3 halfExtents{0.5f};  // box
  float radius = 0.5f;     // sphere, capsule
  float halfHeight = 0.5f; // capsule: half the straight middle (along Y)
  float mass = 1;          // 0 = fixed in place
  float friction = 0.6f, restitution = 0.2f;
  float linearDamping = 0.01f, angularDamping = 0.05f;
  Vec3 position{0}, velocity{0}, angularVelocity{0};
  Quat rotation;
};

struct Body {
  BodyDesc d;
  Vec3 x, v, w;  // position, velocity, angular velocity
  Quat q;
  float invMass = 0;
  Vec3 invInertiaLocal{0};
  Vec3 force{0}, torque{0};
  bool asleep = false;
  float sleepTimer = 0;
  int id = 0;
  Mat3 invInertiaWorld() const;
  AABB bounds() const;
  float kineticEnergy() const;
};

struct Contact {
  int a, b;           // body indices; b = -1 for the static world
  Vec3 point, normal; // normal from b to a
  float depth;
  float nImpulse = 0, tImpulse[2] = {0, 0};
  Vec3 t1, t2;
  float bias = 0, nMass = 0, tMass[2] = {0, 0}, restitutionVel = 0;
};

struct Joint {
  enum class Kind { kDistance, kBall, kHinge } kind = Kind::kBall;
  int a = -1, b = -1;              // b = -1: pinned to the world
  Vec3 localA{0}, localB{0};       // anchor in each body's space (world point for b = -1)
  Vec3 axisA{0, 1, 0}, axisB{0, 1, 0};  // hinge axis in each body's space
  float length = 1;                // distance joint (a rope when `slack`)
  bool slack = false;
};

// A sheet of cloth: a grid of particles held by stretch and bend links.
struct Cloth {
  int nx = 0, ny = 0;
  std::vector<Vec3> x, prev, v;
  std::vector<float> invMass;
  struct Link { int i, j; float rest, compliance; };
  std::vector<Link> links;
  float thickness = 0.01f, damping = 0.01f;
  Vec3 wind{0};
  Mesh mesh() const;  // the current shape as triangles (both sides)
};

struct RayHit {
  float t = kInf;
  Vec3 point, normal;
  int body = -1;  // -1: the static world
  bool hit() const { return t < kInf; }
};

class World {
 public:
  Vec3 gravity{0, -9.81f, 0};
  int velocityIterations = 16, positionIterations = 4;
  float stepRate = 120;           // fixed sub-steps per second
  float sleepSpeed = 0.05f;       // slower than this (m/s) for half a second: asleep
  float staticFriction = 0.6f, staticBounce = 0.2f;  // the fixed world's surfaces
  float walkableCos = 0.6f;       // moveCharacter stands on slopes whose normal.y is above this
  int clothIterations = 8;
  Vec3 wind{0};                   // added to every cloth's own wind
  bool groundPlane = true;  // an endless floor at y = groundHeight
  float groundHeight = 0;

  int addBody(const BodyDesc& d);
  Body& body(int i) { return bodies_[i]; }
  const Body& body(int i) const { return bodies_[i]; }
  size_t bodyCount() const { return bodies_.size(); }
  void applyImpulse(int i, Vec3 impulse, Vec3 at);
  void applyForce(int i, Vec3 f) { bodies_[i].force += f, wake(i); }
  void wake(int i) { bodies_[i].asleep = false, bodies_[i].sleepTimer = 0; }

  // Static things bodies collide with.
  void addStaticMesh(const Mesh& m, const Mat4& xf = Mat4());
  void setTerrain(const HeightMap* h) { terrain_ = h; }
  void clearStatic() { staticTris_.clear(), staticDirty_ = true; }

  int addJoint(const Joint& j);
  Cloth& addCloth(Vec3 corner, Vec3 alongX, Vec3 alongY, int nx, int ny, float mass, float stretch = 0, float bend = 1e-3f);
  std::vector<Cloth>& cloths() { return cloths_; }
  // Pin cloth particle i in place (inverse mass 0).
  void pin(Cloth& c, int i) { c.invMass[i] = 0; }

  // Moves time on by dt, in fixed sub-steps of 1/stepRate s.
  void step(float dt);
  RayHit raycast(const Ray& r, float maxDist = kInf) const;
  const std::vector<Contact>& contacts() const { return contacts_; }
  float totalEnergy() const;  // kinetic + potential (for tests)

  // A walking body: moves `pos` (capsule centre) by `move`, sliding along
  // what it touches and stepping up ledges up to `step` high. Returns
  // whether it ends standing on something.
  bool moveCharacter(Vec3& pos, Vec3 move, float radius, float halfHeight, float stepHeight = 0.3f) const;

 private:
  void substep(float h);
  void collide();
  void collideStatic(int i);
  void addContact(int a, int b, Vec3 p, Vec3 n, float depth);
  void solveJoints(float h);
  void stepCloth(Cloth& c, float h);
  bool sphereVsWorld(Vec3 c, float r, Vec3& n, float& depth, Vec3& p) const;

  std::vector<Body> bodies_;
  std::vector<Contact> contacts_, previous_;
  std::vector<Joint> joints_;
  std::vector<Cloth> cloths_;
  std::vector<Triangle> staticTris_;
  Bvh staticBvh_;
  bool staticDirty_ = false;
  const HeightMap* terrain_ = nullptr;
  float accumulator_ = 0;
};

}  // namespace loop::physics
