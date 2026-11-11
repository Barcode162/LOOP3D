#pragma once
// The pictures the cook bakes from a surface recipe (docs/SURFACES.md section 8;
// order M4): the part of the surface that repeats, as a square tile the phone
// reads, so the patterns never live in a shader. Made once per recipe and size,
// on every core, and kept by the recipe's fingerprint like the cook's cluster
// trees (engine/geometry/cook.h).
#include <memory>
#include <vector>

#include "engine/core/image.h"
#include "engine/render/surface.h"

namespace loop {

// Each picture is a chain of smaller copies, largest first: `size` square, then
// half that, down to one pixel. Pixel (x, y) of the largest covers the tile from
// ((x, y) / size × tile) to ((x + 1, y + 1) / size × tile) metres; x runs along
// u.x and y along u.y (row 0 at the top).
struct SurfacePictures {
  // red, green, blue: the colour after step 3 of section 7, stored sRGB-encoded
  // (0 to 1, not linear like other Images) so it can go up as an sRGB picture;
  // alpha: the tile's height, 0 (lowest) to 1 (highest).
  std::vector<Image> colour;
  // red, green: the bumped facing's two sideways parts (along u.x, along u.y),
  // stored as 0.5 + 0.5 × part, so flat is (0.5, 0.5); blue: roughness; alpha: metal.
  std::vector<Image> shape;
  // The detail tile's chain (section 8; order M7), empty when the recipe has no
  // detail. As large as the others (`size`), one square of `detailTile` metres. Red,
  // green: the detail's bump facing's two sideways parts (0.5 + 0.5 × part, as in
  // `shape`); blue: the detail's height, 0 to 1 (0.5: no change of shine; the
  // shine changes by `detailRoughness × (blue − 0.5) × 2`); alpha: the roughness
  // the smaller copies add where their bumps disagreed (Toksvig, as in `shape`;
  // 0 in the largest, so the shader adds it to the shine).
  std::vector<Image> detail;
};

// What makes a recipe's detail picture: its pattern, tile, bump and the seed (not
// its shine change, its fade or anything else of the recipe). Two recipes with the
// same detail share one picture.
uint64_t detailFingerprint(const SurfaceRecipe& recipe);
// The detail's chain on its own (empty: the recipe has none), kept by that
// fingerprint and the size: the same pointer for any recipe with the same detail.
std::shared_ptr<const std::vector<Image>> bakeDetail(const SurfaceRecipe& recipe, int size);

// The recipe's pictures at `size` pixels square (at least 1; a power of two
// halves exactly at every copy, any other size halves rounded down): baked the
// first time they are asked, the same pointer every later time. Safe from any
// thread; the rows are shared over every core.
std::shared_ptr<const SurfacePictures> bakeSurface(const SurfaceRecipe& recipe, int size);

}  // namespace loop
