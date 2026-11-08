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
