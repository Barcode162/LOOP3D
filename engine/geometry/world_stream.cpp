#include "engine/geometry/world_stream.h"

#include <algorithm>
#include <cmath>

namespace loop {

WorldStream::Work WorldStream::update(Vec2 at, bool makeAll) {
  Work w;
  // the chunks near enough, nearest first
  std::vector<std::pair<float, std::pair<int, int>>> want;
  const int r = static_cast<int>(std::ceil(show / chunk)) + 1;
  const int cx = static_cast<int>(std::floor(at.x / chunk)), cz = static_cast<int>(std::floor(at.y / chunk));
  for (int z = cz - r; z <= cz + r; ++z)
    for (int x = cx - r; x <= cx + r; ++x) {
      const std::pair<int, int> c{x, z};
      const Vec2 m = middle(c);
      if (std::fabs(m.x) > half || std::fabs(m.y) > half) continue;
      const float d = length(m - at);
      if (d <= show) want.push_back({d, c});
    }
  std::sort(want.begin(), want.end());
  int made = 0;
  for (const auto& [d, c] : want) {
    Chunk& ch = chunks[c];
    if (!ch.made) {
      if (!makeAll && made >= makePerStep) continue;
      ch.made = true, ++made;
      w.make.push_back(c);
    }
    if (!ch.shown) ch.shown = true, w.show.push_back(c);
  }
  for (auto& [c, ch] : chunks)
    if (ch.shown && length(middle(c) - at) > hide) ch.shown = false, w.hide.push_back(c);
  return w;
}

int WorldStream::shown() const {
  int n = 0;
  for (const auto& [c, ch] : chunks) n += ch.shown;
  return n;
}

}  // namespace loop
