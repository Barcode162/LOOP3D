#include "engine/render/surface.h"

#include <cstring>

#include "engine/core/image.h"
#include "engine/core/random.h"
#include "engine/render/pattern.h"
#include "engine/render/surface_patterns.h"

namespace loop {

namespace {

// One running hash (FNV-1a, 64 bit) fed with the bytes of each number.
struct Hasher {
  uint64_t h = 1469598103934665603ull;
  void bytes(const void* p, size_t n) {
    const unsigned char* b = static_cast<const unsigned char*>(p);
    for (size_t i = 0; i < n; ++i) h = (h ^ b[i]) * 1099511628211ull;
  }
  void f(float x) {
    if (x == 0) x = 0;  // -0 and 0 are one number
    bytes(&x, sizeof x);
  }
  void i(int64_t x) { bytes(&x, sizeof x); }
  void v(Vec3 c) { f(c.x), f(c.y), f(c.z); }
  void pattern(const SurfacePattern& p) {
    i(static_cast<int>(p.kind));
    f(p.size), f(p.stretch), f(p.contrast), f(p.sharp), f(p.piece);
    i(p.twill);
  }
  void layer(const SurfaceLayer& l) {
    i(l.on);
    v(l.color), v(l.color2);
    f(l.roughness), f(l.roughness2), f(l.metal), f(l.bump);
    pattern(l.pattern);
    f(l.cover), f(l.amount), f(l.ragged), f(l.soft);
    i(static_cast<int>(l.sits)), i(static_cast<int>(l.facing));
  }
};

}  // namespace

// Every number of the recipe, so two recipes with the same look have the same
// fingerprint (the name is not part of the look).
uint64_t SurfaceRecipe::fingerprint() const {
  Hasher h;
  h.i(seed);
  h.f(tile);
  h.i(static_cast<int>(project)), h.i(worldSpace), h.i(along);
  h.layer(base), h.layer(coat[0]), h.layer(coat[1]), h.layer(wear), h.layer(dirt);
  h.f(varyLarge), h.f(varyAmount), h.f(varyRoughness), h.f(varySmall), h.f(varySmallAmount);
  h.v(varyTint);
  h.pattern(detail);
  h.f(detailBump), h.f(detailRoughness), h.f(detailTile), h.f(detailFade);
  return h.h;
}

// ---- the stack (docs/SURFACES.md section 7; order M3c) ----


namespace {

// The total height the tile can rise (metres): the base's bump and every coat without a facing.
float tileBump(const SurfaceRecipe& r) {
  float b = r.base.bump;
  for (const SurfaceLayer& c : r.coat)
    if (c.on && c.facing == SurfaceLayer::Facing::kAny) b += c.bump;
  return b;
}

// A soft step: 0 where `c` is clearly below the line of `cover`, 1 clearly above; cover 0 is never,
// cover 1 always. `sharp` 1 is a hard border, 0 the softest.
float coverStep(float c, float cover, float sharp) {
  if (cover <= 0) return 0;
  if (cover >= 1) return 1;
  float w = 0.25f * (1 - saturate(sharp)) + 1e-4f;
  float line = lerp(1 + w, -w, cover);  // cover 0: above every c; cover 1: below every c
  return smoothstep(line - w, line + w, saturate(c));
}

float layerStep(float t, float soft) {
  float s = std::max(soft, 1e-3f);
  return smoothstep(0.5f - s / 2, 0.5f + s / 2, t);
}

void moveTo(Vec3& color, float& rough, float& metal, const SurfaceLayer& L, float m) {
  color = vlerp(color, L.color, m);
  rough = lerp(rough, L.roughness, m);
  metal = lerp(metal, L.metal, m);
}

uint32_t layerSeed(uint32_t seed, uint32_t which) { return hashCombine(seed * 0x9e3779b1U + 1, which); }

// Planar coordinates on the face the facing points at most, and the two directions they run along.
Vec2 planarAxes(Vec3 p, Vec3 n, Vec3& ax, Vec3& ay) {
  Vec3 a = vabs(n);
  if (a.y >= a.x && a.y >= a.z) { ax = {1, 0, 0}, ay = {0, 0, 1}; return {p.x, p.z}; }
  if (a.x >= a.z) { ax = {0, 0, 1}, ay = {0, 1, 0}; return {p.z, p.y}; }
  ax = {1, 0, 0}, ay = {0, 1, 0};
  return {p.x, p.y};
}

}  // namespace

const Image& wanderPicture() {
  // 64 square, four smooth noises that wrap (value noise on an 8 by 8 lattice and half as much on a
  // 16 by 16 one), the same bytes on every machine: integer hashes only, rounded to 8 bits.
  static const Image img = [] {
    Image im(64, 64);
    auto lattice = [](int x, int y, int n, uint32_t ch) {
      x = ((x % n) + n) % n, y = ((y % n) + n) % n;
      return toUnit(hash32(static_cast<uint32_t>(x) * 0x8da6b343U ^ static_cast<uint32_t>(y) * 0xd8163841U ^ (ch * 0xcb1ab31fU + static_cast<uint32_t>(n)))) * 2 - 1;
    };
    auto smooth = [&](float u, float v, int n, uint32_t ch) {
      float fx = u * n, fy = v * n;
      int x0 = static_cast<int>(std::floor(fx)), y0 = static_cast<int>(std::floor(fy));
      float tx = fx - x0, ty = fy - y0;
      tx = tx * tx * (3 - 2 * tx), ty = ty * ty * (3 - 2 * ty);
      float a = lerp(lattice(x0, y0, n, ch), lattice(x0 + 1, y0, n, ch), tx);
      float b = lerp(lattice(x0, y0 + 1, n, ch), lattice(x0 + 1, y0 + 1, n, ch), tx);
      return lerp(a, b, ty);
    };
    for (int y = 0; y < 64; ++y)
      for (int x = 0; x < 64; ++x) {
        float u = (x + 0.5f) / 64, v = (y + 0.5f) / 64, c[4];
        for (uint32_t ch = 0; ch < 4; ++ch) {
          float w = (smooth(u, v, 8, ch) + 0.5f * smooth(u, v, 16, ch + 4)) / 1.5f;
          c[ch] = std::round((w * 0.5f + 0.5f) * 255) / 255;  // 8 bits, as it goes to the phone
        }
        im.at(x, y) = {c[0], c[1], c[2], c[3]};
      }
    return im;
  }();
  return img;
}

Vec4 wanderAt(Vec2 u, float large) {
  // one picture holds eight features across, so a feature is `large` metres
  const float side = 8 * std::max(large, 0.01f);
  Vec4 w = wanderPicture().sample(u.x / side, u.y / side);
  return {w.x * 2 - 1, w.y * 2 - 1, w.z * 2 - 1, w.w * 2 - 1};
}

SurfacePoint surfaceTile(const SurfaceRecipe& r, Vec2 u) {
  SurfacePoint sp;
  // 1. base
  float mix = 0, height = 0.5f, piece = 0;
  patternAt(r.base.pattern, u, r.tile, layerSeed(r.seed, 0), mix, height, piece);
  sp.color = vlerp(r.base.color, r.base.color2, saturate(mix)) * (1 + piece);
  sp.roughness = lerp(r.base.roughness, r.base.roughness2, saturate(mix));
  sp.metal = r.base.metal;
  float h = height * r.base.bump;  // metres
  // 2. small change, baked
  if (r.varySmall > 0 && r.varySmallAmount != 0) {
    SurfacePattern n;
    n.kind = SurfacePattern::Kind::kNoise, n.size = r.varySmall;
    float nm, nh, np;
    patternAt(n, u, r.tile, layerSeed(r.seed, 7), nm, nh, np);
    sp.color = sp.color * std::max(0.0f, 1 + r.varySmallAmount * (nm - 0.5f) * 2);
  }
  // 3. the coats that do not face a way
  for (int i = 0; i < 2; ++i) {
    const SurfaceLayer& c = r.coat[i];
    if (!c.on || c.facing != SurfaceLayer::Facing::kAny) continue;
    SurfacePattern pat = c.pattern;
    if (pat.kind == SurfacePattern::Kind::kNone) pat.kind = SurfacePattern::Kind::kNoise;
    float cm, ch, cp;
    patternAt(pat, u, r.tile, layerSeed(r.seed, 1 + i), cm, ch, cp);
    float lean = c.sits == SurfaceLayer::Sits::kHigh ? height - 0.5f : c.sits == SurfaceLayer::Sits::kLow ? 0.5f - height : 0;
    float m = coverStep(cm + lean, c.cover, pat.sharp);
    moveTo(sp.color, sp.roughness, sp.metal, c, m);
    h += m * c.bump;
  }
  const float total = tileBump(r);
  sp.height = total > 0 ? saturate(h / total) : saturate(height);
  return sp;
}

// The bumped facing from the tile's height: its slope across the two directions the tile runs.
Vec3 bumpedFacing(const SurfaceRecipe& r, Vec2 u, Vec3 n, Vec3 ax, Vec3 ay, float h0) {
  const float total = tileBump(r);
  if (total <= 0) return n;
  const float e = clamp(r.base.pattern.size * 0.05f, 0.0001f, 0.002f);
  float hx = surfaceTile(r, u + Vec2(e, 0)).height, hy = surfaceTile(r, u + Vec2(0, e)).height;
  float k = total / e;  // height (0..1) to metres, over the step
  // the two directions laid on the surface
  Vec3 tx = normalize(ax - n * dot(ax, n)), ty = normalize(ay - n * dot(ay, n));
  return normalize(n - tx * ((hx - h0) * k) - ty * ((hy - h0) * k));
}

Surface surfaceAt(const SurfaceRecipe& r, const Material& mat, Vec3 place, Vec3 facing, Vec2 uv, float edge, float hollow, float distance) {
  (void)uv, (void)distance;  // uv projection is M6; detail (which fades by distance) is M7
  Surface s;
  s.alpha = mat.opacity;
  Vec3 ax, ay;
  Vec2 u = planarAxes(place, facing, ax, ay);
  // 1 to 3: the tile
  SurfacePoint t = surfaceTile(r, u);
  Vec3 color = t.color;
  float rough = t.roughness, metal = t.metal;
  const float b = t.height;
  s.normal = bumpedFacing(r, u, facing, ax, ay, b);
  // 4. large change
  if (r.varyLarge > 0 && (r.varyAmount != 0 || r.varyRoughness != 0)) {
    Vec4 w = wanderAt(u, r.varyLarge);
    color = color * std::max(0.0f, 1 + r.varyAmount * w.x);
    color = vlerp(color, r.varyTint, saturate(r.varyAmount * std::max(w.y, 0.0f)));
    rough += r.varyRoughness * w.z;
  }
  // 5. the coat that faces a way
  for (int i = 0; i < 2; ++i) {
    const SurfaceLayer& c = r.coat[i];
    if (!c.on || c.facing == SurfaceLayer::Facing::kAny) continue;
    float f = 0;
    switch (c.facing) {
      case SurfaceLayer::Facing::kUp: f = saturate((facing.y - 0.3f) / 0.4f); break;
      case SurfaceLayer::Facing::kDown: f = saturate((-facing.y - 0.3f) / 0.4f); break;
      case SurfaceLayer::Facing::kSide: f = 1 - std::fabs(facing.y); break;
      default: break;
    }
    float bb = c.sits == SurfaceLayer::Sits::kLow ? 1 - b : b;
    float tt = f * c.cover * 2 * (1 + (bb - 0.5f) * 2 * c.ragged);
    moveTo(color, rough, metal, c, layerStep(tt, c.soft));
  }
  // 6. wear, on edges
  if (r.wear.on && edge > 0) {
    float tt = edge * r.wear.amount * 2 * (1 + (b - 0.5f) * 2 * r.wear.ragged);
    moveTo(color, rough, metal, r.wear, layerStep(tt, r.wear.soft));
  }
  // 7. dirt, in hollows, over the wear
  if (r.dirt.on && hollow > 0) {
    float tt = hollow * r.dirt.amount * 2 * (1 + (0.5f - b) * 2 * r.dirt.ragged);
    moveTo(color, rough, metal, r.dirt, layerStep(tt, r.dirt.soft));
  }
  // 9.
  s.base = color;
  s.roughness = clamp(rough, 0.03f, 1.0f);
  s.metallic = saturate(metal);
  return s;
}

}  // namespace loop
