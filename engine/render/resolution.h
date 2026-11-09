#pragma once
// The automatic resolution steered by the frame graph's timings (the picture
// pipeline's phase 7, docs/PIPELINE.md). Only some of a frame's work shrinks
// with the drawing: the steps that work per drawn pixel (finding what each
// pixel sees, lighting it). The shadow maps don't, and since frame blending
// upscales, nor do blending, bloom and the film (they work on the full
// picture). Dropping the resolution for time it can't save only blurs the
// picture, so the size is chosen from the part that scales. A step that
// didn't run in the latest timed frame (a shadow drawn again now and then) is
// not part of the steady cost.
#include <string>
#include <vector>

#include "engine/render/frame_graph.h"

namespace loop {

struct FrameSplit {
  float fixedMs = 0;   // work the resolution doesn't change
  float scaledMs = 0;  // work per drawn pixel (goes with the square of the scale)
};

// The steps whose work is per drawn pixel (with `upscaled` false, blending,
// bloom and the film work on the drawn picture too).
bool scalesWithResolution(const std::string& step, bool upscaled = true);

FrameSplit splitFrame(const std::vector<FrameGraph::Time>& recent, bool upscaled = true);

// The scale at which the frame would take `budgetMs`, from the split at
// `scaleNow`; when the steady work that doesn't scale fills the budget alone,
// shrinking can't help: `scaleNow` back (hold).
float scaleToFit(const FrameSplit& split, float scaleNow, float budgetMs);

// The frame's time at another scale.
float msAtScale(const FrameSplit& split, float scaleNow, float scale);

}  // namespace loop
