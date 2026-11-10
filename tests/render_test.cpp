#include "engine/render/bsdf.h"
#include "engine/render/film.h"
#include "engine/render/pathtracer.h"
#include "engine/render/sky.h"
#include "engine/script/scene_api.h"
#include "tests/test.h"

using namespace loop;

namespace {
// Average light along many paths through the image centre.
Vec3 centre(PathTracer& pt, int n, int w = 64, int h = 64) {
  Vec3 s{0};
  for (int i = 0; i < n; ++i) s += pt.tracePixel(w / 2.0f, h / 2.0f, w, h, i);
  return s / static_cast<float>(n);
}
Scene furnace(Material m) {
  Scene s;
  s.sky.kind = Sky::Kind::kColor;
  s.sky.color = Vec3(1);
  s.sky.colorNits = 1000;
  s.addMaterial(m);
  s.add(s.addMesh(shapes::sphere(1, 96, 48)), 0);
  s.camera.position = {0, 0, 4};
  s.camera.target = {0, 0, 0};
  s.camera.fStop = 64;
  return s;
}
}  // namespace

TEST(bsdf_never_gains_energy) {
  Rng rng(1);
  for (float rough : {0.05f, 0.3f, 0.7f, 1.0f})
    for (float metal : {0.0f, 1.0f}) {
      Bsdf b;
      b.n = {0, 0, 1}, b.t = {1, 0, 0}, b.b = {0, 1, 0};
      b.alpha = rough * rough;
      b.diffuse = Vec3(1 - metal);
      b.f0 = vlerp(Vec3(0.04f), Vec3(1), metal);
      Vec3 wo = normalize(Vec3{0.5f, 0, 0.6f});
      b.finalize(wo);
      Vec3 sum{0};
      const int n = 40000;
      for (int i = 0; i < n; ++i) {
        Bsdf::Sample s;
        if (b.sample(wo, {rng.uniform(), rng.uniform(), rng.uniform()}, s)) sum += s.weight;
      }
      float a = luminance(sum) / n;
      CHECK(a <= 1.02f);
      if (a <= 0.6f) std::printf("    rough %.2f metal %.0f albedo %.3f\n", rough, metal, a);
      CHECK(a > (metal > 0 ? 0.9f : 0.6f));
    }
}

TEST(furnace_diffuse_sphere) {
  Material m;
  m.baseColor = Vec3(0.8f);
  m.roughness = 0.6f;
  Scene s = furnace(m);
  PathTracer pt;
  pt.setScene(s);
  Vec3 c = centre(pt, 4000);
  CHECK_NEAR(luminance(c) / 1000, 0.8, 0.06);
}

TEST(furnace_glass_sphere_loses_nothing) {
  Material m;
  m.baseColor = Vec3(1);
  m.transmission = 1;
  m.roughness = 0;
  Scene s = furnace(m);
  PathTracer pt;
  pt.setScene(s);
  Vec3 c = centre(pt, 3000);
  CHECK_NEAR(luminance(c) / 1000, 1.0, 0.05);
}

TEST(point_light_inverse_square) {
  // A grey floor under a bare 1000 lm bulb 2 m up: E = I / d², L = E·ρ/π.
  Scene s;
  s.sky.kind = Sky::Kind::kNone;
  Material m;
  m.baseColor = Vec3(0.5f);
  m.specular = 0;
  m.roughness = 1;
  s.addMaterial(m);
  s.add(s.addMesh(shapes::plane({40, 40})), 0);
  Light l;
  l.position = {0, 2, 0};
  l.lumens = 1000;
  l.radius = 0.01f;
  s.lights.push_back(l);
  s.camera.position = {0, 1, 0.001f};
  s.camera.target = {0, 0, 0};
  s.camera.fStop = 64;
  RenderSettings rs;
  PathTracer pt;
  pt.setScene(s);
  float I = 1000 / (4 * kPi), E = I / 4;
  float expect = E * 0.5f / kPi;
  // direct light only: one bounce of floor -> bulb dominates; indirect is nil (no walls)
  Vec3 c = centre(pt, 2000);
  CHECK_NEAR(luminance(c), expect, expect * 0.05);
}

TEST(sun_on_ground_matches_lux) {
  Scene s;
  s.sky.kind = Sky::Kind::kNone;
  Material m;
  m.baseColor = Vec3(0.4f);
  m.specular = 0;
  m.roughness = 1;
  s.addMaterial(m);
  s.add(s.addMesh(shapes::plane({400, 400})), 0);
  Light sun;
  sun.kind = Light::Kind::kSun;
  sun.direction = {0, -1, 0};
  sun.lux = 100000;
  sun.color = Vec3(1);
  s.lights.push_back(sun);
  s.camera.position = {0, 1, 0.001f};
  s.camera.target = {0, 0, 0};
  PathTracer pt;
  pt.setScene(s);
  float expect = 100000 * 0.4f / kPi;
  CHECK_NEAR(luminance(centre(pt, 500)), expect, expect * 0.02);
}

TEST(physical_sky_is_blue_and_sunset_is_red) {
  Sky sky;
  SkyModel noon, dusk;
  noon.build(sky, normalize(Vec3{0.2f, 1, 0}), 100000);
  dusk.build(sky, normalize(Vec3{1, 0.02f, 0}), 100000);
  Vec3 zen = noon.radiance({0, 1, 0});
  CHECK(zen.z > zen.x * 1.3f);  // blue overhead
  Vec3 sn = noon.sunIlluminance(), sd = dusk.sunIlluminance();
  CHECK(sd.x / sd.z > sn.x / sn.z * 2);  // the setting sun is much redder
  CHECK(luminance(sd) < luminance(sn) * 0.3f);
  CHECK_NEAR(luminance(sn), 100000, 15000);
  float noonSky = luminance(noon.ambientIrradiance());
  CHECK(noonSky > 5000 && noonSky < 40000);  // clear-sky light: about 10-25 klux
}

TEST(film_middle_grey_and_looks) {
  // a uniform 1000-nit picture on auto exposure lands near middle grey
  Image hdr(32, 32, {1000, 1000, 1000, 1});
  Camera cam;
  Look natural;
  natural.vignette = 0, natural.bloom = 0;
  FilmResult info;
  Image out = develop(hdr, cam, natural, 0, &info);
  float v = out.at(16, 16).x;
  CHECK(v > 0.3f && v < 0.6f);
  CHECK_NEAR(info.exposure * 1000, 0.18, 1e-3);
  // tone curves are monotonic and stay in 0..1
  for (auto f : {tonemapAgX, tonemapAces, tonemapFilmic}) {
    float prev = -1;
    for (float x = 0.001f; x < 100; x *= 1.3f) {
      float y = f(Vec3(x)).x;
      CHECK(y >= prev - 1e-4f && y <= 1.0001f);
      prev = y;
    }
  }
  for (const auto& name : lookNames()) {
    Look l;
    CHECK(lookByName(name, l));
    Image o = develop(hdr, cam, l, 1);
    for (const auto& p : o.px) CHECK(p.x >= 0 && p.x <= 1.01f && std::isfinite(p.y));
  }
  Look noir;
  lookByName("noir", noir);
  Image color(8, 8, {500, 1000, 3000, 1});
  Image bw = develop(color, cam, noir);
  CHECK_NEAR(bw.at(4, 4).x, bw.at(4, 4).z, 0.02);
  // white balance: warmer means more red than blue
  Vec3 warm = whiteBalance(0.5f, 0);
  CHECK(warm.x > warm.z);
}

TEST(render_small_image_and_denoise) {
  Scene s;
  Material floor;
  floor.baseColor = {0.7f, 0.7f, 0.7f};
  s.addMaterial(floor);
  Material red;
  red.baseColor = {0.8f, 0.1f, 0.1f};
  s.addMaterial(red);
  s.add(s.addMesh(shapes::plane({20, 20})), 0);
  s.add(s.addMesh(shapes::sphere(0.5f)), 1, Mat4::translation({0, 0.5f, 0}));
  Light sun;
  sun.kind = Light::Kind::kSun;
  sun.direction = normalize(Vec3{-0.4f, -1, -0.3f});
  s.lights.push_back(sun);
  s.camera.position = {0, 1, 3};
  s.camera.target = {0, 0.5f, 0};
  PathTracer pt;
  pt.setScene(s);
  RenderSettings rs;
  rs.width = 64, rs.height = 40, rs.samples = 8;
  RenderOutput out = pt.render(rs);
  CHECK(out.color.width == 64);
  int bad = 0;
  for (auto& p : out.color.px)
    if (!isFinite(p.xyz()) || p.x < 0) ++bad;
  CHECK(bad == 0);
  // the sphere is red at the centre
  Vec4 c = out.color.at(32, 20);
  CHECK(c.x > c.y * 2);
  // denoising reduces pixel-to-pixel noise on the floor
  auto rough = [](const Image& im) {
    double d = 0;
    for (int x = 1; x < 63; ++x) d += std::fabs(luminance(im.at(x, 36).xyz()) - luminance(im.at(x - 1, 36).xyz()));
    return d;
  };
  CHECK(rough(out.color) <= rough(out.noisy));
}

// ---- haze (the `haze` word): one formula, `hazeAt`

namespace {
SkyModel noonSky() {
  Sky sky;
  SkyModel m;
  m.build(sky, normalize(Vec3{0, 1, 0.01f}), 100000);  // the sun high, so the horizon is plain sky
  return m;
}
Haze hazeOf(float visibility, float height = 1500) {
  Haze h;
  h.visibility = visibility, h.height = height;
  return h;
}
}  // namespace

TEST(haze_nothing_at_distance_zero_and_off) {
  SkyModel sky = noonSky();
  Vec3 c{0.2f, 0.3f, 0.4f};
  Vec3 same = hazeAt(c, {1, 2, 3}, {1, 2, 3}, sky, hazeOf(60000));
  CHECK_NEAR(same.x, c.x, 1e-6);
  CHECK_NEAR(same.y, c.y, 1e-6);
  CHECK_NEAR(same.z, c.z, 1e-6);
  Vec3 off = hazeAt(c, {0, 1, 0}, {0, 1, 50000}, sky, hazeOf(0));
  CHECK_NEAR(off.y, c.y, 1e-6);
}

TEST(haze_visibility_leaves_two_percent_of_green) {
  SkyModel sky = noonSky();
  // a level sight line as long as the visibility: 2% of the thing's own green is left
  // (the difference of a bright thing and a black one is what is left of the bright one)
  Vec3 black = hazeAt({0, 0, 0}, {0, 0, 0}, {0, 0, 60000}, sky, hazeOf(60000));
  Vec3 bright = hazeAt({1000, 1000, 1000}, {0, 0, 0}, {0, 0, 60000}, sky, hazeOf(60000));
  CHECK_NEAR((bright.y - black.y) / 1000, 0.02f, 0.0005f);
  CHECK_NEAR((bright.x - black.x) / 1000, std::pow(0.02f, 0.75f), 0.001f);
}

TEST(haze_from_higher_loses_less) {
  SkyModel sky = noonSky();
  Haze h = hazeOf(60000);
  Vec3 horizon = sky.radiance({0, 0, 1});
  // a black thing 30 km off: what shows is the horizon, in proportion to what the air took
  Vec3 low = hazeAt({0, 0, 0}, {0, 1.6f, 0}, {0, 1.6f, 30000}, sky, h);
  Vec3 high = hazeAt({0, 0, 0}, {0, 3000, 0}, {0, 3000, 30000}, sky, h);
  CHECK(high.y < low.y);
  CHECK(low.y < horizon.y);
}

TEST(haze_far_dark_things_go_blue_first) {
  SkyModel sky = noonSky();
  Vec3 c = hazeAt({0, 0, 0}, {0, 1.6f, 0}, {0, 1.6f, 30000}, sky, hazeOf(60000));
  Vec3 horizon = sky.radiance({0, 0, 1});
  // the share of the horizon that came in: more of blue than of red
  CHECK(c.z / horizon.z > c.x / horizon.x * 1.2f);
  // and a black thing is bluer than it is red, more so than the horizon is
  CHECK(c.z / c.x > horizon.z / horizon.x);
}

TEST(haze_ten_times_the_visibility_is_the_horizon) {
  SkyModel sky = noonSky();
  Vec3 horizon = sky.radiance({0, 0, 1});
  Vec3 c = hazeAt({0.5f, 0.5f, 0.5f}, {0, 0, 0}, {0, 0, 600000}, sky, hazeOf(60000));
  CHECK_NEAR(c.x, horizon.x, horizon.x * 0.01f);
  CHECK_NEAR(c.y, horizon.y, horizon.y * 0.01f);
  CHECK_NEAR(c.z, horizon.z, horizon.z * 0.01f);
}

TEST(haze_word_sets_the_scene_and_the_film_shows_it) {
  using namespace loop::script;
  Vm vm;
  Stage st;
  installStandardLibrary(vm);
  installSceneApi(vm, st);
  bool ok = vm.load("haze visibility: 60 km, height: 1.5 km\n", "h.loop");
  for (auto& p : vm.problems()) std::printf("    %s\n", p.text().c_str());
  CHECK(ok);
  CHECK_NEAR(st.scene.fog.haze.visibility, 60000, 1);
  CHECK_NEAR(st.scene.fog.haze.height, 1500, 1);
  Scene none;
  CHECK(none.fog.haze.visibility == 0);  // off unless a scene says it
  // the film: a dark wall 20 km away is paler with the haze on, and the sky's colour shows in it
  Scene s;
  s.sky.kind = Sky::Kind::kColor;
  s.sky.color = Vec3(0.5f, 0.6f, 1.0f);
  s.sky.colorNits = 3000;
  Material m;
  m.baseColor = Vec3(0.05f);
  m.specular = 0;
  m.roughness = 1;
  s.addMaterial(m);
  s.add(s.addMesh(shapes::box({4000, 4000, 10})), 0, Mat4::translation({0, 0, 20000}));
  s.camera.position = {0, 5, 0};
  s.camera.target = {0, 5, 20000};
  s.camera.fStop = 64;
  PathTracer pt;
  pt.setScene(s);
  Vec3 clear = centre(pt, 64);
  s.fog.haze = st.scene.fog.haze;
  pt.setScene(s);
  Vec3 hazy = centre(pt, 64);
  CHECK(luminance(hazy) > luminance(clear) * 2);
  CHECK(hazy.z / std::max(hazy.x, 1e-6f) > clear.z / std::max(clear.x, 1e-6f) * 1.05f);
}
