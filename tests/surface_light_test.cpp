// The light bake (engine/render/surface_light.h): the light on every still
// surface, baked by the path tracer into cells a few centimetres wide.
#include "engine/geometry/mesh.h"
#include "engine/render/pathtracer.h"
#include "engine/render/surface_light.h"
#include "tests/test.h"

using namespace loop;

namespace {
Material matte(float grey) {
  Material m;
  m.baseColor = Vec3(grey);
  m.roughness = 1, m.specular = 0;
  return m;
}
// A floor 6 x 6 at y = 0, a bulb `height` above its middle, nothing else (a black sky).
Scene floorAndBulb(float height, bool shaded) {
  Scene s;
  s.sky.kind = Sky::Kind::kColor;
  s.sky.color = Vec3(0);
  s.sky.colorNits = 0;
  s.add(s.addMesh(shapes::box({6, 0.2f, 6})), s.addMaterial(matte(0.02f)), Mat4::translation({0, -0.1f, 0}));
  Light l;
  l.position = {0, height, 0};
  l.lumens = 800;
  l.radius = 0.02f;
  s.lights.push_back(l);
  if (shaded) {  // a closed box round the bulb: a shade with no opening
    const int m = s.addMaterial(matte(0.5f));
    const float t = 0.02f, r = 0.15f;
    auto slab = [&](Vec3 size, Vec3 at) { s.add(s.addMesh(shapes::box(size)), m, Mat4::translation(at + Vec3(0, height, 0))); };
    slab({2 * r, t, 2 * r}, {0, -r, 0}), slab({2 * r, t, 2 * r}, {0, r, 0});
    slab({t, 2 * r, 2 * r}, {-r, 0, 0}), slab({t, 2 * r, 2 * r}, {r, 0, 0});
    slab({2 * r, 2 * r, t}, {0, 0, -r}), slab({2 * r, 2 * r, t}, {0, 0, r});
  }
  return s;
}
float bakedFloor(const Scene& s, Vec3 at, int rays = 256) {
  SurfaceLight b = layOutSurfaceLight(s, AABB({-3, -1, -3}, {3, 3, 3}), 0.25f);
  CHECK(b.on);
  PathTracer pt;
  pt.setScene(s);
  // bake only the cells round the point (the rest stay dark)
  for (int i = 0; i < b.cells(); ++i)
    if (length(b.at[static_cast<size_t>(i)] - at) < 0.6f && b.normal[static_cast<size_t>(i)].y > 0.9f) bakeCell(b, i, pt, rays);
  bool found = false;
  const float e = luminance(b.irradiance(at, {0, 1, 0}, &found));
  CHECK(found);
  return e;
}
}  // namespace

TEST(surface_light_rgb9e5_keeps_light_to_a_part_in_200) {
  for (Vec3 c : {Vec3(0.5f, 0.25f, 0.125f), Vec3(1200, 30, 0.7f), Vec3(1e-3f, 2e-3f, 3e-3f), Vec3(0)}) {
    const Vec3 d = unpackRgb9e5(packRgb9e5(c));
    const float m = std::max({c.x, c.y, c.z});
    CHECK_NEAR(d.x, c.x, m / 200 + 1e-9f);
    CHECK_NEAR(d.y, c.y, m / 200 + 1e-9f);
    CHECK_NEAR(d.z, c.z, m / 200 + 1e-9f);
  }
}

TEST(surface_light_cells_lie_on_surfaces_and_face_their_way) {
  const Scene s = floorAndBulb(1, false);
  const SurfaceLight b = layOutSurfaceLight(s, AABB({-3, -1, -3}, {3, 3, 3}), 0.25f);
  CHECK(b.on);
  int up = 0;
  for (int i = 0; i < b.cells(); ++i) {
    const Vec3 p = b.at[static_cast<size_t>(i)], n = b.normal[static_cast<size_t>(i)];
    if (n.y > 0.99f) {
      CHECK_NEAR(p.y, 0.0f, 1e-4f);
      ++up;
    }
    CHECK(b.find(b.keys[static_cast<size_t>(i)]) == i);
  }
  CHECK(up >= 24 * 24);  // the floor's top, 6 m across in quarter metres
  bool found = true;
  b.irradiance({0, 1.5f, 0}, {0, 1, 0}, &found);
  CHECK(!found);  // in the air: no surface, lit live
  b.irradiance({0, 0.1f, 0}, {0, -1, 0}, &found);
  CHECK(!found);  // the floor's top faces up: a surface facing down there has no cell
}

// Every lamp's own shadow is in the bake: inverse-square in the open, dark
// under a closed shade (the live lamps of Phone lit through their shades).
TEST(surface_light_bakes_the_lamps_with_their_shadows) {
  const float near = bakedFloor(floorAndBulb(1, false), {0, 0, 0});
  const float far = bakedFloor(floorAndBulb(2, false), {0, 0, 0});
  CHECK(near > 0);
  CHECK_NEAR(near / far, 4.0f, 0.6f);
  const float shaded = bakedFloor(floorAndBulb(1, true), {0, 0, 0});
  CHECK(shaded < near * 0.01f);
}

TEST(surface_light_open_sky_is_pi_times_its_light) {
  Scene s;
  s.sky.kind = Sky::Kind::kColor;
  s.sky.color = Vec3(1);
  s.sky.colorNits = 1000;
  s.add(s.addMesh(shapes::box({6, 0.2f, 6})), s.addMaterial(matte(0.0f)), Mat4::translation({0, -0.1f, 0}));
  CHECK_NEAR(bakedFloor(s, {0, 0, 0}, 1024) / (kPi * 1000), 1.0f, 0.05f);
}

// Made ahead and shipped: a file gives back the same light, and says which
// scene it was baked from (a moved lamp or shape is another scene).
TEST(surface_light_file_keeps_the_light_and_knows_its_scene) {
  Scene s = floorAndBulb(1, false);
  SurfaceLight b = layOutSurfaceLight(s, AABB({-3, -1, -3}, {3, 3, 3}), 0.25f);
  PathTracer pt;
  pt.setScene(s);
  for (int i = 0; i < b.cells(); i += 7) bakeCell(b, i, pt, 16);
  const std::vector<float> sig = bakeSignature(s);
  const std::vector<uint8_t> bytes = b.save(sig);
  SurfaceLight back;
  std::vector<float> got;
  CHECK(back.load(bytes.data(), bytes.size(), &got));
  CHECK(sameScene(got, sig));
  CHECK(back.cells() == b.cells());
  for (Vec3 p : {Vec3(0, 0, 0), Vec3(1.3f, 0, -0.7f), Vec3(2.6f, 0, 2.2f)}) {
    const Vec3 a = b.irradiance(p, {0, 1, 0}), c = back.irradiance(p, {0, 1, 0});
    CHECK_NEAR(luminance(c), luminance(a), luminance(a) / 100 + 1e-6f);
  }
  CHECK(!back.load(bytes.data(), bytes.size() - 1, &got));  // cut short: refused
  Scene moved = s;
  moved.lights[0].position.x += 0.1f;
  CHECK(!sameScene(bakeSignature(moved), sig));
  Scene shifted = s;
  shifted.instances[0].transform = Mat4::translation({0, -0.2f, 0});
  CHECK(!sameScene(bakeSignature(shifted), sig));
  Scene hair = s;  // worked out a hair differently (another machine's sines): the same scene
  hair.instances[0].transform = Mat4::translation({0, -0.1f + 3e-4f, 0});
  CHECK(sameScene(bakeSignature(hair), sig));
}
