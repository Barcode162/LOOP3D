#pragma once
// The standard skeleton (docs/STUDIO.md, decisions 5 and 6). A skeleton is
// written down bone by bone: its name, the bone it hangs from, where it
// starts and ends in the rest pose (metres, the body standing at the middle
// of the world, facing -z, its left at -x), which way it bends, and how far
// it turns from rest as three angles a person can read: bend, side, twist.
// From that this file makes each bone's own frame (Y along the bone, X the
// hinge it bends about, so a positive bend carries its tip the way `bends`
// points), mirrors every left bone into its right, checks the whole, and
// holds a pose inside its limits.
#include <string>
#include <vector>

#include "engine/anim/anim.h"

namespace loop::anim {

// One bone as written.
struct BoneSpec {
  std::string name, parent;  // parent "": it hangs from nothing (the root)
  Vec3 from{0}, to{0, 0.1f, 0};  // where it starts and where its tip is, in the rest pose
  Vec3 bends{0, 0, -1};          // the way its tip goes for a positive bend
  int ring = 1;
  bool free = false;
  Vec3 lo{0}, hi{0};  // bend, side, twist: the least and the most (radians)
  int line = 0;       // where it was written (for a problem's line)
};

// The most swing (bend and side together) any limited bone is given: short of
// the half turn where three numbers stop telling one turn from another.
constexpr float kMostSwing = 170.0f * kPi / 180.0f;

// Adds the right twin of every bone whose name starts "left_": the same bone
// on the other side (x the other way). Its bend runs the same way; its side
// and twist run the other way, so a pose is mirrored by swapping left and
// right and writing side and twist with the other sign.
void addMirrors(std::vector<BoneSpec>& specs);

// Makes the skeleton. False, with the reason (and the bone's line in `line`),
// when a name is used twice, a bone hangs from one that comes later or is not
// there, a bone has no length, a limit is the wrong way round, or its swing
// could pass kMostSwing.
bool buildSkeleton(const std::string& name, const std::vector<BoneSpec>& specs, Skeleton& out, std::string* why = nullptr, int* line = nullptr);

// A bone's turn from rest (bend, side, twist) held inside its limits: each
// number to the nearest it is allowed, and the swing no further than
// kMostSwing. A free bone keeps its turn.
Vec3 heldToLimits(const Bone& b, Vec3 bendSideTwist);
// The same for every bone of a pose. True when something had to be held back.
bool holdToLimits(const Skeleton& s, Pose& pose);

// How tall the skeleton stands in its rest pose: from the ground to its highest bone tip.
float standingHeight(const Skeleton& s);
// The rotation whose axes are these (each of length 1, at right angles).
Quat quatFromAxes(Vec3 x, Vec3 y, Vec3 z);

}  // namespace loop::anim
