// The score against cinema (engine/render/score.h): how far a live picture is
// from the cinema renderer's.
#include "engine/render/score.h"
#include "tests/test.h"

using namespace loop;

namespace {
Image filled(int w, int h, Vec3 c) { return Image(w, h, Vec4(c, 1)); }
}  // namespace

TEST(score_colours_in_cielab) {
  Vec3 white = linearSrgbToLab({1, 1, 1}), black = linearSrgbToLab({0, 0, 0});
  CHECK_NEAR(white.x, 100, 0.05f);
  CHECK_NEAR(white.y, 0, 0.05f);
  CHECK_NEAR(white.z, 0, 0.05f);
  CHECK_NEAR(black.x, 0, 1e-4f);
  CHECK_NEAR(deltaE(white, black), 100, 0.05f);
  CHECK_NEAR(linearSrgbToLab({0.18f, 0.18f, 0.18f}).x, 49.5f, 0.5f);  // middle grey is about half way
}

TEST(score_the_same_picture_is_zero) {
  Image a = filled(40, 30, {0.2f, 0.4f, 0.1f});
  PictureScore s = scorePictures(a, a);
  CHECK_NEAR(s.score, 0, 1e-4f);
  CHECK_NEAR(s.brightness, 1, 1e-4f);
  CHECK(s.difference.width == 20 && s.difference.height == 15);
}

TEST(score_brightness_is_matched_and_reported) {
  Image cinema = filled(40, 30, {0.2f, 0.4f, 0.1f}), live = filled(40, 30, {0.4f, 0.8f, 0.2f});
  PictureScore s = scorePictures(live, cinema);
  CHECK_NEAR(s.brightness, 2, 1e-3f);  // each renderer meters its own exposure
  CHECK_NEAR(s.score, 0, 1e-3f);       // the same picture, only brighter
}

TEST(score_a_different_colour_scores_high) {
  Image cinema = filled(40, 30, {0.3f, 0.05f, 0.05f}), live = filled(40, 30, {0.05f, 0.3f, 0.05f});
  PictureScore s = scorePictures(live, cinema);
  CHECK(s.score > 40);  // red against green, even at the same brightness
  CHECK_NEAR(s.worstTenth, s.score, 0.5f);
}

TEST(score_leaves_out_black_bars) {
  // the top half black in both (a film's bar), the bottom half differing
  Image cinema = filled(40, 40, {0.3f, 0.3f, 0.3f}), live = filled(40, 40, {0.3f, 0.2f, 0.4f});
  Image cinemaBars = cinema, liveBars = live;
  for (int y = 0; y < 20; ++y)
    for (int x = 0; x < 40; ++x) cinemaBars.at(x, y) = liveBars.at(x, y) = Vec4(0, 0, 0, 1);
  PictureScore whole = scorePictures(live, cinema), barred = scorePictures(liveBars, cinemaBars);
  CHECK(whole.score > 5);
  CHECK_NEAR(barred.score, whole.score, 0.5f);  // the bars don't water it down
}

TEST(score_fine_grain_counts_less_than_what_differs) {
  // a checkerboard of grain around grey, against plain grey: halving evens most of it out
  Image grey = filled(64, 64, {0.2f, 0.2f, 0.2f}), grain = grey;
  for (int y = 0; y < 64; ++y)
    for (int x = 0; x < 64; ++x) grain.at(x, y) = Vec4(Vec3((x + y) % 2 ? 0.25f : 0.15f), 1);
  Image shifted = filled(64, 64, {0.2f, 0.15f, 0.25f});
  CHECK(scorePictures(grain, grey).score < 1);
  CHECK(scorePictures(shifted, grey).score > 5);
}
