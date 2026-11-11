// The whole surface stack (docs/SURFACES.md section 7; order M3c): wear goes
// only where the edge mark is, dirt only where the hollow mark is and over the
// wear, coats cover what they say, the slow change keeps the average, and the
// tile wraps. And how the tile is laid on (section 10; order M6): box, top, uv.
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <vector>

#include "engine/core/random.h"
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

// ---- projection (docs/SURFACES.md section 10; order M6) ----

namespace {

// A plaster-like noise: two colours `spread` apart, no bump, the colour the only thing that changes.
SurfaceRecipe noiseRecipe(float spread) {
  SurfaceRecipe r;
  r.tile = 1;
  r.base.on = true;
  r.base.color = {0.5f, 0.45f, 0.4f};
  r.base.color2 = r.base.color * (1 + spread);
  r.base.roughness = r.base.roughness2 = 0.6f;
  r.base.pattern.kind = Kind::kNoise, r.base.pattern.size = 0.05f;
  return r;
}

Vec3 axisDir(int i, float s) {
  Vec3 v{0};
  v[i] = s;
  return v;
}

// Facings spread evenly over the ball (a Fibonacci spiral), and on purpose the ones between sides: the
// 12 at 45 degrees between two, the 8 corners, and the near-corner ones where the second side swaps.
std::vector<Vec3> facings(int n) {
  std::vector<Vec3> out;
  for (int i = 0; i < n; ++i) {
    float y = 1 - 2 * (i + 0.5f) / n, rr = std::sqrt(std::max(0.0f, 1 - y * y)), phi = i * 2.39996323f;
    out.push_back({rr * std::cos(phi), y, rr * std::sin(phi)});
  }
  for (int sx = -1; sx <= 1; ++sx)
    for (int sy = -1; sy <= 1; ++sy)
      for (int sz = -1; sz <= 1; ++sz) {
        Vec3 v{static_cast<float>(sx), static_cast<float>(sy), static_cast<float>(sz)};
        if (dot(v, v) < 2) continue;
        out.push_back(normalize(v));
        for (int k = 1; k <= 8; ++k) out.push_back(normalize(v + Vec3(0.013f * k, -0.007f * k, 0.003f * k)));
      }
  return out;
}

// `n` turned by `deg` degrees about a way across it (`which` picks one of two).
Vec3 turned(Vec3 n, float deg, int which) {
  Vec3 t, b;
  orthonormalBasis(n, t, b);
  Vec3 axis = which ? b : t;
  float a = deg * kPi / 180;
  return normalize(n * std::cos(a) + cross(axis, n) * std::sin(a));
}

}  // namespace

TEST(surface_box_gives_the_side_a_facing_points_along_all_of_it) {
  SurfaceRecipe r = noiseRecipe(0.03f);
  for (int i = 0; i < 3; ++i)
    for (float s : {1.0f, -1.0f}) {
      SurfaceProjection pr = projectSurface(r, {0.3f, 0.2f, 0.1f}, axisDir(i, s), {}, 0);
      CHECK(pr.sides == 1);
      CHECK(pr.side[0].axis == i);
      CHECK(pr.side[0].weight == 1);
    }
}

TEST(surface_box_weights_always_add_to_one) {
  SurfaceRecipe r = noiseRecipe(0.03f);
  for (Vec3 n : facings(2000)) {
    SurfaceProjection pr = projectSurface(r, {0.1f, 0.2f, 0.3f}, n, {}, 0);
    CHECK(pr.sides >= 1 && pr.sides <= 2);
    float sum = 0;
    for (int k = 0; k < pr.sides; ++k) {
      CHECK(pr.side[k].weight > 0 && pr.side[k].weight <= 1);
      sum += pr.side[k].weight;
    }
    CHECK(near(sum, 1, 1e-6f));
    // the two strongest: the side left out is no stronger than either
    if (pr.sides == 2) {
      int other = 3 - pr.side[0].axis - pr.side[1].axis;
      CHECK(std::fabs(n[other]) <= std::fabs(n[pr.side[1].axis]) && std::fabs(n[pr.side[1].axis]) <= std::fabs(n[pr.side[0].axis]));
    }
  }
}

TEST(surface_box_gives_half_each_at_45_degrees_between_two_sides) {
  SurfaceRecipe r = noiseRecipe(0.03f);
  for (int i = 0; i < 3; ++i)
    for (int j = i + 1; j < 3; ++j)
      for (float si : {1.0f, -1.0f})
        for (float sj : {1.0f, -1.0f}) {
          SurfaceProjection pr = projectSurface(r, {}, normalize(axisDir(i, si) + axisDir(j, sj)), {}, 0);
          CHECK(pr.sides == 2);
          CHECK(near(pr.side[0].weight, 0.5f, 1e-5f) && near(pr.side[1].weight, 0.5f, 1e-5f));
          CHECK((pr.side[0].axis == i && pr.side[1].axis == j) || (pr.side[0].axis == j && pr.side[1].axis == i));
        }
}

TEST(surface_box_reads_nothing_mirrored_and_keeps_up_up) {
  // every side, with the stretched way along x, y or z: seen from outside, the tile's first way, its
  // second and the facing out turn the same way (right hand), so opposite sides never read as each
  // other's mirror; the first way runs along the along axis wherever the side holds it; and on an
  // upright side whose second way is y, that way points up
  SurfaceRecipe r = noiseRecipe(0.03f);
  for (int along = 0; along < 3; ++along)
    for (int i = 0; i < 3; ++i)
      for (float s : {1.0f, -1.0f}) {
        Vec3 out = axisDir(i, s);
        SurfaceProjection pr = projectSurface(r, {}, out, {}, along);
        const SurfaceSide& sd = pr.side[0];
        CHECK(near(cross(sd.ax, sd.ay), out));
        CHECK(near(dot(sd.ax, out), 0) && near(dot(sd.ay, out), 0));
        if (i != along) CHECK(near(std::fabs(sd.ax[along]), 1));
        if (std::fabs(sd.ay.y) > 0.5f) CHECK(sd.ay.y == 1);
      }
  // the place on the tile follows the two ways: a step along the first way moves u.x by that step
  const Vec3 p{0.2f, 0.3f, 0.4f};
  SurfaceProjection pr = projectSurface(r, p, {0, 0, -1}, {}, 0);
  SurfaceProjection pr2 = projectSurface(r, p + pr.side[0].ax * 0.1f, {0, 0, -1}, {}, 0);
  CHECK(near(pr2.side[0].u.x - pr.side[0].u.x, 0.1f, 1e-5f) && near(pr2.side[0].u.y, pr.side[0].u.y, 1e-5f));
}

TEST(surface_box_turning_the_facing_a_degree_barely_changes_a_noise) {
  // A side change never shows as a line: turning the facing by one degree anywhere changes the colour
  // of a noise recipe by 2% or less, though the tile itself, read from two sides at one place, differs
  // by more than that (so reading one side only, as before M6, shows the line).
  SurfaceRecipe r = noiseRecipe(0.03f);
  static const Material m;
  const std::vector<Vec3> ball = facings(1500);
  float worst = 0, sides = 0;
  for (int pi = 0; pi < 16; ++pi) {
    const Vec3 p{toUnit(hash32(pi * 3u + 1)), toUnit(hash32(pi * 3u + 2)), toUnit(hash32(pi * 3u + 3))};
    // the jump a single side would make here: the most two sides' tiles differ at this place
    Vec3 side[6];
    for (int i = 0; i < 6; ++i) side[i] = surfaceAt(r, m, p, axisDir(i / 2, i % 2 ? -1.0f : 1.0f), {}, 0, 0, 1).base;
    for (int i = 0; i < 6; ++i)
      for (int j = 0; j < 6; ++j)
        if (i / 2 != j / 2) sides = std::max(sides, std::fabs(side[i].x / side[j].x - 1));
    for (Vec3 n : ball) {
      Vec3 c = surfaceAt(r, m, p, n, {}, 0, 0, 1).base;
      for (int which = 0; which < 2; ++which)
        for (float deg : {1.0f, -1.0f}) {
          Vec3 c2 = surfaceAt(r, m, p, turned(n, deg, which), {}, 0, 0, 1).base;
          for (int k = 0; k < 3; ++k) worst = std::max(worst, std::fabs(c2[k] / c[k] - 1));
        }
    }
  }
  std::printf("  (a degree's turn changes the noise by at most %.2f%%; two sides differ by up to %.2f%%)\n", worst * 100, sides * 100);
  CHECK(sides > 0.02f);
  CHECK(worst <= 0.02f);
}

TEST(surface_bumped_facing_stays_on_the_things_own_facing) {
  // the bumps tilt the facing a little, from whichever sides are read: a ball stays a ball (its facing
  // is never pulled towards the box's sides), and a flat recipe gives back the very facing
  SurfaceRecipe r = noiseRecipe(0.03f);
  r.base.bump = 0.002f;
  static const Material m;
  int bent = 0;
  for (Vec3 n : facings(500)) {
    Vec3 b = surfaceAt(r, m, n * 0.13f, n, {}, 0, 0, 1).normal;
    CHECK(std::fabs(length(b) - 1) < 1e-4f);
    CHECK(dot(b, n) > 0.95f);
    if (dot(b, n) < 0.99999f) ++bent;
  }
  CHECK(bent > 250);
  r.base.bump = 0;
  Vec3 n = normalize(Vec3(0.3f, 0.5f, -0.8f));
  Vec3 f = surfaceAt(r, m, {0.1f, 0.2f, 0.3f}, n, {}, 0, 0, 1).normal;
  CHECK(f.x == n.x && f.y == n.y && f.z == n.z);
}

TEST(surface_top_reads_from_above_and_uv_from_the_unwrap) {
  SurfaceRecipe r = noiseRecipe(0.03f);
  r.project = SurfaceRecipe::Project::kTop;
  for (Vec3 n : facings(200)) {
    SurfaceProjection pr = projectSurface(r, {0.3f, 0.7f, -0.2f}, n, {}, 0);
    CHECK(pr.sides == 1 && pr.side[0].axis == 1 && pr.side[0].weight == 1);
    CHECK(near(pr.side[0].u.x, 0.3f) && near(pr.side[0].u.y, 0.2f));  // seen from above: x across, -z away from you
  }
  r.project = SurfaceRecipe::Project::kUv;
  static const Material m;
  for (Vec3 n : facings(200)) {
    SurfaceProjection pr = projectSurface(r, {0.3f, 0.7f, -0.2f}, n, {0.25f, 1.5f}, 0);
    CHECK(pr.sides == 1 && pr.side[0].weight == 1);
    CHECK(pr.side[0].u.x == 0.25f && pr.side[0].u.y == 1.5f);  // one unit a metre
  }
  // the unwrap decides: the same uv gives the same colour wherever the point is and whichever way it faces
  Vec3 a = surfaceAt(r, m, {0.1f, 0.2f, 0.3f}, {0, 1, 0}, {0.4f, 0.6f}, 0, 0, 1).base;
  Vec3 b = surfaceAt(r, m, {2.1f, -0.5f, 0.9f}, normalize(Vec3(1, 0.2f, 0.3f)), {0.4f, 0.6f}, 0, 0, 1).base;
  CHECK(near(a, b));
}

TEST(surface_planks_run_along_the_long_side) {
  // boards 2 cm wide and 50 cm long, each a little lighter or darker
  SurfaceRecipe r;
  r.tile = 1;
  r.base.on = true;
  r.base.color = {0.5f, 0.32f, 0.18f}, r.base.color2 = {0.2f, 0.12f, 0.06f};
  r.base.pattern.kind = Kind::kPlanks, r.base.pattern.size = 0.02f, r.base.pattern.stretch = 25, r.base.pattern.piece = 0.3f;
  static const Material m;
  for (int longest = 0; longest < 3; ++longest) {
    Vec3 size{0.1f};
    size[longest] = 2;
    const AABB bounds(size * -0.5f, size * 0.5f);  // a 2 m by 0.1 m by 0.1 m box, its 2 m along x, y or z
    CHECK(alongAxis(r, bounds) == longest);
    SurfaceRecipe laid = r;
    laid.along = alongAxis(r, bounds);  // as the film lays it on the thing
    for (int i = 0; i < 3; ++i) {
      if (i == longest) continue;
      for (float s : {1.0f, -1.0f}) {
        Vec3 n = axisDir(i, s), across = axisDir(3 - i - longest, 1), along = axisDir(longest, 1);
        Vec3 p = n * 0.05f + Vec3(0.0031f, 0.0047f, 0.0053f);
        CHECK(near(std::fabs(projectSurface(laid, p, n, {}, laid.along).side[0].ax[longest]), 1));
        // walking 10 cm along the long side meets one board end or none; across it, about five boards
        int changesAlong = 0, changesAcross = 0;
        Vec3 last = surfaceAt(laid, m, p, n, {}, 0, 0, 1).base, lastX = surfaceAt(laid, m, p - across * 0.05f, n, {}, 0, 0, 1).base;
        for (int k = 1; k <= 100; ++k) {
          Vec3 c = surfaceAt(laid, m, p + along * (k * 0.001f), n, {}, 0, 0, 1).base;
          Vec3 cx = surfaceAt(laid, m, p + across * (k * 0.001f - 0.05f), n, {}, 0, 0, 1).base;
          if (!near(c, last, 1e-3f)) ++changesAlong;
          if (!near(cx, lastX, 1e-3f)) ++changesAcross;
          last = c, lastX = cx;
        }
        CHECK(changesAlong <= 4);   // into a joint and out of it, at most once
        CHECK(changesAcross >= 6);  // into and out of three joints or more
      }
    }
  }
  // a named way wins over the longest side
  r.along = 1;
  CHECK(alongAxis(r, AABB(Vec3(-1, -0.05f, -0.05f), Vec3(1, 0.05f, 0.05f))) == 1);
  // a ball or a cube (sides within 1%): x
  r.along = -1;
  CHECK(alongAxis(r, AABB(Vec3(-0.13f), Vec3(0.13f, 0.1301f, 0.13f))) == 0);
}

TEST(surface_two_things_start_at_different_places) {
  SurfaceRecipe r = noiseRecipe(0.03f);
  static const Material m;
  const Vec3 a = surfaceStart(surfaceStartKey(0), r.tile), b = surfaceStart(surfaceStartKey(1), r.tile);
  CHECK(length(a - b) > 0.05f);
  for (int k = 0; k < 3; ++k) CHECK(a[k] >= 0 && a[k] < r.tile && b[k] >= 0 && b[k] < r.tile);
  CHECK(surfaceStart(surfaceStartKey(7), 2) == surfaceStart(surfaceStartKey(7), 1) * 2.0f);
  // the same point on two copies of a thing: mostly not the same colour
  int differ = 0;
  for (int i = 0; i < 50; ++i) {
    Vec3 p{i * 0.017f, 0.05f, i * 0.023f};
    Vec3 ca = surfaceAt(r, m, p + a, {0, 1, 0}, {}, 0, 0, 1).base, cb = surfaceAt(r, m, p + b, {0, 1, 0}, {}, 0, 0, 1).base;
    if (!near(ca, cb, 1e-4f)) ++differ;
  }
  CHECK(differ > 40);
}

// ---- the detail layer (order M7) ----

namespace {
SurfaceRecipe linen() {
  SurfaceRecipe r;
  r.name = "linen";
  r.tile = 1;
  r.base.on = true;
  r.base.color = r.base.color2 = {0.7f, 0.68f, 0.62f};
  r.base.roughness = r.base.roughness2 = 0.8f;
  r.detail.kind = SurfacePattern::Kind::kWeave, r.detail.size = 0.0005f;
  r.detailTile = 0.05f, r.detailBump = 0.0001f, r.detailRoughness = 0.15f, r.detailFade = 2;
  return r;
}
}  // namespace

TEST(surface_detail_is_gone_at_its_fade_and_beyond) {
  SurfaceRecipe with = linen(), without = linen();
  without.detail.kind = SurfacePattern::Kind::kNone;
  const Material m;
  for (int i = 0; i < 40; ++i) {
    const Vec3 p{i * 0.0137f, 0.2f, i * 0.0091f};
    for (float d : {2.0f, 2.5f, 40.0f}) {
      const Surface a = surfaceAt(with, m, p, {0, 1, 0}, {}, 0, 0, d), b = surfaceAt(without, m, p, {0, 1, 0}, {}, 0, 0, d);
      CHECK(a.normal.x == b.normal.x && a.normal.y == b.normal.y && a.normal.z == b.normal.z);
      CHECK(a.roughness == b.roughness);
    }
  }
}

TEST(surface_detail_tilts_the_facing_and_shifts_the_shine_up_close) {
  SurfaceRecipe with = linen(), without = linen();
  without.detail.kind = SurfacePattern::Kind::kNone;
  const Material m;
  int tilted = 0, shifted = 0;
  float low = 1, high = 0;
  for (int i = 0; i < 200; ++i) {
    const Vec3 p{i * 0.00037f, 0.2f, i * 0.00061f};
    const Surface a = surfaceAt(with, m, p, {0, 1, 0}, {}, 0, 0, 0.3f), b = surfaceAt(without, m, p, {0, 1, 0}, {}, 0, 0, 0.3f);
    if (a.normal.y < 0.9999f && b.normal.y == 1.0f) ++tilted;
    if (a.roughness != b.roughness) ++shifted;
    low = std::min(low, a.roughness), high = std::max(high, a.roughness);
  }
  CHECK(tilted > 20);   // the threads' sides lean the facing
  CHECK(shifted > 100);  // and the shine goes up and down with them
  CHECK(high - low > 0.1f && high - low < 0.31f);  // by detailRoughness 0.15 either way (0.3 across)
  // nothing between: fading eases in, never jumps
  float prev = 0;
  for (float d : {0.5f, 1.0f, 1.25f, 1.5f, 1.75f, 2.0f}) {
    float spread = 0, lo = 1, hi = 0;
    for (int i = 0; i < 100; ++i) {
      const Surface a = surfaceAt(with, m, {i * 0.00031f, 0.2f, i * 0.00053f}, {0, 1, 0}, {}, 0, 0, d);
      lo = std::min(lo, a.roughness), hi = std::max(hi, a.roughness);
    }
    spread = hi - lo;
    if (d > 1.0f) CHECK(spread <= prev + 1e-6f);
    prev = spread;
  }
  CHECK(prev == 0);  // gone at the fade
}

TEST(surface_detail_wraps_at_its_own_tile) {
  const SurfaceRecipe r = linen();
  for (int i = 0; i < 100; ++i) {
    // on a 1/1024 m grid so adding the tile is exact (as surface_tile_wraps does)
    const Vec2 u{std::round(i * 0.0137f * 1024) / 1024, std::round(i * 0.0091f * 1024) / 1024};
    const DetailPoint a = detailAt(r, u), b = detailAt(r, u + Vec2(r.detailTile, 0)), c = detailAt(r, u + Vec2(0, r.detailTile));
    CHECK(near(a.height, b.height, 2e-3f) && near(a.height, c.height, 2e-3f));
  }
  SurfaceRecipe none = linen();
  none.detail.kind = SurfacePattern::Kind::kNone;
  const DetailPoint d = detailAt(none, {0.3f, 0.2f});
  CHECK(d.height == 0.5f && d.slopeX == 0 && d.slopeY == 0);  // no detail: nothing
}
