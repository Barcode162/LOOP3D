#pragma once
// Animation: skeletons and poses that blend (clips of keys: clip.h), limbs that reach
// (two-bone and chain IK), springs for follow-through, and a procedural
// animator for people that needs no recorded motion: breathing and weight
// shifts at rest, a walk and a run that change with speed, feet that plant
// on the ground (and on slopes and steps), arms that swing against the
// legs, a head that turns to look, sitting down and standing up. Every
// change eases, so nothing snaps.
#include <functional>
#include <string>
#include <vector>

#include "engine/core/math.h"

namespace loop::anim {

struct Bone {
  std::string name;
  int parent = -1;
  Transform rest;  // relative to the parent
};

struct Skeleton {
  std::vector<Bone> bones;
  int add(const std::string& name, int parent, Vec3 offset);
  int find(const std::string& name) const;
};

// Local (parent-relative) transforms, one per bone.
struct Pose {
  std::vector<Transform> local;
  static Pose rest(const Skeleton& s);
  // World transforms of every bone, given where the skeleton stands.
  std::vector<Transform> world(const Skeleton& s, const Transform& root) const;
};
// Mixes two poses (rotations slerped, positions lerped).
Pose blend(const Pose& a, const Pose& b, float t);

// A critically damped spring: reaches its target as fast as possible
// without wobbling past it; `halfLife` is the time to close half the gap.
struct Spring {
  Vec3 value{0}, velocity{0};
  void update(Vec3 target, float halfLife, float dt);
};

// Two-bone IK (hip-knee-ankle, shoulder-elbow-wrist): the rotations that
// put the end at `target`, bending toward `pole`. Works on world positions;
// returns the new middle joint position (the end goes to target, clamped
// to the limb's reach).
Vec3 solveTwoBone(Vec3 root, Vec3 mid, Vec3 end, Vec3 target, Vec3 pole, Vec3& newEnd);
// FABRIK (Aristidou & Lasenby): moves a chain of points so its tip reaches
// `target`, keeping each link's length; the root stays put.
void solveFabrik(std::vector<Vec3>& points, Vec3 target, int iterations = 12);

// ---------------------------------------------------------------- people
// A person's skeleton (about 1.75 m scaled to `height`): pelvis, spine,
// chest, neck, head, and arms and legs on both sides.
Skeleton humanSkeleton(float height = 1.75f);

class Animator {
 public:
  explicit Animator(const Skeleton& s);
  // What the person is doing (all eased): how fast they move (m/s) and
  // where, where they look, whether they sit.
  Vec3 position{0};
  float facing = 0;            // yaw (rad), toward -z at 0
  Vec3 velocity{0};
  Vec3 lookAt{0};
  bool looking = false;
  bool sitting = false;
  float seatHeight = 0.45f;
  std::function<float(float x, float z)> ground;  // ground height (null = 0)
  float lift = 0;  // stand this far above the ground (a ledge, a dais, the air)
  // Called when a foot lands: which foot (0 left, 1 right), where, how hard.
  std::function<void(int foot, Vec3 at, float strength)> onFootstep;

  void update(float dt);
  const Pose& pose() const { return pose_; }
  std::vector<Transform> world() const;
  float gaitPhase() const { return phase_; }

 private:
  const Skeleton& s_;
  Pose pose_, rest_;
  float phase_ = 0, speed_ = 0, sit_ = 0, breath_ = 0, time_ = 0, lastFootPhase_ = 0;
  Vec3 headDir_{0, 0, -1};
  Spring sway_;
  int pelvis_, spine_, chest_, neck_, head_, armL_[3], armR_[3], legL_[3], legR_[3];
};

}  // namespace loop::anim
