#pragma once
// The meter: where each frame's time goes on the CPU, part by part (scripts,
// physics, people, sending changes to the GPU, preparing the drawing), so a
// slow frame says which part to fix instead of leaving it to guesses.
//
//   { MeterScope t(Part::kPhysics); world.step(dt); }   // adds to this frame
//   Meter::get().endFrame();                             // once a frame
//   Meter::get().ms(Part::kPhysics)                      // smoothed, ms a frame
//
// A part can be timed many times in one frame (the fixed clock may step
// twice); the frame's total is what counts. Readings are smoothed over about
// half a second, which also averages out a coarse browser clock (Safari's
// ticks are as long as a millisecond: a part that takes 0.3 ms reads 0 or 1
// each frame, and 0.3 on average).
#include <chrono>

namespace loop {

enum class Part { kScript, kPhysics, kPeople, kSend, kDraw, kCount };

class Meter {
 public:
  static constexpr int kParts = static_cast<int>(Part::kCount);
  static Meter& get();
  static double nowMs();                 // a steady clock, in milliseconds
  static const char* name(Part p);       // "scripts", "physics", "people", "sending", "drawing"

  void add(Part p, double ms) { frame_[static_cast<int>(p)] += ms; }
  // The frame is over: its times (and, when given, the whole frame's CPU time)
  // join the smoothed readings, then start again.
  void endFrame(double frameMs = -1);
  double ms(Part p) const { return smooth_[static_cast<int>(p)]; }  // smoothed, per frame
  double lastMs(Part p) const { return last_[static_cast<int>(p)]; } // the last whole frame's
  double totalMs() const;                // the parts together, smoothed
  double frameMs() const { return frameSmooth_; }  // the whole frame on the CPU, smoothed
  // Starts again (a new scene: its loading must not linger in the readings).
  void reset();

 private:
  double frame_[kParts] = {}, last_[kParts] = {}, smooth_[kParts] = {}, frameSmooth_ = 0;
  bool first_ = true;
};

// Times the code in its block and adds it to `part` for this frame.
class MeterScope {
 public:
  explicit MeterScope(Part p) : p_(p), t0_(Meter::nowMs()) {}
  ~MeterScope() { Meter::get().add(p_, Meter::nowMs() - t0_); }
  MeterScope(const MeterScope&) = delete;
  MeterScope& operator=(const MeterScope&) = delete;

 private:
  Part p_;
  double t0_;
};

}  // namespace loop
