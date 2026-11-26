#pragma once
// The Budget page: everything one live frame spends, each line as "used of
// what's there", like the storage screen in a phone's settings. One sheet for
// the player's Budget page and `loop budget`, so the phone and the command line
// read the same numbers the same way.
//
// Each line says where its number comes from: measured (timed while the game
// runs), counted (worked out from the scene, frame_cost.h), or estimated (the
// browser hides it, so a known safe figure for the phone stands in).
#include <string>
#include <vector>

#include "engine/core/json.h"
#include "engine/render/frame_cost.h"

namespace loop {

// The live frame as the engine's settings draw it, from a camera, at a
// budget's size: the view `loop budget` and the player's Budget page count.
FrameView liveFrameView(const Scene& scene, const Camera& camera, const FrameBudget& budget);

// What only the running player knows (0 where it isn't running: the command line).
struct LiveNumbers {
  bool running = false;     // the player: times and memory below are real
  float cpuMs = 0, gpuMs = 0;
  float targetFps = 60;     // render.targetFps: the frame's time is 1000 / this
  float sharpness = 1;      // the share of full resolution drawn now (automatic resolution)
  double gpuMB = 0;         // what the engine holds on the graphics chip
  double heapMB = 0;        // the engine's own memory (reserved)
};

enum class Source { kMeasured, kCounted, kEstimated };

struct BudgetLine {
  std::string id, name, unit;
  double used = 0, budget = 0;
  Source source = Source::kCounted;    // where `used` comes from
  bool budgetEstimated = false;        // the "what's there" side is a known safe figure, not read from the device
  bool higherIsBetter = false;         // sharpness: full is the aim, lower is what's given up
  std::string says;                    // one plain sentence: what it is
  // 0 fine, 1 near (over 80%), 2 over; for higher-is-better, under 60% is over and under 85% near
  int state() const;
};

std::vector<BudgetLine> budgetSheet(const FrameCost& cost, const FrameBudget& budget, const LiveNumbers& live);
Json budgetSheetJson(const std::vector<BudgetLine>& lines);
const char* sourceName(Source s);

}  // namespace loop
