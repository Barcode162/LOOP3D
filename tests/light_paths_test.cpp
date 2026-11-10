// Light paths (phase 9, bidirectional): a bulb's light through glass, followed
// from the bulb (engine/render/pathtracer.h, RenderSettings::lightPaths).
#include "engine/geometry/mesh.h"
#include "engine/render/pathtracer.h"
#include "tests/test.h"

using namespace loop;

namespace {
// a bulb above a glass ball above a white floor
Scene ballOverFloor(bool glassBall) {
  Scene s;
  s.sky.kind = Sky::Kind::kColor;
  s.sky.color = Vec3(1);
  s.sky.colorNits = 0;
  Material white, glass;
  white.baseColor = Vec3(0.8f), white.roughness = 1, white.specular = 0;
  glass.baseColor = Vec3(1), glass.transmission = 1, glass.roughness = 0, glass.ior = 1.5f, glass.dispersion = 0;
  const int w = s.addMaterial(white), g = s.addMaterial(glass);
  s.add(s.addMesh(shapes::plane({6, 6})), w);
  if (glassBall) s.add(s.addMesh(shapes::sphere(0.3f, 64, 32)), g, Mat4::translation({0, 0.8f, 0}));
  Light l;
  l.position = {0, 2.0f, 0};
  l.lumens = 1000;
  l.radius = 0.01f;
  s.lights.push_back(l);
  s.camera.position = {0, 1.0f, 3.0f};  // low, from the side: the floor under the ball in view, the bulb not
  s.camera.target = {0, 0, 0};
  s.camera.fStop = 64;  // all sharp
  return s;
}
// the light on the floor in the middle (under the ball), and its brightest pixel there
void floorLight(const Image& im, double& sum, float& peak) {
  sum = 0, peak = 0;
  for (int y = im.height / 4; y < im.height * 3 / 4; ++y)
    for (int x = im.width / 4; x < im.width * 3 / 4; ++x) {
      const float v = luminance(im.at(x, y).xyz());
      sum += v, peak = std::max(peak, v);
    }
}
}  // namespace

TEST(light_paths_focus_a_bulb_through_glass) {
  RenderSettings rs;
  rs.width = 48, rs.height = 48, rs.samples = 64, rs.denoise = false, rs.clamp = -1, rs.maxBounces = 4;
  Scene withBall = ballOverFloor(true), none = ballOverFloor(false);
  PathTracer a, b;
  a.setScene(withBall), b.setScene(none);
  rs.lightPaths = true;
  RenderOutput on = a.render(rs);
  rs.lightPaths = false;
  RenderOutput off = a.render(rs);
  RenderOutput bare = b.render(rs);
  double sOn, sOff, sBare;
  float pOn, pOff, pBare;
  floorLight(on.color, sOn, pOn);
  floorLight(off.color, sOff, pOff);
  floorLight(bare.color, sBare, pBare);
  std::printf("    floor light: bare %.0f (peak %.1f), glass straight through %.0f (peak %.1f), glass with light paths %.0f (peak %.1f)\n", sBare,
              pBare, sOff, pOff, sOn, pOn);
  // the ball focuses: somewhere under it far brighter than the bulb alone makes the floor
  CHECK(pOn > 3 * pBare);
  CHECK(pOff < 1.5f * pBare);  // straight through: no focus
  // no light made or lost beyond what the glass reflects away (and the ball's
  // own picture over the floor): the middle within a quarter of straight-through
  CHECK(sOn > 0.75 * sOff && sOn < 1.25 * sOff);
}
