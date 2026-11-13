// The standard skeleton (docs/STUDIO.md, decisions 5 and 6; order T-B1): the
// human entry of the library read through the words `skeleton` and `bone`,
// its left and right, how tall it stands, and every joint held to its limits.
#include <cmath>
#include <set>

#include "engine/anim/clip.h"
#include "engine/anim/skeleton.h"
#include "engine/project/project.h"
#include "tests/test.h"

using namespace loop;
using namespace loop::anim;

namespace {
const float kDeg = kPi / 180.0f;

// A scene that brings the library's human in, as a project's scene does.
std::unique_ptr<looptest::SceneRun> withHuman(const std::string& rest) {
  std::vector<std::string> seen;
  return looptest::runScene(loop::expandUses("use \"human\"\n" + rest, "projects/samples/", seen));
}

// The library's human, read as a scene reads it.
const Skeleton* human(std::unique_ptr<looptest::SceneRun>& run) {
  run = withHuman("");
  auto f = run->stage.skeletons.find("human");
  return run->ok && f != run->stage.skeletons.end() ? &f->second : nullptr;
}

BoneSpec spec(const std::string& name, const std::string& parent, Vec3 from, Vec3 to, int line = 0) {
  BoneSpec b;
  b.name = name, b.parent = parent, b.from = from, b.to = to, b.line = line;
  return b;
}

// A bone's turn from rest, read back from a pose.
Vec3 turnOf(const Skeleton& s, const Pose& p, int i) {
  return bendSideTwistFromTurn(s.bones[i].rest.rotation.conjugate() * p.local[i].rotation);
}
}  // namespace

TEST(skeleton_human_has_67_bones_each_with_one_name) {
  std::unique_ptr<looptest::SceneRun> run;
  const Skeleton* s = human(run);
  CHECK(run->problems().empty());
  CHECK(s != nullptr);
  if (!s) return;
  // root, hips, 4 of the back, 2 of the neck, head, jaw, 2 eyes; an arm of 4
  // and a hand of 19, twice; a leg of 4, twice
  CHECK(s->bones.size() == 67);
  std::set<std::string> names;
  int left = 0, right = 0, free = 0;
  for (size_t i = 0; i < s->bones.size(); ++i) {
    const Bone& b = s->bones[i];
    names.insert(b.name);
    CHECK(b.parent < static_cast<int>(i));  // a bone comes after the one it hangs from
    CHECK(b.length > 0.005f);
    CHECK(b.ring == 1);
    left += b.name.rfind("left_", 0) == 0, right += b.name.rfind("right_", 0) == 0, free += b.free;
  }
  CHECK(names.size() == 67);
  CHECK(left == 28 && right == 28);
  CHECK(free == 2);  // the root and the hips; every other joint has its limits
  for (const char* n : {"root", "hips", "coccyx", "spine", "spine_2", "chest", "upper_chest", "neck", "neck_2", "head", "jaw", "left_eye", "left_collar",
                        "left_upper_arm", "left_forearm", "left_hand", "left_thumb_3", "left_index_0", "left_little_3", "right_middle_2",
                        "left_thigh", "left_shin", "left_foot", "right_toes"})
    CHECK(s->find(n) >= 0);
}

TEST(skeleton_every_left_bone_has_its_right) {
  std::unique_ptr<looptest::SceneRun> run;
  const Skeleton* s = human(run);
  CHECK(s != nullptr);
  if (!s) return;
  const std::vector<Transform> w = Pose::rest(*s).world(*s, Transform());
  for (size_t i = 0; i < s->bones.size(); ++i) {
    const Bone& b = s->bones[i];
    const bool sided = b.name.rfind("left_", 0) == 0 || b.name.rfind("right_", 0) == 0;
    CHECK(sided == (b.mirror >= 0));
    if (b.mirror < 0) {
      CHECK_NEAR(w[i].position.x, 0, 1e-5);  // what has no twin sits on the middle
      continue;
    }
    const Bone& o = s->bones[b.mirror];
    CHECK(o.mirror == static_cast<int>(i));
    CHECK(o.name.substr(o.name.find('_')) == b.name.substr(b.name.find('_')));
    // the same place and tip, on the other side
    const Vec3 tip = w[i].apply({0, b.length, 0}), otherTip = w[b.mirror].apply({0, o.length, 0});
    CHECK_NEAR(w[i].position.x, -w[b.mirror].position.x, 1e-4);
    CHECK_NEAR(w[i].position.y, w[b.mirror].position.y, 1e-4);
    CHECK_NEAR(w[i].position.z, w[b.mirror].position.z, 1e-4);
    CHECK_NEAR(tip.x, -otherTip.x, 1e-4);
    CHECK_NEAR(tip.y, otherTip.y, 1e-4);
    CHECK_NEAR(tip.z, otherTip.z, 1e-4);
    CHECK_NEAR(b.length, o.length, 1e-5);
    // bend runs the same way; side and twist the other way
    CHECK_NEAR(b.lo.x, o.lo.x, 1e-6);
    CHECK_NEAR(b.hi.x, o.hi.x, 1e-6);
    CHECK_NEAR(b.lo.y, -o.hi.y, 1e-6);
    CHECK_NEAR(b.hi.y, -o.lo.y, 1e-6);
    CHECK_NEAR(b.lo.z, -o.hi.z, 1e-6);
    CHECK_NEAR(b.hi.z, -o.lo.z, 1e-6);
  }
}

TEST(skeleton_rest_pose_stands_one_metre_seventy_five) {
  std::unique_ptr<looptest::SceneRun> run;
  const Skeleton* s = human(run);
  CHECK(s != nullptr);
  if (!s) return;
  CHECK_NEAR(standingHeight(*s), 1.75, 1e-3);
  // nothing below the floor, and the feet on it
  const std::vector<Transform> w = Pose::rest(*s).world(*s, Transform());
  float low = 10;
  for (size_t i = 0; i < s->bones.size(); ++i) low = std::min({low, w[i].position.y, w[i].apply({0, s->bones[i].length, 0}).y});
  CHECK(low >= -1e-4f && low < 0.03f);
  // a bone starts where the one it continues ends: the forearm at the upper arm's tip, the shin at the thigh's
  for (const char* n : {"left_forearm", "left_hand", "right_shin", "right_foot", "neck", "head", "left_index_2"}) {
    const int i = s->find(n);
    if (i < 0) continue;
    const Bone& p = s->bones[s->bones[i].parent];
    const Vec3 tip = w[s->bones[i].parent].apply({0, p.length, 0});
    CHECK_NEAR(length(tip - w[i].position), 0, 1e-4);
  }
  // arm span about the height (a little under: the arms hang 45 degrees down)
  const int lh = s->find("left_middle_3"), rh = s->find("right_middle_3");
  if (lh >= 0 && rh >= 0) CHECK(w[lh].position.x < -0.55f && w[rh].position.x > 0.55f);
}

TEST(skeleton_a_positive_bend_carries_the_tip_the_way_it_says) {
  std::unique_ptr<looptest::SceneRun> run;
  const Skeleton* s = human(run);
  CHECK(s != nullptr);
  if (!s) return;
  auto tipAfter = [&](const char* bone, Vec3 bst, const char* watch) {
    Pose p = Pose::rest(*s);
    const int i = s->find(bone), w = s->find(watch);
    p.local[i].rotation = (s->bones[i].rest.rotation * turnFromBendSideTwist(bst)).normalized();
    const std::vector<Transform> world = p.world(*s, Transform());
    return world[w].apply({0, s->bones[w].length, 0});
  };
  const Vec3 restL = tipAfter("left_forearm", {0, 0, 0}, "left_hand");
  // an elbow bent brings the hand forward (-z) on both sides
  const Vec3 l = tipAfter("left_forearm", {90 * kDeg, 0, 0}, "left_hand"), r = tipAfter("right_forearm", {90 * kDeg, 0, 0}, "right_hand");
  CHECK(l.z < restL.z - 0.2f);
  CHECK_NEAR(l.x, -r.x, 1e-4);
  CHECK_NEAR(l.y, r.y, 1e-4);
  CHECK_NEAR(l.z, r.z, 1e-4);
  // a knee bent carries the foot back (+z) and up
  const Vec3 footRest = tipAfter("left_shin", {0, 0, 0}, "left_shin"), foot = tipAfter("left_shin", {90 * kDeg, 0, 0}, "left_shin");
  CHECK(foot.z > footRest.z + 0.3f && foot.y > footRest.y + 0.3f);
  // the arm lifted to the side (a positive side on the left, the mirrored number on the right): both hands rise alike
  const Vec3 ul = tipAfter("left_upper_arm", {0, 45 * kDeg, 0}, "left_hand"), ur = tipAfter("right_upper_arm", {0, -45 * kDeg, 0}, "right_hand");
  CHECK(ul.y > restL.y + 0.3f);
  CHECK_NEAR(ul.x, -ur.x, 1e-4);
  CHECK_NEAR(ul.y, ur.y, 1e-4);
  // the jaw drops the chin; a finger curls towards the palm
  CHECK(tipAfter("jaw", {20 * kDeg, 0, 0}, "jaw").y < tipAfter("jaw", {0, 0, 0}, "jaw").y - 0.01f);
  const Vec3 curl = tipAfter("left_index_1", {80 * kDeg, 0, 0}, "left_index_3") - tipAfter("left_index_1", {0, 0, 0}, "left_index_3");
  CHECK(dot(curl, Vec3(0.7071f, -0.7071f, 0)) > 0.05f);  // the left palm faces down and in
}

TEST(skeleton_every_limit_holds_when_a_pose_is_pushed_past_it) {
  std::unique_ptr<looptest::SceneRun> run;
  const Skeleton* s = human(run);
  CHECK(s != nullptr);
  if (!s) return;
  // the rest pose is inside every limit: nothing is held back
  Pose rest = Pose::rest(*s);
  CHECK(!holdToLimits(*s, rest));
  const float slack = 0.02f * kDeg;
  int checked = 0;
  for (size_t i = 0; i < s->bones.size(); ++i) {
    const Bone& b = s->bones[i];
    if (b.free) continue;
    for (int k = 0; k < 3; ++k) {
      for (int end = 0; end < 2; ++end) {
        // 30 degrees past this limit, the other two numbers a third of the way along their range
        Vec3 ask;
        for (int j = 0; j < 3; ++j) ask[j] = b.lo[j] + (b.hi[j] - b.lo[j]) * 0.33f;
        ask[k] = end ? b.hi[k] + 30 * kDeg : b.lo[k] - 30 * kDeg;
        const Vec3 kept = heldToLimits(b, ask);
        for (int j = 0; j < 3; ++j) CHECK(kept[j] >= b.lo[j] - 1e-6f && kept[j] <= b.hi[j] + 1e-6f);
        CHECK_NEAR(kept[k], end ? b.hi[k] : b.lo[k], 1e-6);  // the nearest it is allowed, not somewhere else in the range
        // and through a whole pose: built as a rotation, held, read back
        if (std::fabs(ask.x) > 150 * kDeg || std::fabs(ask.y) > 150 * kDeg) continue;  // (no one turn says this ask)
        Pose p = Pose::rest(*s);
        p.local[i].rotation = (b.rest.rotation * turnFromBendSideTwist(ask)).normalized();
        CHECK(holdToLimits(*s, p));
        const Vec3 now = turnOf(*s, p, static_cast<int>(i));
        for (int j = 0; j < 3; ++j) CHECK(now[j] >= b.lo[j] - slack && now[j] <= b.hi[j] + slack);
        // every other bone is left as it was
        for (size_t o = 0; o < s->bones.size(); ++o)
          if (o != i) CHECK(std::fabs(dot(p.local[o].rotation, s->bones[o].rest.rotation)) > 1 - 1e-6f);
        ++checked;
      }
    }
  }
  CHECK(checked > 300);
  // the ones a body knows: an elbow never bends sideways and stops at 150; a knee stops at 140 and barely past straight
  const Bone& elbow = s->bones[s->find("left_forearm")];
  const Vec3 e = heldToLimits(elbow, {170 * kDeg, 40 * kDeg, 0});
  CHECK_NEAR(e.x, 150 * kDeg, 1e-5);
  CHECK_NEAR(e.y, 0, 1e-6);
  const Bone& knee = s->bones[s->find("right_shin")];
  CHECK_NEAR(heldToLimits(knee, {160 * kDeg, 0, 0}).x, 140 * kDeg, 1e-5);
  CHECK_NEAR(heldToLimits(knee, {-30 * kDeg, 20 * kDeg, 0}).x, -3 * kDeg, 1e-5);
  CHECK_NEAR(heldToLimits(knee, {-30 * kDeg, 20 * kDeg, 0}).y, 0, 1e-6);
}

TEST(skeleton_a_held_pose_is_the_nearest_allowed_never_a_snap) {
  std::unique_ptr<looptest::SceneRun> run;
  const Skeleton* s = human(run);
  CHECK(s != nullptr);
  if (!s) return;
  const int i = s->find("left_forearm");
  const Bone& b = s->bones[i];
  // inside its limits a turn is kept as it is
  const Vec3 inside{70 * kDeg, 0, -40 * kDeg};
  const Vec3 same = heldToLimits(b, inside);
  CHECK_NEAR(same.x, inside.x, 1e-7);
  CHECK_NEAR(same.z, inside.z, 1e-7);
  Pose p = Pose::rest(*s);
  p.local[i].rotation = (b.rest.rotation * turnFromBendSideTwist(inside)).normalized();
  const Quat before = p.local[i].rotation;
  CHECK(!holdToLimits(*s, p));
  CHECK(std::fabs(dot(before, p.local[i].rotation)) > 1 - 1e-7f);
  // pushed steadily through the limit and beyond, the elbow's tip moves a little each step and then stays: no jump
  Vec3 last{0};
  float biggest = 0;
  for (int step = 0; step <= 120; ++step) {
    const float bend = (100 + step * 0.5f) * kDeg;  // 100 to 160 degrees; the limit is 150
    Pose q = Pose::rest(*s);
    q.local[i].rotation = (b.rest.rotation * turnFromBendSideTwist({bend, 0, 0})).normalized();
    holdToLimits(*s, q);
    const Vec3 tip = q.world(*s, Transform())[i].apply({0, b.length, 0});
    if (step > 0) biggest = std::max(biggest, length(tip - last));
    last = tip;
    CHECK(turnOf(*s, q, i).x <= 150 * kDeg + 1e-3f);
  }
  CHECK(biggest < b.length * 0.5f * kDeg * 1.2f);  // never more than one half-degree step of the forearm
  // a free bone keeps any turn
  const Bone& hips = s->bones[s->find("hips")];
  CHECK_NEAR(heldToLimits(hips, {3, -2, 1}).x, 3, 1e-7);
  // the swing of a wide joint never reaches the half turn
  Bone wide;
  wide.free = false, wide.lo = Vec3(-170 * kDeg, -170 * kDeg, 0), wide.hi = Vec3(170 * kDeg, 170 * kDeg, 0);
  const Vec3 w = heldToLimits(wide, {160 * kDeg, 160 * kDeg, 0});
  CHECK(std::sqrt(w.x * w.x + w.y * w.y) <= kMostSwing + 1e-5f);
  CHECK_NEAR(w.x, w.y, 1e-6);  // pulled straight back towards rest
}

TEST(skeleton_a_wrong_one_is_refused_by_name) {
  Skeleton out;
  std::string why;
  int line = 0;
  const Vec3 a{0, 0, 0}, b{0, 0.3f, 0}, c{0, 0.6f, 0};
  // a name used twice
  CHECK(!buildSkeleton("t", {spec("arm", "", a, b, 1), spec("arm", "arm", b, c, 2)}, out, &why, &line));
  CHECK(why.find("two bones are called \"arm\"") != std::string::npos && line == 2);
  // a parent that comes later
  CHECK(!buildSkeleton("t", {spec("hand", "arm", b, c, 4), spec("arm", "", a, b, 5)}, out, &why, &line));
  CHECK(why.find("written after it") != std::string::npos && why.find("\"hand\"") != std::string::npos && line == 4);
  // a parent that is not there
  CHECK(!buildSkeleton("t", {spec("arm", "", a, b, 1), spec("hand", "amr", b, c, 7)}, out, &why, &line));
  CHECK(why.find("no bone of that name") != std::string::npos && line == 7);
  // a limit the wrong way round
  BoneSpec bad = spec("elbow", "", a, b, 9);
  bad.lo.x = 150 * kDeg, bad.hi.x = -5 * kDeg;
  CHECK(!buildSkeleton("t", {bad}, out, &why, &line));
  CHECK(why.find("\"elbow\" has a limit the wrong way round") != std::string::npos && line == 9);
  // a turn no three numbers can say
  BoneSpec far = spec("shoulder", "", a, b, 3);
  far.lo.y = -175 * kDeg, far.hi.y = 175 * kDeg;
  CHECK(!buildSkeleton("t", {far}, out, &why, &line));
  CHECK(why.find("\"shoulder\" may turn further") != std::string::npos);
  // no length, no name, bending along itself
  CHECK(!buildSkeleton("t", {spec("dot", "", a, a, 6)}, out, &why, &line));
  CHECK(why.find("\"dot\" has no length") != std::string::npos && line == 6);
  CHECK(!buildSkeleton("t", {spec("", "", a, b)}, out, &why, &line));
  BoneSpec along = spec("stick", "", a, b);
  along.bends = Vec3(0, 1, 0);
  CHECK(!buildSkeleton("t", {along}, out, &why, &line));
  CHECK(why.find("bends along its own length") != std::string::npos);
  // and a right one is taken, the bone hanging where it was written
  std::vector<BoneSpec> good = {spec("arm", "", a, b), spec("left_hand", "arm", Vec3(-0.1f, 0.3f, 0), Vec3(-0.1f, 0.5f, 0))};
  addMirrors(good);
  CHECK(buildSkeleton("t", good, out, &why, &line));
  CHECK(out.bones.size() == 3 && out.name == "t");
  const std::vector<Transform> w = Pose::rest(out).world(out, Transform());
  CHECK_NEAR(w[2].position.x, 0.1, 1e-6);
  CHECK_NEAR(w[2].apply({0, out.bones[2].length, 0}).y, 0.5, 1e-6);
  CHECK(out.bones[1].mirror == 2 && out.bones[2].mirror == 1 && out.bones[0].mirror == -1);
}

TEST(skeleton_words_read_bones_and_say_what_is_wrong) {
  auto r = looptest::runScene(R"(
skeleton "arm"
  bone "upper", from: vec(0, 1, 0), to: vec(0, 0.7, 0), bend: [-60 deg, 135 deg], side: [-60 deg, 135 deg], twist: [-90 deg, 70 deg]
  bone "left_fore", parent: "upper", from: vec(-0.1, 0.7, 0), to: vec(-0.1, 0.45, 0), bend: [-5 deg, 150 deg]
let shown = bones("arm", at: vec(2, 0, 0))
)");
  CHECK(r->ok);
  CHECK(r->problems().empty());
  CHECK(r->stage.skeletons.count("arm") == 1);
  if (!r->stage.skeletons.count("arm")) return;
  const Skeleton& s = r->stage.skeletons["arm"];
  CHECK(s.bones.size() == 3);  // left_fore got its right_fore
  CHECK_NEAR(s.bones[1].hi.x, 150 * kDeg, 1e-5);
  CHECK_NEAR(s.bones[1].hi.y, 0, 1e-9);  // a turn not named does not happen
  CHECK(r->stage.scene.instances.size() == 3);  // one slim shape a bone
  CHECK(r->stage.placed[1].name == "left_fore");
  // without mirroring a left bone stays alone
  CHECK(looptest::runScene("skeleton \"a\", mirror: no\n  bone \"left_x\", from: vec(0, 0, 0), to: vec(0, 1, 0)\n")->stage.skeletons["a"].bones.size() == 1);
  // what is wrong is said in plain words
  CHECK(looptest::runScene("bone \"x\", from: vec(0, 0, 0), to: vec(0, 1, 0)\n")->hasProblem("a bone belongs to a skeleton"));
  CHECK(looptest::runScene("skeleton \"a\"\n  bone \"x\", from: vec(0, 0, 0), to: vec(0, 1, 0), bend: 90 deg\n")->hasProblem("least and most"));
  CHECK(looptest::runScene("skeleton \"a\"\n  bone \"x\", from: vec(0, 0, 0), to: vec(0, 1, 0), twsit: [0, 1]\n")->hasProblem("did you mean `twist`"));
  CHECK(looptest::runScene("skeleton \"a\"\n  bone \"x\", from: vec(0, 0, 0), to: vec(0, 1, 0), bend: [2, 1]\n")->hasProblem("the wrong way round"));
  CHECK(looptest::runScene("skeleton \"a\"\n  bone \"x\", from: vec(0, 0, 0), to: vec(0, 1, 0)\n  bone \"x\", from: vec(0, 0, 0), to: vec(0, 1, 0)\n")->hasProblem("two bones are called"));
  CHECK(looptest::runScene("bones(\"humna\")\n")->hasProblem("no skeleton called"));
}

TEST(skeleton_bones_shows_the_human_at_a_height) {
  auto r = withHuman("bones(\"human\", at: vec(1, 0, -2), height: 1.4)\n");
  CHECK(r->ok);
  CHECK(r->problems().empty());
  CHECK(r->stage.scene.instances.size() == 67);
  float top = 0;
  for (const auto& p : r->stage.placed) top = std::max(top, p.position.y);
  CHECK(top > 1.2f && top < 1.4f);  // the head's bone starts under the crown
}
