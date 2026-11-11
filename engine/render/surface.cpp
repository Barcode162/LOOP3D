#include "engine/render/surface.h"

#include <cstring>

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

}  // namespace loop
