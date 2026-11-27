// The Detail slider: one dial, 1 to 10, that sets many render settings at
// once. Each step is one row of a table: the quality it draws at (Phone or
// High) and the values it gives a dozen settings (sharpness, lamp shadows, the
// sun's shadow map, shader detail, the mirror, bounced light, shape detail).
//
// The low steps give up costly light first and keep the picture sharp (the
// smooth feel); the high steps add light back. Step 5 is Phone as it was before
// the slider, step 7 is High as it was. The values go in the settings' lowest
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

constexpr int kDetailSteps = 10;
constexpr int kDetailPhone = 5;  // where a phone starts
constexpr int kDetailHigh = 7;   // where a computer starts

// The step's row (clamped to 1 .. 10).
const DetailStep& detailStep(int step);

// Puts the step's values in the detail layer (clearing what was there) and
// returns the quality the step draws at.
int applyDetail(int step);

}  // namespace loop
