// The whole surface stack (docs/SURFACES.md section 7; order M3c): wear goes
// only where the edge mark is, dirt only where the hollow mark is and over the
// wear, coats cover what they say, the slow change keeps the average, and the
// tile wraps.
#include <cmath>

#include "engine/render/pattern.h"
#include "engine/render/surface.h"
#include "engine/render/surface_patterns.h"
#include "tests/test.h"

using namespace loop;
using Kind = SurfacePattern::Kind;

namespace {

bool near(float a, float b, float e = 1e-5f) { return std::fabs(a - b) <= e; }
bool near(Vec3 a, Vec3 b, float e = 1e-5f) { return near(a.x, b.x, e) && near(a.y, b.y, e) && near(a.z, b.z, e); }

// Red paint over steel, worn to the steel on edges, gathering near-black dirt in hollows.
SurfaceRecipe redPaint() {
  SurfaceRecipe r;
  r.name = "red paint";
  r.tile = 1;
  r.base.on = true;
  r.base.color = r.base.color2 = {0.45f, 0.05f, 0.04f};
  r.base.roughness = r.base.roughness2 = 0.4f;
  r.base.pattern.kind = Kind::kNoise, r.base.pattern.size = 0.05f;
  r.base.bump = 0.0002f;
  r.wear.on = true;
  r.wear.color = {0.6f, 0.6f, 0.62f}, r.wear.roughness = 0.25f, r.wear.metal = 1;
  r.wear.amount = 1, r.wear.ragged = 0;
  r.dirt.on = true;
  r.dirt.color = {0.02f, 0.02f, 0.02f}, r.dirt.roughness = 0.9f;
  r.dirt.amount = 1, r.dirt.ragged = 0;
  return r;
}

Surface at(const SurfaceRecipe& r, Vec3 p, float edge, float hollow) {
  static const Material m;
  return surfaceAt(r, m, p, {0, 1, 0}, {}, edge, hollow, 1);
}

}  // namespace

TEST(surface_marks_of_zero_give_no_wear_and_no_dirt) {
  SurfaceRecipe r = redPaint();
  r.wear.ragged = r.dirt.ragged = 1;  // whatever the other numbers
  r.wear.amount = r.dirt.amount = 1;
  r.wear.soft = r.dirt.soft = 0.01f;
  SurfaceRecipe bare = r;
  bare.wear.on = bare.dirt.on = false;
  for (int i = 0; i < 50; ++i) {
    Vec3 p{i * 0.037f, 0, i * 0.051f};
    Surface a = at(r, p, 0, 0), b = at(bare, p, 0, 0);
    CHECK(near(a.base, b.base));
    CHECK(near(a.roughness, b.roughness));
    CHECK(near(a.metallic, b.metallic));
  }
}

TEST(surface_full_edge_is_exactly_the_wear) {
  SurfaceRecipe r = redPaint();
  for (int i = 0; i < 20; ++i) {
    Surface s = at(r, {i * 0.11f, 0, i * 0.07f}, 1, 0);
    CHECK(near(s.base, r.wear.color));
    CHECK(near(s.roughness, r.wear.roughness));
    CHECK(near(s.metallic, r.wear.metal));
  }
}

TEST(surface_dirt_lies_over_wear) {
  SurfaceRecipe r = redPaint();
  Surface s = at(r, {0.3f, 0, 0.2f}, 1, 1);
  CHECK(near(s.base, r.dirt.color));
  CHECK(near(s.roughness, r.dirt.roughness));
  CHECK(near(s.metallic, 0));  // dirt is no metal
}

TEST(surface_coat_covers_what_it_says) {
  SurfaceRecipe r = redPaint();
  r.coat[0].on = true;
  r.coat[0].color = {0.2f, 0.4f, 0.15f}, r.coat[0].roughness = 0.8f;
  r.coat[0].pattern.kind = Kind::kNoise, r.coat[0].pattern.size = 0.1f;
  SurfaceRecipe none = r;
  none.coat[0].on = false;
  r.coat[0].cover = 0;
  for (int i = 0; i < 40; ++i) {
    Vec2 u{i * 0.023f, i * 0.031f};
    SurfacePoint a = surfaceTile(r, u), b = surfaceTile(none, u);
    CHECK(near(a.color, b.color) && near(a.roughness, b.roughness));
  }
  r.coat[0].cover = 1;
  for (int i = 0; i < 40; ++i) {
    SurfacePoint a = surfaceTile(r, {i * 0.023f, i * 0.031f});
    CHECK(near(a.color, r.coat[0].color) && near(a.roughness, r.coat[0].roughness));
  }
  // in between: some covered, some not
  r.coat[0].cover = 0.5f;
  int covered = 0;
  for (int i = 0; i < 400; ++i)
    if (near(surfaceTile(r, {(i % 20) * 0.05f, (i / 20) * 0.05f}).color, r.coat[0].color, 1e-3f)) ++covered;
  CHECK(covered > 40 && covered < 360);
}

TEST(surface_slow_change_keeps_the_average) {
  SurfaceRecipe r = redPaint();
  r.base.pattern.kind = Kind::kNone, r.base.bump = 0;
  r.varyLarge = 1.2f, r.varyAmount = 0.12f, r.varyRoughness = 0.08f;
  r.varyTint = r.base.color;  // the word's default when no tint is given
  // over ten tiles each way, on a grid finer than the change
  Vec3 sum{0};
  int n = 0, differs = 0;
  for (int y = 0; y < 100; ++y)
    for (int x = 0; x < 100; ++x) {
      Surface s = at(r, {x * 0.1f + 0.013f, 0, y * 0.1f + 0.007f}, 0, 0);
      sum += s.base, ++n;
      if (!near(s.base, r.base.color, 1e-3f)) ++differs;
    }
  Vec3 avg = sum / static_cast<float>(n);
  for (int c = 0; c < 3; ++c) CHECK(std::fabs(avg[c] / r.base.color[c] - 1) < 0.02f);
  CHECK(differs > n / 2);  // and it does change
}

TEST(surface_tile_wraps) {
  SurfaceRecipe r = redPaint();
  r.tile = 2;
  r.base.pattern.kind = Kind::kBricks, r.base.pattern.size = 0.215f, r.base.pattern.stretch = 3.3f;
  r.base.color2 = {0.3f, 0.3f, 0.28f};
  r.base.pattern.piece = 0.1f;
  r.varySmall = 0.05f, r.varySmallAmount = 0.05f;
  r.coat[0].on = true, r.coat[0].cover = 0.3f, r.coat[0].color = {0.5f, 0.5f, 0.5f};
  r.coat[0].pattern.kind = Kind::kNoise, r.coat[0].pattern.size = 0.2f;
  for (int i = 0; i < 100; ++i) {
    // places on a 1/1024 m grid, so adding the tile is exact (see surface_patterns_test)
    Vec2 u{(i * 37 % 1024) / 1024.0f * 1.7f, (i * 91 % 1024) / 1024.0f * 1.3f};
    u = {std::round(u.x * 1024) / 1024, std::round(u.y * 1024) / 1024};
    SurfacePoint a = surfaceTile(r, u), b = surfaceTile(r, u + Vec2(r.tile, 0)), c = surfaceTile(r, u + Vec2(0, r.tile));
    CHECK(near(a.color, b.color, 1e-4f) && near(a.color, c.color, 1e-4f));
    CHECK(near(a.height, b.height, 1e-4f) && near(a.roughness, c.roughness, 1e-4f));
  }
}

TEST(surface_bump_bends_the_facing_and_shade_keeps_flat_ones_as_they_are) {
  SurfaceRecipe r = redPaint();
  r.base.pattern.kind = Kind::kBricks, r.base.pattern.size = 0.215f, r.base.pattern.stretch = 3.3f;
  r.base.bump = 0.005f;
  int bent = 0;
  for (int i = 0; i < 200; ++i)
    if (at(r, {i * 0.0071f, 0, 0.0333f}, 0, 0).normal.y < 0.9999f) ++bent;
  CHECK(bent > 0);  // the joints tilt the facing
  // through shadeSurface: a surface with no bump gives back the very facing it was given
  SurfaceRecipe flat = redPaint();
  flat.base.bump = 0;
  Material m;
  m.surface = 0;
  SurfaceContext sc;
  sc.recipe = &flat;
  sc.place = {0.2f, 0, 0.3f}, sc.facing = {0, 1, 0};
  Vec3 n = normalize(Vec3(0.01f, 1, 0.02f));
  Surface s = shadeSurface(m, {}, {}, n, {}, nullptr, &sc);
  CHECK(s.normal.x == n.x && s.normal.y == n.y && s.normal.z == n.z);
  // and without a context, a surface is its base, flat
  m.baseColor = {0.1f, 0.2f, 0.3f};
  Surface f = shadeSurface(m, {}, {}, n, {});
  CHECK(near(f.base, m.baseColor));
}

TEST(surface_wander_picture_is_the_same_everywhere) {
  const Image& w = wanderPicture();
  CHECK(w.width == 64 && w.height == 64);
  // it wraps: reading at u and u + one side agree
  float side = 8 * 1.5f;
  for (int i = 0; i < 20; ++i) {
    Vec2 u{i * 0.37f, i * 0.53f};
    Vec4 a = wanderAt(u, 1.5f), b = wanderAt(u + Vec2(side, 0), 1.5f);
    CHECK(near(a.x, b.x, 1e-4f) && near(a.w, b.w, 1e-4f));
    CHECK(a.x >= -1 && a.x <= 1 && a.y >= -1 && a.y <= 1);
  }
  // a fixed byte: the picture is made from integer hashes only, so this never changes between machines
  // (the phone gets these bytes too, so film and phone wander alike)
  uint32_t sum = 0;
  for (const Vec4& p : w.px)
    sum = sum * 31 + static_cast<uint32_t>(std::lround(p.x * 255)) + static_cast<uint32_t>(std::lround(p.y * 255)) * 3 +
          static_cast<uint32_t>(std::lround(p.z * 255)) * 5 + static_cast<uint32_t>(std::lround(p.w * 255)) * 7;
  CHECK(sum == 572009218u);
}
