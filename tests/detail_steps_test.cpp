// The Detail slider's table (engine/render/detail_steps.h).
#include "engine/core/settings.h"
#include "engine/render/detail_steps.h"
#include "tests/test.h"

using namespace loop;

TEST(detail_steps_every_value_is_one_the_setting_takes) {
  size_t n = detailStep(1).values.size();
  for (int i = 1; i <= kDetailSteps; ++i) {
    const DetailStep& s = detailStep(i);
    CHECK(s.step == i);
    CHECK(s.values.size() == n);  // every row sets the same settings
    for (const auto& [id, v] : s.values) {
      std::string why;
      CHECK(Settings::get().check(id, v, &why));
      CHECK(s.values[0].first == detailStep(1).values[0].first);
    }
    if (i > 1) CHECK(s.quality >= detailStep(i - 1).quality);  // never back from High to Phone
  }
  CHECK(detailStep(0).step == 1 && detailStep(99).step == kDetailSteps);
}

TEST(detail_steps_sit_under_what_a_scene_sets) {
  Settings& st = Settings::get();
  st.resetAll();
  CHECK(applyDetail(kDetailPhone) == 0 && applyDetail(kDetailHigh) == 1);
  CHECK(st.value("render.lamps").number() == 2);  // step 7: two lamps traced
  st.set(Settings::kScene, "render.lamps", Json(4));
  applyDetail(1);
  CHECK(st.value("render.lamps").number() == 4);  // the scene's own choice wins
  CHECK(st.value("render.mirrorPane").boolean() == false);  // the dial's
  st.resetAll();
  CHECK(st.value("render.mirrorPane").boolean() == true);  // the default back
}
