#include "engine/render/budget_sheet.h"

#include <algorithm>

#include "engine/core/settings.h"

namespace loop {

FrameView liveFrameView(const Scene& scene, const Camera& camera, const FrameBudget& budget) {
  const Config& C = config();
  FrameView v;
  v.camera = camera;
  v.width = budget.width, v.height = budget.height;
  v.drawDistance = C.world.drawDistance, v.maxLights = C.render.maxLights;
  v.cullOffscreen = C.render.cull;
  v.lodBias = C.world.lodBias;
  v.portraitWide = C.camera.portraitWide;
  v.clusters = C.render.clusters, v.clusterError = C.render.clusterError;
  v.textureSize = C.render.textureSize;
  v.lightGrid = C.render.lightGrid, v.lampGrouping = C.render.lampGrouping, v.lampError = C.render.lampError;
  v.mirrorSmallest = C.render.mirrorSmallest;
  v.mirrorPane = C.render.mirrorPane && budget.quality != "ultra";
  int lamps = 0;
  for (const Light& l : scene.lights) lamps += l.kind != Light::Kind::kSun;
  v.prepass = prepassOn(C.render.prepass, std::min(lamps, std::max(C.render.maxLights, 1)));
  v.shadowRadius = C.render.shadowDistance > 0 ? C.render.shadowDistance : budget.quality == "high" ? 40.0f : 25.0f;
  return v;
}

int BudgetLine::state() const {
  if (budget <= 0) return 0;
  double share = used / budget;
  if (higherIsBetter) return share < 0.6 ? 2 : share < 0.85 ? 1 : 0;
  return share > 1 ? 2 : share > 0.8 ? 1 : 0;
}

const char* sourceName(Source s) { return s == Source::kMeasured ? "measured" : s == Source::kCounted ? "counted" : "estimated"; }

// Safe memory for one browser tab on the phones Loop3D is judged on (an
// iPhone 15 Pro Max has 8 GB; Safari closes a tab well before that and won't
// say where). Estimates until a phone run finds the real edge.
static double safeGpuMB(const FrameBudget& b) { return b.quality == "high" ? 1536 : 1024; }
static constexpr double kSafeHeapMB = 1024;

std::vector<BudgetLine> budgetSheet(const FrameCost& c, const FrameBudget& b, const LiveNumbers& live) {
  std::vector<BudgetLine> out;
  auto add = [&](const char* id, const char* name, const char* unit, double used, double budget, Source src, const char* says) -> BudgetLine& {
    BudgetLine l;
    l.id = id, l.name = name, l.unit = unit, l.used = used, l.budget = budget, l.source = src, l.says = says;
    out.push_back(l);
    return out.back();
  };
  const double frameMs = 1000.0 / std::max(1.0f, live.targetFps);
  if (live.running) {
    add("gpu", "Graphics chip time", "ms", live.gpuMs, frameMs, Source::kMeasured,
        "How long the graphics chip takes to draw one frame. Over the line, the frame is late and it stutters.");
    add("cpu", "Processor time", "ms", live.cpuMs, frameMs, Source::kMeasured,
        "How long the game itself takes each frame: scripts, physics, people, getting the drawing ready.");
    add("sharpness", "Sharpness", "%", live.sharpness * 100, 100, Source::kMeasured,
        "How much of full sharpness is drawn. The engine lowers it to stay smooth when the frame costs too much.")
        .higherIsBetter = true;
  }
  add("triangles", "Shapes (triangles)", "M", static_cast<double>(c.triangles()) / 1e6, static_cast<double>(b.triangles) / 1e6, Source::kCounted,
      "The tiny flat pieces every shape is made of, in every pass (what you see, the sun's shadow, the mirror).");
  add("light", "Light (lamp work per pixel)", "", c.lampWorkPerPixel(), b.lampWorkPerPixel, Source::kCounted,
      "How many lamps each pixel works out, times the surfaces it shades. The cost of many live lamps.");
  add("shaded", "Surfaces shaded per pixel", "", c.shadedPerPixel, b.shadedPerPixel, Source::kCounted,
      "How many times each pixel is coloured in. Things behind other things still cost unless they're skipped.");
  add("draws", "Draw calls", "", static_cast<double>(c.draws()), static_cast<double>(b.draws), Source::kCounted,
      "How many separate jobs are sent to the graphics chip each frame. Each one has a small fixed cost.");
  if (live.running) {
    add("gpuMem", "Graphics memory", "MB", live.gpuMB, safeGpuMB(b), Source::kMeasured,
        "Pictures, shapes and shadows held on the graphics chip. Too much and Safari closes the page.")
        .budgetEstimated = true;
    add("heap", "Engine memory", "MB", live.heapMB, kSafeHeapMB, Source::kMeasured,
        "The engine's own memory: the scene, its scripts and its sounds.")
        .budgetEstimated = true;
  } else {
    add("pictures", "Pictures on the graphics chip", "MB", static_cast<double>(c.pictureBytes) / (1024.0 * 1024.0), safeGpuMB(b), Source::kCounted,
        "The scene's model and surface pictures as the renderer holds them.")
        .budgetEstimated = true;
  }
  return out;
}

Json budgetSheetJson(const std::vector<BudgetLine>& lines) {
  Json a = Json::array();
  for (const BudgetLine& l : lines) {
    Json o = Json::object();
    o.set("id", Json(l.id));
    o.set("name", Json(l.name));
    o.set("unit", Json(l.unit));
    o.set("used", Json(l.used));
    o.set("budget", Json(l.budget));
    o.set("source", Json(sourceName(l.source)));
    o.set("budgetEstimated", Json(l.budgetEstimated));
    o.set("higherIsBetter", Json(l.higherIsBetter));
    o.set("state", Json(l.state()));
    o.set("says", Json(l.says));
    a.push(o);
  }
  return a;
}

}  // namespace loop
