#include "engine/anim/anim.h"

#include "engine/core/settings.h"

#include <algorithm>

namespace loop::anim {

int Skeleton::add(const std::string& name, int parent, Vec3 offset) {
  Bone b;
  b.name = name, b.parent = parent;
  b.rest.position = offset;
  bones.push_back(b);
  return static_cast<int>(bones.size() - 1);
}

int Skeleton::find(const std::string& name) const {
  for (size_t i = 0; i < bones.size(); ++i)
    if (bones[i].name == name) return static_cast<int>(i);
  return -1;
}

Pose Pose::rest(const Skeleton& s) {
  Pose p;
  for (const Bone& b : s.bones) p.local.push_back(b.rest);
  return p;
}

std::vector<Transform> Pose::world(const Skeleton& s, const Transform& root) const {
  std::vector<Transform> w(s.bones.size());
  for (size_t i = 0; i < s.bones.size(); ++i) {
    int p = s.bones[i].parent;  // parents always come before children
    w[i] = (p < 0 ? root : w[p]) * local[i];
  }
  return w;
}

Pose blend(const Pose& a, const Pose& b, float t) {
  Pose o = a;
  for (size_t i = 0; i < o.local.size() && i < b.local.size(); ++i) {
    o.local[i].position = vlerp(a.local[i].position, b.local[i].position, t);
    o.local[i].rotation = slerp(a.local[i].rotation, b.local[i].rotation, t);
    o.local[i].scale = vlerp(a.local[i].scale, b.local[i].scale, t);
  }
  return o;
}

Pose Clip::sample(const Skeleton& s, float t) const {
  Pose p = Pose::rest(s);
  if (length > 0) t = loop ? t - std::floor(t / length) * length : clamp(t, 0.0f, length);
  for (size_t b = 0; b < tracks.size() && b < p.local.size(); ++b) {
    const auto& k = tracks[b];
    if (k.empty()) continue;
    size_t i = 0;
    while (i + 1 < k.size() && k[i + 1].time <= t) ++i;
    const Key& a = k[i];
    const Key& c = i + 1 < k.size() ? k[i + 1] : (loop ? k[0] : k[i]);
    float span = i + 1 < k.size() ? c.time - a.time : (loop ? length - a.time + c.time : 1);
    float u = span > 0 ? clamp((t - a.time) / span, 0.0f, 1.0f) : 0;
    u = u * u * (3 - 2 * u);  // eased between keys
    p.local[b].rotation = slerp(a.rotation, c.rotation, u);
    p.local[b].position = s.bones[b].rest.position + vlerp(a.offset, c.offset, u);
  }
  return p;
}

void Spring::update(Vec3 target, float halfLife, float dt) {
  // exact critically damped step (Holden, "Spring-It-On")
  float y = 4 * 0.69314718f / std::max(halfLife, 1e-5f) / 2;
  Vec3 j0 = value - target, j1 = velocity + j0 * y;
  float e = std::exp(-y * dt);
  value = target + (j0 + j1 * dt) * e;
  velocity = (velocity - j1 * (y * dt)) * e;
}

Vec3 solveTwoBone(Vec3 root, Vec3 mid, Vec3 end, Vec3 target, Vec3 pole, Vec3& newEnd) {
  float l1 = length(mid - root), l2 = length(end - mid);
  Vec3 toT = target - root;
  float d = clamp(length(toT), std::fabs(l1 - l2) + 1e-4f, (l1 + l2) * 0.9999f);
  Vec3 dir = length(toT) > 1e-6f ? normalize(toT) : Vec3(0, -1, 0);
  newEnd = root + dir * d;
  // law of cosines: how far along the line and out toward the pole the knee sits
  float a = (l1 * l1 - l2 * l2 + d * d) / (2 * d);
  float h = std::sqrt(std::max(0.0f, l1 * l1 - a * a));
  Vec3 bend = pole - root;
  bend -= dir * dot(bend, dir);
  if (lengthSq(bend) < 1e-8f) {
    Vec3 t, b;
    orthonormalBasis(dir, t, b);
    bend = t;
  }
  return root + dir * a + normalize(bend) * h;
}

void solveFabrik(std::vector<Vec3>& p, Vec3 target, int iterations) {
  if (p.size() < 2) return;
  std::vector<float> len(p.size() - 1);
  float total = 0;
  for (size_t i = 0; i + 1 < p.size(); ++i) total += len[i] = length(p[i + 1] - p[i]);
  Vec3 root = p[0];
  if (length(target - root) >= total) {  // out of reach: point straight at it
    Vec3 d = normalize(target - root);
    for (size_t i = 0; i + 1 < p.size(); ++i) p[i + 1] = p[i] + d * len[i];
    return;
  }
  for (int it = 0; it < iterations; ++it) {
    p.back() = target;
    for (int i = static_cast<int>(p.size()) - 2; i >= 0; --i) p[i] = p[i + 1] + normalize(p[i] - p[i + 1]) * len[i];
    p[0] = root;
    for (size_t i = 0; i + 1 < p.size(); ++i) p[i + 1] = p[i] + normalize(p[i + 1] - p[i]) * len[i];
    if (length(p.back() - target) < 1e-4f) break;
  }
}

// ---------------------------------------------------------------- people
Skeleton humanSkeleton(float H) {
  float k = H / 1.75f;
  Skeleton s;
  int pelvis = s.add("pelvis", -1, {0, 0.95f * k, 0});
  int spine = s.add("spine", pelvis, {0, 0.12f * k, 0});
  int chest = s.add("chest", spine, {0, 0.2f * k, 0});
  int neck = s.add("neck", chest, {0, 0.2f * k, 0});
  s.add("head", neck, {0, 0.1f * k, 0});
  for (int side = 0; side < 2; ++side) {
    float x = side == 0 ? -1.0f : 1.0f;
    const char* n = side == 0 ? "left" : "right";
    int sh = s.add(std::string(n) + " shoulder", chest, {x * 0.19f * k, 0.15f * k, 0});
    int el = s.add(std::string(n) + " elbow", sh, {0, -0.28f * k, 0});
    s.add(std::string(n) + " wrist", el, {0, -0.26f * k, 0});
    int hip = s.add(std::string(n) + " hip", pelvis, {x * 0.1f * k, -0.05f * k, 0});
    int knee = s.add(std::string(n) + " knee", hip, {0, -0.43f * k, 0});
    s.add(std::string(n) + " ankle", knee, {0, -0.42f * k, 0});
  }
  return s;
}

Animator::Animator(const Skeleton& s) : s_(s), pose_(Pose::rest(s)), rest_(Pose::rest(s)) {
  pelvis_ = s.find("pelvis"), spine_ = s.find("spine"), chest_ = s.find("chest"), neck_ = s.find("neck"), head_ = s.find("head");
  const char* names[3] = {"shoulder", "elbow", "wrist"};
  const char* legs[3] = {"hip", "knee", "ankle"};
  for (int i = 0; i < 3; ++i) {
    armL_[i] = s.find(std::string("left ") + names[i]), armR_[i] = s.find(std::string("right ") + names[i]);
    legL_[i] = s.find(std::string("left ") + legs[i]), legR_[i] = s.find(std::string("right ") + legs[i]);
  }
}

std::vector<Transform> Animator::world() const {
  Transform root;
  root.position = position;
  root.rotation = Quat::axisAngle({0, 1, 0}, -facing);
  return pose_.world(s_, root);
}

void Animator::update(float dt) {
  time_ += dt;
  float groundY = (ground ? ground(position.x, position.z) : 0) + lift;
  float targetSpeed = std::sqrt(velocity.x * velocity.x + velocity.z * velocity.z);
  const Config::Anim& cfg = config().anim;
  speed_ += (targetSpeed - speed_) * (1 - std::exp(-dt / std::max(cfg.blend, 0.01f)));
  sit_ += ((sitting ? 1.0f : 0.0f) - sit_) * (1 - std::exp(-3 * dt));
  breath_ += dt * kTwoPi / 4.2f;  // ~14 breaths a minute
  // face where you're going, smoothly
  if (targetSpeed > 0.2f) {
    float want = std::atan2(velocity.x, -velocity.z);
    float diff = std::remainder(want - facing, kTwoPi);
    facing += diff * (1 - std::exp(-8 * dt));
  }
  // the gait: stride grows with speed, so cadence doesn't run away
  float run = smoothstep(2.2f, 3.8f, speed_);
  float stride = lerp(0.7f, 1.6f, run) * (0.5f + 0.5f * smoothstep(0.0f, 1.2f, speed_));
  float walkAmt = smoothstep(0.05f, 0.6f, speed_) * (1 - sit_);
  phase_ += speed_ / std::max(stride * 2, 0.3f) * dt;
  float ph = phase_ * kTwoPi;

  pose_ = rest_;
  float k = s_.bones[pelvis_].rest.position.y / 0.95f;  // scale
  // ---- body: bob, sway, breath, lean into speed
  float bob = walkAmt * (std::cos(ph * 2) * lerp(0.025f, 0.05f, run)) * k;
  sway_.update(Vec3(walkAmt * std::sin(ph) * 0.03f * k, 0, 0), 0.08f, dt);
  float idle = 1 - walkAmt;
  float shift = idle * std::sin(time_ * 0.35f) * 0.015f * k;  // weight from foot to foot
  Transform& pel = pose_.local[pelvis_];
  pel.position = rest_.local[pelvis_].position + Vec3(sway_.value.x + shift, bob - sit_ * (0.95f * k - seatHeight), 0);
  pel.rotation = Quat::axisAngle({0, 1, 0}, walkAmt * std::sin(ph) * 0.12f) * Quat::axisAngle({0, 0, 1}, walkAmt * std::cos(ph) * 0.04f);
  float lean = walkAmt * lerp(0.05f, 0.18f, run);
  pose_.local[spine_].rotation = Quat::axisAngle({1, 0, 0}, -lean - sit_ * 0.05f) * Quat::axisAngle({0, 1, 0}, -walkAmt * std::sin(ph) * 0.15f);
  float br = cfg.breathing ? std::sin(breath_) : 0.0f;
  pose_.local[chest_].rotation = Quat::axisAngle({1, 0, 0}, br * 0.015f);
  pose_.local[chest_].scale = Vec3(1 + br * 0.008f, 1, 1 + br * 0.012f);
  // ---- arms swing against the legs; elbows bend more when running
  for (int side = 0; side < 2; ++side) {
    int* arm = side == 0 ? armL_ : armR_;
    float s = side == 0 ? 1.0f : -1.0f;
    float swing = cfg.armSwing ? walkAmt * std::sin(ph) * s * lerp(0.35f, 0.7f, run) : 0.0f;
    pose_.local[arm[0]].rotation = Quat::axisAngle({1, 0, 0}, swing) * Quat::axisAngle({0, 0, 1}, -s * (0.08f + idle * 0.02f * br));
    pose_.local[arm[1]].rotation = Quat::axisAngle({1, 0, 0}, -(0.15f + walkAmt * lerp(0.15f, 1.3f, run) + sit_ * 0.6f));
  }
  // ---- head looks, within a neck's reach, with a little lag
  if (looking) {
    auto w = world();
    Vec3 hp = w[head_].position;
    Vec3 want = normalize(lookAt - hp);
    headDir_ = normalize(vlerp(headDir_, want, 1 - std::exp(-5 * dt)));
  } else headDir_ = normalize(vlerp(headDir_, Vec3(std::sin(facing), 0, -std::cos(facing)), 1 - std::exp(-3 * dt)));
  {
    float yaw = std::remainder(std::atan2(headDir_.x, -headDir_.z) - facing, kTwoPi);
    float pitch = std::asin(clamp(headDir_.y, -1.0f, 1.0f));
    yaw = clamp(yaw, -1.3f, 1.3f), pitch = clamp(pitch, -0.7f, 0.6f);
    // the neck takes 40%, the head the rest
    pose_.local[neck_].rotation = Quat::axisAngle({0, 1, 0}, -yaw * 0.4f) * Quat::axisAngle({1, 0, 0}, pitch * 0.4f);
    pose_.local[head_].rotation = Quat::axisAngle({0, 1, 0}, -yaw * 0.6f) * Quat::axisAngle({1, 0, 0}, pitch * 0.6f);
  }
  // ---- legs: feet follow a stepping path and are planted with IK
  auto w = world();
  Vec3 fwd{std::sin(facing), 0, -std::cos(facing)};
  for (int side = 0; side < 2; ++side) {
    int* leg = side == 0 ? legL_ : legR_;
    float p = ph + (side == 0 ? 0 : kPi);
    float c = std::cos(p);
    // swing forward when the foot is lifted, slide back (planted) when down
    float liftAmt = std::max(0.0f, std::sin(p));
    float along = walkAmt * c * stride * 0.25f;
    Vec3 hip = w[leg[0]].position;
    Vec3 foot{hip.x + fwd.x * along, 0, hip.z + fwd.z * along};
    float gy = ground ? ground(foot.x, foot.z) + lift : groundY;
    foot.y = gy + 0.08f * k + walkAmt * liftAmt * lerp(0.08f, 0.18f, run) * k;
    if (sit_ > 0.01f) foot = vlerp(foot, Vec3(hip.x + fwd.x * 0.45f * k, gy + 0.08f * k, hip.z + fwd.z * 0.45f * k), sit_);
    Vec3 knee = w[leg[1]].position, ankle = w[leg[2]].position;
    Vec3 endP;
    Vec3 newKnee = solveTwoBone(hip, knee, ankle, foot, hip + fwd * 1.0f, endP);
    // turn the world positions back into the bones' rotations
    auto aim = [&](int bone, Vec3 from, Vec3 to, const Transform& parentWorld) {
      Vec3 restDir = normalize(s_.bones[bone + 1].rest.position);  // child offset direction
      Vec3 wantDir = parentWorld.rotation.conjugate().rotate(normalize(to - from));
      pose_.local[bone].rotation = Quat::fromTo(restDir, wantDir);
    };
    if (cfg.ik) {  // without IK the legs hang as they are
      int pel = s_.bones[leg[0]].parent;
      aim(leg[0], hip, newKnee, w[pel]);
      auto w2 = world();
      aim(leg[1], newKnee, endP, w2[leg[0]]);
    }
    // a foot lands as its phase passes the bottom
    float fp = std::fmod(phase_ + (side == 0 ? 0.0f : 0.5f), 1.0f);
    float prev = std::fmod(lastFootPhase_ + (side == 0 ? 0.0f : 0.5f), 1.0f);
    if (walkAmt > 0.3f && onFootstep && prev < 0.75f && fp >= 0.75f) onFootstep(side, Vec3(foot.x, gy, foot.z), clamp(speed_ / 3, 0.2f, 1.0f));
  }
  lastFootPhase_ = phase_;
  position += Vec3(velocity.x, 0, velocity.z) * dt;
  position.y = groundY;
}

}  // namespace loop::anim
