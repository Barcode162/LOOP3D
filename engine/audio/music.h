#pragma once
// Music, made by the engine itself: instruments built from oscillators, noise,
// envelopes and filters (no recordings, so nothing to license), and a step
// sequencer that plays patterns in time with the world.
//
//   tempo 100
//   track "kick", "x...x...x...x..."           # x a hit, X a loud one, . nothing
//   track "bass", "C2 . . . G1 - - - A1 . . . F1 - . ."  # notes; - holds; . rests
//
// Each step is a sixteenth note (4 steps a beat). A track loops over its own
// length, so a 16-step drum line and a 32-step melody keep in step.
#include <cstdint>
#include <functional>
#include <memory>
#include <string>
#include <vector>

#include "engine/audio/audio.h"

namespace loop::audio {

// The instruments by name. Drums (no pitch): kick, snare, hat, open_hat, clap,
// tom. With notes: bass, lead, pad, pluck, keys, bell.
std::vector<std::string> instrumentNames();
bool isDrum(const std::string& instrument);
bool isInstrument(const std::string& instrument);
// One note: `midi` 60 is middle C (ignored by drums), `seconds` how long it's
// held, `velocity` 0..1.
std::unique_ptr<Voice> makeNote(const std::string& instrument, float midi, float seconds, float velocity, uint32_t seed);
// "C4" → 60, "A4" → 69, "Eb2" → 39, "F#3" → 54; -1 if it isn't a note.
int noteNumber(const std::string& name);

struct Track {
  std::string instrument;
  struct Step {
    bool on = false;
    int midi = 60;
    int length = 1;        // in steps (held with -)
    float velocity = 0.8f;
  };
  std::vector<Step> steps;
  float volume = 1;
};
// A pattern's text as steps: drums "x.X." (x hit, X accent); notes "C2 . - G1"
// (a note, . rest, - hold the note before). False with a reason if it can't be read.
bool parseTrack(const std::string& instrument, const std::string& pattern, Track& out, std::string* error = nullptr);

class Sequencer {
 public:
  float tempo = 100;  // beats a minute; 4 steps a beat
  float swing = 0;    // 0..0.5: every second step comes late, for a shuffle
  std::vector<Track> tracks;
  // Moves time on and calls `play` for every note that starts (instrument,
  // midi, seconds held, velocity × the track's volume).
  void advance(double dt, const std::function<void(const Track&, const Track::Step&, float seconds)>& play);
  // The step playing now (0, 1, 2 …), counted from the start; -1 before any.
  long long step() const { return step_; }
  double time() const { return time_; }
  void restart() { time_ = 0, step_ = -1; }

 private:
  double time_ = 0;
  long long step_ = -1;
};

}  // namespace loop::audio
