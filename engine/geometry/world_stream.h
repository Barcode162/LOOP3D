#pragma once
// Streaming a big world (the rebuild's step 12): the land in square chunks;
// those near the player are made (a few a step, nearest first) and shown,
// those far away hidden (kept, so coming back costs nothing), with a margin
// between the two so walking along a chunk's edge doesn't flicker it.
// Pure bookkeeping: the script's `world` word makes and hides the shapes.
#include <map>
#include <utility>
#include <vector>

#include "engine/core/math.h"

namespace loop {

struct WorldStream {
  float chunk = 256;       // metres a side
  float show = 1000;       // chunks whose middle is this near are shown
  float hide = 1250;       // and hidden again beyond this
  int makePerStep = 1;     // chunks made a step at most (making is the costly part)
  float half = 4096;       // the world's half-width: no chunks beyond it
  struct Chunk {
    bool made = false, shown = false;
  };
  std::map<std::pair<int, int>, Chunk> chunks;
  struct Work {
    std::vector<std::pair<int, int>> make, show, hide;
  };
  // What to do now, the player at `at` (x, z); the chunks' states follow it.
  // `makeAll`: every chunk wanted is made now (the first look round).
  Work update(Vec2 at, bool makeAll = false);
  Vec2 middle(std::pair<int, int> c) const { return {(static_cast<float>(c.first) + 0.5f) * chunk, (static_cast<float>(c.second) + 0.5f) * chunk}; }
  int shown() const;
};

}  // namespace loop
