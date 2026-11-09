// The light cache (engine/render/light_cache.h): probes baked by the path
// tracer, holding the light that isn't straight from a lamp or the sun.
#include "engine/geometry/mesh.h"
#include "engine/render/light_cache.h"
#include "engine/render/pathtracer.h"
#include "tests/test.h"

using namespace loop;

namespace {
Scene skyOnly(float nits) {
  Scene s;
  s.sky.kind = Sky::Kind::kColor;
  s.sky.color = Vec3(1);
  s.sky.colorNits = nits;
  return s;
}
// A closed room (six slabs, 4 x 3 x 4 inside) round the origin, its walls
// `wall`, under a bright sky; a bulb inside when `lumens` > 0.
Scene room(Material wall, float lumens) {
  Scene s = skyOnly(5000);
  int m = s.addMaterial(wall);
  const float t = 0.2f;
  auto slab = [&](Vec3 size, Vec3 at) { s.add(s.addMesh(shapes::box(size)), m, Mat4::translation(at)); };
  slab({4 + 2 * t, t, 4 + 2 * t}, {0, -t / 2, 0});     // floor
  slab({4 + 2 * t, t, 4 + 2 * t}, {0, 3 + t / 2, 0});  // ceiling
  slab({t, 3, 4}, {-2 - t / 2, 1.5f, 0});
  slab({t, 3, 4}, {2 + t / 2, 1.5f, 0});
  slab({4 + 2 * t, 3, t}, {0, 1.5f, -2 - t / 2});
  slab({4 + 2 * t, 3, t}, {0, 1.5f, 2 + t / 2});
  if (lumens > 0) {
    Light l;
    l.position = {0, 2.5f, 0};
    l.lumens = lumens;
    l.radius = 0.05f;
    s.lights.push_back(l);
  }
  return s;
}
// the average of a probe's six faces (the light on a surface facing any way, roughly)
float mean(const LightCache& c, int i) {
  float t = 0;
  for (const Vec3& f : c.face[static_cast<size_t>(i)]) t += luminance(f);
  return t / 6;
}
LightCache oneProbe(Vec3 at) {
  // a 2 x 2 x 2 cache whose first probe sits at `at`
  LightCache c = layOutLightCache(AABB(at, at + Vec3(1)), 8, 1.0f);
  c.origin = at;
  return c;
}
}  // namespace

TEST(light_cache_open_sky_is_pi_times_its_light) {
  Scene s = skyOnly(1000);
  PathTracer pt;
  pt.setScene(s);
  LightCache c = oneProbe({0, 0, 0});
  bakeProbe(c, 0, pt, 256);
  CHECK(c.state[0] == LightCache::kBaked);
  // a surface facing any way under a uniform sky all round: E = pi L
  for (const Vec3& f : c.face[0]) CHECK_NEAR(luminance(f), kPi * 1000, kPi * 1000 * 0.03);
}

TEST(light_cache_closed_room_keeps_the_sky_out) {
  Material white;
  white.baseColor = Vec3(0.7f);
  white.roughness = 1, white.specular = 0;
  Scene dark = room(white, 0);
  PathTracer pt;
  pt.setScene(dark);
  LightCache c = oneProbe({0, 1.5f, 0});
  bakeProbe(c, 0, pt, 128);
  CHECK(c.state[0] == LightCache::kBaked);
  CHECK(mean(c, 0) < 1e-3f);  // sealed: no sky at all (the old sky light lit it with 5000 nits)
  // with a bulb inside, its light comes back off the walls (not the bulb itself: the renderer draws that)
  Scene lit = room(white, 800);
  pt.setScene(lit);
  bakeProbe(c, 0, pt, 256);
  const float E = mean(c, 0);
  CHECK(E > 1.0f && E < 60.0f);
}

TEST(light_cache_red_wall_tints_its_side) {
  Material white, red;
  white.baseColor = Vec3(0.7f), red.baseColor = Vec3(0.8f, 0.05f, 0.05f);
  white.roughness = red.roughness = 1, white.specular = red.specular = 0;
  Scene s = skyOnly(1000);
  int w = s.addMaterial(white), r = s.addMaterial(red);
  s.add(s.addMesh(shapes::plane({40, 40})), w);                                   // a white ground
  s.add(s.addMesh(shapes::box({0.2f, 6, 6})), r, Mat4::translation({1.1f, 3, 0}));  // a red wall to +x
  PathTracer pt;
  pt.setScene(s);
  LightCache c = oneProbe({0, 1, 0});
  bakeProbe(c, 0, pt, 512);
  // light coming from +x (onto a surface facing +x) is redder than from -x
  Vec3 fromRed = c.face[0][0], fromOpen = c.face[0][1];
  CHECK(fromRed.x / std::max(fromRed.y, 1e-3f) > 1.5f * fromOpen.x / std::max(fromOpen.y, 1e-3f));
}

TEST(light_cache_probe_inside_a_wall_takes_its_neighbours) {
  Material white;
  white.baseColor = Vec3(0.7f);
  Scene s = skyOnly(1000);
  int m = s.addMaterial(white);
  s.add(s.addMesh(shapes::box({1.0f, 1.0f, 1.0f})), m, Mat4::translation({0, 0, 0}));  // a block round the first probe
  PathTracer pt;
  pt.setScene(s);
  LightCache c = layOutLightCache(AABB({0, 0, 0}, {2, 2, 2}), 27, 1.0f);
  c.origin = {0, 0, 0};
  for (int i = 0; i < c.probes(); ++i) bakeProbe(c, i, pt, 64);
  CHECK(c.state[0] == LightCache::kInside);
  CHECK(mean(c, 0) == 0);
  fillInside(c);
  CHECK(mean(c, 0) > 100);  // its neighbours' sky light
  // the order: nearest first
  auto order = bakeOrder(c, c.probePos(5));
  CHECK(order[0] == 5);
}

TEST(light_cache_picking_lamps_gives_the_same_light) {
  // twelve 100 lm bulbs in one place (more than 8: a few picked at each
  // bounce) against one 1200 lm bulb there (every lamp, weighed the old way)
  Material white;
  white.baseColor = Vec3(0.7f);
  white.roughness = 1, white.specular = 0;
  Scene one = room(white, 1200), many = room(white, 0);
  for (int k = 0; k < 12; ++k) {
    Light l;
    l.position = {0, 2.5f, 0};
    l.lumens = 100;
    l.radius = 0.05f;
    many.lights.push_back(l);
  }
  PathTracer a, b;
  a.setScene(one), b.setScene(many);
  LightCache ca = oneProbe({1, 1, 1}), cb = oneProbe({1, 1, 1});
  bakeProbe(ca, 0, a, 2048);
  bakeProbe(cb, 0, b, 2048);
  const float ea = mean(ca, 0), eb = mean(cb, 0);
  CHECK(ea > 1);
  CHECK_NEAR(eb, ea, ea * 0.06);
}

TEST(light_cache_smoothing_keeps_even_light_and_calms_a_spike) {
  LightCache c = layOutLightCache(AABB({0, 0, 0}, {4, 4, 4}), 125, 1.0f);
  for (int i = 0; i < c.probes(); ++i) c.face[static_cast<size_t>(i)].fill(Vec3(10)), c.state[static_cast<size_t>(i)] = LightCache::kBaked;
  const int mid = c.probeAt(2, 2, 2);
  c.face[static_cast<size_t>(mid)].fill(Vec3(70));  // one noisy probe
  LightCache s = smoothed(c);
  CHECK_NEAR(s.face[static_cast<size_t>(c.probeAt(0, 0, 0))][2].x, 10, 1e-4);  // even light stays as it is
  CHECK(s.face[static_cast<size_t>(mid)][2].x < 40);                          // the spike well down
  CHECK(s.face[static_cast<size_t>(c.probeAt(3, 2, 2))][2].x > 10);           // spread a little to its neighbours
}
