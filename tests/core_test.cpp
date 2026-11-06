#include <atomic>

#include "engine/core/image.h"
#include "engine/core/jobs.h"
#include "engine/core/json.h"
#include "engine/core/math.h"
#include "engine/core/meter.h"
#include "engine/core/random.h"
#include "tests/test.h"

using namespace loop;

TEST(math_quat_rotate) {
  Quat q = Quat::axisAngle({0, 1, 0}, kPi / 2);
  Vec3 v = q.rotate({1, 0, 0});
  CHECK_NEAR(v.x, 0, 1e-5);
  CHECK_NEAR(v.z, -1, 1e-5);
  Quat f = Quat::fromTo({1, 0, 0}, {0, 0, 1});
  Vec3 w = f.rotate({1, 0, 0});
  CHECK_NEAR(w.z, 1, 1e-5);
}

TEST(math_matrix_inverse) {
  Mat4 m = Mat4::trs({1, 2, 3}, Quat::euler(0.3f, 0.5f, -0.2f), {2, 1, 0.5f});
  Mat4 i = m * m.inverse();
  for (int c = 0; c < 4; ++c)
    for (int r = 0; r < 4; ++r) CHECK_NEAR(i.m[c][r], c == r ? 1 : 0, 1e-4);
  Vec3 p = m.point({1, 1, 1});
  Vec3 back = m.inverse().point(p);
  CHECK_NEAR(back.x, 1, 1e-4);
  CHECK_NEAR(back.y, 1, 1e-4);
}

TEST(math_frustum) {
  Mat4 vp = Mat4::perspective(radians(60), 1, 0.1f, 100) * Mat4::lookAt({0, 0, 0}, {0, 0, -1}, {0, 1, 0});
  Frustum f = Frustum::fromViewProj(vp);
  CHECK(f.visible(AABB({-1, -1, -11}, {1, 1, -9})));
  CHECK(!f.visible(AABB({-1, -1, 9}, {1, 1, 11})));
  CHECK(!f.visible(AABB({50, -1, -11}, {52, 1, -9})));
}

TEST(math_aabb_ray) {
  AABB b({-1, -1, -1}, {1, 1, 1});
  Vec3 o{0, 0, -5}, d{0, 0, 1};
  Vec3 inv{1 / 1e-30f, 1 / 1e-30f, 1};
  CHECK_NEAR(intersect(b, o, inv, 0, 100), 4, 1e-5);
  CHECK(intersect(b, {3, 0, -5}, inv, 0, 100) >= kInf);
  (void)d;
}

TEST(random_uniform_mean) {
  Rng r(42);
  double s = 0;
  for (int i = 0; i < 100000; ++i) s += r.uniform();
  CHECK_NEAR(s / 100000, 0.5, 0.01);
}

TEST(zlib_roundtrip) {
  std::vector<uint8_t> data;
  Rng r(7);
  for (int i = 0; i < 50000; ++i) data.push_back(static_cast<uint8_t>(i % 97 < 50 ? i % 13 : r.next()));
  auto z = zlibCompress(data.data(), data.size());
  std::vector<uint8_t> back;
  CHECK(zlibDecompress(z.data(), z.size(), back));
  CHECK(back == data);
  CHECK(z.size() < data.size());
}

TEST(png_roundtrip) {
  std::vector<uint8_t> px(37 * 23 * 4);
  for (size_t i = 0; i < px.size(); ++i) px[i] = static_cast<uint8_t>((i * 7) ^ (i >> 3));
  auto png = encodePng(px.data(), 37, 23);
  std::vector<uint8_t> back;
  int w = 0, h = 0;
  CHECK(decodePng(png.data(), png.size(), back, w, h));
  CHECK(w == 37 && h == 23);
  CHECK(back == px);
}

TEST(json_roundtrip) {
  Json j;
  CHECK(Json::parse(R"({"a": [1, 2.5, "x\n", true, null], "b": {"c": -3e2}})", j));
  CHECK(j["a"].size() == 5);
  CHECK_NEAR(j["a"][1].number(), 2.5, 0);
  CHECK(j["a"][2].string() == "x\n");
  CHECK_NEAR(j["b"]["c"].number(), -300, 0);
  Json k;
  CHECK(Json::parse(j.dump(2), k));
  CHECK(k.dump() == j.dump());
  CHECK(!Json::parse("{\"a\": }", k));
}

TEST(jobs_parallel_for) {
  JobSystem js(4);
  for (int round = 0; round < 50; ++round) {
    std::atomic<long> sum{0};
    js.parallelFor(1000, [&](int i) { sum += i; }, 7);
    CHECK(sum == 999 * 1000 / 2);
  }
}

// The meter adds every timing of a part in a frame, keeps the last frame
// whole and eases its readings toward what the frames say.
TEST(meter_parts_add_up_and_settle) {
  Meter m;
  m.add(Part::kPhysics, 1.0);
  m.add(Part::kPhysics, 0.5);  // the fixed clock stepped twice
  m.add(Part::kDraw, 2.0);
  m.endFrame();
  CHECK_NEAR(m.lastMs(Part::kPhysics), 1.5, 1e-9);
  CHECK_NEAR(m.ms(Part::kPhysics), 1.5, 1e-9);  // the first frame is taken as it is
  CHECK_NEAR(m.totalMs(), 3.5, 1e-9);
  for (int i = 0; i < 300; ++i) m.add(Part::kPhysics, 4.0), m.endFrame();
  CHECK_NEAR(m.ms(Part::kPhysics), 4.0, 1e-3);  // settles on a steady load
  CHECK_NEAR(m.ms(Part::kDraw), 0.0, 1e-3);     // a part that stopped fades out
  // a coarse clock (whole milliseconds, 0 or 1) still averages to the truth
  Meter c;
  for (int i = 0; i < 3000; ++i) c.add(Part::kScript, i % 10 < 3 ? 1.0 : 0.0), c.endFrame();
  CHECK_NEAR(c.ms(Part::kScript), 0.3, 0.12);
  // a scope times its block
  Meter::get().reset();
  { MeterScope t(Part::kPeople); volatile double x = 0; for (int i = 0; i < 100000; ++i) x = x + i; }
  Meter::get().endFrame();
  CHECK(Meter::get().lastMs(Part::kPeople) > 0);
  CHECK(std::string(Meter::name(Part::kSend)) == "sending");
  // the whole frame is smoothed the same way, and a new scene starts afresh
  Meter f;
  f.endFrame(5000);  // a frame that loaded a scene, if it were counted
  f.reset();
  f.endFrame(3);
  CHECK_NEAR(f.frameMs(), 3, 1e-9);
}
