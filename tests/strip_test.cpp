// The word `strip`: one glowing rectangle and the lamps that stand for it.
#include <cmath>
#include <cstdio>

#include "engine/geometry/mesh.h"
#include "engine/render/pathtracer.h"
#include "engine/render/scene.h"
#include "tests/test.h"

using namespace loop;

TEST(strip_makes_one_glowing_thing_and_lamps_that_stand_for_it) {
  auto r = runScene("let s = strip(vec(-1, 2, 0), vec(1, 2, 0), lumens: 300)\n");
  CHECK(r->ok);
  const Scene& sc = r->stage.scene;
  int glowing = -1, count = 0;
  for (size_t i = 0; i < sc.instances.size(); ++i)
    if (sc.materials[sc.instances[i].material].emission.x > 0) glowing = static_cast<int>(i), ++count;
  CHECK(count == 1);
  CHECK(sc.instances.size() == 2);  // the glow and its channel
  CHECK(sc.lights.size() == 8);
  double total = 0;
  for (auto& l : sc.lights) {
    CHECK(l.kind == Light::Kind::kRect);
    CHECK(l.standsFor == glowing);
    CHECK(!l.castShadows);
    CHECK(std::fabs(l.direction.y + 1) < 1e-4f);
    CHECK(std::fabs(l.size.x * l.size.y - 0.25f * 0.012f) < 1e-5f);
    total += l.lumens;
  }
  CHECK_NEAR(total, 600, 0.01);
  // the glowing rectangle's normal faces `facing`
  const Mat4& xf = sc.instances[glowing].transform;
  Vec3 n = xf.point({0, 1, 0}) - xf.point({0, 0, 0});
  CHECK(n.y < -0.999f);
}

TEST(strip_says_plain_problems) {
  CHECK(runScene("strip(vec(0, 1, 0), vec(0, 1, 0))\n")->hasProblem("different places"));
  CHECK(runScene("strip(vec(0, 1, 0), vec(0, 3, 0))\n")->hasProblem("along its own length"));
  CHECK(runScene("strip(vec(0, 1, 0), vec(1, 1, 0), lumens: -1)\n")->hasProblem("below 0"));
  CHECK(runScene("strip(vec(0, 1, 0), vec(1, 1, 0), shadows: yes)\n")->stage.scene.lights[0].castShadows);
}

namespace {
double floorLit(bool lampsAlone) {
  auto r = runScene("let s = strip(vec(-1, 0.5, 0), vec(1, 0.5, 0), lumens: 300)\n");
  CHECK(r->ok);
  Scene s = r->stage.scene;
  s.sky.kind = Sky::Kind::kColor, s.sky.color = Vec3(1), s.sky.colorNits = 0;
  Material white;
  white.baseColor = Vec3(0.8f), white.roughness = 1, white.specular = 0;
  s.add(s.addMesh(shapes::plane({8, 8})), s.addMaterial(white));
  if (lampsAlone) {
    for (auto& m : s.materials) m.emission = Vec3(0);
    for (auto& l : s.lights) l.standsFor = -1;
  }
  s.camera.position = {0, 0.3f, 3.0f};
  s.camera.target = {0, -0.3f, 0};
  s.camera.fStop = 64;
  RenderSettings rs;
  rs.width = 48, rs.height = 48, rs.samples = 256, rs.denoise = false, rs.clamp = -1, rs.maxBounces = 3;
  PathTracer pt;
  pt.setScene(s);
  RenderOutput o = pt.render(rs);
  double sum = 0;
  for (int y = o.color.height / 4; y < o.color.height * 3 / 4; ++y)
    for (int x = o.color.width / 4; x < o.color.width * 3 / 4; ++x) sum += luminance(o.color.at(x, y).xyz());
  return sum;
}
}  // namespace

TEST(film_lights_a_floor_from_a_strips_glow_as_the_lamps_would) {
  const double glow = floorLit(false), lamps = floorLit(true);
  std::printf("    floor light: the strip's glow %.1f, the lamps alone %.1f\n", glow, lamps);
  CHECK(glow > 0);
  CHECK(std::fabs(glow - lamps) < 0.10 * lamps);
}
