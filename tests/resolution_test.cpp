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
