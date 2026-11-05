# Physics (engine/physics/)

**Files:** `physics.h` (154), `physics.cpp` (802).

- Bodies: sphere, box, capsule (`BodyDesc`; mass 0 = fixed). Semi-implicit Euler,
  fixed sub-steps at `stepRate` (setting `physics.rate`, 120) with an accumulator.
- Contacts: sequential impulses (Catto) with friction, warm starting from
  `previous_` contacts, speculative contacts and split-impulse position correction;
  `velocityIterations` (`physics.iterations`, 16), `positionIterations` 4.
- Static world: ground plane (`groundPlane`, `groundHeight`), height-map terrain
  (`setTerrain`, pointer into Stage's deque), static triangle meshes in a BVH.
- Sleeping below `sleepSpeed` for 0.5 s.
- Joints: distance (rope when `slack`), ball, hinge.
- Cloth: XPBD grid (stretch + bend links), pins, wind (`physics.wind`).
- `moveCharacter`: capsule slide-and-step (radius `physics.charRadius`, step
  `charStep`, slope `charSlope`); used by the player's walking.
- `raycast` against bodies and the static world.

**Tests (8):** free fall vs g, ball rests, friction stopping distance, box stack
stands, pendulum energy, mixed shapes on a mesh, cloth drapes, character steps up.

**Gaps:** no continuous collision (NB `physics.ccd`; fast things tunnel), no
convex hulls or compound shapes, no mesh-vs-mesh dynamic bodies, broad phase is a
one-axis sweep and prune (fine for hundreds, weak for thousands spread along x), no triggers /
collision events to scripts beyond impact sounds, no vehicles, no ragdolls.
