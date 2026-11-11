// The cook's baked pictures of a surface (docs/SURFACES.md section 8; order M4):
// a recipe without a pattern bakes to one colour and flat; bricks' joints are
// lower and lean; the tile has no seam; far copies are rougher only where the
// bumps disagree; a second ask is free.
#include <chrono>
#include <cmath>
#include <cstdio>
#include <thread>
#include <vector>

#include "engine/core/image.h"
#include "engine/core/jobs.h"
#include "engine/render/surface.h"
#include "engine/render/surface_bake.h"
#include "engine/render/surface_patterns.h"
#include "tests/test.h"

using namespace loop;
using Kind = SurfacePattern::Kind;

namespace {

// Brick-red bricks a quarter of a metre long in four-times-thinner courses, with pale mortar,
// on a 1 m tile: at 256 pixels a brick is 64 pixels long and a course 16 high.
SurfaceRecipe bricks(float bump, uint32_t seed = 3) {
  SurfaceRecipe r;
  r.name = "bricks";
  r.seed = seed;
  r.tile = 1;
  r.base.on = true;
  r.base.color = {0.30f, 0.08f, 0.05f}, r.base.color2 = {0.40f, 0.38f, 0.35f};
  r.base.roughness = 0.7f, r.base.roughness2 = 0.9f;
  r.base.bump = bump;
  r.base.pattern.kind = Kind::kBricks, r.base.pattern.size = 0.25f, r.base.pattern.stretch = 4;
  return r;
}

int byte(float v) { return static_cast<int>(std::lround(v * 255)); }

// How far the stored facing leans: the length of its two sideways parts, unpacked.
float lean(const Vec4& s) { return std::sqrt((2 * s.x - 1) * (2 * s.x - 1) + (2 * s.y - 1) * (2 * s.y - 1)); }

// The bricks' joint at the middle of pixel (x, y): 1 in the joint, 0 on the brick.
float jointAt(const SurfaceRecipe& r, int x, int y, int n) {
  float mix, h, piece;
  patternAt(r.base.pattern, {(x + 0.5f) / n * r.tile, (y + 0.5f) / n * r.tile}, r.tile, 0, mix, h, piece);
  return mix;
}

float diff(const Vec4& a, const Vec4& b, int c) { return std::fabs(a[c] - b[c]); }

// Across the tile's edge (the last column to the first, or the last row to the first), the
// average difference in channel `c`, and the average between neighbours inside.
void seam(const Image& im, bool across, int c, float& edge, float& inside) {
  const int n = im.width;
  double e = 0, in = 0;
  for (int a = 0; a < n; ++a) {
    e += across ? diff(im.at(n - 1, a), im.at(0, a), c) : diff(im.at(a, n - 1), im.at(a, 0), c);
    for (int b = 0; b + 1 < n; ++b) in += across ? diff(im.at(b, a), im.at(b + 1, a), c) : diff(im.at(a, b), im.at(a, b + 1), c);
  }
  edge = static_cast<float>(e / n), inside = static_cast<float>(in / (static_cast<double>(n) * (n - 1)));
}

}  // namespace

TEST(surface_bake_without_a_pattern_is_one_colour_and_flat) {
  SurfaceRecipe r;
  r.name = "plain";
  r.tile = 0.5f;
  r.base.on = true;
  r.base.color = r.base.color2 = {0.30f, 0.50f, 0.20f};
  r.base.roughness = r.base.roughness2 = 0.6f;
  r.base.metal = 1;
  for (float bump : {0.0f, 0.002f}) {
    r.base.bump = bump;
    auto p = bakeSurface(r, 64);
    CHECK(p && p->colour.size() == 7 && p->shape.size() == 7);  // 64, 32, ... 1
    CHECK(p && p->detail.empty());                              // order M7
    if (!p || p->colour.size() != 7 || p->shape.size() != 7) continue;
    int wrong = 0;
    for (size_t i = 0; i < p->colour.size(); ++i) {
      const Image &col = p->colour[i], &sh = p->shape[i];
      CHECK(col.width == (64 >> i) && col.height == (64 >> i) && sh.width == col.width && sh.height == col.height);
      for (size_t k = 0; k < col.px.size(); ++k) {
        const Vec4 c = col.px[k], s = sh.px[k];
        bool ok = std::fabs(c.x - linearToSrgb(r.base.color.x)) <= 1 / 255.0f &&
                  std::fabs(c.y - linearToSrgb(r.base.color.y)) <= 1 / 255.0f &&
                  std::fabs(c.z - linearToSrgb(r.base.color.z)) <= 1 / 255.0f;
        ok = ok && byte(s.x) == 128 && byte(s.y) == 128;
        ok = ok && std::fabs(s.z - r.base.roughness) < 1e-5f && std::fabs(s.w - 1) < 1e-5f;
        wrong += !ok;
      }
    }
    CHECK(wrong == 0);
  }
}

TEST(surface_bake_any_size_halves_rounded_down) {
  auto p = bakeSurface(bricks(0.004f), 6);
  CHECK(p->colour.size() == 3 && p->shape.size() == 3);
  if (p->colour.size() == 3) CHECK(p->colour[0].width == 6 && p->colour[1].width == 3 && p->colour[2].width == 1);
}

TEST(surface_bake_bricks_joints_are_lower_and_lean) {
  const SurfaceRecipe r = bricks(0.004f);
  const int n = 256;
  auto p = bakeSurface(r, n);
  const Image &col = p->colour[0], &sh = p->shape[0];
  double hJoint = 0, hBrick = 0, leanJoint = 0;
  int joints = 0, faces = 0, inside = 0, leaning = 0;
  for (int y = 0; y < n; ++y)
    for (int x = 0; x < n; ++x) {
      float m = jointAt(r, x, y, n);
      if (m > 0.5f) hJoint += col.at(x, y).w, leanJoint += lean(sh.at(x, y)), ++joints;
      if (m < 0.01f) hBrick += col.at(x, y).w, ++faces;
      // well inside a brick (nothing of a joint within two pixels): flat
      bool clear = true;
      for (int d = -2; d <= 2 && clear; ++d)
        clear = jointAt(r, (x + d + n) % n, y, n) < 1e-3f && jointAt(r, x, (y + d + n) % n, n) < 1e-3f;
      if (clear) ++inside, leaning += lean(sh.at(x, y)) > 1e-4f;
    }
  CHECK(joints > 1000 && faces > 10000 && inside > 10000);
  hJoint /= joints, hBrick /= faces, leanJoint /= joints;
  std::printf("  bricks: joint height %.3f, brick %.3f; joint lean %.3f\n", hJoint, hBrick, leanJoint);
  CHECK(hJoint < 0.3 && hBrick > 0.9);  // the joint is lower
  CHECK(leanJoint > 0.1);               // and its sides lean
  CHECK(leaning == 0);                  // the brick's face does not
}

TEST(surface_bake_tile_has_no_seam) {
  const SurfaceRecipe r = bricks(0.004f);
  const int n = 256;
  auto p = bakeSurface(r, n);
  const Image &col = p->colour[0], &sh = p->shape[0];
  // The left column continues the right, and the top the bottom: no more different than
  // neighbours inside are, on average. Colour, height, roughness and metal.
  for (bool across : {true, false}) {
    for (int c = 0; c < 4; ++c) {
      float edge, inside;
      seam(col, across, c, edge, inside);
      CHECK(edge <= 2 * inside + 1e-6f);
    }
    // roughness, metal, and the facing's part along the edge
    for (int c : {across ? 1 : 0, 2, 3}) {
      float edge, inside;
      seam(sh, across, c, edge, inside);
      CHECK(edge <= 2 * inside + 1e-6f);
    }
  }
  // The facing's part across the edge flips sign in a joint's middle, and the bond puts one
  // exactly on the tile's edge, so the average test would fail on a joint, not on a seam
  // (measured: 13 and 6 times the inside average). Instead, every channel: the pair across
  // the edge is the same as the pair across the joint one brick in (64 pixels), and the pair
  // across the top and bottom as the one two courses down (32 pixels).
  int wrong = 0;
  for (int a = 0; a < n; ++a)
    for (int c = 0; c < 4; ++c) {
      wrong += diff(sh.at(n - 1, a), sh.at(63, a), c) > 1e-3f || diff(sh.at(0, a), sh.at(64, a), c) > 1e-3f;
      wrong += diff(sh.at(a, n - 1), sh.at(a, 31), c) > 1e-3f || diff(sh.at(a, 0), sh.at(a, 32), c) > 1e-3f;
    }
  CHECK(wrong == 0);
  // Bricks are the same on both sides of the edge (so a facing made without wrapping would
  // look right there too): soft noise is not, so every channel of it must pass the average test.
  SurfaceRecipe noise = bricks(0.002f);
  noise.base.pattern.kind = Kind::kNoise, noise.base.pattern.size = 0.05f;
  auto q = bakeSurface(noise, n);
  for (bool across : {true, false})
    for (int c = 0; c < 4; ++c) {
      float e1, i1, e2, i2;
      seam(q->colour[0], across, c, e1, i1);
      seam(q->shape[0], across, c, e2, i2);
      CHECK(e1 <= 2 * i1 + 1e-6f);
      CHECK(e2 <= 2 * i2 + 1e-6f);
    }
}

TEST(surface_bake_facing_is_the_slope_in_metres_wrapping) {
  // Every pixel's facing, the edges' too, is the slope of the true height (alpha times the
  // bump, metres) between its two neighbours (wrapping) over their true distance (metres).
  SurfaceRecipe r = bricks(0.003f, 5);
  r.tile = 0.5f;
  r.base.pattern.kind = Kind::kNoise, r.base.pattern.size = 0.04f;
  const int n = 64;
  auto p = bakeSurface(r, n);
  const Image &col = p->colour[0], &sh = p->shape[0];
  const float k = r.base.bump / (2 * r.tile / n);
  float worst = 0, steepest = 0;
  for (int y = 0; y < n; ++y)
    for (int x = 0; x < n; ++x) {
      float sx = (col.at((x + 1) % n, y).w - col.at((x + n - 1) % n, y).w) * k;
      float sy = (col.at(x, (y + 1) % n).w - col.at(x, (y + n - 1) % n).w) * k;
      Vec3 f = normalize(Vec3{-sx, -sy, 1});
      worst = std::max({worst, std::fabs(sh.at(x, y).x - (0.5f + 0.5f * f.x)), std::fabs(sh.at(x, y).y - (0.5f + 0.5f * f.y))});
      steepest = std::max(steepest, lean(sh.at(x, y)));
    }
  CHECK(worst < 1e-5f);
  CHECK(steepest > 0.05f);  // the noise really bumps
}

TEST(surface_bake_far_copies_are_rougher_only_where_bumps_disagree) {
  auto average = [](const Image& im) {
    double s = 0;
    for (const Vec4& v : im.px) s += v.z;
    return s / im.px.size();
  };
  auto bumped = bakeSurface(bricks(0.004f), 256), flat = bakeSurface(bricks(0), 256);
  double b0 = average(bumped->shape[0]), b2 = average(bumped->shape[2]);
  double f0 = average(flat->shape[0]), f2 = average(flat->shape[2]);
  std::printf("  bricks roughness, first copy and third: bumped %.4f %.4f, flat %.4f %.4f\n", b0, b2, f0, f2);
  CHECK(b2 > b0 + 1e-3);
  CHECK(std::fabs(f2 - f0) < 1e-4);
  // a far copy is never rougher than 1
  for (const Image& im : bumped->shape)
    for (const Vec4& v : im.px) CHECK(v.z <= 1);
  // and the facing stays whole: the smallest copy is flat on average for bricks
  const Vec4 last = bumped->shape.back().px[0];
  CHECK(byte(last.x) == 128 && byte(last.y) == 128);
}

TEST(surface_bake_smaller_copies_follow_the_rules) {
  // Each pixel of a smaller copy from its four in the copy before: the colour averaged in
  // linear light, the height and metal averaged, the facing averaged and made whole, and the
  // roughness raised by Toksvig's rule from the averaged facing's length.
  SurfaceRecipe r = bricks(0.004f, 7);
  r.base.metal = 0.25f;
  auto p = bakeSurface(r, 64);
  float worst = 0;
  auto unpack = [](const Vec4& s) {
    float x = 2 * s.x - 1, y = 2 * s.y - 1;
    return Vec3{x, y, std::sqrt(std::max(0.0f, 1 - x * x - y * y))};
  };
  for (size_t i = 1; i < p->shape.size(); ++i) {
    const Image &c0 = p->colour[i - 1], &s0 = p->shape[i - 1], &c1 = p->colour[i], &s1 = p->shape[i];
    for (int y = 0; y < c1.height; ++y)
      for (int x = 0; x < c1.width; ++x) {
        Vec3 lin{0}, f{0};
        float h = 0, rough = 0, metal = 0;
        for (int j = 0; j < 2; ++j)
          for (int k = 0; k < 2; ++k) {
            const Vec4 &c = c0.at(2 * x + k, 2 * y + j), &s = s0.at(2 * x + k, 2 * y + j);
            lin = lin + Vec3{srgbToLinear(c.x), srgbToLinear(c.y), srgbToLinear(c.z)};
            f = f + unpack(s), h += c.w, rough += s.z, metal += s.w;
          }
        lin = lin * 0.25f, f = f * 0.25f, h *= 0.25f, rough *= 0.25f, metal *= 0.25f;
        const float L = length(f);
        const float want = std::min(1.0f, std::sqrt(rough * rough + (1 - L) / L));
        const Vec3 whole = f / L;
        const Vec4 &c = c1.at(x, y), &s = s1.at(x, y);
        worst = std::max({worst, std::fabs(c.x - linearToSrgb(lin.x)), std::fabs(c.y - linearToSrgb(lin.y)),
                          std::fabs(c.z - linearToSrgb(lin.z)), std::fabs(c.w - h), std::fabs(s.w - metal),
                          std::fabs(s.z - want), std::fabs(s.x - (0.5f + 0.5f * whole.x)),
                          std::fabs(s.y - (0.5f + 0.5f * whole.y))});
      }
  }
  std::printf("  smaller copies: worst difference from the rules %.2g\n", worst);
  CHECK(worst < 1e-4f);
}

TEST(surface_bake_second_ask_gives_the_same_pictures) {
  const SurfaceRecipe r = bricks(0.004f, 11);
  auto a = bakeSurface(r, 32), b = bakeSurface(r, 32);
  CHECK(a == b);
  CHECK(bakeSurface(r, 16) != a);              // another size is another bake
  CHECK(bakeSurface(bricks(0.004f, 12), 32) != a);  // and another recipe
  // Asked from four threads at once, never baked before: all four get the same pictures.
  const SurfaceRecipe fresh = bricks(0.004f, 21);
  std::shared_ptr<const SurfacePictures> got[4];
  std::vector<std::thread> threads;
  for (int i = 0; i < 4; ++i) threads.emplace_back([&, i] { got[i] = bakeSurface(fresh, 64); });
  for (std::thread& t : threads) t.join();
  CHECK(got[0] && got[0] == got[1] && got[0] == got[2] && got[0] == got[3]);
  CHECK(got[0] == bakeSurface(fresh, 64));
}

TEST(surface_bake_time_at_256) {
  // A typical recipe never baked before (its own seed): bricks, a small change, a coat of soot.
  SurfaceRecipe r = bricks(0.004f, 977);
  r.varySmall = 0.02f, r.varySmallAmount = 0.1f;
  r.coat[0].on = true;
  r.coat[0].color = {0.05f, 0.05f, 0.05f}, r.coat[0].roughness = 0.95f, r.coat[0].bump = 0.0005f;
  r.coat[0].pattern.kind = Kind::kNoise, r.coat[0].pattern.size = 0.1f;
  r.coat[0].cover = 0.3f;
  auto t0 = std::chrono::steady_clock::now();
  auto p = bakeSurface(r, 256);
  double ms = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - t0).count();
  std::printf("  surface bake at 256: %.1f ms on %d threads (wanted: under 200 ms on four cores)\n", ms,
              JobSystem::shared().threads());
  CHECK(p->colour.size() == 9 && p->shape.size() == 9);
}
