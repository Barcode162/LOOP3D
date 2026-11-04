#pragma once
// Easing curves: t in 0..1 → 0..1. Everything that moves in Loop3D eases
// (cameras, things, sounds fading) so nothing snaps or stops dead.
#include <cmath>
#include <string>

namespace loop {

enum class Ease { kLinear, kIn, kOut, kInOut, kSmoother, kSine, kBack, kElastic, kBounce, kSpring };

inline double ease(Ease e, double t) {
  t = t < 0 ? 0 : t > 1 ? 1 : t;
  const double c1 = 1.70158, c3 = c1 + 1, pi = 3.14159265358979323846;
  switch (e) {
    case Ease::kLinear: return t;
    case Ease::kIn: return t * t * t;
    case Ease::kOut: return 1 - std::pow(1 - t, 3);
    case Ease::kInOut: return t < 0.5 ? 4 * t * t * t : 1 - std::pow(-2 * t + 2, 3) / 2;
    case Ease::kSmoother: return t * t * t * (t * (t * 6 - 15) + 10);
    case Ease::kSine: return -(std::cos(pi * t) - 1) / 2;
    case Ease::kBack: return 1 + c3 * std::pow(t - 1, 3) + c1 * std::pow(t - 1, 2);
    case Ease::kElastic: return t == 0 || t == 1 ? t : std::pow(2, -10 * t) * std::sin((t * 10 - 0.75) * (2 * pi / 3)) + 1;
    case Ease::kBounce: {
      const double n1 = 7.5625, d1 = 2.75;
      if (t < 1 / d1) return n1 * t * t;
      if (t < 2 / d1) return t -= 1.5 / d1, n1 * t * t + 0.75;
      if (t < 2.5 / d1) return t -= 2.25 / d1, n1 * t * t + 0.9375;
      return t -= 2.625 / d1, n1 * t * t + 0.984375;
    }
    case Ease::kSpring: {
      // critically damped spring, rescaled to land exactly on 1
      const double k = 8, end = 1 - (1 + k) * std::exp(-k);
      return (1 - (1 + k * t) * std::exp(-k * t)) / end;
    }
  }
  return t;
}

inline bool easeByName(const std::string& n, Ease& out) {
  static const struct { const char* name; Ease e; } names[] = {
      {"linear", Ease::kLinear}, {"in", Ease::kIn}, {"ease_in", Ease::kIn}, {"out", Ease::kOut}, {"ease_out", Ease::kOut},
      {"in_out", Ease::kInOut}, {"ease_in_out", Ease::kInOut}, {"smooth", Ease::kInOut}, {"smoother", Ease::kSmoother},
      {"sine", Ease::kSine}, {"back", Ease::kBack}, {"elastic", Ease::kElastic}, {"bounce", Ease::kBounce}, {"spring", Ease::kSpring}};
  for (auto& x : names)
    if (n == x.name) { out = x.e; return true; }
  return false;
}

}  // namespace loop
