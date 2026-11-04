#include "engine/anim/anim.h"
#include "tests/test.h"

using namespace loop;
using namespace loop::anim;

TEST(anim_two_bone_ik_reaches) {
  Vec3 root{0, 1, 0}, mid{0, 0.5f, 0}, end{0, 0, 0};
  Vec3 target{0.3f, 0.3f, 0.2f}, newEnd;
  Vec3 knee = solveTwoBone(root, mid, end, target, {0, 0.5f, 1}, newEnd);
  CHECK_NEAR(length(newEnd - target), 0, 1e-4);
  CHECK_NEAR(length(knee - root), 0.5, 1e-4);
  CHECK_NEAR(length(newEnd - knee), 0.5, 1e-4);
  CHECK(knee.z > 0);  // bends toward the pole
  // out of reach: stretches toward it
  solveTwoBone(root, mid, end, {0, -5, 0}, {0, 0.5f, 1}, newEnd);
  CHECK_NEAR(newEnd.y, 0, 1e-3);
}

TEST(anim_fabrik_keeps_lengths) {
  std::vector<Vec3> p = {{0, 0, 0}, {0, 1, 0}, {0, 2, 0}, {0, 3, 0}};
  solveFabrik(p, {1.5f, 1.5f, 0.5f});
  CHECK_NEAR(length(p.back() - Vec3(1.5f, 1.5f, 0.5f)), 0, 1e-3);
  for (size_t i = 0; i + 1 < p.size(); ++i) CHECK_NEAR(length(p[i + 1] - p[i]), 1, 1e-3);
  CHECK(p[0] == Vec3(0, 0, 0));
}

TEST(anim_spring_settles_without_overshoot) {
  Spring s;
  float maxV = 0;
  for (int i = 0; i < 240; ++i) {
    s.update({1, 0, 0}, 0.1f, 1.0f / 60);
    maxV = std::max(maxV, s.value.x);
  }
  CHECK_NEAR(s.value.x, 1, 1e-3);
  CHECK(maxV <= 1.0001f);
}

TEST(anim_clip_samples_and_blends) {
  Skeleton s;
  s.add("root", -1, {0, 0, 0});
  s.add("arm", 0, {1, 0, 0});
  Clip c;
  c.length = 1;
  c.tracks.resize(2);
  c.tracks[1] = {{0, Quat()}, {0.5f, Quat::axisAngle({0, 0, 1}, kPi / 2)}};
  Pose p = c.sample(s, 0.5f);
  Vec3 tip = p.world(s, Transform())[1].apply({1, 0, 0});
  CHECK_NEAR(tip.y, 1, 1e-4);  // the arm sits at x = 1; turned 90°, its tip points up
  CHECK_NEAR(tip.x, 1, 1e-4);
  Pose half = blend(Pose::rest(s), p, 0.5f);
  CHECK(half.local[1].rotation.z > 0.1f);
}

TEST(anim_person_walks_with_planted_feet) {
  Skeleton s = humanSkeleton(1.8f);
  CHECK(s.bones.size() == 17);
  Animator a(s);
  int steps = 0;
  a.onFootstep = [&](int, Vec3, float) { ++steps; };
  a.velocity = {0, 0, -1.4f};  // a normal walk
  float lowestAnkle = 10, highestAnkle = -10;
  for (int i = 0; i < 600; ++i) {
    a.update(1.0f / 60);
    if (i > 120) {
      auto w = a.world();
      for (const char* n : {"left ankle", "right ankle"}) {
        float y = w[s.find(n)].position.y;
        lowestAnkle = std::min(lowestAnkle, y), highestAnkle = std::max(highestAnkle, y);
      }
      for (auto& t : w) CHECK(isFinite(t.position));
    }
  }
  CHECK_NEAR(a.position.z, -1.4 * 10, 0.5);
  CHECK(lowestAnkle > 0.0f && lowestAnkle < 0.15f);  // feet reach the ground
  CHECK(highestAnkle - lowestAnkle > 0.04f);        // and lift when stepping
  // about two steps per stride length; 14 m at ~0.7 m a step
  CHECK(steps > 12 && steps < 30);
  // standing still: breathing only, feet stay down
  a.velocity = Vec3(0);
  for (int i = 0; i < 240; ++i) a.update(1.0f / 60);
  auto w = a.world();
  CHECK(w[s.find("left ankle")].position.y < 0.15f);
}

TEST(anim_person_looks_at_things) {
  Skeleton s = humanSkeleton();
  Animator a(s);
  a.looking = true;
  a.lookAt = {3, 1.6f, 0};  // off to the right
  for (int i = 0; i < 180; ++i) a.update(1.0f / 60);
  auto w = a.world();
  Vec3 faceDir = w[s.find("head")].rotation.rotate({0, 0, -1});
  CHECK(faceDir.x > 0.5f);
}
