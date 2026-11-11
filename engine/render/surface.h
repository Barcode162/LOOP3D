#pragma once
// Surface recipes (docs/SURFACES.md): a material written as a fixed stack of
// layers in real sizes: base, vary, coat (up to two), wear, dirt, detail.
// One recipe gives the film renderer every point exactly, the cook the small
// pictures it bakes (the part that repeats), and the phone what it adds where a
// thing is drawn (slow change, wear on edges, dirt in creases).
//
// These structs are the seam of section 12; every order of track M builds on
// them. Sizes are in metres (`6 mm` arrives as 0.006).
#include <cstdint>
#include <string>

#include "engine/core/math.h"

namespace loop {

struct Material;  // engine/render/scene.h
struct Surface;   // engine/render/pattern.h

// A pattern: a pure function of a place on the tile and the seed (section 5).
struct SurfacePattern {
  enum class Kind : uint8_t { kNone, kNoise, kSpeckle, kCells, kPores, kCracks, kVeins,
                              kStrata, kGrain, kPlanks, kBricks, kTiles, kWeave, kKnit, kBrushed, kChecker, kGrid };
  Kind kind = Kind::kNone;
  float size = 0.01f, stretch = 1, contrast = 1, sharp = 0.5f, piece = 0;
  int twill = 1;
};

// base, a coat, wear or dirt: the settings each uses (section 4).
struct SurfaceLayer {
  bool on = false;
  Vec3 color{0.5f}, color2{0.5f};
  float roughness = 0.5f, roughness2 = 0.5f, metal = 0, bump = 0;  // bump in metres
  SurfacePattern pattern;
  float cover = 0.5f, amount = 0, ragged = 0.5f, soft = 0.2f;
  enum class Sits : uint8_t { kAny, kHigh, kLow } sits = Sits::kAny;
  enum class Facing : uint8_t { kAny, kUp, kDown, kSide } facing = Facing::kAny;
};

struct SurfaceRecipe {
  std::string name;
  uint32_t seed = 1;
  float tile = 1;                                 // the side of the square that repeats, metres
  enum class Project : uint8_t { kBox, kTop, kUv } project = Project::kBox;
  bool worldSpace = false;
  int along = -1;                                 // -1: the longest side; 0, 1, 2: x, y, z
  SurfaceLayer base, coat[2], wear, dirt;
  float varyLarge = 0, varyAmount = 0, varyRoughness = 0, varySmall = 0, varySmallAmount = 0;
  Vec3 varyTint{1};
  SurfacePattern detail;
  float detailBump = 0, detailRoughness = 0, detailTile = 0.1f, detailFade = 2;
  uint64_t fingerprint() const;                   // every number above (order M3a)
};

// After step 3 of section 7: what the cook bakes into the tile.
struct SurfacePoint { Vec3 color; float height, roughness, metal; };

// Steps 1 to 3 at `u` (metres on the tile; wraps at the tile's side). Order M3c.
SurfacePoint surfaceTile(const SurfaceRecipe&, Vec2 u);
// Steps 1 to 9: the whole stack at one point of one thing. Order M3c.
Surface surfaceAt(const SurfaceRecipe&, const Material&, Vec3 place, Vec3 facing, Vec2 uv,
                  float edge, float hollow, float distance);

}  // namespace loop
