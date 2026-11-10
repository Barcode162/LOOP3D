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

namespace {
// a white floor under a square of light 0.4 m across, 1 m up, 600 lumens: as a
// glowing thing (with or without a lamp standing for it), or as a panel lamp
Scene squareOverFloor(bool glowing, bool lamp, bool standIn) {
  Scene s;
  s.sky.kind = Sky::Kind::kColor;
  s.sky.color = Vec3(1);
  s.sky.colorNits = 0;
  Material white, glow;
  white.baseColor = Vec3(0.8f), white.roughness = 1, white.specular = 0;
  glow.baseColor = Vec3(0), glow.emission = Vec3(glowNits(600, 0.4f * 0.4f));
  const int w = s.addMaterial(white), g = s.addMaterial(glow);
  s.add(s.addMesh(shapes::plane({6, 6})), w);
  int thing = -1;
  if (glowing) {
    s.add(s.addMesh(shapes::box({0.4f, 0.002f, 0.4f})), g, Mat4::translation({0, 1.001f, 0}));
    thing = static_cast<int>(s.instances.size()) - 1;
  }
  if (lamp) {
    Light l;
    l.kind = Light::Kind::kRect;
    l.position = {0, 1, 0}, l.direction = {0, -1, 0}, l.size = {0.4f, 0.4f};
    l.lumens = 600;
    if (standIn) l.standsFor = thing;
    s.lights.push_back(l);
  }
  s.camera.position = {0, 0.6f, 3.0f};  // below the square: only the floor's light is measured
  s.camera.target = {0, -0.6f, 0};
  s.camera.fStop = 64;
  return s;
}
double floorSum(const Scene& s) {
  RenderSettings rs;
  rs.width = 48, rs.height = 48, rs.samples = 256, rs.denoise = false, rs.clamp = -1, rs.maxBounces = 3;
  PathTracer pt;
  pt.setScene(s);
  RenderOutput o = pt.render(rs);
  double sum;
  float peak;
  floorLight(o.color, sum, peak);
  return sum;
}
}  // namespace

TEST(a_lamp_that_stands_for_a_glow_is_counted_once) {
  const double glowAlone = floorSum(squareOverFloor(true, false, false));
  const double lampAlone = floorSum(squareOverFloor(false, true, false));
  const double stoodFor = floorSum(squareOverFloor(true, true, true));
  const double both = floorSum(squareOverFloor(true, true, false));
  std::printf("    floor light: the glow %.1f, the panel lamp %.1f, the glow with its stand-in %.1f, both counted %.1f\n", glowAlone, lampAlone,
              stoodFor, both);
  CHECK(glowAlone > 0);
  CHECK(std::fabs(glowAlone - lampAlone) < 0.05 * lampAlone);  // glowNits: the glow gives the lamp's light
  CHECK(std::fabs(stoodFor - glowAlone) < 0.05 * glowAlone);   // the stand-in adds nothing in film
  CHECK(both > 1.8 * glowAlone);                                // an ordinary lamp beside the glow still adds
}

TEST(a_probe_skips_a_glow_its_lamp_stands_for) {
  // a light-cache probe 0.5 m under the square, looking straight up at it
  Scene plain = squareOverFloor(true, false, false), stood = squareOverFloor(true, true, true);
  PathTracer a, b;
  a.setScene(plain), b.setScene(stood);
  const float seen = luminance(a.probeLight({0, 0.5f, 0}, {0, 1, 0}, 1, 2, nullptr));
  const float skipped = luminance(b.probeLight({0, 0.5f, 0}, {0, 1, 0}, 1, 2, nullptr));
  // looking down, both still gather the floor's bounced light from the glow
  double downA = 0, downB = 0;
  for (uint32_t i = 0; i < 400; ++i) {
    downA += luminance(a.probeLight({0, 0.5f, 0}, {0, -1, 0}, i, 2, nullptr));
    downB += luminance(b.probeLight({0, 0.5f, 0}, {0, -1, 0}, i, 2, nullptr));
  }
  std::printf("    a probe looking at the square: %.0f nits plain, %.0f stood for; at the floor: %.1f and %.1f\n", seen, skipped, downA / 400,
              downB / 400);
  CHECK(seen > 1000);
  CHECK(skipped < 1);
  CHECK(downB > 0.8 * downA && downB < 1.25 * downA);
}
