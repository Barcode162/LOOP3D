#pragma once
// Procedural noise: gradient (Perlin), simplex, cellular (Worley), fractal
// sums, ridges and domain warping. Deterministic for a seed.
#include <cstdint>

#include "engine/core/math.h"

namespace loop::noise {

float perlin(Vec3 p, uint32_t seed = 0);   // about -1..1
float perlin(Vec2 p, uint32_t seed = 0);
float simplex(Vec2 p, uint32_t seed = 0);  // about -1..1
// Distance to the nearest and second-nearest feature point (cells of size 1).
struct Cell { float f1, f2; Vec3 nearest; uint32_t id; };
Cell worley(Vec3 p, uint32_t seed = 0);

struct Fractal {
  int octaves = 6;
  float lacunarity = 2.0f, gain = 0.5f;
  uint32_t seed = 0;
};
float fbm(Vec3 p, const Fractal& f = {});
float fbm(Vec2 p, const Fractal& f = {});
// Sharp mountain ridges (Musgrave), 0..~1.
float ridged(Vec2 p, const Fractal& f = {});
// fbm sampled at a point pushed around by two more fbm fields (Quilez).
float warped(Vec2 p, float strength, const Fractal& f = {});

}  // namespace loop::noise
