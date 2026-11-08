#pragma once
// The score: how far a live picture is from the cinema renderer's picture of
// the same view, the picture pipeline's second number beside milliseconds
// (docs/PIPELINE.md). Both pictures are first made half as big (the path
// tracer's last grain and the live picture's edges count less than what is
// really different), the live one is brought to the cinema one's brightness
// (each renderer meters its own exposure; how far apart they were comes back
// as `brightness`), and the score is the average colour difference a person
// sees between them: ΔE in CIELAB (1976), over the pixels either shows (a
// film's black bars are left out). 0 is the same picture; about 2 is barely
// visible side by side; 10 and more is plainly different.
#include "engine/core/image.h"

namespace loop {

struct PictureScore {
  float score = 0;       // the average ΔE
  float worstTenth = 0;  // the ΔE the worst tenth of the picture is over
  float brightness = 1;  // live ÷ cinema, before matching
  Image difference;      // where they differ (half size): black the same, white 20 ΔE or more
};

// `live` and `cinema`: linear colour, the same size.
PictureScore scorePictures(const Image& live, const Image& cinema);

// CIELAB (D65 white) of a linear sRGB colour, and the distance between two.
Vec3 linearSrgbToLab(Vec3 c);
float deltaE(Vec3 lab1, Vec3 lab2);

}  // namespace loop
