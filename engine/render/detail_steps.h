// The Detail slider: one dial, 1 to 10, that sets many render settings at
// once. Each step is one row of a table: the quality it draws at (Phone or
// High) and the values it gives a dozen settings (sharpness, lamp shadows, the
// sun's shadow map, shader detail, the mirror, bounced light, shape detail).
//
// The low steps give up what costs most and keep the picture sharp (the smooth
// feel); the high steps add it back. What costs most depends on the kind of
// scene, so each kind has its own recipe (a table of ten rows):
//   balanced  the first table: step 5 is Phone and step 7 High as they were
//   interior  a night room: lamps cost most, so lamps are grouped and their
//             shadows go first; shapes (few triangles) keep full detail
//   open      a daylight space: shapes and the sun's shadow cost most, so far
//             shapes get simpler and the sun's shadow shorter first
// A scene or project names its kind with `render.style`; "" works it out. The values go in the settings' lowest
// layer (Settings::kDetail), so anything a project, a scene or the editor sets
// on purpose still wins.
#pragma once

#include <string>
#include <utility>
#include <vector>

#include "engine/core/json.h"

namespace loop {

struct DetailStep {
  int step = 5;             // 1 .. 10
  int quality = 0;          // 0 Phone, 1 High (Ultra is the path tracer, not on the dial)
  const char* says = "";    // one plain line for the menu
  std::vector<std::pair<std::string, Json>> values;  // setting id -> value
};

enum class DetailStyle { kBalanced, kInterior, kOpen };

constexpr int kDetailSteps = 10;
constexpr int kDetailPhone = 5;  // where a phone starts
constexpr int kDetailHigh = 7;   // where a computer starts

const char* detailStyleName(DetailStyle style);  // "balanced", "interior", "open"

// The recipe for a scene: `named` is render.style ("" or an unknown name works
// it out: many lamps, or lamps and no sun, is an interior; the sun with few
// lamps is open; anything else balanced).
DetailStyle detailStyleFor(const std::string& named, int lamps, bool sun);

// The step's row in a recipe (the step clamped to 1 .. 10).
const DetailStep& detailStep(int step, DetailStyle style = DetailStyle::kBalanced);

// Puts the step's values in the detail layer (clearing what was there) and
// returns the quality the step draws at.
int applyDetail(int step, DetailStyle style = DetailStyle::kBalanced);

}  // namespace loop
