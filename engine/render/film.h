#pragma once
// The film: turns light measured in the scene (nits) into the picture you
// see, the way a camera, a lens, film stock and a colourist would: exposure,
// lens (fringing, distortion, vignette), bloom and halation, white balance,
// grading, a tone curve (AgX / ACES / filmic), grain, bars. The live GPU
// renderer runs the same steps in its shader (gpu/), with these numbers.
#include <cstdint>

#include "engine/core/image.h"
#include "engine/render/scene.h"

namespace loop {

struct FilmResult {
  float exposure = 1;  // the multiplier used
  float ev100 = 0;     // the exposure value it equals
  float averageLuminance = 0;
};

// Meters the scene like a camera on auto: the log-average brightness with
// the brightest and darkest few percent left out, aiming for middle grey.
float meterLuminance(const Image& hdr);

// `hdr`: linear scene light (nits). Returns the display picture (sRGB,
// encoded, 0..1). `frame` seeds the grain so it dances between frames.
Image develop(const Image& hdr, const Camera& cam, const Look& look, uint32_t frame = 0, FilmResult* info = nullptr);

// The per-pixel colour steps alone (after exposure and lens), for tests and
// for the GPU to match: linear exposed colour → display sRGB.
Vec3 gradePixel(Vec3 c, const Look& look);
Vec3 tonemapAgX(Vec3 c);
Vec3 tonemapAces(Vec3 c);
Vec3 tonemapFilmic(Vec3 c);
// White balance as a colour multiplier (temperature, tint in -1..1).
Vec3 whiteBalance(float temperature, float tint);

}  // namespace loop
