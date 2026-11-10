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

void ScaleLearner::observe(const std::vector<FrameGraph::Time>& recent, float scale) {
  for (const FrameGraph::Time& t : recent) {
    if (t.missed > 0 || !scalesWithResolution(t.step)) continue;
    Seen* s = nullptr;
    for (Seen& e : seen_)
      if (e.step == t.step) s = &e;
    if (!s) seen_.push_back({t.step}), s = &seen_.back();
    if (std::fabs(scale - s->scaleA) < 1e-3f) {
      s->msA = s->msA * 0.5f + t.ms * 0.5f;  // the same size: smoothed
    } else {
      s->scaleB = s->scaleA, s->msB = s->msA;  // a new size: the last one kept to compare
      s->scaleA = scale, s->msA = t.ms;
    }
  }
}

float ScaleLearner::scaledShare(const std::string& step, float scale) const {
  for (const Seen& s : seen_) {
    if (s.step != step) continue;
    const float a2 = s.scaleA * s.scaleA, b2 = s.scaleB * s.scaleB;
    if (s.scaleB <= 0 || std::fabs(a2 - b2) < 0.15f * std::max(a2, b2)) return 1;  // not two sizes far enough apart
    const float perPixel = std::max((s.msA - s.msB) / (a2 - b2), 0.0f);
    const float fixed = std::max(s.msA - perPixel * a2, 0.0f);
    const float scaled = perPixel * scale * scale;
    return std::clamp(scaled / std::max(scaled + fixed, 1e-6f), 0.2f, 1.0f);
  }
  return 1;
}

FrameSplit splitFrame(const std::vector<FrameGraph::Time>& recent, bool upscaled, const ScaleLearner& learned, float scale) {
  FrameSplit s;
  for (const FrameGraph::Time& t : recent) {
    if (t.missed > 0) continue;
    if (!scalesWithResolution(t.step, upscaled)) {
      s.fixedMs += t.ms;
      continue;
    }
    const float k = scalesWithResolution(t.step) ? learned.scaledShare(t.step, scale) : 1.0f;
    s.scaledMs += t.ms * k, s.fixedMs += t.ms * (1 - k);
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
