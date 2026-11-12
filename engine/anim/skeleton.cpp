#include "engine/anim/skeleton.h"

#include <algorithm>
#include <cmath>
#include <map>

#include "engine/anim/clip.h"

namespace loop::anim {

Quat quatFromAxes(Vec3 x, Vec3 y, Vec3 z) {
  // the matrix whose columns are the axes, as a quaternion (Shepperd's method: the largest term first)
  const float m00 = x.x, m01 = y.x, m02 = z.x, m10 = x.y, m11 = y.y, m12 = z.y, m20 = x.z, m21 = y.z, m22 = z.z;
  const float trace = m00 + m11 + m22;
  Quat q;
  if (trace > 0) {
    const float s = std::sqrt(trace + 1) * 2;
    q = {(m21 - m12) / s, (m02 - m20) / s, (m10 - m01) / s, 0.25f * s};
  } else if (m00 > m11 && m00 > m22) {
    const float s = std::sqrt(1 + m00 - m11 - m22) * 2;
    q = {0.25f * s, (m01 + m10) / s, (m02 + m20) / s, (m21 - m12) / s};
  } else if (m11 > m22) {
    const float s = std::sqrt(1 + m11 - m00 - m22) * 2;
    q = {(m01 + m10) / s, 0.25f * s, (m12 + m21) / s, (m02 - m20) / s};
  } else {
    const float s = std::sqrt(1 + m22 - m00 - m11) * 2;
    q = {(m02 + m20) / s, (m12 + m21) / s, 0.25f * s, (m10 - m01) / s};
  }
  return q.normalized();
}

void addMirrors(std::vector<BoneSpec>& specs) {
  auto isLeft = [](const std::string& n) { return n.rfind("left_", 0) == 0; };
  auto other = [](const std::string& n) { return "right_" + n.substr(5); };
  std::vector<BoneSpec> rights;
  for (const BoneSpec& s : specs) {
    if (!isLeft(s.name)) continue;
    BoneSpec r = s;
    r.name = other(s.name);
    if (isLeft(s.parent)) r.parent = other(s.parent);
    r.from.x = -s.from.x, r.to.x = -s.to.x, r.bends.x = -s.bends.x;
    // side and twist run the other way on the other side
    r.lo.y = -s.hi.y, r.hi.y = -s.lo.y;
    r.lo.z = -s.hi.z, r.hi.z = -s.lo.z;
    rights.push_back(r);
  }
  specs.insert(specs.end(), rights.begin(), rights.end());
}

bool buildSkeleton(const std::string& name, const std::vector<BoneSpec>& specs, Skeleton& out, std::string* why, int* line) {
  auto fail = [&](const BoneSpec& s, const std::string& what) {
    if (why) *why = what;
    if (line) *line = s.line;
    return false;
  };
  Skeleton sk;
  sk.name = name;
  std::map<std::string, int> index;
  std::vector<Transform> model;  // each bone's frame in the rest pose, in the body's own space
  for (const BoneSpec& s : specs) {
    if (s.name.empty()) return fail(s, "a bone needs a name");
    if (index.count(s.name)) return fail(s, "two bones are called \"" + s.name + "\"");
    int parent = -1;
    if (!s.parent.empty()) {
      auto p = index.find(s.parent);
      if (p == index.end()) {
        bool later = false;
        for (const BoneSpec& o : specs) later = later || o.name == s.parent;
        return fail(s, later ? "\"" + s.name + "\" hangs from \"" + s.parent + "\", which is written after it; a bone comes after the one it hangs from"
                             : "\"" + s.name + "\" hangs from \"" + s.parent + "\", and there is no bone of that name");
      }
      parent = p->second;
    }
    const Vec3 along = s.to - s.from;
    const float len = length(along);
    if (len < 1e-5f) return fail(s, "\"" + s.name + "\" has no length: its `to` is where its `from` is");
    // its frame: Y along the bone, Z the way its tip goes for a positive bend, X the hinge
    const Vec3 y = along / len;
    Vec3 z = s.bends - y * dot(s.bends, y);
    if (lengthSq(z) < 1e-8f) return fail(s, "\"" + s.name + "\" bends along its own length: `bends` must point across the bone");
    z = normalize(z);
    const Vec3 x = cross(y, z);
    Transform frame;
    frame.position = s.from, frame.rotation = quatFromAxes(x, y, z);
    Bone b;
    b.name = s.name, b.parent = parent, b.length = len, b.ring = s.ring, b.free = s.free;
    if (!s.free) {
      for (int k = 0; k < 3; ++k)
        if (s.lo[k] > s.hi[k]) return fail(s, "\"" + s.name + "\" has a limit the wrong way round: the least comes first, like [-10 deg, 150 deg]");
      const float bend = std::max(std::fabs(s.lo.x), std::fabs(s.hi.x)), side = std::max(std::fabs(s.lo.y), std::fabs(s.hi.y));
      if (bend > kMostSwing + 1e-4f || side > kMostSwing + 1e-4f || std::fabs(s.lo.z) > kPi || std::fabs(s.hi.z) > kPi)
        return fail(s, "\"" + s.name + "\" may turn further than a joint can be told to: bend and side up to 170 degrees, twist up to 180");
      b.lo = s.lo, b.hi = s.hi;
    }
    // relative to its parent's frame
    if (parent >= 0) {
      const Transform& pf = model[parent];
      const Quat inv = pf.rotation.conjugate();
      b.rest.position = inv.rotate(frame.position - pf.position);
      b.rest.rotation = (inv * frame.rotation).normalized();
    } else b.rest = frame;
    index[s.name] = static_cast<int>(sk.bones.size());
    sk.bones.push_back(b);
    model.push_back(frame);
  }
  // each left bone and its right know each other
  for (size_t i = 0; i < sk.bones.size(); ++i) {
    const std::string& n = sk.bones[i].name;
    if (n.rfind("left_", 0) != 0) continue;
    auto r = index.find("right_" + n.substr(5));
    if (r != index.end()) sk.bones[i].mirror = r->second, sk.bones[r->second].mirror = static_cast<int>(i);
  }
  out = std::move(sk);
  return true;
}

Vec3 heldToLimits(const Bone& b, Vec3 t) {
  if (b.free) return t;
  Vec3 o{std::clamp(t.x, b.lo.x, b.hi.x), std::clamp(t.y, b.lo.y, b.hi.y), std::clamp(t.z, b.lo.z, b.hi.z)};
  const float swing = std::sqrt(o.x * o.x + o.y * o.y);
  if (swing > kMostSwing) o.x *= kMostSwing / swing, o.y *= kMostSwing / swing;
  return o;
}

bool holdToLimits(const Skeleton& s, Pose& pose) {
  bool held = false;
  for (size_t i = 0; i < s.bones.size() && i < pose.local.size(); ++i) {
    const Bone& b = s.bones[i];
    if (b.free) continue;
    const Quat turn = b.rest.rotation.conjugate() * pose.local[i].rotation;
    const Vec3 now = bendSideTwistFromTurn(turn);
    const Vec3 kept = heldToLimits(b, now);
    if (std::fabs(kept.x - now.x) + std::fabs(kept.y - now.y) + std::fabs(kept.z - now.z) < 1e-6f) continue;
    pose.local[i].rotation = (b.rest.rotation * turnFromBendSideTwist(kept)).normalized();
    held = true;
  }
  return held;
}

float standingHeight(const Skeleton& s) {
  float top = 0;
  const std::vector<Transform> w = Pose::rest(s).world(s, Transform());
  for (size_t i = 0; i < s.bones.size(); ++i) top = std::max({top, w[i].position.y, w[i].apply({0, s.bones[i].length, 0}).y});
  return top;
}

}  // namespace loop::anim
