#pragma once
// The change tracker (the picture pipeline's phase 3, docs/PIPELINE.md):
// version numbers on what a frame is made from, so work happens only where
// something changed. Whoever changes a thing bumps its number; whoever makes
// something from things (a shadow map from the placements, the sun and the
// view) remembers the numbers it saw and makes it again only when one of
// them moved on. A value that is worked out each frame (where the sun's view
// is) is told to the tracker with `set`: it bumps only when the value differs.
// It knows nothing of the graphics chip, so it is tested on its own
// (tests/change_tracker_test.cpp).
#include <cstdint>
#include <cstring>
#include <string>
#include <vector>

namespace loop {

class ChangeTracker {
 public:
  // Something changed.
  void bump(const std::string& thing) { ++entry(thing).version; }
  // A value worked out again: bumps the thing only when its bytes differ.
  template <class T>
  void set(const std::string& thing, const T& value) {
    Entry& e = entry(thing);
    std::vector<unsigned char> bytes(sizeof(T));
    std::memcpy(bytes.data(), &value, sizeof(T));
    if (bytes != e.value) e.value = std::move(bytes), ++e.version;
  }
  // Every thing's number added up: the same as before only if nothing at all was bumped.
  uint64_t sum() const {
    uint64_t s = 0;
    for (const Entry& e : entries_) s += e.version;
    return s;
  }
  uint64_t version(const std::string& thing) const {
    for (const Entry& e : entries_)
      if (e.name == thing) return e.version;
    return 0;
  }

  // Something made from things: stale until made, then again whenever one of
  // them moves on (or what it is made from is a different list).
  struct Made {
    std::vector<std::string> from;
    std::vector<uint64_t> versions;
    bool valid = false;
  };
  bool stale(const Made& m, const std::vector<std::string>& from) const {
    if (!m.valid || m.from != from) return true;
    for (size_t i = 0; i < from.size(); ++i)
      if (version(from[i]) != m.versions[i]) return true;
    return false;
  }
  void made(Made& m, const std::vector<std::string>& from) const {
    m.from = from, m.versions.clear(), m.valid = true;
    for (const std::string& f : from) m.versions.push_back(version(f));
  }

 private:
  struct Entry {
    std::string name;
    uint64_t version = 1;  // anything made before the first bump is stale anyway (valid = false)
    std::vector<unsigned char> value;
  };
  Entry& entry(const std::string& thing) {
    for (Entry& e : entries_)
      if (e.name == thing) return e;
    entries_.push_back({thing, 1, {}});
    return entries_.back();
  }
  std::vector<Entry> entries_;
};

}  // namespace loop
