#pragma once
// A clip: movement written down. A clip holds channels; a channel is one
// value that changes (where a thing is, its turn, a lamp's brightness, a
// bone's bend) and holds keys; a key pins that value to a moment and says
// how the value travels to it from the key before:
//   smooth  through the keys with no overshoot (the usual way)
//   line    straight there
//   hold    stays where it was, then jumps
//   an ease by name (in, out, in_out, back, bounce, elastic ...)
// A key may carry its own slopes ("free"), and then it may overshoot.
// The engine can ask for any moment, between keys too. One format for
// everything that moves by keys: a box, a lamp, a body (docs/STUDIO.md).
#include <cstdint>
#include <deque>
#include <string>
#include <vector>

#include "engine/anim/anim.h"
#include "engine/core/ease.h"
#include "engine/core/json.h"
#include "engine/core/math.h"

namespace loop::anim {

enum class Way : uint8_t { kSmooth, kLine, kHold, kEase };

struct Key {
  double time = 0;            // seconds from the clip's start
  float v[4] = {0, 0, 0, 0};  // the value (a channel uses its first `size` numbers)
  Way way = Way::kSmooth;     // how the value arrives here from the key before
  Ease ease = Ease::kInOut;   // which ease, when way is kEase
  bool free = false;          // slopes given by hand (else worked out so nothing overshoots)
  float in[4] = {0, 0, 0, 0}, out[4] = {0, 0, 0, 0};  // slopes arriving and leaving (value a second), when free
};

struct Channel {
  std::string name;
  int size = 1;        // numbers in each key: 1, 3 or 4
  bool angle = false;  // its numbers are angles (written in degrees)
  std::vector<Key> keys;  // in order of time

  // The key at this moment (within `snap` seconds), or -1.
  int find(double time, double snap = 1e-4) const;
  // Puts a key at this moment (a key already there takes the new value and keeps its way).
  Key& set(double time, const float* v, double snap = 1e-4);
  bool remove(double time, double snap = 1e-4);
  // The value at `t`. `cyclic`: the keys repeat every `period` seconds, and
  // the last meets the first with no jolt (a key at the very end closes the loop).
  bool sample(double t, float* out, bool cyclic = false, double period = 0) const;
  // The slope at key `i` leaving (or arriving), number `c`.
  float slope(int i, int c, bool leaving, bool cyclic = false, double period = 0) const;
};

enum class Repeat : uint8_t { kOnce, kLoop, kBackAndForth };

struct Clip {
  std::string name;
  std::string skeleton;  // the skeleton it was made for ("" = a thing's own values)
  double rate = 30;      // frames a second: where the editor puts keys
  double length = 0;     // seconds; 0 = up to its last key
  Repeat repeat = Repeat::kOnce;
  std::deque<Channel> channels;  // a deque: adding a channel never moves the others

  Channel* find(const std::string& name);
  const Channel* find(const std::string& name) const;
  Channel& channel(const std::string& name, int size, bool angle = false);  // found, or added
  double duration() const;  // its length, or the moment of its last key
  // A moment moved onto the nearest frame when it is within `within` seconds of one.
  double snapped(double time, double within = 6e-4) const;
  // The clip's own moment for a moment since it started (once: stops at the
  // end; loop: round again; back and forth: there and back).
  double wrap(double t) const;
  // A channel's value `t` seconds after the clip started.
  bool sample(const Channel& c, double t, float* out) const;
  // The pose of a skeleton: each channel named as a bone turns it from its
  // rest by (bend, side, twist); "<bone>_at" moves it from its rest place.
  Pose pose(const Skeleton& s, double t) const;
  // The clip as Looped Script: a `clip` line and its `key` lines under it.
  // `target` is what follows the word clip (a thing's name, or the clip's own
  // name in quotes); `indent` goes before every line.
  // `more` is added to the clip line's settings (", play: no").
  std::string script(const std::string& target, const std::string& indent = "", const std::string& more = "") const;
};

// A bone's turn from its rest as three angles a person can read (radians):
// bend about the bone's X, side about its Z, twist about its length (Y).
// The swing (bend and side together) is one turn about an axis lying across
// the bone, so the three never lock up short of a half turn of swing.
Quat turnFromBendSideTwist(Vec3 bst);
// The other way. Of the twists that give the same turn, the one nearest
// `nearTwist` (so a recording's twist never jumps a full turn).
Vec3 bendSideTwistFromTurn(Quat q, float nearTwist = 0);

// How a key arrives, by the name scripts and the editor use: "smooth", "line",
// "hold", or an ease's name ("in", "out", "back", "bounce" ...). False for a
// name that is none of them.
bool wayByName(const std::string& name, Way& way, Ease& ease);
std::string wayName(Way way, Ease ease);

// A clip as data for the editor, and back: {"name", "for", "rate", "length"
// (0: to its last key), "duration", "loop": "once" | "loop" | "back and
// forth", "channels": [{"name", "size", "angle", "keys": [{"t", "v": [..],
// "way", "in": [..], "out": [..]}]}]}. Reading takes what is there and
// leaves the rest as it was; false with the reason when something is wrong.
Json clipJson(const Clip& clip);
bool clipFromJson(const Json& j, Clip& clip, std::string* why = nullptr);

// A number as a person would write it: at most `decimals`, no "-0", no "1.500".
std::string plainNumber(double n, int decimals = 3);

}  // namespace loop::anim
