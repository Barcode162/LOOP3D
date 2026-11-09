#include "engine/render/resolution.h"

#include <algorithm>
#include <cmath>

namespace loop {

bool scalesWithResolution(const std::string& step, bool upscaled) {
  if (step == "visibility" || step == "light" || step == "forward" || step == "ultra") return true;
  return !upscaled && (step == "blend" || step == "bloom" || step == "film");
}

FrameSplit splitFrame(const std::vector<FrameGraph::Time>& recent, bool upscaled) {
  FrameSplit s;
  for (const FrameGraph::Time& t : recent) {
    if (t.missed > 0) continue;  // not in the latest timed frame: now and then, not steady
    (scalesWithResolution(t.step, upscaled) ? s.scaledMs : s.fixedMs) += t.ms;
  }
  return s;
}

float msAtScale(const FrameSplit& s, float scaleNow, float scale) {
  const float r = scale / std::max(scaleNow, 1e-3f);
  return s.fixedMs + s.scaledMs * r * r;
}

float scaleToFit(const FrameSplit& s, float scaleNow, float budgetMs) {
  const float room = budgetMs - s.fixedMs;
  if (room <= 0 || s.scaledMs <= 0) return scaleNow;
  return scaleNow * std::sqrt(room / s.scaledMs);
}

}  // namespace loop
