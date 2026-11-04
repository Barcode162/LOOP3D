#include "engine/physics/physics.h"

#include <algorithm>

namespace loop::physics {

// Contacts start this far before things touch (speculative contacts):
// resting faces don't flicker in and out of touching.
constexpr float kMargin = 0.02f;

// ------------------------------------------------------------------ bodies
Mat3 Body::invInertiaWorld() const {
  Mat3 r = Mat3::fromQuat(q);
  return r * Mat3::diag(invInertiaLocal) * r.transposed();
}

AABB Body::bounds() const {
  switch (d.shape) {
    case ShapeKind::kSphere: return {x - Vec3(d.radius), x + Vec3(d.radius)};
    case ShapeKind::kCapsule: {
      Vec3 a = q.rotate({0, d.halfHeight, 0});
      AABB b;
      b.grow(x + a), b.grow(x - a);
      b.lo -= Vec3(d.radius), b.hi += Vec3(d.radius);
      return b;
    }
    case ShapeKind::kBox: {
      Mat3 r = Mat3::fromQuat(q);
      Vec3 e = vabs(r.c[0]) * d.halfExtents.x + vabs(r.c[1]) * d.halfExtents.y + vabs(r.c[2]) * d.halfExtents.z;
      return {x - e, x + e};
    }
  }
  return {};
}

float Body::kineticEnergy() const {
  if (invMass == 0) return 0;
  Vec3 wl = q.conjugate().rotate(w);
  Vec3 I{invInertiaLocal.x > 0 ? 1 / invInertiaLocal.x : 0, invInertiaLocal.y > 0 ? 1 / invInertiaLocal.y : 0, invInertiaLocal.z > 0 ? 1 / invInertiaLocal.z : 0};
  return 0.5f / invMass * dot(v, v) + 0.5f * dot(wl * wl, I);
}

int World::addBody(const BodyDesc& d) {
  Body b;
  b.d = d;
  b.x = d.position, b.v = d.velocity, b.w = d.angularVelocity, b.q = d.rotation.normalized();
  b.invMass = d.mass > 0 ? 1 / d.mass : 0;
  if (d.mass > 0) {
    Vec3 I;
    float m = d.mass;
    switch (d.shape) {
      case ShapeKind::kSphere: I = Vec3(0.4f * m * d.radius * d.radius); break;
      case ShapeKind::kBox: {
        Vec3 s = d.halfExtents * 2.0f;
        I = Vec3(m / 12 * (s.y * s.y + s.z * s.z), m / 12 * (s.x * s.x + s.z * s.z), m / 12 * (s.x * s.x + s.y * s.y));
        break;
      }
      case ShapeKind::kCapsule: {
        float r = d.radius, h = d.halfHeight * 2;
        float iy = 0.5f * m * r * r, ix = m * (3 * r * r + h * h) / 12 + 0.25f * m * r * r;
        I = {ix, iy, ix};
        break;
      }
    }
    b.invInertiaLocal = {1 / I.x, 1 / I.y, 1 / I.z};
  }
  b.id = static_cast<int>(bodies_.size());
  bodies_.push_back(b);
  return b.id;
}

void World::applyImpulse(int i, Vec3 j, Vec3 at) {
  Body& b = bodies_[i];
  if (b.invMass == 0) return;
  b.v += j * b.invMass;
  b.w += b.invInertiaWorld() * cross(at - b.x, j);
  wake(i);
}

void World::addStaticMesh(const Mesh& m, const Mat4& xf) {
  for (size_t i = 0; i + 2 < m.indices.size(); i += 3) {
    Vec3 a = xf.point(m.positions[m.indices[i]]), b = xf.point(m.positions[m.indices[i + 1]]), c = xf.point(m.positions[m.indices[i + 2]]);
    staticTris_.push_back({a, b - a, c - a});
  }
  staticDirty_ = true;
}

int World::addJoint(const Joint& j) {
  joints_.push_back(j);
  return static_cast<int>(joints_.size() - 1);
}

// ------------------------------------------------------------------ geometry helpers
namespace {

Vec3 closestOnSegment(Vec3 a, Vec3 b, Vec3 p) {
  Vec3 ab = b - a;
  float t = saturate(dot(p - a, ab) / std::max(dot(ab, ab), 1e-12f));
  return a + ab * t;
}

void closestSegmentSegment(Vec3 p1, Vec3 q1, Vec3 p2, Vec3 q2, Vec3& c1, Vec3& c2) {
  Vec3 d1 = q1 - p1, d2 = q2 - p2, r = p1 - p2;
  float a = dot(d1, d1), e = dot(d2, d2), f = dot(d2, r);
  float s = 0, t = 0;
  if (a <= 1e-12f && e <= 1e-12f) { c1 = p1, c2 = p2; return; }
  if (a <= 1e-12f) t = saturate(f / e);
  else {
    float c = dot(d1, r);
    if (e <= 1e-12f) s = saturate(-c / a);
    else {
      float b = dot(d1, d2), den = a * e - b * b;
      s = den != 0 ? saturate((b * f - c * e) / den) : 0;
      t = (b * s + f) / e;
      if (t < 0) t = 0, s = saturate(-c / a);
      else if (t > 1) t = 1, s = saturate((b - c) / a);
    }
  }
  c1 = p1 + d1 * s, c2 = p2 + d2 * t;
}

Vec3 closestOnTriangle(const Triangle& t, Vec3 p) {
  // Ericson, Real-Time Collision Detection 5.1.5
  Vec3 a = t.v0, b = t.v0 + t.e1, c = t.v0 + t.e2;
  Vec3 ab = b - a, ac = c - a, ap = p - a;
  float d1 = dot(ab, ap), d2 = dot(ac, ap);
  if (d1 <= 0 && d2 <= 0) return a;
  Vec3 bp = p - b;
  float d3 = dot(ab, bp), d4 = dot(ac, bp);
  if (d3 >= 0 && d4 <= d3) return b;
  float vc = d1 * d4 - d3 * d2;
  if (vc <= 0 && d1 >= 0 && d3 <= 0) return a + ab * (d1 / (d1 - d3));
  Vec3 cp = p - c;
  float d5 = dot(ab, cp), d6 = dot(ac, cp);
  if (d6 >= 0 && d5 <= d6) return c;
  float vb = d5 * d2 - d1 * d6;
  if (vb <= 0 && d2 >= 0 && d6 <= 0) return a + ac * (d2 / (d2 - d6));
  float va = d3 * d6 - d5 * d4;
  if (va <= 0 && (d4 - d3) >= 0 && (d5 - d6) >= 0) return b + (c - b) * ((d4 - d3) / ((d4 - d3) + (d5 - d6)));
  float den = 1 / (va + vb + vc);
  return a + ab * (vb * den) + ac * (vc * den);
}

// The body's surface as spheres (for touching the static world): a sphere
// is itself, a capsule is three, a box is its corners and edge middles.
void collisionSpheres(const Body& b, std::vector<std::pair<Vec3, float>>& out) {
  out.clear();
  switch (b.d.shape) {
    case ShapeKind::kSphere: out.push_back({b.x, b.d.radius}); break;
    case ShapeKind::kCapsule: {
      Vec3 a = b.q.rotate({0, b.d.halfHeight, 0});
      out.push_back({b.x + a, b.d.radius});
      out.push_back({b.x, b.d.radius});
      out.push_back({b.x - a, b.d.radius});
      break;
    }
    case ShapeKind::kBox: {
      Vec3 e = b.d.halfExtents;
      for (int i = 0; i < 8; ++i) out.push_back({b.x + b.q.rotate({i & 1 ? e.x : -e.x, i & 2 ? e.y : -e.y, i & 4 ? e.z : -e.z}), 0.0f});
      break;
    }
  }
}

}  // namespace

// ------------------------------------------------------------------ collision
void World::addContact(int a, int b, Vec3 p, Vec3 n, float depth) {
  Contact c;
  c.a = a, c.b = b, c.point = p, c.normal = normalize(n), c.depth = depth;
  orthonormalBasis(c.normal, c.t1, c.t2);
  contacts_.push_back(c);
}

bool World::sphereVsWorld(Vec3 c, float r, Vec3& n, float& depth, Vec3& p) const {
  depth = -kMargin;
  bool hit = false;
  auto consider = [&](Vec3 nn, float d, Vec3 pp) {
    if (d > depth) depth = d, n = nn, p = pp, hit = true;
  };
  if (groundPlane) consider({0, 1, 0}, groundHeight + r - c.y, {c.x, groundHeight, c.z});
  if (terrain_) {
    float h = terrain_->height(c.x, c.z);
    Vec3 tn = terrain_->normal(c.x, c.z);
    float d = (h - c.y) * tn.y + r;
    consider(tn, d, {c.x, h, c.z});
  }
  if (!staticTris_.empty()) {
    AABB q(c - Vec3(r + kMargin), c + Vec3(r + kMargin));
    const auto& nodes = staticBvh_.nodes();
    const auto& order = staticBvh_.order();
    uint32_t stack[64];
    int sp = 0;
    if (!nodes.empty()) stack[sp++] = 0;
    while (sp) {
      const Bvh::Node& nd = nodes[stack[--sp]];
      if (!nd.box.overlaps(q)) continue;
      if (nd.count) {
        for (uint32_t k = 0; k < nd.count; ++k) {
          const Triangle& t = staticTris_[order[nd.left + k]];
          Vec3 cp = closestOnTriangle(t, c);
          Vec3 dv = c - cp;
          float dist = length(dv);
          Vec3 tn = normalize(cross(t.e1, t.e2));
          float side = dot(c - t.v0, tn);
          bool overFace = lengthSq(cp - (c - tn * side)) < 1e-8f;  // the nearest point is inside the face
          if (overFace && side < 0 && side > -(r + 0.05f)) {
            // just behind a face: push back out the front (keeps corners from tunnelling)
            consider(tn, r - side, cp);
          } else if (dist < r + kMargin && dist > 1e-6f) {
            consider(dv / dist, r - dist, cp);  // edges and corners: away from the nearest point
          } else if (dist <= 1e-6f) consider(tn, r, cp);
        }
      } else if (sp < 62) stack[sp++] = nd.left, stack[sp++] = nd.left + 1;
    }
  }
  return hit;
}

void World::collideStatic(int i) {
  Body& b = bodies_[i];
  static thread_local std::vector<std::pair<Vec3, float>> spheres;
  collisionSpheres(b, spheres);
  for (auto& [c, r] : spheres) {
    Vec3 n, p;
    float depth;
    if (sphereVsWorld(c, r, n, depth, p)) addContact(i, -1, c - n * (r - depth * 0.5f), n, depth);
  }
}

namespace {
struct Obb {
  Vec3 c, e;
  Vec3 ax[3];
};
Obb obbOf(const Body& b) {
  Obb o;
  o.c = b.x, o.e = b.d.halfExtents;
  Mat3 r = Mat3::fromQuat(b.q);
  for (int i = 0; i < 3; ++i) o.ax[i] = r.c[i];
  return o;
}
Vec3 closestOnObb(const Obb& o, Vec3 p) {
  Vec3 d = p - o.c, q = o.c;
  for (int i = 0; i < 3; ++i) q += o.ax[i] * clamp(dot(d, o.ax[i]), -o.e[i], o.e[i]);
  return q;
}
bool insideObb(const Obb& o, Vec3 p, float slack) {
  Vec3 d = p - o.c;
  for (int i = 0; i < 3; ++i)
    if (std::fabs(dot(d, o.ax[i])) > o.e[i] + slack) return false;
  return true;
}
float projectRadius(const Obb& o, Vec3 axis) {
  return o.e.x * std::fabs(dot(o.ax[0], axis)) + o.e.y * std::fabs(dot(o.ax[1], axis)) + o.e.z * std::fabs(dot(o.ax[2], axis));
}
}  // namespace

void World::collide() {
  previous_.swap(contacts_);
  contacts_.clear();
  const int n = static_cast<int>(bodies_.size());
  for (int i = 0; i < n; ++i)
    if (bodies_[i].invMass > 0 && !bodies_[i].asleep) collideStatic(i);
  // pairs: sort by x for a sweep (sweep and prune on one axis)
  std::vector<std::pair<float, int>> xs;
  std::vector<AABB> boxes(n);
  for (int i = 0; i < n; ++i) boxes[i] = bodies_[i].bounds(), xs.push_back({boxes[i].lo.x, i});
  std::sort(xs.begin(), xs.end());
  for (size_t s = 0; s < xs.size(); ++s) {
    int i = xs[s].second;
    for (size_t t = s + 1; t < xs.size() && xs[t].first <= boxes[i].hi.x; ++t) {
      int j = xs[t].second;
      if (!boxes[i].overlaps(boxes[j])) continue;
      Body &A = bodies_[i], &B = bodies_[j];
      if (A.invMass == 0 && B.invMass == 0) continue;
      if (A.asleep && B.asleep) continue;
      if ((A.asleep || A.invMass == 0) && (B.asleep || B.invMass == 0)) continue;
      // order so shape kinds come in a fixed order: sphere < box < capsule
      int a = std::min(i, j), b = std::max(i, j);  // a fixed order keeps warm starting working
      if (static_cast<int>(bodies_[a].d.shape) > static_cast<int>(bodies_[b].d.shape)) std::swap(a, b);
      Body &P = bodies_[a], &Q = bodies_[b];
      ShapeKind ka = P.d.shape, kb = Q.d.shape;
      auto sphereSphere = [&](Vec3 ca, float ra, Vec3 cb, float rb) {
        Vec3 d = ca - cb;
        float l = length(d);
        if (l < ra + rb + kMargin) addContact(a, b, cb + d * (rb / std::max(l, 1e-6f)), l > 1e-6f ? d / l : Vec3(0, 1, 0), ra + rb - l);
      };
      auto sphereBox = [&](Vec3 c, float r, const Obb& o, bool sphereIsA) {
        Vec3 cp = closestOnObb(o, c);
        Vec3 d = c - cp;
        float l = length(d);
        if (l >= r + kMargin) return;
        Vec3 nrm;
        float depth;
        if (l > 1e-6f) nrm = d / l, depth = r - l;
        else {  // centre inside: push out the nearest face
          Vec3 rel = c - o.c;
          float best = kInf;
          for (int k = 0; k < 3; ++k) {
            float pd = dot(rel, o.ax[k]);
            float pen = o.e[k] - std::fabs(pd);
            if (pen < best) best = pen, nrm = o.ax[k] * (pd >= 0 ? 1.0f : -1.0f);
          }
          depth = best + r;
        }
        if (sphereIsA) addContact(a, b, cp, nrm, depth);
        else addContact(b, a, cp, nrm, depth);
      };
      auto capsuleEnds = [](const Body& c, Vec3& p0, Vec3& p1) {
        Vec3 h = c.q.rotate({0, c.d.halfHeight, 0});
        p0 = c.x + h, p1 = c.x - h;
      };
      if (ka == ShapeKind::kSphere && kb == ShapeKind::kSphere) sphereSphere(P.x, P.d.radius, Q.x, Q.d.radius);
      else if (ka == ShapeKind::kSphere && kb == ShapeKind::kBox) sphereBox(P.x, P.d.radius, obbOf(Q), true);
      else if (ka == ShapeKind::kSphere && kb == ShapeKind::kCapsule) {
        Vec3 p0, p1;
        capsuleEnds(Q, p0, p1);
        sphereSphere(P.x, P.d.radius, closestOnSegment(p0, p1, P.x), Q.d.radius);
      } else if (ka == ShapeKind::kCapsule && kb == ShapeKind::kCapsule) {
        Vec3 a0, a1, b0, b1, c1, c2;
        capsuleEnds(P, a0, a1), capsuleEnds(Q, b0, b1);
        closestSegmentSegment(a0, a1, b0, b1, c1, c2);
        sphereSphere(c1, P.d.radius, c2, Q.d.radius);
      } else if (ka == ShapeKind::kBox && kb == ShapeKind::kCapsule) {
        Vec3 p0, p1;
        capsuleEnds(Q, p0, p1);
        Obb o = obbOf(P);
        // a few spheres along the capsule against the box
        for (int k = 0; k <= 4; ++k) {
          Vec3 c = vlerp(p0, p1, k / 4.0f);
          Vec3 cp = closestOnObb(o, c);
          Vec3 d = c - cp;
          float l = length(d);
          if (l < Q.d.radius && l > 1e-6f) addContact(b, a, cp, d / l, Q.d.radius - l);
        }
      } else if (ka == ShapeKind::kBox && kb == ShapeKind::kBox) {
        Obb A = obbOf(P), B = obbOf(Q);
        // separating axis test over 15 axes; keep the shallowest overlap
        Vec3 t = A.c - B.c;
        float best = kInf;
        Vec3 bestAxis;
        int bestKind = 0;  // 0 face of A, 1 face of B, 2 edge
        int bi = 0, bj = 0;
        bool separated = false;
        auto test = [&](Vec3 axis, int kind, int ii, int jj) {
          float l = length(axis);
          if (l < 1e-6f) return;
          axis = axis / l;
          float ov = projectRadius(A, axis) + projectRadius(B, axis) - std::fabs(dot(t, axis));
          if (ov < -kMargin) { separated = true; return; }
          // prefer faces a little over edges (stabler contacts)
          float score = kind == 2 ? ov * 1.05f + 1e-4f : ov;
          if (score < best) best = score, bestAxis = dot(t, axis) < 0 ? -axis : axis, bestKind = kind, bi = ii, bj = jj;
        };
        for (int k = 0; k < 3 && !separated; ++k) test(A.ax[k], 0, k, 0);
        for (int k = 0; k < 3 && !separated; ++k) test(B.ax[k], 1, k, 0);
        for (int k = 0; k < 3 && !separated; ++k)
          for (int m = 0; m < 3 && !separated; ++m) test(cross(A.ax[k], B.ax[m]), 2, k, m);
        if (separated) continue;
        Vec3 nrm = bestAxis;  // from B to A
        float depth = projectRadius(A, nrm) + projectRadius(B, nrm) - std::fabs(dot(t, nrm));
        size_t before = contacts_.size();
        if (bestKind != 2) {
          // corners of each box inside the other
          // corners of the box meeting the other's face (the face that won the test)
          for (int side = bestKind == 0 ? 0 : 1; side < (bestKind == 0 ? 1 : 2); ++side) {
            const Obb& inc = side == 0 ? B : A;
            const Obb& ref = side == 0 ? A : B;
            for (int v = 0; v < 8; ++v) {
              Vec3 p = inc.c + inc.ax[0] * (v & 1 ? inc.e.x : -inc.e.x) + inc.ax[1] * (v & 2 ? inc.e.y : -inc.e.y) + inc.ax[2] * (v & 4 ? inc.e.z : -inc.e.z);
              if (!insideObb(ref, p, kMargin)) continue;
              // depth of this corner along the normal
              float dpt = side == 0 ? (dot(p, nrm) - (dot(A.c, nrm) - projectRadius(A, nrm))) : ((dot(B.c, nrm) + projectRadius(B, nrm)) - dot(p, nrm));
              if (dpt > -kMargin) addContact(a, b, p, nrm, std::min(dpt, depth + 0.01f));
            }
          }
        }
        if (contacts_.size() == before) {
          // edge against edge: the nearest points of the two edges
          Vec3 ca = A.c, cb = B.c;
          for (int k = 0; k < 3; ++k) {
            if (k != bi) ca += A.ax[k] * (dot(-nrm, A.ax[k]) > 0 ? A.e[k] : -A.e[k]);
            if (k != bj) cb += B.ax[k] * (dot(nrm, B.ax[k]) > 0 ? B.e[k] : -B.e[k]);
          }
          Vec3 c1, c2;
          closestSegmentSegment(ca - A.ax[bi] * A.e[bi], ca + A.ax[bi] * A.e[bi], cb - B.ax[bj] * B.e[bj], cb + B.ax[bj] * B.e[bj], c1, c2);
          addContact(a, b, (c1 + c2) * 0.5f, nrm, depth);
        }
      }
    }
  }
  // warm start: reuse last step's impulses for contacts that persist
  for (Contact& c : contacts_)
    for (const Contact& p : previous_)
      if (c.depth >= 0)
        if (p.a == c.a && p.b == c.b && lengthSq(p.point - c.point) < 0.0004f) {
        c.nImpulse = p.nImpulse * 0.9f;
        c.tImpulse[0] = p.tImpulse[0] * 0.9f, c.tImpulse[1] = p.tImpulse[1] * 0.9f;
        break;
      }
}

// ------------------------------------------------------------------ stepping
void World::step(float dt) {
  if (staticDirty_) {
    staticBvh_.build(staticTris_);
    staticDirty_ = false;
  }
  accumulator_ += std::min(dt, 0.25f);
  const float h = 1.0f / 120;
  while (accumulator_ >= h) {
    substep(h);
    accumulator_ -= h;
  }
}

void World::substep(float h) {
  // 1. forces → velocities (semi-implicit: velocities first, positions from them)
  for (Body& b : bodies_) {
    if (b.invMass == 0 || b.asleep) continue;
    b.v += (gravity + b.force * b.invMass) * h;
    b.w += b.invInertiaWorld() * b.torque * h;
    b.v *= 1 / (1 + h * b.d.linearDamping);
    b.w *= 1 / (1 + h * b.d.angularDamping);
    b.force = b.torque = Vec3(0);
  }
  collide();
  // sleeping bodies hold still like the ground until something wakes them
  std::vector<std::pair<float, Vec3>> saved;
  for (Body& b : bodies_)
    if (b.asleep && b.invMass > 0) {
      saved.push_back({b.invMass, b.invInertiaLocal});
      b.invMass = 0, b.invInertiaLocal = Vec3(0);
    }
  // 2. prepare contacts
  for (Contact& c : contacts_) {
    Body& A = bodies_[c.a];
    Body* B = c.b >= 0 ? &bodies_[c.b] : nullptr;
    Vec3 ra = c.point - A.x, rb = B ? c.point - B->x : Vec3(0);
    Mat3 Ia = A.invInertiaWorld(), Ib = B ? B->invInertiaWorld() : Mat3::diag(Vec3(0));
    auto effMass = [&](Vec3 dir) {
      float k = A.invMass + (B ? B->invMass : 0) + dot(cross(Ia * cross(ra, dir), ra), dir);
      if (B) k += dot(cross(Ib * cross(rb, dir), rb), dir);
      return k > 0 ? 1 / k : 0;
    };
    c.nMass = effMass(c.normal);
    c.tMass[0] = effMass(c.t1), c.tMass[1] = effMass(c.t2);
    Vec3 vr = A.v + cross(A.w, ra) - (B ? B->v + cross(B->w, rb) : Vec3(0));
    float vn = dot(vr, c.normal);
    float e = std::max(A.d.restitution, B ? B->d.restitution : 0.2f);
    c.restitutionVel = vn < -1.0f ? -e * vn : 0;
    // push apart what overlaps; let what's about to touch close the gap exactly
    // the gap about to close may close exactly; overlap is fixed separately
    // below (split impulses), so pushing apart never adds bounce
    c.bias = c.depth < 0 ? c.depth / h : 0;
    // warm start
    Vec3 P = c.normal * c.nImpulse + c.t1 * c.tImpulse[0] + c.t2 * c.tImpulse[1];
    A.v += P * A.invMass, A.w += Ia * cross(ra, P);
    if (B) B->v -= P * B->invMass, B->w -= Ib * cross(rb, P);
  }
  // 3. sequential impulses
  for (int it = 0; it < velocityIterations; ++it) {
    for (Contact& c : contacts_) {
      Body& A = bodies_[c.a];
      Body* B = c.b >= 0 ? &bodies_[c.b] : nullptr;
      Vec3 ra = c.point - A.x, rb = B ? c.point - B->x : Vec3(0);
      Mat3 Ia = A.invInertiaWorld(), Ib = B ? B->invInertiaWorld() : Mat3::diag(Vec3(0));
      auto apply = [&](Vec3 P) {
        A.v += P * A.invMass, A.w += Ia * cross(ra, P);
        if (B) B->v -= P * B->invMass, B->w -= Ib * cross(rb, P);
      };
      Vec3 vr = A.v + cross(A.w, ra) - (B ? B->v + cross(B->w, rb) : Vec3(0));
      // friction (inside the cone set by the normal impulse)
      float mu = std::sqrt(A.d.friction * (B ? B->d.friction : 0.6f));
      for (int k = 0; k < 2; ++k) {
        Vec3 t = k == 0 ? c.t1 : c.t2;
        float jt = -dot(vr, t) * c.tMass[k];
        float old = c.tImpulse[k], maxF = mu * c.nImpulse;
        c.tImpulse[k] = clamp(old + jt, -maxF, maxF);
        apply(t * (c.tImpulse[k] - old));
        vr = A.v + cross(A.w, ra) - (B ? B->v + cross(B->w, rb) : Vec3(0));
      }
      // a real hit bounces; a gap about to close may close exactly, no more
      float target = c.restitutionVel > 0 ? c.restitutionVel : c.bias;
      float jn = (-dot(vr, c.normal) + target) * c.nMass;
      float old = c.nImpulse;
      c.nImpulse = std::max(0.0f, old + jn);
      apply(c.normal * (c.nImpulse - old));
    }
    solveJoints(h);
  }
  // 3b. split impulses: pseudo-velocities that only move positions
  std::vector<Vec3> pv(bodies_.size(), Vec3(0)), pw(bodies_.size(), Vec3(0));
  std::vector<float> pImp(contacts_.size(), 0);
  for (int it = 0; it < positionIterations * 2; ++it)
    for (size_t ci = 0; ci < contacts_.size(); ++ci) {
      Contact& c = contacts_[ci];
      if (c.depth <= 0.002f) continue;
      Body& A = bodies_[c.a];
      Body* B = c.b >= 0 ? &bodies_[c.b] : nullptr;
      Vec3 ra = c.point - A.x, rb = B ? c.point - B->x : Vec3(0);
      Mat3 Ia = A.invInertiaWorld(), Ib = B ? B->invInertiaWorld() : Mat3::diag(Vec3(0));
      Vec3 vrel = pv[c.a] + cross(pw[c.a], ra) - (B ? pv[c.b] + cross(pw[c.b], rb) : Vec3(0));
      float target = 0.3f / h * (c.depth - 0.002f);
      float lambda = (target - dot(vrel, c.normal)) * c.nMass;
      float old = pImp[ci];
      pImp[ci] = std::max(0.0f, old + lambda);
      Vec3 P = c.normal * (pImp[ci] - old);
      pv[c.a] += P * A.invMass, pw[c.a] += Ia * cross(ra, P);
      if (B) pv[c.b] -= P * B->invMass, pw[c.b] -= Ib * cross(rb, P);
    }
  {
    size_t k = 0;
    for (Body& b : bodies_)
      if (b.asleep && b.invMass == 0 && k < saved.size() && b.d.mass > 0) {
        b.invMass = saved[k].first, b.invInertiaLocal = saved[k].second;
        b.v = b.w = Vec3(0);
        ++k;
      }
  }
  // 4. velocities → positions
  for (size_t bi = 0; bi < bodies_.size(); ++bi) {
    Body& b = bodies_[bi];
    if (b.invMass == 0 || b.asleep) continue;
    b.x += (b.v + pv[bi]) * h;
    Vec3 wt = b.w + pw[bi];
    Quat dq{wt.x * h * 0.5f, wt.y * h * 0.5f, wt.z * h * 0.5f, 0};
    dq = dq * b.q;
    b.q = Quat{b.q.x + dq.x, b.q.y + dq.y, b.q.z + dq.z, b.q.w + dq.w}.normalized();
    // sleep when still for half a second
    if (dot(b.v, b.v) < 0.0025f && dot(b.w, b.w) < 0.01f) {
      b.sleepTimer += h;
      if (b.sleepTimer > 0.5f) b.asleep = true, b.v = b.w = Vec3(0);
    } else b.sleepTimer = 0;
  }
  // a sleeping body wakes when something awake and moving touches it
  for (const Contact& c : contacts_) {
    if (c.b < 0) continue;
    Body &A = bodies_[c.a], &B = bodies_[c.b];
    if (A.asleep != B.asleep) {
      Body& awake = A.asleep ? B : A;
      if (dot(awake.v, awake.v) > 0.01f) wake(A.asleep ? c.a : c.b);
    }
  }
  for (Cloth& c : cloths_) stepCloth(c, h);
}

void World::solveJoints(float h) {
  for (Joint& j : joints_) {
    Body& A = bodies_[j.a];
    Body* B = j.b >= 0 ? &bodies_[j.b] : nullptr;
    Vec3 pa = A.x + A.q.rotate(j.localA), pb = B ? B->x + B->q.rotate(j.localB) : j.localB;
    Vec3 ra = pa - A.x, rb = B ? pb - B->x : Vec3(0);
    Mat3 Ia = A.invInertiaWorld(), Ib = B ? B->invInertiaWorld() : Mat3::diag(Vec3(0));
    auto apply = [&](Vec3 P) {
      A.v += P * A.invMass, A.w += Ia * cross(ra, P);
      if (B) B->v -= P * B->invMass, B->w -= Ib * cross(rb, P);
    };
    Vec3 vrel = A.v + cross(A.w, ra) - (B ? B->v + cross(B->w, rb) : Vec3(0));
    Vec3 err = pa - pb;
    auto effMass = [&](Vec3 dir) {
      float k = A.invMass + (B ? B->invMass : 0) + dot(cross(Ia * cross(ra, dir), ra), dir);
      if (B) k += dot(cross(Ib * cross(rb, dir), rb), dir);
      return k > 0 ? 1 / k : 0;
    };
    if (j.kind == Joint::Kind::kDistance) {
      float len = length(err);
      if (len < 1e-6f) continue;
      Vec3 n = err / len;
      float C = len - j.length;
      if (j.slack && C < 0) continue;  // a rope only pulls
      float lambda = -(dot(vrel, n) + 0.2f / h * C) * effMass(n);
      apply(n * lambda);
    } else {
      // ball: the two anchors stay together (each axis in turn)
      for (int k = 0; k < 3; ++k) {
        Vec3 n{k == 0 ? 1.0f : 0, k == 1 ? 1.0f : 0, k == 2 ? 1.0f : 0};
        vrel = A.v + cross(A.w, ra) - (B ? B->v + cross(B->w, rb) : Vec3(0));
        float lambda = -(dot(vrel, n) + 0.2f / h * dot(err, n)) * effMass(n);
        apply(n * lambda);
      }
      if (j.kind == Joint::Kind::kHinge) {
        // hinge: the bodies may only turn about the shared axis
        Vec3 axA = A.q.rotate(j.axisA), axB = B ? B->q.rotate(j.axisB) : j.axisB;
        Vec3 dw = A.w - (B ? B->w : Vec3(0));
        Vec3 off = dw - axA * dot(dw, axA);
        Vec3 corr = cross(axA, axB) * (0.2f / h);
        Vec3 dwFix = -off - corr;
        Mat3 Isum = Ia;
        if (B) for (int c = 0; c < 3; ++c) Isum.c[c] += Ib.c[c];
        Vec3 L = Isum.inverse() * dwFix;
        A.w += Ia * L;
        if (B) B->w -= Ib * L;
      }
    }
  }
}

// ------------------------------------------------------------------ cloth
Cloth& World::addCloth(Vec3 corner, Vec3 ax, Vec3 ay, int nx, int ny, float mass, float stretch, float bend) {
  Cloth c;
  c.nx = nx, c.ny = ny;
  float pm = mass / (nx * ny);
  for (int j = 0; j < ny; ++j)
    for (int i = 0; i < nx; ++i) {
      Vec3 p = corner + ax * (static_cast<float>(i) / (nx - 1)) + ay * (static_cast<float>(j) / (ny - 1));
      c.x.push_back(p), c.prev.push_back(p), c.v.push_back(Vec3(0)), c.invMass.push_back(1 / pm);
    }
  auto link = [&](int a, int b, float comp) { c.links.push_back({a, b, length(c.x[a] - c.x[b]), comp}); };
  for (int j = 0; j < ny; ++j)
    for (int i = 0; i < nx; ++i) {
      int k = j * nx + i;
      if (i + 1 < nx) link(k, k + 1, stretch);
      if (j + 1 < ny) link(k, k + nx, stretch);
      if (i + 1 < nx && j + 1 < ny) link(k, k + nx + 1, stretch * 4 + 1e-6f), link(k + 1, k + nx, stretch * 4 + 1e-6f);
      if (i + 2 < nx) link(k, k + 2, bend);
      if (j + 2 < ny) link(k, k + 2 * nx, bend);
    }
  cloths_.push_back(std::move(c));
  return cloths_.back();
}

void World::stepCloth(Cloth& c, float h) {
  const size_t n = c.x.size();
  for (size_t i = 0; i < n; ++i) {
    if (c.invMass[i] == 0) continue;
    c.v[i] += gravity * h;
    c.v[i] += (c.wind - c.v[i]) * (0.6f * h);  // air drag toward the wind
    c.v[i] *= 1 - c.damping;
    c.prev[i] = c.x[i];
    c.x[i] += c.v[i] * h;
  }
  for (int it = 0; it < 8; ++it) {
    for (const Cloth::Link& l : c.links) {
      float wa = c.invMass[l.i], wb = c.invMass[l.j];
      if (wa + wb == 0) continue;
      Vec3 d = c.x[l.i] - c.x[l.j];
      float len = length(d);
      if (len < 1e-9f) continue;
      float C = len - l.rest;
      float alpha = l.compliance / (h * h);
      float dl = -C / (wa + wb + alpha);
      Vec3 g = d / len;
      c.x[l.i] += g * (dl * wa);
      c.x[l.j] -= g * (dl * wb);
    }
    // collisions: the world and the bodies' spheres
    for (size_t i = 0; i < n; ++i) {
      if (c.invMass[i] == 0) continue;
      Vec3 nn, p;
      float depth;
      if (sphereVsWorld(c.x[i], c.thickness, nn, depth, p) && depth > 0) c.x[i] += nn * depth;
      for (const Body& b : bodies_) {
        if (b.d.shape != ShapeKind::kSphere) continue;
        Vec3 d = c.x[i] - b.x;
        float l = length(d), r = b.d.radius + c.thickness;
        if (l < r && l > 1e-6f) c.x[i] = b.x + d * (r / l);
      }
    }
  }
  for (size_t i = 0; i < n; ++i) {
    if (c.invMass[i] == 0) continue;
    Vec3 nv = (c.x[i] - c.prev[i]) / h;
    // friction on whatever it rests on
    Vec3 nn, p;
    float depth;
    if (sphereVsWorld(c.x[i], c.thickness * 1.5f, nn, depth, p) && depth > 0) nv -= (nv - nn * dot(nv, nn)) * 0.3f;
    c.v[i] = nv;
  }
}

Mesh Cloth::mesh() const {
  Mesh m;
  for (int j = 0; j < ny; ++j)
    for (int i = 0; i < nx; ++i) m.addVertex(x[j * nx + i], {0, 1, 0}, {static_cast<float>(i) / (nx - 1), static_cast<float>(j) / (ny - 1)});
  for (int j = 0; j + 1 < ny; ++j)
    for (int i = 0; i + 1 < nx; ++i) {
      uint32_t a = j * nx + i;
      m.addQuad(a, a + 1, a + nx + 1, a + nx);
    }
  m.computeNormals();
  return m;
}

// ------------------------------------------------------------------ queries
RayHit World::raycast(const Ray& r, float maxDist) const {
  RayHit best;
  best.t = maxDist;
  if (groundPlane && r.d.y < 0) {
    float t = (groundHeight - r.o.y) / r.d.y;
    if (t > 0 && t < best.t) best.t = t, best.normal = {0, 1, 0}, best.body = -1;
  }
  if (!staticBvh_.empty()) {
    Hit h = staticBvh_.closest(r, 1e-4f, best.t);
    if (h.valid()) {
      const Triangle& t = staticTris_[h.prim];
      best.t = h.t, best.normal = normalize(cross(t.e1, t.e2)), best.body = -1;
      if (dot(best.normal, r.d) > 0) best.normal = -best.normal;
    }
  }
  if (terrain_) {
    // march, then refine
    float step = 0.5f, prevT = 0;
    for (float t = 0; t < std::min(best.t, 2000.0f); t += step) {
      Vec3 p = r.at(t);
      if (p.y < terrain_->height(p.x, p.z)) {
        float lo = prevT, hi = t;
        for (int k = 0; k < 20; ++k) {
          float m = (lo + hi) / 2;
          Vec3 q = r.at(m);
          (q.y < terrain_->height(q.x, q.z) ? hi : lo) = m;
        }
        best.t = hi, best.normal = terrain_->normal(r.at(hi).x, r.at(hi).z), best.body = -1;
        break;
      }
      prevT = t;
      step = std::min(4.0f, 0.5f + t * 0.02f);
    }
  }
  for (const Body& b : bodies_) {
    // bodies as their bounding spheres, then exact for spheres
    float rad = b.d.shape == ShapeKind::kSphere ? b.d.radius : length(b.bounds().extent()) * 0.5f;
    Vec3 oc = r.o - b.x;
    float bb = dot(oc, r.d), c = dot(oc, oc) - rad * rad, disc = bb * bb - c;
    if (disc < 0) continue;
    float t = -bb - std::sqrt(disc);
    if (t > 0 && t < best.t) best.t = t, best.normal = normalize(r.at(t) - b.x), best.body = b.id;
  }
  if (best.t >= maxDist) return RayHit{};
  best.point = r.at(best.t);
  return best;
}

float World::totalEnergy() const {
  float e = 0;
  for (const Body& b : bodies_)
    if (b.invMass > 0) e += b.kineticEnergy() - dot(gravity, b.x) / b.invMass;
  return e;
}

bool World::moveCharacter(Vec3& pos, Vec3 move, float radius, float halfHeight, float stepHeight) const {
  // move in small pieces; after each, push the capsule's three spheres out
  // of whatever they're in. Blocked low down but clear higher up = a step.
  int pieces = 1;
  bool grounded = false;
  auto resolve = [&](Vec3& p) {
    bool ground = false;
    for (int it = 0; it < 4; ++it) {
      bool any = false;
      for (float off : {-halfHeight, 0.0f, halfHeight}) {
        Vec3 c = p + Vec3(0, off, 0), n, hp;
        float depth;
        if (sphereVsWorld(c, radius, n, depth, hp) && depth > 0) {
          float foot = p.y - halfHeight - radius;
          if (n.y > 0.6f) ground = true, p.y += depth / std::max(n.y, 0.6f);  // stand: push straight up
          else if (hp.y < foot + stepHeight && n.y > 0.02f) {
            // a ledge low enough to step onto: rise over it instead of being pushed back
            float horiz = std::sqrt(sqr(c.x - hp.x) + sqr(c.z - hp.z));
            if (horiz < radius) p.y += std::max(0.0f, hp.y + std::sqrt(radius * radius - horiz * horiz) - c.y);
            ground = true;
          } else p += Vec3(n.x, 0, n.z) * (depth / std::max(1e-3f, std::sqrt(n.x * n.x + n.z * n.z)));
          any = true;
        }
      }
      if (!any) break;
    }
    return ground;
  };
  // sideways first (stepping up ledges), then up / down on its own
  Vec3 flat{move.x, 0, move.z};
  pieces = std::max(1, static_cast<int>(std::ceil(length(flat) / (radius * 0.25f))));
  Vec3 dh = flat / static_cast<float>(pieces);
  for (int k = 0; k < pieces; ++k) {
    Vec3 before = pos;
    Vec3 tryPos = pos + dh;
    grounded = resolve(tryPos) || grounded;
    Vec3 moved = tryPos - before;
    float wanted = length(dh), got = std::sqrt(moved.x * moved.x + moved.z * moved.z);
    if (wanted > 1e-6f && got < wanted * 0.5f) {
      Vec3 up = before + Vec3(0, stepHeight, 0) + dh;
      Vec3 n, hp;
      float depth;
      bool blocked = false;
      for (float off : {-halfHeight + 0.01f, 0.0f, halfHeight})
        if (sphereVsWorld(up + Vec3(0, off, 0), radius * 0.98f, n, depth, hp) && depth > 0 && n.y < 0.6f) blocked = true;
      if (!blocked) {
        tryPos = up;
        resolve(tryPos);
      }
    }
    pos = tryPos;
  }
  int vp = std::max(1, static_cast<int>(std::ceil(std::fabs(move.y) / (radius * 0.25f))));
  for (int k = 0; k < vp; ++k) {
    pos.y += move.y / vp;
    if (resolve(pos)) grounded = true;
  }
  return grounded;
}

}  // namespace loop::physics
