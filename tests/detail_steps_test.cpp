// The Detail slider's recipes (engine/render/detail_steps.h).
#include "engine/core/settings.h"
#include "engine/render/detail_steps.h"
#include "tests/test.h"

using namespace loop;

namespace {
double num(const DetailStep& s, const char* id) {
  for (const auto& [k, v] : s.values)
    if (k == id) return v.type() == Json::Type::kBool ? (v.boolean() ? 1 : 0) : v.number();
  return -1e9;
}
const DetailStyle kStyles[] = {DetailStyle::kBalanced, DetailStyle::kInterior, DetailStyle::kOpen};
}  // namespace

TEST(detail_steps_every_value_is_one_the_setting_takes) {
  size_t n = detailStep(1).values.size();
  for (DetailStyle st : kStyles)
    for (int i = 1; i <= kDetailSteps; ++i) {
      const DetailStep& s = detailStep(i, st);
      CHECK(s.step == i);
      CHECK(s.values.size() == n);  // every row of every recipe sets the same settings
      for (size_t k = 0; k < s.values.size(); ++k) {
        std::string why;
        CHECK(Settings::get().check(s.values[k].first, s.values[k].second, &why));
        CHECK(s.values[k].first == detailStep(1).values[k].first);
      }
      if (i > 1) {
        const DetailStep& p = detailStep(i - 1, st);
        CHECK(s.quality >= p.quality);  // never back from High to Phone
        // a step up never gives anything back
        CHECK(num(s, "render.clusterError") <= num(p, "render.clusterError"));
        CHECK(num(s, "render.lampGrouping") >= num(p, "render.lampGrouping"));
        CHECK(num(s, "render.shadowDistance") >= num(p, "render.shadowDistance"));
        CHECK(num(s, "render.lamps") >= num(p, "render.lamps"));
        CHECK(num(s, "render.shading") >= num(p, "render.shading"));
      }
    }
  CHECK(detailStep(0).step == 1 && detailStep(99).step == kDetailSteps);
}

TEST(detail_styles_give_up_what_costs_most_first) {
  const DetailStep& room = detailStep(1, DetailStyle::kInterior);
  const DetailStep& out = detailStep(1, DetailStyle::kOpen);
  // a night room keeps its (cheap) shapes and groups its lamps
  CHECK(num(room, "render.clusterError") == 1.0);
  CHECK(num(room, "render.lampGrouping") < num(out, "render.lampGrouping"));
  // a daylight space makes far shapes simpler and the sun's shadow short
  CHECK(num(out, "render.clusterError") > 4.0);
  CHECK(num(out, "render.shadowDistance") <= 15);
  // at the top, the open recipe spends on the sun's shadow, the room on lamps
  CHECK(num(detailStep(10, DetailStyle::kOpen), "render.shadowDistance") > num(detailStep(10, DetailStyle::kInterior), "render.shadowDistance"));
  CHECK(num(detailStep(10, DetailStyle::kInterior), "render.lamps") > num(detailStep(10, DetailStyle::kOpen), "render.lamps"));
}

TEST(detail_style_worked_out_from_the_lamps_and_the_sun) {
  CHECK(detailStyleFor("", 46, false) == DetailStyle::kInterior);  // the Loft
  CHECK(detailStyleFor("", 0, true) == DetailStyle::kOpen);         // the Glade
  CHECK(detailStyleFor("", 2, true) == DetailStyle::kOpen);
  CHECK(detailStyleFor("", 1, false) == DetailStyle::kInterior);
  CHECK(detailStyleFor("", 0, false) == DetailStyle::kBalanced);
  CHECK(detailStyleFor("open", 46, false) == DetailStyle::kOpen);   // a scene's own word wins
  CHECK(detailStyleFor("balanced", 46, false) == DetailStyle::kBalanced);
  CHECK(std::string(detailStyleName(DetailStyle::kInterior)) == "interior");
}

TEST(detail_steps_sit_under_what_a_scene_sets) {
  Settings& st = Settings::get();
  st.resetAll();
  CHECK(applyDetail(kDetailPhone) == 0 && applyDetail(kDetailHigh) == 1);
  CHECK(st.value("render.lamps").number() == 2);  // step 7: two lamps traced
  st.set(Settings::kScene, "render.lamps", Json(4));
  applyDetail(1, DetailStyle::kInterior);
  CHECK(st.value("render.lamps").number() == 4);  // the scene's own choice wins
  CHECK(st.value("render.lampGrouping").number() == 3);  // the dial's
  st.resetAll();
  CHECK(st.value("render.lampGrouping").number() == 7);  // the default back
}
