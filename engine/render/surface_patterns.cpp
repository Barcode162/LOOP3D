#include "engine/render/surface_patterns.h"

#include <algorithm>
#include <cmath>

namespace loop {
namespace {

using Kind = SurfacePattern::Kind;

float clamp01(float v) { return v < 0 ? 0 : (v > 1 ? 1 : v); }
float clampf(float v, float lo, float hi) { return v < lo ? lo : (v > hi ? hi : v); }

// Integer hashes only, so every machine gives the same numbers.
uint32_t fmix(uint32_t h) {
  h ^= h >> 16;
  h *= 0x85EBCA6Bu;
  h ^= h >> 13;
  h *= 0xC2B2AE35u;
  h ^= h >> 16;
  return h;
}
uint32_t hash2(int x, int y, uint32_t seed, uint32_t salt) {
  uint32_t h = fmix(seed * 0x9E3779B1u + salt * 0x85EBCA6Bu + 0x1234567u);
  h = fmix(h ^ (static_cast<uint32_t>(x) * 0xC2B2AE35u));
  h = fmix(h ^ (static_cast<uint32_t>(y) * 0x27D4EB2Fu));
  return h;
}
float rnd(int x, int y, uint32_t seed, uint32_t salt) {
  return static_cast<float>(hash2(x, y, seed, salt) >> 8) * (1.0f / 16777216.0f);
}

int wrapIdx(int i, int n) {
  i %= n;
  return i < 0 ? i + n : i;
}

// How many whole features fit: at least 1 (or 2 when `even`).
int fitCount(float tile, float len, bool even) {
  int n = static_cast<int>(std::lround(tile / std::max(len, 1e-6f)));
  if (even) n = std::max(2, (n + 1) & ~1);
  return std::max(1, n);
}

// A feature's nominal length along u.x and across it (u.y), in metres.
struct Dims { float along, across; bool evenAlong, evenAcross; int period; };
Dims dimsOf(const SurfacePattern& p) {
  float s = std::max(p.size, 1e-5f), st = std::max(p.stretch, 0.05f);
  switch (p.kind) {
    case Kind::kPlanks:
    case Kind::kGrain:
    case Kind::kBrushed: return {s * st, s, false, false, 1};
    case Kind::kBricks: return {s, s / st, false, true, 1};  // courses even: the bond wraps
    case Kind::kChecker: return {s, s, true, true, 1};
    case Kind::kWeave: return {s, s, false, false, 2 * std::max(1, p.twill)};  // the cross-over repeats every `period` threads
    default: return {s, s, false, false, 1};
  }
}
int fitPeriod(float tile, float len, int period) {
  return std::max(period, static_cast<int>(std::lround(tile / std::max(len, 1e-6f) / period)) * period);
}
int countAlong(const SurfacePattern& p, float tile) {
  Dims d = dimsOf(p);
  return d.period > 1 ? fitPeriod(tile, d.along, d.period) : fitCount(tile, d.along, d.evenAlong);
}
int countAcross(const SurfacePattern& p, float tile) {
  Dims d = dimsOf(p);
  return d.period > 1 ? fitPeriod(tile, d.across, d.period) : fitCount(tile, d.across, d.evenAcross);
}

float smooth5(float t) { return t * t * t * (t * (t * 6 - 15) + 10); }

// Value noise on a lattice of nx by ny points that wraps.
float vnoise(float gx, float gy, int nx, int ny, uint32_t seed, uint32_t salt) {
  int ix = static_cast<int>(std::floor(gx)), iy = static_cast<int>(std::floor(gy));
  float fx = smooth5(gx - ix), fy = smooth5(gy - iy);
  auto at = [&](int i, int j) { return rnd(wrapIdx(i, nx), wrapIdx(j, ny), seed, salt); };
  float a = at(ix, iy), b = at(ix + 1, iy), c = at(ix, iy + 1), d = at(ix + 1, iy + 1);
  return lerp(lerp(a, b, fx), lerp(c, d, fx), fy);
}
// Fractal sum of `oct` octaves, each twice as fine (so each still wraps), 0..1.
float fbm(float gx, float gy, int nx, int ny, int oct, uint32_t seed, uint32_t salt) {
  float sum = 0, amp = 1, tot = 0;
  for (int o = 0; o < oct; ++o) {
    sum += amp * vnoise(gx, gy, nx, ny, seed, salt + o * 7);
    tot += amp;
    amp *= 0.5f;
    gx *= 2, gy *= 2, nx *= 2, ny *= 2;
  }
  return sum / tot;
}

// Cellular distances on a wrapped grid, in cells: nearest, second nearest, the nearest's id.
struct Worley { float f1, f2; int cx, cy; };
Worley worley(float gx, float gy, int nx, int ny, uint32_t seed) {
  int ix = static_cast<int>(std::floor(gx)), iy = static_cast<int>(std::floor(gy));
  Worley w{1e9f, 1e9f, 0, 0};
  for (int j = -1; j <= 1; ++j)
    for (int i = -1; i <= 1; ++i) {
      int cx = ix + i, cy = iy + j;
      int wx = wrapIdx(cx, nx), wy = wrapIdx(cy, ny);
      float px = cx + 0.1f + 0.8f * rnd(wx, wy, seed, 1), py = cy + 0.1f + 0.8f * rnd(wx, wy, seed, 2);
      float d = std::sqrt((px - gx) * (px - gx) + (py - gy) * (py - gy));
      if (d < w.f1) { w.f2 = w.f1; w.f1 = d; w.cx = wx; w.cy = wy; }
      else if (d < w.f2) w.f2 = d;
    }
  return w;
}

struct Out {
  float mix = 0, height = 0.5f;
  int pcx = 0, pcy = 0;       // which piece this place is in
  bool smoothPiece = false;   // no pieces (noise, veins): the tone follows the field instead
  float pieceValue = 0;
};

// One dot at most in each cell (grains, pores), kept inside its own cell.
void dots(Out& o, float fx, float fy, int cx, int cy, uint32_t seed, float prob, float rmin, float rmax,
          float sharp, bool pore) {
  float on = rnd(cx, cy, seed, 11) < prob ? 1.0f : 0.0f;
  float r = rmin + (rmax - rmin) * rnd(cx, cy, seed, 12);
  float px = r + (1 - 2 * r) * rnd(cx, cy, seed, 13), py = r + (1 - 2 * r) * rnd(cx, cy, seed, 14);
  float d = std::sqrt((fx - px) * (fx - px) + (fy - py) * (fy - py));
  float w = 0.01f + 0.2f * r * (1 - sharp);
  float m = on * (1 - smoothstep(r - w, r + w, d));
  o.mix = m;
  o.height = pore ? 1 - 0.85f * m * (1 - 0.6f * std::min(1.0f, d / r)) : 0.35f + 0.5f * m * (1 - 0.3f * d / r);
  o.pcx = cx, o.pcy = cy;
}

}  // namespace

float fittedSize(const SurfacePattern& p, float tile) {
  if (p.kind == Kind::kNone) return p.size;
  if (p.kind == Kind::kBricks) return tile / countAlong(p, tile);
  return tile / countAcross(p, tile);
}

void patternAt(const SurfacePattern& p, Vec2 u, float tile, uint32_t seed, float& mix, float& height,
               float& piece) {
  if (p.kind == Kind::kNone || tile <= 0) { mix = 0, height = 0.5f, piece = 0; return; }
  // Bring u into the tile first, so a place and its copies one tile away give the same numbers.
  double x = std::fmod(static_cast<double>(u.x), static_cast<double>(tile));
  double y = std::fmod(static_cast<double>(u.y), static_cast<double>(tile));
  if (x < 0) x += tile;
  if (y < 0) y += tile;
  int nx = countAlong(p, tile), ny = countAcross(p, tile);
  float gx = static_cast<float>(x / tile * nx), gy = static_cast<float>(y / tile * ny);
  int ix = std::min(static_cast<int>(std::floor(gx)), nx - 1), iy = std::min(static_cast<int>(std::floor(gy)), ny - 1);
  float fx = gx - ix, fy = gy - iy;
  float sharp = clamp01(p.sharp);
  float cellX = tile / nx, cellY = tile / ny;  // the cell's size in metres
  Out o;
  switch (p.kind) {
    case Kind::kNoise: {
      float v = fbm(gx, gy, nx, ny, 3, seed, 21);
      float w = 0.5f * (1 - sharp) + 0.01f;
      o.mix = smoothstep(0.5f - w, 0.5f + w, v);
      o.height = v;
      o.smoothPiece = true, o.pieceValue = v - 0.5f;
      break;
    }
    case Kind::kSpeckle: dots(o, fx, fy, ix, iy, seed, 0.6f, 0.15f, 0.4f, sharp, false); break;
    case Kind::kPores: dots(o, fx, fy, ix, iy, seed, 0.25f, 0.12f, 0.3f, sharp, true); break;
    case Kind::kCells: {
      Worley w = worley(gx, gy, nx, ny, seed);
      float g = w.f2 - w.f1, soft = 0.02f + 0.3f * (1 - sharp);
      o.mix = 1 - smoothstep(0.02f, 0.02f + soft, g);
      o.height = smoothstep(0.0f, 0.35f, g);
      o.pcx = w.cx, o.pcy = w.cy;
      break;
    }
    case Kind::kCracks: {
      Worley w = worley(gx, gy, nx, ny, seed);
      float g = w.f2 - w.f1, wc = 0.04f, ew = 0.002f + 0.05f * (1 - sharp);
      o.mix = 1 - smoothstep(wc - ew * 0.5f, wc + ew, g);
      o.height = 1 - o.mix;
      o.pcx = w.cx, o.pcy = w.cy;
      break;
    }
    case Kind::kVeins: {
      float v = fbm(gx, gy, nx, ny, 2, seed, 31);
      float d = std::fabs(v - 0.5f), wv = 0.015f, w = 0.05f * (1 - sharp) + 0.003f;
      o.mix = 1 - smoothstep(wv - w * 0.5f, wv + w, d);
      o.height = 1 - 0.3f * o.mix;
      o.smoothPiece = true, o.pieceValue = v - 0.5f;
      break;
    }
    case Kind::kStrata: {
      float yy = gy + 0.4f * vnoise(gx, gy, nx, ny, seed, 41);
      int bed = static_cast<int>(std::floor(yy));
      float f = yy - bed, tone = rnd(wrapIdx(bed, ny), 0, seed, 42);
      o.mix = tone;
      o.height = clamp01(0.5f * tone + 0.5f * f);
      o.pcx = wrapIdx(bed, ny), o.pcy = 0;
      break;
    }
    case Kind::kGrain: {
      // Rings cut by a board: the rings run along u.x and bend into long arches.
      float ph = gy + 2.0f * (1 - std::cos(2 * kPi * gx)) + 0.5f * vnoise(gx, gy, nx, ny, seed, 51);
      int ring = static_cast<int>(std::floor(ph));
      float f = ph - ring;
      o.mix = lerp(0.5f - 0.5f * std::cos(2 * kPi * f), f, sharp);
      o.height = o.mix;
      o.pcx = wrapIdx(ring, ny), o.pcy = 0;
      break;
    }
    case Kind::kPlanks: {
      // Each row is a board's width; every row starts its boards at a different place.
      float bx = gx + rnd(iy, 0, seed, 61) * nx;
      int b = static_cast<int>(std::floor(bx));
      float fbx = bx - b;
      float dEnd = std::min(fbx, 1 - fbx) * cellX, dSide = std::min(fy, 1 - fy) * cellY;
      float d = std::min(dEnd, dSide), half = 0.0005f, w = 0.0005f * (1 - sharp) + 2e-5f;
      o.mix = 1 - smoothstep(half - w, half + w, d);
      o.height = 1 - o.mix;
      o.pcx = wrapIdx(b, nx), o.pcy = iy;
      break;
    }
    case Kind::kBricks: {
      // Running bond: every second course moves half a brick; a 10 mm joint, lower.
      float bx = gx + ((iy & 1) ? 0.5f : 0.0f);
      int b = static_cast<int>(std::floor(bx));
      float fbx = bx - b;
      float dEnd = std::min(fbx, 1 - fbx) * cellX, dSide = std::min(fy, 1 - fy) * cellY;
      float d = std::min(dEnd, dSide), half = 0.005f, w = 0.003f * (1 - sharp) + 3e-5f;
      o.mix = 1 - smoothstep(half - w, half + w, d);
      o.height = 1 - o.mix;
      o.pcx = wrapIdx(b, nx), o.pcy = iy;
      break;
    }
    case Kind::kTiles: {
      float half = (0.1f - 0.08f * sharp) * 0.5f;  // the joint's share of a tile's side: 0.1 (soft) to 0.02 (hard)
      float d = std::min(std::min(fx, 1 - fx), std::min(fy, 1 - fy));
      o.mix = 1 - smoothstep(half - 0.002f, half + 0.002f, d);
      o.height = 1 - o.mix;
      o.pcx = ix, o.pcy = iy;
      break;
    }
    case Kind::kWeave: {
      int twill = std::max(1, p.twill);
      bool horizOver = ((ix + iy) % (2 * twill)) < twill;  // the thread along u.x is on top here
      // The top thread is round across its width and dips where it goes under at the cell's ends.
      float across = horizOver ? fy : fx, along = horizOver ? fx : fy;
      float dip = 1 - 0.35f * (1 - std::sin(kPi * along));
      o.mix = horizOver ? 1.0f : 0.0f;
      o.height = clamp01(0.25f + 0.65f * std::sin(kPi * across) * dip);
      o.pcx = horizOver ? iy : ix, o.pcy = horizOver ? 1 : 0;
      break;
    }
    case Kind::kKnit: {
      // Each stitch is a V of two leaning legs, rounded across.
      auto leg = [&](float cx) { return std::max(0.0f, 1 - std::fabs(fx - cx) / 0.22f); };
      float k = 0.3f * (fy - 0.5f);
      float h = std::max(leg(0.27f + k), leg(0.73f - k));
      o.height = clamp01(std::sin(h * kPi * 0.5f));
      o.mix = o.height;
      o.pcx = ix, o.pcy = iy;
      break;
    }
    case Kind::kBrushed: {
      // Thin scratches along u.x, each row cut into runs of its own tone.
      float bx = gx + rnd(iy, 0, seed, 71) * nx;
      int b = static_cast<int>(std::floor(bx));
      float t = rnd(wrapIdx(b, nx), iy, seed, 72);
      o.mix = lerp(0.5f, t, 0.4f + 0.6f * sharp);
      o.height = 0.25f + 0.5f * o.mix;
      o.pcx = wrapIdx(b, nx), o.pcy = iy;
      break;
    }
    case Kind::kChecker: {
      o.mix = ((ix + iy) & 1) ? 1.0f : 0.0f;
      o.height = o.mix;
      o.pcx = ix, o.pcy = iy;
      break;
    }
    case Kind::kGrid: {
      float edge = std::min(std::min(fx, 1 - fx), std::min(fy, 1 - fy));
      o.mix = 1 - smoothstep(0.008f, 0.016f, edge);
      o.height = 1 - o.mix;
      o.pcx = ix, o.pcy = iy;
      break;
    }
    case Kind::kNone: break;
  }
  mix = clamp01(0.5f + (o.mix - 0.5f) * clampf(p.contrast, 0.0f, 2.0f));
  height = clamp01(o.height);
  float setting = clampf(p.piece, 0.0f, 0.3f);
  if (setting == 0) piece = 0;
  else if (o.smoothPiece) piece = clampf(o.pieceValue * 2, -1.0f, 1.0f) * setting;
  else piece = (rnd(o.pcx, o.pcy, seed, 99) * 2 - 1) * setting;
}

}  // namespace loop
