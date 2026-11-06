#include "engine/core/meter.h"

namespace loop {

Meter& Meter::get() {
  static Meter m;
  return m;
}

double Meter::nowMs() {
  using namespace std::chrono;
  return duration<double, std::milli>(steady_clock::now().time_since_epoch()).count();
}

const char* Meter::name(Part p) {
  switch (p) {
    case Part::kScript: return "scripts";
    case Part::kPhysics: return "physics";
    case Part::kPeople: return "people";
    case Part::kSend: return "sending";
    case Part::kDraw: return "drawing";
    default: return "?";
  }
}

void Meter::endFrame(double frameMs) {
  // about half a second at 60 frames a second (a 1/30 step a frame)
  const double k = first_ ? 1.0 : 1.0 / 30;
  if (frameMs >= 0) frameSmooth_ += (frameMs - frameSmooth_) * k;
  for (int i = 0; i < kParts; ++i) {
    last_[i] = frame_[i];
    smooth_[i] += (frame_[i] - smooth_[i]) * k;
    frame_[i] = 0;
  }
  first_ = false;
}

double Meter::totalMs() const {
  double t = 0;
  for (double v : smooth_) t += v;
  return t;
}

void Meter::reset() {
  for (int i = 0; i < kParts; ++i) frame_[i] = last_[i] = smooth_[i] = 0;
  frameSmooth_ = 0;
  first_ = true;
}

}  // namespace loop
