#include "engine/anim/clip.h"
#include "tests/test.h"

#include <algorithm>

using namespace loop;
using namespace loop::anim;

namespace {
Key& key(Channel& c, double t, float a, float b = 0, float d = 0) {
  float v[4] = {a, b, d, 0};
  return c.set(t, v);
}
float at(const Clip& clip, const Channel& c, double t, int n = 0) {
  float v[4] = {0, 0, 0, 0};
  clip.sample(c, t, v);
  return v[n];
}
}  // namespace

TEST(clip_two_keys_start_gently_and_land_softly) {
  Clip clip;
  Channel& c = clip.channel("at", 3);
  key(c, 0, -2, 0.5f, 0);
  key(c, 2, 2, 0.5f, 0);
  CHECK_NEAR(at(clip, c, 0), -2, 1e-6);
  CHECK_NEAR(at(clip, c, 2), 2, 1e-6);
  CHECK_NEAR(at(clip, c, 1), 0, 1e-6);       // half way at the middle moment
  CHECK_NEAR(at(clip, c, 1, 1), 0.5, 1e-6);  // what does not change stays
  CHECK(at(clip, c, 0.2) < -2 + 0.4 * 0.5);  // slower than a straight line at the start
  CHECK(at(clip, c, 1.8) > 2 - 0.4 * 0.5);   // and at the end
  CHECK_NEAR(at(clip, c, -5), -2, 1e-6);     // before the first key and after the last: their values
  CHECK_NEAR(at(clip, c, 9), 2, 1e-6);
}

TEST(clip_smooth_through_five_keys_never_overshoots) {
  Clip clip;
  Channel& c = clip.channel("height", 1);
  const float values[5] = {0, 1, 1.05f, 4, 3.9f};
  const double times[5] = {0, 0.5, 0.6, 2, 3};
  for (int i = 0; i < 5; ++i) key(c, times[i], values[i]);
  float before = values[0];
  for (int i = 0; i + 1 < 5; ++i) {
    const float lo = std::min(values[i], values[i + 1]), hi = std::max(values[i], values[i + 1]);
    for (int s = 0; s <= 200; ++s) {
      const double t = times[i] + (times[i + 1] - times[i]) * s / 200;
      const float v = at(clip, c, t);
      CHECK(v >= lo - 1e-5f && v <= hi + 1e-5f);                       // inside its two keys' values
      if (i < 3) CHECK(v >= before - 1e-5f);                          // and rising all the way up
      before = v;
    }
  }
  // it passes through each key
  for (int i = 0; i < 5; ++i) CHECK_NEAR(at(clip, c, times[i]), values[i], 1e-5);
}

TEST(clip_the_ways_to_arrive) {
  Clip clip;
  Channel& c = clip.channel("x", 1);
  key(c, 0, 0);
  key(c, 1, 10).way = Way::kHold;
  key(c, 2, 20).way = Way::kLine;
  Key& bounce = key(c, 3, 30);
  bounce.way = Way::kEase, bounce.ease = Ease::kBounce;
  CHECK_NEAR(at(clip, c, 0.99), 0, 1e-6);   // held, then it jumps
  CHECK_NEAR(at(clip, c, 1), 10, 1e-6);
  CHECK_NEAR(at(clip, c, 1.25), 12.5, 1e-5);  // straight
  CHECK_NEAR(at(clip, c, 2.5), 20 + 10 * ease(Ease::kBounce, 0.5), 1e-4);
}

TEST(clip_free_slopes_are_obeyed) {
  Clip clip;
  Channel& c = clip.channel("x", 1);
  Key& a = key(c, 0, 0);
  a.free = true, a.out[0] = 6;  // leaves fast
  Key& b = key(c, 1, 1);
  b.free = true, b.in[0] = 0;
  CHECK(at(clip, c, 0.5) > 1);  // so it overshoots, as asked
  const double h = 1e-4;
  CHECK_NEAR((at(clip, c, h) - at(clip, c, 0)) / h, 6, 0.02);
}

TEST(clip_a_loop_meets_itself_with_no_jolt) {
  // closed by hand: a key at the start and its twin at the end
  Clip clip;
  clip.repeat = Repeat::kLoop;
  Channel& c = clip.channel("x", 1);
  key(c, 0, 1);
  key(c, 0.5, 3);
  key(c, 1.5, 0);
  key(c, 2, 1);
  const double h = 1e-4;
  const float before = (at(clip, c, 2 - h) - at(clip, c, 2 - 2 * h)) / static_cast<float>(h);
  const float after = (at(clip, c, 2 + 2 * h) - at(clip, c, 2 + h)) / static_cast<float>(h);
  CHECK(std::fabs(before) > 0.5f);          // it is moving at the join
  CHECK_NEAR(before, after, 0.02);          // at the same speed on both sides
  CHECK_NEAR(at(clip, c, 2.5), at(clip, c, 0.5), 1e-5);  // the second time round is the first

  // open: the last key travels round to the first
  Clip open;
  open.repeat = Repeat::kLoop, open.length = 2;
  Channel& o = open.channel("x", 1);
  key(o, 0.5, 3);
  key(o, 1.5, 0);
  const float b2 = (at(open, o, 2 - h) - at(open, o, 2 - 2 * h)) / static_cast<float>(h);
  const float a2 = (at(open, o, 2 + 2 * h) - at(open, o, 2 + h)) / static_cast<float>(h);
  CHECK(std::fabs(b2) > 0.5f);
  CHECK_NEAR(b2, a2, 0.02);
  CHECK_NEAR(at(open, o, 0), 1.5, 1e-4);  // half way between its two keys, round the join
}

TEST(clip_once_loop_and_back_and_forth) {
  Clip clip;
  Channel& c = clip.channel("x", 1);
  key(c, 0, 0);
  key(c, 2, 4).way = Way::kLine;
  CHECK_NEAR(at(clip, c, 3), 4, 1e-6);  // once: stays at the end
  clip.repeat = Repeat::kBackAndForth;
  CHECK_NEAR(at(clip, c, 3), 2, 1e-5);  // on its way back
  CHECK_NEAR(at(clip, c, 4), 0, 1e-5);
  CHECK_NEAR(at(clip, c, 5), 2, 1e-5);
  CHECK_NEAR(clip.duration(), 2, 1e-9);
  clip.length = 3;                      // a length longer than its last key holds the end
  clip.repeat = Repeat::kOnce;
  CHECK_NEAR(clip.duration(), 3, 1e-9);
}

TEST(clip_keys_are_set_moved_and_removed) {
  Clip clip;
  Channel& c = clip.channel("x", 1);
  key(c, 1, 5);
  key(c, 0, 2);
  key(c, 1, 7);  // a key already there takes the new value
  CHECK(c.keys.size() == 2);
  CHECK(c.keys[0].time == 0 && c.keys[1].time == 1);
  CHECK_NEAR(c.keys[1].v[0], 7, 1e-6);
  CHECK(c.remove(0) && !c.remove(0.5));
  CHECK(c.keys.size() == 1);
  CHECK_NEAR(clip.snapped(0.033), 1.0 / 30, 1e-12);  // written with three decimals, back on its frame
  CHECK_NEAR(clip.snapped(0.05), 0.05, 1e-12);       // far from a frame: left alone
}

TEST(clip_bend_side_twist_there_and_back_over_the_whole_range) {
  // every turn a joint can make (and far more): swing up to 175 degrees any
  // way, twist up to 179 degrees; back to the same three numbers within 0.01 degree
  const float deg = kPi / 180;
  float worst = 0;
  for (int swing = 0; swing <= 175; swing += 5)
    for (int dir = 0; dir < 360; dir += 15)
      for (int twist = -179; twist <= 179; twist += 11) {
        const Vec3 in{swing * deg * std::cos(dir * deg), swing * deg * std::sin(dir * deg), twist * deg};
        const Vec3 out = bendSideTwistFromTurn(turnFromBendSideTwist(in));
        worst = std::max({worst, std::fabs(out.x - in.x), std::fabs(out.y - in.y), std::fabs(out.z - in.z)});
      }
  CHECK(worst < 0.01f * deg);
  // what the three numbers mean: bend is about X, side about Z, twist about the bone's length
  const Vec3 along{0, 1, 0};
  Vec3 bent = turnFromBendSideTwist({90 * deg, 0, 0}).rotate(along);
  CHECK_NEAR(bent.z, 1, 1e-5);
  Vec3 sideways = turnFromBendSideTwist({0, 90 * deg, 0}).rotate(along);
  CHECK_NEAR(sideways.x, -1, 1e-5);
  Vec3 twisted = turnFromBendSideTwist({0, 0, 90 * deg}).rotate(along);
  CHECK_NEAR(twisted.y, 1, 1e-5);  // a twist leaves the bone where it points
  // a twist past a half turn is found again when asked for the one nearest
  const Vec3 far = bendSideTwistFromTurn(turnFromBendSideTwist({0.3f, 0.1f, 400 * deg}), 390 * deg);
  CHECK_NEAR(far.z, 400 * deg, 1e-4);
}

TEST(clip_poses_a_skeleton) {
  Skeleton s;
  s.add("root", -1, {0, 0, 0});
  s.add("arm", 0, {1, 0, 0});
  Clip clip;
  Channel& c = clip.channel("arm", 3, true);
  key(c, 0, 0, 0, 0);
  key(c, 0.5, 0, kPi / 2, 0);
  Pose p = clip.pose(s, 0.5);
  Vec3 tip = p.world(s, Transform())[1].apply({1, 0, 0});
  CHECK_NEAR(tip.y, 1, 1e-4);  // the arm sits at x = 1; turned 90 degrees to the side, its tip points up
  CHECK_NEAR(tip.x, 1, 1e-4);
  Pose half = blend(Pose::rest(s), p, 0.5f);
  CHECK(half.local[1].rotation.z > 0.1f);
  Channel& lift = clip.channel("arm_at", 3);
  key(lift, 0, 0, 0.25f, 0);
  CHECK_NEAR(clip.pose(s, 0).local[1].position.y, 0.25, 1e-6);
}

TEST(clip_writes_itself_as_script) {
  Clip clip;
  clip.repeat = Repeat::kLoop, clip.length = 2;
  Channel& at = clip.channel("at", 3);
  Channel& turn = clip.channel("turn", 1, true);
  key(at, 0, -2, 0.5f, 0);
  key(turn, 0, 0);
  key(at, 2, 2, 0.5f, 0);
  key(turn, 2, kPi / 2).way = Way::kLine;
  Key& free = key(at, 1, 0, 1.25f, 0);
  free.free = true, free.in[1] = 0.5f, free.out[1] = -0.5f;
  const std::string text = clip.script("crate");
  CHECK(text ==
        "clip crate, length: 2 s, loop: yes\n"
        "  key 0 s, at: vec(-2, 0.5, 0), turn: 0\n"
        "  key 1 s, at: vec(0, 1.25, 0), in: vec(0, 0.5, 0), out: vec(0, -0.5, 0)\n"
        "  key 2 s, at: vec(2, 0.5, 0)\n"
        "  key 2 s, turn: 90 deg, ease: \"line\"\n");
  CHECK(plainNumber(1.5) == "1.5" && plainNumber(-0.0001) == "0" && plainNumber(0.0333333) == "0.033");
}
