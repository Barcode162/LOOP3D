// The fifteen surface patterns (docs/SURFACES.md section 5): they wrap at the
// tile, stay in range, depend on the seed, and bricks are true bricks.
#include <algorithm>
#include <cstdio>

#include "engine/core/image.h"
#include "engine/render/surface_patterns.h"
#include "tests/test.h"

using namespace loop;
using Kind = SurfacePattern::Kind;

namespace {
struct Named { const char* name; SurfacePattern p; };

SurfacePattern make(Kind k, float size, float stretch = 1, int twill = 1) {
  SurfacePattern p;
  p.kind = k, p.size = size, p.stretch = stretch, p.twill = twill;
  p.piece = 0.2f;
  return p;
}

// A good size for each kind on a 1 m tile (also used for the picture).
std::vector<Named> allKinds() {
  return {{"noise", make(Kind::kNoise, 0.2f)},       {"speckle", make(Kind::kSpeckle, 0.02f)},
          {"cells", make(Kind::kCells, 0.1f)},       {"pores", make(Kind::kPores, 0.05f)},
          {"cracks", make(Kind::kCracks, 0.15f)},    {"veins", make(Kind::kVeins, 0.2f)},
          {"strata", make(Kind::kStrata, 0.1f)},     {"grain", make(Kind::kGrain, 0.03f, 8)},
          {"planks", make(Kind::kPlanks, 0.15f, 6)}, {"bricks", make(Kind::kBricks, 0.215f, 3.3f)},
          {"tiles", make(Kind::kTiles, 0.25f)},      {"weave", make(Kind::kWeave, 0.02f, 1, 2)},
          {"knit", make(Kind::kKnit, 0.05f)},        {"brushed", make(Kind::kBrushed, 0.01f, 20)},
          {"checker", make(Kind::kChecker, 0.25f)},  {"grid", make(Kind::kGrid, 0.25f)}};
}

struct Lcg {
  uint32_t s = 12345;
  float next() { s = s * 1664525u + 1013904223u; return static_cast<float>(s >> 8) / 16777216.0f; }
};
}  // namespace

TEST(patterns_wrap_and_stay_in_range) {
  for (float tile : {1.0f, 2.5f})
    for (uint32_t seed : {1u, 77u})
      for (auto& k : allKinds()) {
        Lcg r;
        float worst = 0;
        bool inRange = true;
        for (int i = 0; i < 200; ++i) {
          // Places on a 1/1024 m grid, so that u + tile is exact in a float (no rounding of the input itself).
          Vec2 u(std::floor(r.next() * tile * 1024) / 1024, std::floor(r.next() * tile * 1024) / 1024);
          float m0, h0, p0, m1, h1, p1, m2, h2, p2;
          patternAt(k.p, u, tile, seed, m0, h0, p0);
          patternAt(k.p, u + Vec2(tile, 0), tile, seed, m1, h1, p1);
          patternAt(k.p, u + Vec2(0, tile), tile, seed, m2, h2, p2);
          worst = std::max({worst, std::fabs(m0 - m1), std::fabs(h0 - h1), std::fabs(p0 - p1), std::fabs(m0 - m2),
                            std::fabs(h0 - h2), std::fabs(p0 - p2)});
          if (m0 < 0 || m0 > 1 || h0 < 0 || h0 > 1) inRange = false;
          if (std::fabs(p0) > 0.2f + 1e-5f) inRange = false;
        }
        if (worst > 0.0001f) std::printf("  %s tile %.1f seed %u: wrap off by %g\n", k.name, tile, seed, worst);
        CHECK(worst <= 0.0001f);
        CHECK(inRange);
      }
}

TEST(patterns_seeds_and_repeats) {
  for (auto& k : allKinds()) {
    Lcg r;
    float diff = 0;
    for (int i = 0; i < 200; ++i) {
      Vec2 u(r.next(), r.next());
      float a[3], b[3], c[3];
      patternAt(k.p, u, 1, 5, a[0], a[1], a[2]);
      patternAt(k.p, u, 1, 5, b[0], b[1], b[2]);
      patternAt(k.p, u, 1, 6, c[0], c[1], c[2]);
      CHECK(a[0] == b[0] && a[1] == b[1] && a[2] == b[2]);  // the same seed twice: exactly
      diff += std::fabs(a[0] - c[0]) + std::fabs(a[1] - c[1]) + std::fabs(a[2] - c[2]);
    }
    if (!(diff > 0.01f)) std::printf("  %s: two seeds look the same\n", k.name);
    CHECK(diff > 0.01f);
  }
}

TEST(patterns_none_and_settings) {
  SurfacePattern none;
  float m, h, pc;
  patternAt(none, Vec2(0.3f, 0.2f), 1, 1, m, h, pc);
  CHECK(m == 0 && h == 0.5f && pc == 0);
  // piece 0 means no change of tone; contrast 0 flattens mix to the middle.
  SurfacePattern b = make(Kind::kBricks, 0.215f, 3.3f);
  b.piece = 0;
  patternAt(b, Vec2(0.3f, 0.2f), 1, 1, m, h, pc);
  CHECK(pc == 0);
  b.contrast = 0;
  patternAt(b, Vec2(0.3f, 0.2f), 1, 1, m, h, pc);
  CHECK_NEAR(m, 0.5f, 1e-6);
}

TEST(patterns_bricks_are_bricks) {
  SurfacePattern b = make(Kind::kBricks, 0.215f, 3.3f);
  float fit = fittedSize(b, 2.0f);
  CHECK(std::fabs(fit - 0.215f) / 0.215f < 0.05f);
  // Along the middle of one course (course 3 of 32, height 62.5 mm): count the joints. A tile
  // that wraps holds as many joints as bricks, so 9 joints are 9 whole bricks.
  const float courseY = 3.5f * 2.0f / 32;
  const int N = 4000;
  int joints = 0;
  bool wasBrick = false, samePiece = true;
  float prevPiece = 0;
  for (int i = 0; i < N; ++i) {
    float m, he, pc;
    patternAt(b, Vec2(2.0f * (i + 0.5f) / N, courseY), 2.0f, 3, m, he, pc);
    bool brick = m < 0.5f;
    if (!brick && wasBrick) ++joints;
    // Across one brick the piece number does not change.
    if (brick && wasBrick && std::fabs(pc - prevPiece) > 1e-6f) samePiece = false;
    if (brick) prevPiece = pc;
    wasBrick = brick;
  }
  CHECK(joints == 9);
  CHECK(samePiece);
  // The joint is lower than the brick: height near 0 in a joint, 1 on a brick.
  bool sawJoint = false, sawBrick = false;
  for (int i = 0; i < N; ++i) {
    float m, he, pc;
    patternAt(b, Vec2(2.0f * (i + 0.5f) / N, courseY), 2.0f, 3, m, he, pc);
    if (m > 0.99f) { sawJoint = true; CHECK(he < 0.02f); }
    if (m < 0.01f) { sawBrick = true; CHECK(he > 0.98f); }
  }
  CHECK(sawJoint && sawBrick);
}

TEST(patterns_picture) {
  auto kinds = allKinds();
  const int S = 160, cols = 5, rows = 4;
  Image img(S * cols, S * rows, Vec4(0.1f, 0.1f, 0.1f, 1));
  for (size_t k = 0; k < kinds.size(); ++k) {
    int ox = static_cast<int>(k % cols) * S, oy = static_cast<int>(k / cols) * S;
    for (int y = 0; y < S - 2; ++y)
      for (int x = 0; x < S - 2; ++x) {
        // The whole tile is sampled; `mix` fills the left half of the square, `height` the right.
        bool left = x < (S - 2) / 2;
        Vec2 u((x + 0.5f) / (S - 2), (y + 0.5f) / (S - 2));
        float m, h, pc;
        patternAt(kinds[k].p, u, 1.0f, 1, m, h, pc);
        float v = left ? m : h;
        img.at(ox + x, oy + y) = Vec4(v, v, v, 1);
      }
  }
  CHECK(writePng("build/patterns.png", img, true));
}
