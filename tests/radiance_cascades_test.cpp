// Radiance cascades (engine/render/radiance_cascades.h): the phase 9 experiment.
#include "engine/geometry/mesh.h"
#include "engine/render/light_cache.h"
#include "engine/render/pathtracer.h"
#include "engine/render/radiance_cascades.h"
#include "tests/test.h"

using namespace loop;

TEST(cascades_directions_cover_the_sphere) {
  for (int n : {2, 4, 8}) {
    double total = 0;
    for (int v = 0; v < n; ++v)
      for (int u = 0; u < n; ++u) total += octSolidAngle(u, v, n);
    CHECK_NEAR(total, 4 * kPi, 4 * kPi * 0.01);
  }
}

TEST(cascades_open_sky_is_pi_times_its_light) {
  // nothing to meet: every level's rays go through, and the top one's reach the sky
  Scene s;
  s.sky.kind = Sky::Kind::kColor;
  s.sky.color = Vec3(1);
  s.sky.colorNits = 1000;
  PathTracer pt;
  pt.setScene(s);
  RadianceCascades rc = buildCascades(pt, AABB(Vec3(0), Vec3(2)), 1.0f, 3, 4, 1.0f);
  CHECK(rc.levels.size() == 3 && rc.levels[2].dirs == 16);
  for (Vec3 n : {Vec3(0, 1, 0), Vec3(1, 0, 0), normalize(Vec3(1, -1, 1))})
    CHECK_NEAR(luminance(rc.irradiance(Vec3(0.7f, 1.2f, 0.4f), n)), kPi * 1000, kPi * 1000 * 0.03);
}

TEST(cascades_a_wall_near_blocks_the_far_light) {
  // a big black block from 0.95 m to +x: the levels meet it, so the sky beyond is
  // not added that way (a thin wall leaks: a coarse level's probe can sit inside it,
  // or its stretch start beyond it; the experiment's finding, docs/PIPELINE.md)
  Scene s;
  s.sky.kind = Sky::Kind::kColor;
  s.sky.color = Vec3(1);
  s.sky.colorNits = 1000;
  Material black;
  black.baseColor = Vec3(0);
  black.specular = 0;
  int m = s.addMaterial(black);
  s.add(s.addMesh(shapes::box({4, 40, 40})), m, Mat4::translation({2.95f, 0, 0}));
  PathTracer pt;
  pt.setScene(s);
  RadianceCascades rc = buildCascades(pt, AABB(Vec3(0, 0, -0.5f), Vec3(0.5f, 0.5f, 0.5f)), 0.25f, 3, 4, 0.5f);
  const float toWall = luminance(rc.irradiance(Vec3(0.25f, 0.25f, 0), Vec3(1, 0, 0)));
  const float away = luminance(rc.irradiance(Vec3(0.25f, 0.25f, 0), Vec3(-1, 0, 0)));
  CHECK(toWall < away * 0.1f);
  CHECK_NEAR(away, kPi * 1000, kPi * 1000 * 0.05);
}
