#pragma once
// Random numbers and sampling: PCG32 (O'Neill), integer hashes, low-
// discrepancy sequences and the warps the renderers use.
#include <cstdint>

#include "engine/core/math.h"

namespace loop {

inline uint32_t hash32(uint32_t x) {  // lowbias32 (Wellons)
  x ^= x >> 16;
  x *= 0x7feb352dU;
  x ^= x >> 15;
  x *= 0x846ca68bU;
  x ^= x >> 16;
  return x;
}
inline uint32_t hashCombine(uint32_t a, uint32_t b) { return hash32(a ^ (b + 0x9e3779b9U + (a << 6) + (a >> 2))); }
inline uint64_t fnv1a(const char* s, uint64_t h = 1469598103934665603ULL) {
  while (*s) h = (h ^ static_cast<uint8_t>(*s++)) * 1099511628211ULL;
  return h;
}
// Uniform float in [0,1) from 32 random bits.
inline float toUnit(uint32_t v) { return static_cast<float>(v >> 8) * (1.0f / 16777216.0f); }

class Rng {
 public:
  explicit Rng(uint64_t seed = 0x853c49e6748fea9bULL, uint64_t stream = 0xda3e39cb94b95bdbULL) {
    inc_ = (stream << 1u) | 1u;
    state_ = 0;
    next();
    state_ += seed;
    next();
  }
  uint32_t next() {
    uint64_t old = state_;
    state_ = old * 6364136223846793005ULL + inc_;
    uint32_t xorshifted = static_cast<uint32_t>(((old >> 18u) ^ old) >> 27u);
    uint32_t rot = static_cast<uint32_t>(old >> 59u);
    return (xorshifted >> rot) | (xorshifted << ((32 - rot) & 31));
  }
  float uniform() { return toUnit(next()); }
  float range(float a, float b) { return a + (b - a) * uniform(); }
  int below(int n) { return n <= 0 ? 0 : static_cast<int>(next() % static_cast<uint32_t>(n)); }
  Vec2 uniform2() { return {uniform(), uniform()}; }
  // Standard normal (Box-Muller).
  float gaussian() {
    float u1 = std::max(uniform(), 1e-7f), u2 = uniform();
    return std::sqrt(-2 * std::log(u1)) * std::cos(kTwoPi * u2);
  }

 private:
  uint64_t state_, inc_;
};

// Radical inverse in base 2 with a scramble (for Sobol-like dimension 0).
inline float vanDerCorput(uint32_t i, uint32_t scramble = 0) {
  i = (i << 16) | (i >> 16);
  i = ((i & 0x00ff00ff) << 8) | ((i & 0xff00ff00) >> 8);
  i = ((i & 0x0f0f0f0f) << 4) | ((i & 0xf0f0f0f0) >> 4);
  i = ((i & 0x33333333) << 2) | ((i & 0xcccccccc) >> 2);
  i = ((i & 0x55555555) << 1) | ((i & 0xaaaaaaaa) >> 1);
  return toUnit(i ^ scramble);
}
// R2 sequence (Roberts): an excellent 2D low-discrepancy sequence.
inline Vec2 r2(uint32_t i, Vec2 offset = {}) {
  constexpr double a1 = 0.7548776662466927, a2 = 0.5698402909980532;
  double x = offset.x + a1 * i, y = offset.y + a2 * i;
  return {static_cast<float>(x - std::floor(x)), static_cast<float>(y - std::floor(y))};
}

// ---- Warps ----
inline Vec2 concentricDisk(Vec2 u) {
  Vec2 o{2 * u.x - 1, 2 * u.y - 1};
  if (o.x == 0 && o.y == 0) return {};
  float r, th;
  if (std::fabs(o.x) > std::fabs(o.y)) r = o.x, th = (kPi / 4) * (o.y / o.x);
  else r = o.y, th = kPi / 2 - (kPi / 4) * (o.x / o.y);
  return {r * std::cos(th), r * std::sin(th)};
}
// A regular polygon aperture with `blades` sides (0 = round), rotated.
inline Vec2 polygonAperture(Vec2 u, int blades, float rotation) {
  if (blades < 3) return concentricDisk(u);
  float seg = std::floor(u.x * blades);
  float a0 = rotation + seg * kTwoPi / blades, a1 = a0 + kTwoPi / blades;
  // uniform point in triangle (0, p0, p1)
  float s = std::sqrt(u.y), t = u.x * blades - seg;
  Vec2 p0{std::cos(a0), std::sin(a0)}, p1{std::cos(a1), std::sin(a1)};
  return (p0 * (1 - t) + p1 * t) * s;
}
inline Vec3 cosineHemisphere(Vec2 u) {
  Vec2 d = concentricDisk(u);
  float z = std::sqrt(std::max(0.0f, 1 - d.x * d.x - d.y * d.y));
  return {d.x, d.y, z};
}
inline Vec3 uniformSphere(Vec2 u) {
  float z = 1 - 2 * u.x, r = std::sqrt(std::max(0.0f, 1 - z * z)), phi = kTwoPi * u.y;
  return {r * std::cos(phi), r * std::sin(phi), z};
}
inline Vec3 uniformCone(Vec2 u, float cosMax) {
  float c = (1 - u.x) + u.x * cosMax, s = std::sqrt(std::max(0.0f, 1 - c * c)), phi = kTwoPi * u.y;
  return {std::cos(phi) * s, std::sin(phi) * s, c};
}
inline Vec2 uniformTriangle(Vec2 u) {
  float su = std::sqrt(u.x);
  return {1 - su, u.y * su};
}
inline float powerHeuristic(float a, float b) {
  float a2 = a * a, b2 = b * b;
  return a2 + b2 > 0 ? a2 / (a2 + b2) : 0;
}

}  // namespace loop
