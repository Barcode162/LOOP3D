#pragma once
// Wall-clock timing.
#include <chrono>

namespace loop {
class Stopwatch {
 public:
  Stopwatch() : start_(std::chrono::steady_clock::now()) {}
  void reset() { start_ = std::chrono::steady_clock::now(); }
  double seconds() const { return std::chrono::duration<double>(std::chrono::steady_clock::now() - start_).count(); }
  double ms() const { return seconds() * 1000.0; }
 private:
  std::chrono::steady_clock::time_point start_;
};
}  // namespace loop
