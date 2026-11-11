#pragma once
// The fifteen patterns of a surface recipe (docs/SURFACES.md section 5), in one
// place. A pattern is a pure function of a place on the tile and the seed, and
// it wraps: the value at u, u + (tile, 0) and u + (0, tile) is the same. To
// wrap, a whole number of features is fitted into the tile by changing `size`
// a little (fittedSize says what size was used).
#include <cstdint>

#include "engine/core/math.h"
#include "engine/render/surface.h"

namespace loop {

// `u` in metres. mix 0..1 (which of the two colours), height 0..1, piece about
// -setting..+setting (the same across one brick, plank, tile, grain or cell).
// kNone gives mix 0, height 0.5, piece 0.
void patternAt(const SurfacePattern& p, Vec2 u, float tile, uint32_t seed, float& mix, float& height,
               float& piece);

// The size patternAt really uses on this tile: `size` changed so a whole number
// of features fits. (For bricks it is the brick's length; for the rest, the
// feature's width across.) The cook warns when it is more than 5% off.
float fittedSize(const SurfacePattern& p, float tile);

}  // namespace loop
