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
  // The detail tile's chain (section 8). Empty until order M7 fills it.
  std::vector<Image> detail;
};

// The recipe's pictures at `size` pixels square (at least 1; a power of two
// halves exactly at every copy, any other size halves rounded down): baked the
// first time they are asked, the same pointer every later time. Safe from any
// thread; the rows are shared over every core.
std::shared_ptr<const SurfacePictures> bakeSurface(const SurfaceRecipe& recipe, int size);

}  // namespace loop
