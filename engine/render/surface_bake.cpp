#include "engine/render/surface_bake.h"

#include <algorithm>
#include <cmath>
#include <mutex>
#include <unordered_map>

#include "engine/core/jobs.h"

namespace loop {

namespace {

// The total height the tile can rise, in metres: the base's bump and every coat
// that faces no way. The same one-line rule as tileBump in
// engine/render/surface.cpp (kept to that file there), so the baked height in
// metres is SurfacePoint::height (0 to 1) times this.
float tileBump(const SurfaceRecipe& r) {
  float b = r.base.bump;
  for (const SurfaceLayer& c : r.coat)
    if (c.on && c.facing == SurfaceLayer::Facing::kAny) b += c.bump;
  return b;
}

// One copy of the chain while it is made. The colour stays in linear light and
// the facing a whole 3-vector until the end, so every smaller copy is averaged
// from the true numbers, not from what is stored.
struct Level {
  int size = 0;
  std::vector<Vec3> color, facing;
  std::vector<float> height, roughness, metal;
  explicit Level(int n) : size(n) {
    const size_t count = static_cast<size_t>(n) * n;
    color.resize(count), facing.resize(count, Vec3{0, 0, 1});
    height.resize(count), roughness.resize(count), metal.resize(count);
  }
  size_t at(int x, int y) const { return static_cast<size_t>(y) * size + x; }
};

int wrap(int i, int n) { return i < 0 ? i + n : (i >= n ? i - n : i); }

// The largest copy: steps 1 to 3 (surfaceTile) four times a pixel, on a 2 by 2
// grid inside it, averaged; then the bumped facing from the height's slope.
Level largest(const SurfaceRecipe& r, int n) {
  Level lv(n);
  const float tile = r.tile;
  JobSystem& jobs = JobSystem::shared();
  jobs.parallelFor(n, [&](int y) {
    for (int x = 0; x < n; ++x) {
      Vec3 c{0};
      float h = 0, rough = 0, metal = 0;
      for (int j = 0; j < 2; ++j)
        for (int i = 0; i < 2; ++i) {
          Vec2 u{(x + 0.25f + 0.5f * i) / n * tile, (y + 0.25f + 0.5f * j) / n * tile};
          SurfacePoint s = surfaceTile(r, u);
          c = c + s.color, h += s.height, rough += s.roughness, metal += s.metal;
        }
      const size_t k = lv.at(x, y);
      lv.color[k] = c * 0.25f, lv.height[k] = h * 0.25f;
      lv.roughness[k] = rough * 0.25f, lv.metal[k] = metal * 0.25f;
    }
  });
  // The slope: the true height in metres across the two neighbouring pixels
  // (wrapping at the edges, as the tile does), over their true distance.
  const float bump = tileBump(r), pixel = tile / n;
  if (bump > 0 && pixel > 0) {
    const float k = bump / (2 * pixel);
    jobs.parallelFor(n, [&](int y) {
      for (int x = 0; x < n; ++x) {
        float sx = (lv.height[lv.at(wrap(x + 1, n), y)] - lv.height[lv.at(wrap(x - 1, n), y)]) * k;
        float sy = (lv.height[lv.at(x, wrap(y + 1, n))] - lv.height[lv.at(x, wrap(y - 1, n))]) * k;
        lv.facing[lv.at(x, y)] = normalize(Vec3{-sx, -sy, 1});
      }
    });
  }
  return lv;
}

// The next smaller copy: each pixel from 2 by 2 of the one before. Colour,
// height, roughness and metal are averaged; the facing is averaged and made
// whole again, and the roughness raised by how much the four facings disagreed
// (Toksvig: the shorter their average, the more they disagreed).
Level smaller(const Level& a) {
  Level lv(std::max(1, a.size / 2));
  const int n = lv.size;
  JobSystem::shared().parallelFor(n, [&](int y) {
    for (int x = 0; x < n; ++x) {
      Vec3 c{0}, f{0};
      float h = 0, rough = 0, metal = 0;
      for (int j = 0; j < 2; ++j)
        for (int i = 0; i < 2; ++i) {
          const size_t k = a.at(std::min(2 * x + i, a.size - 1), std::min(2 * y + j, a.size - 1));
          c = c + a.color[k], f = f + a.facing[k];
          h += a.height[k], rough += a.roughness[k], metal += a.metal[k];
        }
      f = f * 0.25f, rough *= 0.25f;
      const float L = std::max(length(f), 1e-6f);
      const size_t k = lv.at(x, y);
      lv.color[k] = c * 0.25f, lv.height[k] = h * 0.25f, lv.metal[k] = metal * 0.25f;
      lv.facing[k] = f / L;
      lv.roughness[k] = std::min(1.0f, std::sqrt(rough * rough + (1 - L) / L));
    }
  });
  return lv;
}

// One copy as the two pictures of section 8's table.
void store(const Level& lv, Image& colour, Image& shape) {
  colour = Image(lv.size, lv.size), shape = Image(lv.size, lv.size);
  for (size_t k = 0; k < colour.px.size(); ++k) {
    const Vec3 c = lv.color[k], f = lv.facing[k];
    colour.px[k] = {linearToSrgb(c.x), linearToSrgb(c.y), linearToSrgb(c.z), lv.height[k]};
    shape.px[k] = {0.5f + 0.5f * f.x, 0.5f + 0.5f * f.y, lv.roughness[k], lv.metal[k]};
  }
}

// ---- the detail tile (order M7): a chain like the tile's, of the detail alone ----

struct DLevel {
  int size = 0;
  std::vector<Vec3> facing;
  std::vector<float> height, extra;  // the height 0..1, the roughness the smaller copies add
  explicit DLevel(int n) : size(n) {
    const size_t count = static_cast<size_t>(n) * n;
    facing.resize(count, Vec3{0, 0, 1}), height.resize(count), extra.resize(count, 0);
  }
  size_t at(int x, int y) const { return static_cast<size_t>(y) * size + x; }
};

DLevel detailLargest(const SurfaceRecipe& r, int n) {
  DLevel lv(n);
  const float tile = r.detailTile;
  JobSystem& jobs = JobSystem::shared();
  jobs.parallelFor(n, [&](int y) {
    for (int x = 0; x < n; ++x) {
      float h = 0;
      for (int j = 0; j < 2; ++j)
        for (int i = 0; i < 2; ++i) h += detailAt(r, Vec2((x + 0.25f + 0.5f * i) / n * tile, (y + 0.25f + 0.5f * j) / n * tile)).height;
      lv.height[lv.at(x, y)] = h * 0.25f;
    }
  });
  // the slope of the true height in metres across the two neighbouring pixels (wrapping)
  const float pixel = tile / n, k = r.detailBump / (2 * pixel);
  if (r.detailBump > 0 && pixel > 0) {
    jobs.parallelFor(n, [&](int y) {
      for (int x = 0; x < n; ++x) {
        const float sx = (lv.height[lv.at(wrap(x + 1, n), y)] - lv.height[lv.at(wrap(x - 1, n), y)]) * k;
        const float sy = (lv.height[lv.at(x, wrap(y + 1, n))] - lv.height[lv.at(x, wrap(y - 1, n))]) * k;
        lv.facing[lv.at(x, y)] = normalize(Vec3{-sx, -sy, 1});
      }
    });
  }
  return lv;
}

DLevel detailSmaller(const DLevel& a) {
  DLevel lv(std::max(1, a.size / 2));
  const int n = lv.size;
  JobSystem::shared().parallelFor(n, [&](int y) {
    for (int x = 0; x < n; ++x) {
      Vec3 f{0};
      float h = 0, e = 0;
      for (int j = 0; j < 2; ++j)
        for (int i = 0; i < 2; ++i) {
          const size_t k = a.at(std::min(2 * x + i, a.size - 1), std::min(2 * y + j, a.size - 1));
          f = f + a.facing[k], h += a.height[k], e += a.extra[k];
        }
      f = f * 0.25f, e *= 0.25f;
      const float L = std::max(length(f), 1e-6f);
      const size_t k = lv.at(x, y);
      lv.facing[k] = f / L, lv.height[k] = h * 0.25f;
      lv.extra[k] = std::min(1.0f, std::sqrt(e * e + (1 - L) / L));  // Toksvig, as in the shape picture
    }
  });
  return lv;
}

std::vector<Image> bakeDetailChain(const SurfaceRecipe& r, int size) {
  std::vector<Image> chain;
  DLevel lv = detailLargest(r, size);
  for (;;) {
    Image im(lv.size, lv.size);
    for (size_t k = 0; k < im.px.size(); ++k) im.px[k] = {0.5f + 0.5f * lv.facing[k].x, 0.5f + 0.5f * lv.facing[k].y, lv.height[k], lv.extra[k]};
    chain.push_back(std::move(im));
    if (lv.size <= 1) break;
    lv = detailSmaller(lv);
  }
  return chain;
}

bool hasDetail(const SurfaceRecipe& r) { return r.detail.kind != SurfacePattern::Kind::kNone && r.detailTile > 0; }

struct KeptDetail {
  std::mutex m;
  std::unordered_map<uint64_t, std::shared_ptr<const std::vector<Image>>> chains;
};
KeptDetail& keptDetail() {
  static KeptDetail k;
  return k;
}

SurfacePictures bake(const SurfaceRecipe& r, int size) {
  SurfacePictures out;
  Level lv = largest(r, size);
  for (;;) {
    out.colour.emplace_back(), out.shape.emplace_back();
    store(lv, out.colour.back(), out.shape.back());
    if (lv.size <= 1) break;
    lv = smaller(lv);
  }
  if (hasDetail(r)) out.detail = *bakeDetail(r, size);
  return out;
}

// What is kept, by the recipe's fingerprint and the size (like the cook's trees).
struct Kept {
  std::mutex m;
  std::unordered_map<uint64_t, std::shared_ptr<const SurfacePictures>> pictures;
};
Kept& kept() {
  static Kept k;
  return k;
}

}  // namespace

uint64_t detailFingerprint(const SurfaceRecipe& r) {
  uint64_t h = 1469598103934665603ull;  // FNV-1a over the numbers, as the recipe's own
  auto bytes = [&](const void* p, size_t n) {
    for (size_t i = 0; i < n; ++i) h = (h ^ static_cast<const unsigned char*>(p)[i]) * 1099511628211ull;
  };
  auto f = [&](float x) {
    if (x == 0) x = 0;
    bytes(&x, sizeof x);
  };
  const int kind = static_cast<int>(r.detail.kind);
  bytes(&kind, sizeof kind);
  f(r.detail.size), f(r.detail.stretch), f(r.detail.contrast), f(r.detail.sharp), f(r.detail.piece);
  bytes(&r.detail.twill, sizeof r.detail.twill);
  f(r.detailTile), f(r.detailBump);
  bytes(&r.seed, sizeof r.seed);
  return h;
}

std::shared_ptr<const std::vector<Image>> bakeDetail(const SurfaceRecipe& r, int size) {
  size = std::max(size, 1);
  if (!hasDetail(r)) return std::make_shared<const std::vector<Image>>();
  const uint64_t key = detailFingerprint(r) ^ (static_cast<uint64_t>(size) * 0x9e3779b97f4a7c15ull);
  KeptDetail& k = keptDetail();
  {
    std::lock_guard<std::mutex> l(k.m);
    auto it = k.chains.find(key);
    if (it != k.chains.end()) return it->second;
  }
  auto made = std::make_shared<const std::vector<Image>>(bakeDetailChain(r, size));
  std::lock_guard<std::mutex> l(k.m);
  return k.chains.emplace(key, std::move(made)).first->second;
}

std::shared_ptr<const SurfacePictures> bakeSurface(const SurfaceRecipe& recipe, int size) {
  size = std::max(size, 1);
  const uint64_t key = recipe.fingerprint() ^ (static_cast<uint64_t>(size) * 0x9e3779b97f4a7c15ull);
  Kept& k = kept();
  {
    std::lock_guard<std::mutex> l(k.m);
    auto it = k.pictures.find(key);
    if (it != k.pictures.end()) return it->second;
  }
  auto made = std::make_shared<const SurfacePictures>(bake(recipe, size));
  std::lock_guard<std::mutex> l(k.m);
  return k.pictures.emplace(key, std::move(made)).first->second;  // another thread may have kept one first: the same
}

}  // namespace loop
