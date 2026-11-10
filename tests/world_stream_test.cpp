// Streaming a big world (engine/geometry/world_stream.h).
#include "engine/geometry/world_stream.h"
#include "tests/test.h"

using namespace loop;

TEST(world_stream_makes_near_chunks_first_a_few_a_step) {
  WorldStream s;
  WorldStream::Work w = s.update({0, 0});
  CHECK(w.make.size() == 1);  // one a step
  // the first is the one the player stands in (or next to it)
  CHECK(length(s.middle(w.make[0])) < s.chunk);
  int steps = 1;
  while (!s.update({0, 0}).make.empty()) ++steps;
  // every chunk within reach made, none beyond
  const int all = static_cast<int>(s.chunks.size());
  CHECK(steps == all);
  for (const auto& [c, ch] : s.chunks) CHECK(length(s.middle(c)) <= s.show);
  CHECK(s.shown() == all);
  CHECK(all > 40 && all < 60);  // pi x 1000^2 / 256^2: about 48
}

TEST(world_stream_follows_the_player_and_keeps_what_it_made) {
  WorldStream s;
  s.update({0, 0}, true);
  const int first = static_cast<int>(s.chunks.size());
  // walk 2 km east: chunks behind hidden, ahead made
  int hidden = 0, made = 0;
  for (float x = 0; x <= 2000; x += 20) {
    WorldStream::Work w = s.update({x, 0}, true);
    hidden += static_cast<int>(w.hide.size()), made += static_cast<int>(w.make.size());
    for (const auto& [c, ch] : s.chunks)
      if (ch.shown) CHECK(length(s.middle(c) - Vec2(x, 0)) <= s.hide);
  }
  CHECK(hidden > 20 && made > 20);
  // walk back: nothing new to make, the old ones shown again
  int remade = 0, reshown = 0;
  for (float x = 2000; x >= 0; x -= 20) {
    WorldStream::Work w = s.update({x, 0}, true);
    remade += static_cast<int>(w.make.size()), reshown += static_cast<int>(w.show.size());
  }
  CHECK(remade == 0 && reshown > 20);
  CHECK(static_cast<int>(s.chunks.size()) == first + made);
  // standing on a chunk's edge, stepping back and forth: nothing flickers
  WorldStream t;
  t.update({1000, 0}, true);
  int changes = 0;
  for (int k = 0; k < 20; ++k) {
    WorldStream::Work w = t.update({k % 2 ? 1002.0f : 998.0f, 0}, true);
    changes += static_cast<int>(w.show.size() + w.hide.size());
  }
  CHECK(changes == 0);
}

TEST(world_stream_stays_inside_the_world) {
  WorldStream s;
  s.half = 1024;
  s.update({1000, 1000}, true);
  for (const auto& [c, ch] : s.chunks) CHECK(std::fabs(s.middle(c).x) <= 1024 && std::fabs(s.middle(c).y) <= 1024);
}
