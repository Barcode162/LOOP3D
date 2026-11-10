// The automatic resolution steered by the step timings (engine/render/resolution.h).
#include "engine/render/resolution.h"
#include "tests/test.h"

using namespace loop;

namespace {
FrameGraph::Time step(const char* name, float ms, uint32_t missed = 0) {
  FrameGraph::Time t;
  t.step = name, t.ms = ms, t.missed = missed;
  return t;
}
}  // namespace

TEST(resolution_split_counts_what_shrinks) {
  // the Glade on High: lighting per pixel, blending and bloom on the full picture,
  // the far shadow drawn again a few frames ago (not steady)
  FrameSplit s = splitFrame({step("visibility", 5), step("light", 1), step("blend", 1.6f), step("bloom", 1.5f), step("film", 0.2f),
                             step("shadow", 1.5f), step("far shadow", 23, 7)});
  CHECK_NEAR(s.scaledMs, 6, 1e-4);
  CHECK_NEAR(s.fixedMs, 4.8f, 1e-4);
  // not upscaled: blending, bloom and the film work on the drawn picture
  FrameSplit d = splitFrame({step("light", 2), step("blend", 1), step("bloom", 1), step("shadow", 1)}, false);
  CHECK_NEAR(d.scaledMs, 4, 1e-4);
  CHECK_NEAR(d.fixedMs, 1, 1e-4);
}

TEST(resolution_fits_only_the_part_that_shrinks) {
  FrameSplit s{4.8f, 6};  // 10.8 ms at 0.5
  // room for 16.7 * 0.85: the drawn part may grow, not drop
  float fit = scaleToFit(s, 0.5f, 16.7f * 0.85f);
  CHECK(fit > 0.6f);
  CHECK_NEAR(msAtScale(s, 0.5f, fit), 16.7f * 0.85f, 1e-3);
  // the old way (all the time scales) would have asked for less
  CHECK(fit > 0.5f * std::sqrt(16.7f * 0.85f / 10.8f));
}

TEST(resolution_holds_when_shrinking_cant_help) {
  FrameSplit s{18, 3};  // the work that doesn't scale fills the budget alone
  CHECK_NEAR(scaleToFit(s, 0.7f, 16.7f * 0.85f), 0.7f, 1e-6);
}

TEST(resolution_learns_what_really_shrinks) {
  // the Glade on High (your phone): visibility 8 ms at 0.47, 6.9 at 0.35 (its
  // leaves and grass: mostly the same at any size); light 2.1 and 1.05 (per pixel)
  ScaleLearner L;
  CHECK_NEAR(L.scaledShare("visibility", 0.47f), 1, 1e-6);  // unknown: by its name
  L.observe({step("visibility", 8), step("light", 2.07f), step("blend", 1.4f)}, 0.47f);
  CHECK_NEAR(L.scaledShare("visibility", 0.47f), 1, 1e-6);  // one size seen: still by its name
  L.observe({step("visibility", 6.86f), step("light", 1.05f), step("blend", 1.3f)}, 0.35f);
  const float vis = L.scaledShare("visibility", 0.35f), light = L.scaledShare("light", 0.35f);
  CHECK(vis > 0.2f && vis < 0.45f);  // mostly fixed
  CHECK(light > 0.9f);               // per pixel
  FrameSplit s = splitFrame({step("visibility", 6.86f), step("light", 1.05f), step("blend", 1.3f)}, true, L, 0.35f);
  CHECK_NEAR(s.scaledMs + s.fixedMs, 6.86f + 1.05f + 1.3f, 1e-3);
  CHECK(s.fixedMs > 5.0f);
  // shrinking from 0.35 for an 18 ms frame: the visibility's fixed part makes it hold, not blur
  FrameSplit heavy{s.fixedMs + 9, s.scaledMs};
  CHECK_NEAR(scaleToFit(heavy, 0.35f, 16.7f * 0.85f), 0.35f, 1e-6);
  // a noisy fit (heavier at the smaller size) can't freeze it: a fifth still shrinks
  ScaleLearner N;
  N.observe({step("light", 5)}, 0.5f);
  N.observe({step("light", 7)}, 0.35f);
  CHECK_NEAR(N.scaledShare("light", 0.35f), 0.2f, 1e-6);
}
