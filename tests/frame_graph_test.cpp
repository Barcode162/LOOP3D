// The frame graph (engine/render/frame_graph.h): a frame as named steps, each
// saying what it reads and writes, checked and timed.
#include "engine/render/frame_graph.h"
#include "tests/test.h"

using namespace loop;

TEST(frame_graph_runs_its_steps_in_order) {
  FrameGraph g({"scene"});
  std::string ran;
  int seen = -2;
  g.add("shadow", {"scene"}, {"shadow map"}, [&] { ran += "s"; seen = g.current(); });
  g.add("light", {"scene", "shadow map"}, {"hdr"}, [&] { ran += "l"; });
  g.add("bloom", {"hdr"}, {"bloom"}, [&] { ran += "b"; }, false);  // off: skipped
  g.add("film", {"hdr", "bloom"}, {"screen"}, [&] { ran += "f"; });
  g.run();
  CHECK(ran == "slf");
  CHECK(seen == 0);           // while it runs, a step knows it is the one running
  CHECK(g.current() == -1);   // and between frames no step is
  CHECK(g.check().empty());   // every read comes from before (a step that is off counts)
  CHECK(g.describe()[1] == "light: reads scene, shadow map; writes hdr");
}

TEST(frame_graph_finds_a_read_with_nothing_before_it) {
  FrameGraph g({"scene"});
  g.add("light", {"scene", "shadow map"}, {"hdr"}, nullptr);
  g.add("shadow", {"scene"}, {"shadow map"}, nullptr);  // too late
  g.add("light", {"hdr"}, {}, nullptr);                  // the same name, and writes nothing
  auto faults = g.check();
  CHECK(faults.size() == 3);
  CHECK(faults[0].find("\"light\" reads shadow map") == 0);
  CHECK(faults[1].find("two steps are called \"light\"") == 0);
  CHECK(faults[2].find("writes nothing") != std::string::npos);
  g.clear();
  CHECK(g.steps().empty() && g.check().empty());
}

TEST(frame_graph_adds_up_each_steps_time) {
  FrameGraph g;
  // bloom stamps two passes in a frame: they add up; times are in nanoseconds
  g.addTimes({{"shadow", 1000, 2001000}, {"bloom", 3000000, 3500000}, {"bloom", 4000000, 4250000}, {"film", 9, 5}});
  CHECK(g.times().size() == 2);  // a timer that ran backwards says nothing
  CHECK_NEAR(g.times()[0].ms, 2.0f, 1e-4f);
  CHECK_NEAR(g.times()[1].ms, 0.75f, 1e-4f);
  g.addTimes({{"shadow", 0, 4000000}});
  CHECK_NEAR(g.times()[0].ms, 2.2f, 1e-4f);  // smoothed for the meter
  auto avg = g.takeAverages();               // averaged over the frames, for the recordings
  CHECK(avg.size() == 2 && avg[0].first == "shadow");
  CHECK_NEAR(avg[0].second, 3.0f, 1e-4f);
  CHECK_NEAR(avg[1].second, 0.75f, 1e-4f);
  CHECK(g.takeAverages().empty());  // and starts again
  CHECK(g.timed());
}

TEST(frame_graph_forgets_a_step_that_stopped) {
  // a scene drawn the old way while its clusters cook, then in clusters
  FrameGraph g;
  g.addTimes({{"forward", 0, 9000000}});
  for (uint32_t i = 0; i < FrameGraph::kGone; ++i) g.addTimes({{"light", 0, 2000000}});
  auto recent = g.recent();
  CHECK(recent.size() == 1 && recent[0].step == "light");  // the meter shows what runs now
  CHECK(g.times().size() == 2);                              // the old one is still owed to the recordings
  auto avg = g.takeAverages();
  CHECK(avg.size() == 2);
  CHECK(g.times().size() == 1);  // counted, then forgotten
}

// The iPhone's limit (8 storage buffers a shader stage; Safari reports more):
// a pipeline's bind groups add up, stage by stage.
TEST(frame_graph_counts_storage_buffers_against_the_iphone) {
  using SB = FrameGraph::StageBuffers;
  // the lighting pass today: the scene's group, the rays', the clusters' (8 in the fragment stage, just in)
  std::vector<SB> light = {{4, 3, 2}, {0, 4, 4}, {0, 1, 0}};
  CHECK(FrameGraph::storageFaults("light", light).empty());
  light.push_back({0, 1, 0});  // one more: what broke the phone on 2026-11-06
  auto faults = FrameGraph::storageFaults("light", light);
  CHECK(faults.size() == 1);
  CHECK(faults[0] == "the light pipeline's fragment stage reads 9 storage buffers; the iPhone allows 8");
  CHECK(FrameGraph::storageFaults("cull", {{0, 0, 9}}, 8).size() == 1);
  CHECK(FrameGraph::storageFaults("cull", {{0, 0, 9}}, 10).empty());
}

