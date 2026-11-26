// The Budget page's sheet: every line used of what's there, where each number
// comes from, and which are over.
#include "engine/render/budget_sheet.h"
#include "tests/test.h"

using namespace loop;

static const BudgetLine* line(const std::vector<BudgetLine>& s, const char* id) {
  for (const BudgetLine& l : s)
    if (l.id == id) return &l;
  return nullptr;
}

TEST(budget_sheet_counts_without_a_phone) {
  FrameBudget b = budgetFor("phone");
  FrameCost c;
  c.sceneTriangles = 300000, c.shadowTriangles = 60000;
  c.shadedPerPixel = 1, c.lampsComputed = 34;
  auto s = budgetSheet(c, b, LiveNumbers{});
  CHECK(line(s, "gpu") == nullptr);  // no times on the command line
  const BudgetLine* tri = line(s, "triangles");
  CHECK(tri && tri->source == Source::kCounted);
  CHECK_NEAR(tri->used, 0.36, 1e-9);
  CHECK(tri->state() == 0);  // 0.36 of 0.6 is 60%: fine (near starts over 80%)
  const BudgetLine* light = line(s, "light");
  CHECK(light && light->used == 34 && light->state() == 2);  // the Loft: 34 against 24
  CHECK(line(s, "pictures") && line(s, "pictures")->budgetEstimated);
}

TEST(budget_sheet_states_fine_near_over) {
  BudgetLine l;
  l.budget = 10;
  l.used = 5;
  CHECK(l.state() == 0);
  l.used = 9;
  CHECK(l.state() == 1);
  l.used = 11;
  CHECK(l.state() == 2);
  l.higherIsBetter = true;
  l.used = 9;
  CHECK(l.state() == 0);
  l.used = 7;
  CHECK(l.state() == 1);
  l.used = 3.5;
  CHECK(l.state() == 2);  // sharpness at 35%: the junky feel
}

TEST(budget_sheet_live_numbers_from_the_player) {
  LiveNumbers live;
  live.running = true, live.gpuMs = 14.4f, live.cpuMs = 1.1f, live.sharpness = 0.35f, live.gpuMB = 180, live.heapMB = 90;
  auto s = budgetSheet(FrameCost{}, budgetFor("phone"), live);
  const BudgetLine* gpu = line(s, "gpu");
  CHECK(gpu && gpu->source == Source::kMeasured);
  CHECK_NEAR(gpu->budget, 1000.0 / 60, 1e-4);
  CHECK(gpu->state() == 1);
  CHECK(line(s, "sharpness")->state() == 2);
  CHECK(line(s, "gpuMem")->budgetEstimated && line(s, "gpuMem")->state() == 0);
  CHECK(line(s, "pictures") == nullptr);  // the live graphics memory holds them
  Json j = budgetSheetJson(s);
  CHECK(j.size() == s.size());
}
