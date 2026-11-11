#include "engine/render/pattern.h"

#include "engine/core/random.h"
#include "engine/geometry/noise.h"

namespace loop {

namespace {
// Planar coordinates on the face the normal points at most.
Vec2 planar(Vec3 p, Vec3 n) {
  Vec3 a = vabs(n);
  if (a.y >= a.x && a.y >= a.z) return {p.x, p.z};
  if (a.x >= a.z) return {p.z, p.y};
  return {p.x, p.y};
}
inline float cellHash(int x, int y, uint32_t s = 0) {
  return toUnit(hash32(static_cast<uint32_t>(x) * 0x9e3779b1U ^ static_cast<uint32_t>(y) * 0x85ebca77U ^ s));
}
inline float fract(float v) { return v - std::floor(v); }
}  // namespace

void patternValue(const Pattern& pt, const std::vector<Image>& images, Vec3 p, Vec3 n, Vec2 uv, float& mix, float& height, Vec3& tint, Vec3& glow) {
  mix = 0, height = 0.5f, tint = Vec3(1), glow = Vec3(1);
  Vec3 q = pt.worldSpace ? p * pt.scale : Vec3(uv.x, uv.y, 0) * pt.scale;
  Vec2 pl = pt.worldSpace ? planar(q, n) : Vec2(q.x, q.y);
  noise::Fractal f;
  f.octaves = 5;
  switch (pt.kind) {
    case Pattern::Kind::kNone: return;
    case Pattern::Kind::kChecker: {
      int c = static_cast<int>(std::floor(pl.x)) + static_cast<int>(std::floor(pl.y));
      mix = (c & 1) ? 1.0f : 0.0f;
      return;
    }
    case Pattern::Kind::kGrid: {  // even squares with thin lines (colorB): a floor to judge sizes by
      float gx = fract(pl.x), gy = fract(pl.y);
      float edge = std::min(std::min(gx, 1 - gx), std::min(gy, 1 - gy));
      mix = 1 - smoothstep(0.008f, 0.016f, edge);
      return;
    }
    case Pattern::Kind::kWindows: {
      // cells one floor high and one bay wide, a window in each one's middle
      glow = Vec3(0);
      if (std::fabs(n.y) > 0.7f) return;  // roofs and floors have none
      Vec2 w = pt.worldSpace ? planar(p, n) : uv;
      float cx = w.x / pt.bay, cy = w.y / pt.floorHeight;
      int ix = static_cast<int>(std::floor(cx)), iy = static_cast<int>(std::floor(cy));
      float dx = (fract(cx) - 0.5f) * pt.bay, dy = (fract(cy) - 0.5f) * pt.floorHeight;
      if (std::fabs(dx) > pt.window.x * 0.5f || std::fabs(dy) > pt.window.y * 0.5f) return;
      mix = 1;
      tint = Vec3(0.02f) / std::max(luminance(pt.colorB), 0.001f);  // glass: near black
      uint32_t s = hash32(static_cast<uint32_t>(pt.seed) * 0x2545f491U + 1);
      if (cellHash(ix, iy, s) >= pt.lit) return;  // dark
      float bright = 0.3f + 0.7f * cellHash(ix, iy, s ^ 0x68e31da4U);
      float cool = cellHash(ix, iy, s ^ 0xb5297a4dU);
      glow = kelvin(cool < 0.125f ? 6500.0f : 2700.0f + 1300.0f * cellHash(ix, iy, s ^ 0x1b873593U)) * bright;
      return;
    }
    case Pattern::Kind::kTiles: {
      float gx = fract(pl.x), gy = fract(pl.y);
      float edge = std::min(std::min(gx, 1 - gx), std::min(gy, 1 - gy));
      float grout = 1 - smoothstep(0.015f, 0.03f, edge);
      int ix = static_cast<int>(std::floor(pl.x)), iy = static_cast<int>(std::floor(pl.y));
      float v = cellHash(ix, iy);
      tint = Vec3(0.88f + 0.24f * v);
      mix = grout;
      height = 1 - grout * 0.8f;
      return;
    }
    case Pattern::Kind::kBricks: {
      // a standard brick 21.5 x 6.5 cm with 1 cm joints, scaled
      float bx = pl.x / 0.225f, by = pl.y / 0.075f;
      int row = static_cast<int>(std::floor(by));
      if (row & 1) bx += 0.5f;
      float gx = fract(bx), gy = fract(by);
      float ex = std::min(gx, 1 - gx) * 0.225f, ey = std::min(gy, 1 - gy) * 0.075f;
      float edge = std::min(ex, ey);
      float mortar = 1 - smoothstep(0.004f, 0.007f, edge);
      float v = cellHash(static_cast<int>(std::floor(bx)), row, 7);
      float speck = noise::fbm(q * 40.0f, f) * 0.15f;
      tint = Vec3(0.8f + 0.35f * v + speck, 0.8f + 0.3f * v + speck, 0.8f + 0.25f * v + speck);
      mix = mortar;
      height = (1 - mortar) * (0.9f + 0.1f * noise::fbm(q * 15.0f, f));
      return;
    }
    case Pattern::Kind::kWood: {
      // rings round the vertical axis, pushed around by noise; fine grain
      float r = std::sqrt(q.x * q.x + q.z * q.z) * 6 + noise::fbm(q * Vec3(1.5f, 0.15f, 1.5f), f) * 2.5f;
      float ring = fract(r);
      float rings = smoothstep(0.0f, 0.35f, ring) * (1 - smoothstep(0.55f, 1.0f, ring));
      float grain = noise::perlin(q * Vec3(40, 1.5f, 40)) * 0.5f + 0.5f;
      mix = saturate(rings * 0.7f + grain * 0.3f) * pt.contrast;
      height = 1 - mix * 0.3f;
      return;
    }
    case Pattern::Kind::kPlanks: {
      // floorboards: 14 cm wide, 1.2-2.4 m long, staggered
      float bx = pl.y / 0.14f;
      int plank = static_cast<int>(std::floor(bx));
      float len = 1.2f + 1.2f * cellHash(plank, 0, 3);
      float ax = pl.x / len + cellHash(plank, 1, 5);
      int seg = static_cast<int>(std::floor(ax));
      float gx = fract(bx), gy = fract(ax);
      float edge = std::min(std::min(gx, 1 - gx) * 0.14f, std::min(gy, 1 - gy) * len);
      float gap = 1 - smoothstep(0.0008f, 0.002f, edge);
      float id = cellHash(plank, seg, 11);
      Vec3 wq{pl.x * 0.2f + id * 13, 0, (gx - 0.5f) * 0.14f + id * 7};
      float r = std::sqrt(wq.x * wq.x * 0.02f + wq.z * wq.z) * 40 + noise::fbm(Vec3(pl.x * 0.6f, id * 9, gx * 2), f) * 3;
      float ring = fract(r);
      float figure = smoothstep(0.0f, 0.4f, ring) * (1 - smoothstep(0.6f, 1.0f, ring));
      float grain = noise::perlin(Vec3(pl.x * 3, gx * 60, id * 50)) * 0.5f + 0.5f;
      tint = Vec3(0.75f + 0.5f * id, 0.75f + 0.45f * id, 0.75f + 0.4f * id);
      mix = std::max(gap, saturate(figure * 0.55f + grain * 0.25f) * pt.contrast);
      height = 1 - gap - figure * 0.05f;
      return;
    }
    case Pattern::Kind::kMarble: {
      float t = noise::fbm(q * 1.5f, f);
      float v = std::sin((q.x + q.y * 0.5f) * 3 + t * 9) * 0.5f + 0.5f;
      v = std::pow(1 - v, 6.0f);
      float t2 = noise::fbm(q * 6.0f + Vec3(4), f);
      mix = saturate(v * 1.4f + std::pow(std::fabs(t2), 3.0f) * 0.6f) * pt.contrast;
      height = 0.5f;
      return;
    }
    case Pattern::Kind::kNoise: {
      float v = noise::fbm(q, f) * 0.5f + 0.5f;
      mix = saturate((v - 0.5f) * 2 * pt.contrast + 0.5f);
      height = v;
      return;
    }
    case Pattern::Kind::kGround: {
      // grass where flat, soil showing through in patches, rock where steep
      float slope = 1 - saturate(n.y);
      float patch = noise::fbm(Vec3(p.x, 0, p.z) * (0.15f * pt.scale), f) * 0.5f + 0.5f;
      float fine = noise::fbm(Vec3(p.x, p.y, p.z) * (4.0f * pt.scale), f) * 0.5f + 0.5f;
      float soil = smoothstep(0.55f, 0.7f, patch + fine * 0.2f);
      float rock = smoothstep(0.25f, 0.45f, slope + fine * 0.1f);
      mix = std::max(soil * 0.6f, rock);
      tint = Vec3(0.8f + 0.4f * fine);
      height = fine;
      return;
    }
    case Pattern::Kind::kImage: {
      if (pt.image < 0 || pt.image >= static_cast<int>(images.size())) return;
      Vec2 tuv = pt.worldSpace ? pl : uv * pt.scale;
      Vec4 c = images[pt.image].sample(tuv.x, tuv.y);
      tint = c.xyz();
      mix = 0;
      height = luminance(c.xyz());
      return;
    }
  }
}

float windowsMeanGlow(const Pattern& pt) {
  return pt.lit * 0.65f * std::min(1.0f, pt.window.x * pt.window.y / (pt.bay * pt.floorHeight));
}

Surface shadeSurface(const Material& m, const std::vector<Image>& images, Vec3 p, Vec3 n, Vec2 uv, const std::vector<Texture>* textures) {
  Surface s;
  s.base = m.baseColor;
  s.roughness = m.roughness;
  s.metallic = m.metallic;
  s.normal = n;
  s.alpha = m.opacity;
  if (textures) {
    auto tex = [&](int i) -> const Texture* { return i >= 0 && i < static_cast<int>(textures->size()) && !(*textures)[i].empty() ? &(*textures)[i] : nullptr; };
    if (const Texture* t = tex(m.baseTexture)) {
      Vec4 c = t->sample(uv.x, uv.y, true);
      s.base = s.base * c.xyz();
      s.alpha *= c.w;
    }
    if (const Texture* t = tex(m.metalRoughTexture)) {
      Vec4 c = t->sample(uv.x, uv.y, false);
      s.roughness = saturate(s.roughness * c.y);
      s.metallic = saturate(s.metallic * c.z);
    }
  }
  s.roughness = std::max(s.roughness, m.roughnessMin);
  if (m.pattern.kind == Pattern::Kind::kNone) return s;
  float mix, h;
  Vec3 tint;
  patternValue(m.pattern, images, p, n, uv, mix, h, tint, s.glow);
  s.base = vlerp(m.baseColor, m.pattern.colorB, saturate(mix)) * tint;
  if (m.pattern.kind == Pattern::Kind::kImage) s.base = tint;
  s.roughness = saturate(m.roughness + (mix - 0.5f) * m.pattern.roughnessVariation);
  if (m.pattern.kind == Pattern::Kind::kWindows && mix > 0.5f) s.roughness = 0.05f;  // glass is smooth
  if (m.pattern.bump > 0) {
    // finite-difference gradient of the height over the surface
    Vec3 t, b;
    orthonormalBasis(n, t, b);
    float e = 0.0015f / std::max(m.pattern.scale, 0.1f);
    float m1, h1, m2, h2;
    Vec3 tt, tg;
    patternValue(m.pattern, images, p + t * e, n, uv, m1, h1, tt, tg);
    patternValue(m.pattern, images, p + b * e, n, uv, m2, h2, tt, tg);
    float k = m.pattern.bump * 0.001f / e;
    s.normal = normalize(n - t * ((h1 - h) * k) - b * ((h2 - h) * k));
  }
  return s;
}

}  // namespace loop
