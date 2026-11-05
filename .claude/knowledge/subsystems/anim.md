# Animation (engine/anim/)

**Files:** `anim.h` (103), `anim.cpp` (253).

- `Skeleton` (bones with parent + rest `Transform`), `Pose` (local transforms;
  `world()`), `blend` (slerp/lerp), `Clip` (keyed tracks, loop), `Spring`
  (critically damped, `halfLife`), `solveTwoBone`, `solveFabrik`.
- `humanSkeleton(height)`: pelvis, spine, chest, neck, head, arms and legs.
- `Animator`: procedural person, no recorded motion: breathing, weight shift, gait
  that changes with speed (walk → run), planted feet on `ground(x,z)` (slopes,
  steps), arm swing, look-at with eased head, sit/stand at `seatHeight`, `lift`,
  `onFootstep(foot, at, strength)` callback.
- People are drawn as ~16 rigid shapes (capsules) attached to bones
  (`Stage::Person::parts`), not a skinned mesh.

**Tests (6):** two-bone reach, FABRIK lengths, spring no overshoot, clip
sample/blend, walk with planted feet, look-at.

**Gaps (NB):** skinned meshes (`anim.skinned`), faces, hair, steering around
obstacles / pathfinding (`anim.steering`), crowd LOD; also no clip import
(glTF), no animation state machine for scripts, no root motion.
