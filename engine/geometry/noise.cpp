#include "engine/geometry/noise.h"

#include "engine/core/random.h"

namespace loop::noise {

namespace {
inline float fade(float t) { return t * t * t * (t * (t * 6 - 15) + 10); }
inline uint32_t h3(int x, int y, int z, uint32_t s) {
  return hash32(static_cast<uint32_t>(x) * 0x8da6b343U ^ static_cast<uint32_t>(y) * 0xd8163841U ^ static_cast<uint32_t>(z) * 0xcb1ab31fU ^ s * 0x9e3779b9U);
}
inline float grad3(uint32_t h, float x, float y, float z) {
  switch (h & 15) {
    case 0: return x + y; case 1: return -x + y; case 2: return x - y; case 3: return -x - y;
    case 4: return x + z; case 5: return -x + z; case 6: return x - z; case 7: return -x - z;
    case 8: return y + z; case 9: return -y + z; case 10: return y - z; case 11: return -y - z;
    case 12: return x + y; case 13: return -x + y; case 14: return -y + z; default: return -y - z;
  }
}
inline float grad2(uint32_t h, float x, float y) {
  float a = static_cast<float>(h & 255) * (kTwoPi / 256.0f);
  return std::cos(a) * x + std::sin(a) * y;
}
}  // namespace

float perlin(Vec3 p, uint32_t seed) {
  int xi = static_cast<int>(std::floor(p.x)), yi = static_cast<int>(std::floor(p.y)), zi = static_cast<int>(std::floor(p.z));
  float x = p.x - xi, y = p.y - yi, z = p.z - zi;
  float u = fade(x), v = fade(y), w = fade(z);
  auto g = [&](int dx, int dy, int dz) { return grad3(h3(xi + dx, yi + dy, zi + dz, seed), x - dx, y - dy, z - dz); };
  float x00 = lerp(g(0, 0, 0), g(1, 0, 0), u), x10 = lerp(g(0, 1, 0), g(1, 1, 0), u);
  float x01 = lerp(g(0, 0, 1), g(1, 0, 1), u), x11 = lerp(g(0, 1, 1), g(1, 1, 1), u);
  return lerp(lerp(x00, x10, v), lerp(x01, x11, v), w);
}

float perlin(Vec2 p, uint32_t seed) {
  int xi = static_cast<int>(std::floor(p.x)), yi = static_cast<int>(std::floor(p.y));
  float x = p.x - xi, y = p.y - yi;
  float u = fade(x), v = fade(y);
  auto g = [&](int dx, int dy) { return grad2(h3(xi + dx, yi + dy, 0, seed), x - dx, y - dy); };
  return 1.41421356f * lerp(lerp(g(0, 0), g(1, 0), u), lerp(g(0, 1), g(1, 1), u), v);
}

float simplex(Vec2 p, uint32_t seed) {
  const float F2 = 0.366025403f, G2 = 0.211324865f;
  float s = (p.x + p.y) * F2;
  int i = static_cast<int>(std::floor(p.x + s)), j = static_cast<int>(std::floor(p.y + s));
  float t = (i + j) * G2;
  float x0 = p.x - (i - t), y0 = p.y - (j - t);
  int i1 = x0 > y0 ? 1 : 0, j1 = 1 - i1;
  float x1 = x0 - i1 + G2, y1 = y0 - j1 + G2, x2 = x0 - 1 + 2 * G2, y2 = y0 - 1 + 2 * G2;
  auto corner = [&](float x, float y, int ci, int cj) {
    float tt = 0.5f - x * x - y * y;
    if (tt < 0) return 0.0f;
    tt *= tt;
    return tt * tt * grad2(h3(ci, cj, 1, seed), x, y);
  };
  return 70.0f * (corner(x0, y0, i, j) + corner(x1, y1, i + i1, j + j1) + corner(x2, y2, i + 1, j + 1));
}

Cell worley(Vec3 p, uint32_t seed) {
  int xi = static_cast<int>(std::floor(p.x)), yi = static_cast<int>(std::floor(p.y)), zi = static_cast<int>(std::floor(p.z));
  Cell c{kInf, kInf, {}, 0};
  for (int dz = -1; dz <= 1; ++dz)
    for (int dy = -1; dy <= 1; ++dy)
      for (int dx = -1; dx <= 1; ++dx) {
        uint32_t h = h3(xi + dx, yi + dy, zi + dz, seed);
        Vec3 fp{xi + dx + toUnit(h), yi + dy + toUnit(hash32(h)), zi + dz + toUnit(hash32(h ^ 0x51ed27U))};
        float d = length(fp - p);
        if (d < c.f1) c.f2 = c.f1, c.f1 = d, c.nearest = fp, c.id = h;
        else if (d < c.f2) c.f2 = d;
      }
  return c;
}

float fbm(Vec3 p, const Fractal& f) {
  float sum = 0, amp = 1, norm = 0;
  for (int o = 0; o < f.octaves; ++o) {
    sum += amp * perlin(p, f.seed + o * 131);
    norm += amp;
    amp *= f.gain;
    p = p * f.lacunarity + Vec3(1.7f, 9.2f, 3.1f);
  }
  return sum / norm;
}

float fbm(Vec2 p, const Fractal& f) {
  float sum = 0, amp = 1, norm = 0;
  for (int o = 0; o < f.octaves; ++o) {
    sum += amp * perlin(p, f.seed + o * 131);
    norm += amp;
    amp *= f.gain;
    // rotate each octave so the grid never lines up
    p = Vec2{0.8f * p.x - 0.6f * p.y, 0.6f * p.x + 0.8f * p.y} * f.lacunarity + Vec2(5.3f, 1.3f);
  }
  return sum / norm;
}

float ridged(Vec2 p, const Fractal& f) {
  float sum = 0, amp = 0.5f, weight = 1, norm = 0;
  for (int o = 0; o < f.octaves; ++o) {
    float n = 1 - std::fabs(perlin(p, f.seed + o * 71));
    n *= n * weight;
    weight = saturate(n * 2);
    sum += n * amp;
    norm += amp;
    amp *= f.gain;
    p = Vec2{0.8f * p.x - 0.6f * p.y, 0.6f * p.x + 0.8f * p.y} * f.lacunarity;
  }
  return sum / norm;
}

float warped(Vec2 p, float strength, const Fractal& f) {
  Fractal g = f;
  g.octaves = std::max(2, f.octaves - 2);
  Vec2 q{fbm(p, g), fbm(p + Vec2(5.2f, 1.3f), g)};
  return fbm(p + q * strength, f);
}

}  // namespace loop::noise
