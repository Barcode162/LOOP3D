// Skin (docs/STUDIO.md, order T-S1): points that follow bones, pulls worked
// out from the bones, a mesh bent by a pose, and film bending a skin before
// it builds its tree.
#include <cmath>
#include <fstream>
#include <sstream>

#include "engine/anim/clip.h"
#include "engine/anim/skeleton.h"
#include "engine/anim/skin.h"
#include "engine/render/scene_bvh.h"
#include "tests/test.h"

using namespace loop;
using namespace loop::anim;

namespace {
const float kDeg = kPi / 180.0f;

// A closed tube standing on the ground: radius r, `tall` high, a ring of points every `step`.
Mesh tubeMesh(float r, float tall, float step = 0.01f, Vec3 at = Vec3(0)) {
  std::vector<Vec2> outline = {{0, 0}};
  for (float h = 0; h < tall + step * 0.5f; h += step) outline.push_back({r, std::min(h, tall)});
  outline.push_back({0, tall});
  Mesh m = shapes::lathe(outline, 32);
  m.transform(Mat4::translation(at));
  return m;
}

// Bones standing one on the next, straight up from `at`: each `len` long, bending towards +x.
Skeleton chain(int count, float len, Vec3 at = Vec3(0), const std::string& prefix = "b") {
  std::vector<BoneSpec> specs;
  for (int i = 0; i < count; ++i) {
    BoneSpec b;
    b.name = prefix + std::to_string(i), b.parent = i ? prefix + std::to_string(i - 1) : "";
    b.from = at + Vec3(0, len * i, 0), b.to = at + Vec3(0, len * (i + 1), 0);
    b.bends = {1, 0, 0}, b.free = true;
    specs.push_back(b);
  }
  Skeleton s;
  buildSkeleton("chain", specs, s);
  return s;
}

Pose bentAt(const Skeleton& s, int bone, float bend) {
  Pose p = Pose::rest(s);
  p.local[bone].rotation = s.bones[bone].rest.rotation * turnFromBendSideTwist({bend, 0, 0});
  return p;
}
}  // namespace

TEST(skin_pulls_sum_to_one_and_each_bone_holds_its_own_middle) {
  Mesh m = tubeMesh(0.05f, 0.6f);
  Skeleton s = chain(2, 0.3f);
  weigh(m, Mat4(), s, Pose::rest(s).world(s, Transform()));
  CHECK(m.pulls.size() == m.positions.size());
  for (size_t i = 0; i < m.positions.size(); ++i) {
    const Pulls& p = m.pulls[i];
    float sum = 0, of[2] = {0, 0};
    for (int k = 0; k < 8; ++k) {
      sum += p.pull[k];
      CHECK(p.pull[k] >= 0);
      if (k) CHECK(p.pull[k] <= p.pull[k - 1]);  // strongest first
      if (p.pull[k] > 0) of[p.bone[k]] += p.pull[k];
    }
    CHECK_NEAR(sum, 1, 1e-5);
    const float h = m.positions[i].y;
    if (h < 0.1f) CHECK(of[0] > 0.99f);   // far from the joint, one bone holds a point alone
    if (h > 0.5f) CHECK(of[1] > 0.99f);
    if (std::fabs(h - 0.3f) < 0.002f) CHECK_NEAR(of[0], 0.5, 1e-3);  // at the joint, both pull alike
    // and the pull passes smoothly from one to the other: more of the upper bone the higher the point
    // each as the straight distance to it says, beside a bone 5 cm away.
    if (h > 0.1f && h < 0.5f) {
      const float other = std::exp(-(std::hypot(0.05f, h - 0.3f) - 0.05f) / (0.5f * 0.05f));
      CHECK_NEAR(of[h < 0.3f ? 1 : 0], other / (1 + other), 0.01);
    }
  }
}

TEST(skin_a_bone_never_pulls_across_a_gap) {
  // an arm hanging beside a body: a thin tube 2 cm from a thick one, each with its own bone.
  // The body's side facing the arm is nearer the arm's bone than its own, and still follows only its own.
  Mesh m = tubeMesh(0.05f, 0.5f);
  const size_t armPoints = m.positions.size();
  m.append(tubeMesh(0.15f, 0.5f, 0.01f, {0.22f, 0, 0}));
  std::vector<BoneSpec> specs(2);
  specs[0].name = "arm", specs[0].from = {0, 0, 0}, specs[0].to = {0, 0.5f, 0}, specs[0].free = true;
  specs[1].name = "body", specs[1].from = {0.22f, 0, 0}, specs[1].to = {0.22f, 0.5f, 0}, specs[1].free = true;
  Skeleton s;
  CHECK(buildSkeleton("pair", specs, s));
  weigh(m, Mat4(), s, Pose::rest(s).world(s, Transform()));
  int nearerTheOther = 0;
  for (size_t i = 0; i < m.positions.size(); ++i) {
    const int mine = i < armPoints ? 0 : 1;
    CHECK(m.pulls[i].bone[0] == mine);
    CHECK_NEAR(m.pulls[i].pull[0], 1, 1e-5);
    const Vec3 p = m.positions[i];
    if (mine == 1 && std::hypot(p.x, p.z) < std::hypot(p.x - 0.22f, p.z)) ++nearerTheOther;
  }
  CHECK(nearerTheOther > 100);  // the test means something: many points were nearer the wrong bone
}

TEST(skin_rest_pose_leaves_every_point_where_it_was) {
  Mesh m = tubeMesh(0.05f, 0.9f);
  Skeleton s = chain(3, 0.3f);
  // the skeleton stands somewhere else, turned and smaller, and the mesh is placed to match
  Transform root;
  root.position = {2, 1, -3}, root.rotation = Quat::axisAngle({0, 1, 0}, 0.7f), root.scale = Vec3(0.8f);
  const Mat4 place = root.matrix();
  const std::vector<Transform> rest = Pose::rest(s).world(s, root);
  weigh(m, place, s, rest);
  Mesh out;
  bend(m, bendMatrices(place, rest, rest), out);
  CHECK(out.positions.size() == m.positions.size());
  for (size_t i = 0; i < m.positions.size(); ++i) {
    CHECK_NEAR(length(out.positions[i] - m.positions[i]), 0, 1e-5);
    CHECK_NEAR(length(out.normals[i] - m.normals[i]), 0, 1e-4);
  }
}

TEST(skin_no_point_moves_farther_than_its_bones_do) {
  Mesh m = tubeMesh(0.05f, 0.9f);
  Skeleton s = chain(3, 0.3f);
  const std::vector<Transform> rest = Pose::rest(s).world(s, Transform());
  weigh(m, Mat4(), s, rest);
  Pose pose = bentAt(s, 1, 80 * kDeg);
  pose.local[2].rotation = s.bones[2].rest.rotation * turnFromBendSideTwist({-40 * kDeg, 20 * kDeg, 60 * kDeg});
  const std::vector<Mat4> bones = bendMatrices(Mat4(), rest, pose.world(s, Transform()));
  Mesh out;
  bend(m, bones, out);
  float most = 0;
  for (size_t i = 0; i < m.positions.size(); ++i) {
    float farthest = 0;  // how far the bone that carries it farthest would carry it alone
    for (int k = 0; k < 8 && m.pulls[i].pull[k] > 0; ++k)
      farthest = std::max(farthest, length(bones[m.pulls[i].bone[k]].point(m.positions[i]) - m.positions[i]));
    const float moved = length(out.positions[i] - m.positions[i]);
    CHECK(moved <= farthest + 1e-5f);
    most = std::max(most, moved);
    CHECK_NEAR(length(out.normals[i]), 1, 1e-4);
  }
  CHECK(most > 0.2f);  // and it did bend
  // the foot of the tube follows the first bone, which did not move
  for (size_t i = 0; i < m.positions.size(); ++i)
    if (m.positions[i].y < 0.1f) CHECK_NEAR(length(out.positions[i] - m.positions[i]), 0, 1e-5);
}

TEST(skin_a_tube_bent_90_degrees_keeps_this_much_of_its_volume) {
  // The figure the helper bones (T-B2) must beat: linear blend alone, a tube
  // of radius 5 cm bent to a right angle at a joint, pulls as `weigh` makes them.
  Mesh m = tubeMesh(0.05f, 0.6f);
  Skeleton s = chain(2, 0.3f);
  const std::vector<Transform> rest = Pose::rest(s).world(s, Transform());
  weigh(m, Mat4(), s, rest);
  Mesh out;
  bend(m, bendMatrices(Mat4(), rest, bentAt(s, 1, 90 * kDeg).world(s, Transform())), out);
  const float kept = std::fabs(out.volume()) / std::fabs(m.volume());
  std::printf("     a tube bent 90 degrees keeps %.4f of its volume\n", kept);
  CHECK_NEAR(kept, 0.929, 0.004);
  // no gap: every triangle is still there
  CHECK(out.indices == m.indices);
}

TEST(skin_film_bends_a_skin_before_building_its_tree) {
  Scene scene;
  Mesh m = tubeMesh(0.05f, 0.6f);
  Skeleton s = chain(2, 0.3f);
  const std::vector<Transform> rest = Pose::rest(s).world(s, Transform());
  weigh(m, Mat4(), s, rest);
  scene.addMaterial(Material());
  scene.add(scene.addMesh(m), 0);
  scene.add(0, 0, Mat4::translation({3, 0, 0}));  // the same mesh, no skin: stays straight
  scene.instances[0].bend = 0;
  scene.bends.push_back(bendMatrices(Mat4(), rest, bentAt(s, 1, 90 * kDeg).world(s, Transform())));
  SceneBvh tree;
  tree.build(scene);
  // the bent one: nothing left above the joint, and its top half now lies along +x
  CHECK(tree.closest({{-1, 0.5f, 0}, {1, 0, 0}}).inst != 0);
  SceneHit side = tree.closest({{0.2f, 1, 0}, {0, -1, 0}});
  CHECK(side.valid() && side.inst == 0);
  CHECK_NEAR(1 - side.t, 0.35, 0.01);  // the tube's top side, 5 cm above the joint's height
  // the plain copy is as it was
  SceneHit straight = tree.closest({{2, 0.5f, 0}, {1, 0, 0}});
  CHECK(straight.valid() && straight.inst == 1);
  CHECK_NEAR(straight.t, 0.95, 1e-3);
  // and the scene's own mesh was not touched
  CHECK_NEAR(scene.meshes[0].bounds().hi.y, 0.6, 1e-5);
}

TEST(skin_words_skin_and_pose_bend_a_thing_and_hold_its_limits) {
  auto run = looptest::runScene(R"(
skeleton "arm"
  bone "upper", from: vec(0, 0, 0), to: vec(0, 0.3, 0), bends: vec(1, 0, 0)
  bone "fore", parent: "upper", from: vec(0, 0.3, 0), to: vec(0, 0.6, 0), bends: vec(1, 0, 0), bend: [-5 deg, 150 deg]
let other = lathe([vec(0, 0), vec(0.05, 0), vec(0.05, 0.3), vec(0.05, 0.6), vec(0, 0.6)], at: vec(5, 1, 0))
let a = lathe([vec(0, 0), vec(0.05, 0), vec(0.05, 0.3), vec(0.05, 0.6), vec(0, 0.6)], at: vec(2, 1, 0))
skin(a, "arm")
pose(a, "fore", bend: 400 deg)
)");
  CHECK(run->problems().empty());
  CHECK(run->ok);
  const Scene& sc = run->stage.scene;
  CHECK(sc.instances.size() == 2 && sc.bends.size() == 1);
  if (sc.instances.size() != 2 || sc.bends.size() != 1) return;
  // the skinned thing has a shape of its own; the other keeps the shared one, without pulls
  CHECK(sc.instances[0].mesh != sc.instances[1].mesh);
  CHECK(sc.meshes[sc.instances[0].mesh].pulls.empty());
  CHECK(sc.instances[1].bend == 0 && !sc.meshes[sc.instances[1].mesh].pulls.empty());
  // held at the elbow's most: 150 degrees, so the forearm's tip is where that puts it
  const Vec3 tip = sc.bends[0][1].point({0, 0.6f, 0});
  CHECK_NEAR(tip.x, 0.3f * std::sin(150 * kDeg), 1e-4);
  CHECK_NEAR(tip.y, 0.3f + 0.3f * std::cos(150 * kDeg), 1e-4);

  CHECK(looptest::runScene("let b = box(size: 1)\nskin(b, \"nobody\")")->hasProblem("there's no skeleton called"));
  CHECK(looptest::runScene("let b = box(size: 1)\npose(b, \"fore\", bend: 1)")->hasProblem("a pose needs a thing with a skin"));
}

TEST(skin_the_arm_sample_reads_and_bends) {
  std::ifstream f("projects/samples/arm.loop");
  std::stringstream text;
  text << f.rdbuf();
  auto run = looptest::runScene(text.str());
  CHECK(run->problems().empty());
  CHECK(run->stage.skinned.size() == 3);
  CHECK(run->stage.scene.bends.size() == 3);
}
