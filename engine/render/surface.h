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
struct Image;     // engine/core/image.h

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

// The wander picture: 64 square, four smooth noises that wrap (each -1 to 1 once
// read with wanderAt), the same bytes on every machine. Read at a scale of metres
// for a surface's slow change (`vary.large`), so a floor never shows its tile.
const Image& wanderPicture();
// The four noises at `u` (metres) for slow change of size `large` (metres).
Vec4 wanderAt(Vec2 u, float large);

// The detail layer (step 8; docs/SURFACES.md section 7, order M7) at one place of
// its own small tile: its height 0 to 1 (the pattern's), and its slope along the
// tile's two ways in metres per metre (that height times `detailBump`). `u` is in
// metres and wraps at `detailTile`; the slope is measured on the place reduced to
// the tile first, so far from the origin it keeps its precision. A recipe with no
// detail gives height 0.5 and no slope.
struct DetailPoint { float height = 0.5f, slopeX = 0, slopeY = 0; };
DetailPoint detailAt(const SurfaceRecipe&, Vec2 u);

// Steps 1 to 3 at `u` (metres on the tile; wraps at the tile's side). Order M3c.
SurfacePoint surfaceTile(const SurfaceRecipe&, Vec2 u);

// Where a point reads the tile (section 10; order M6). One side of the box the
// tile is laid on from: the axis it faces (0, 1, 2: x, y, z), the place on the
// tile there (metres), the tile's two ways there in the recipe's space (the
// first is the pattern's stretched way), and how much of the point it gives.
struct SurfaceSide {
  int axis = 1;
  Vec2 u{0};
  Vec3 ax{1, 0, 0}, ay{0, 0, 1};
  float weight = 1;
};
// Up to two sides whose weights add to 1. `box`: the facing's three parts to the
// eighth power, the two strongest kept; `top`: the side facing up only; `uv`: the
// mesh's own unwrap (one unit a metre), its two ways those of the strongest side.
// `along` (0, 1, 2) is the axis the stretched way runs along. The live renderer's
// projectSurface (engine/gpu/shaders/scene.wgsl.h) is the same rule.
struct SurfaceProjection {
  SurfaceSide side[2];
  int sides = 1;
};
SurfaceProjection projectSurface(const SurfaceRecipe&, Vec3 place, Vec3 facing, Vec2 uv, int along);
// The axis a recipe's stretched way runs along on a mesh with these bounds (in the
// thing's own space): the one it names, or the mesh's longest side (where two are
// within 1% of each other, x before y before z). Worked out once a mesh.
int alongAxis(const SurfaceRecipe&, const AABB& bounds);
// Each thing starts at its own place on the tile (in its own space): a key from
// its number (the live renderer gets the key in InstanceU::material[2]) and the
// place from the key, the same hash in both renderers.
uint32_t surfaceStartKey(uint32_t thing);
Vec3 surfaceStart(uint32_t key, float tile);

// Steps 1 to 9: the whole stack at one point of one thing. Order M3c. `place` and
// `facing` are in the recipe's space (the thing's, or the world's); the bumped
// facing comes back in that space. `edge` and `hollow` are the marks (0 to 1).
// Steps 1 to 3 (and the slow change's read) are blended from projectSurface's
// sides; a recipe whose `along` is still -1 (the longest side) is read along x.
Surface surfaceAt(const SurfaceRecipe&, const Material&, Vec3 place, Vec3 facing, Vec2 uv,
                  float edge, float hollow, float distance);

}  // namespace loop
