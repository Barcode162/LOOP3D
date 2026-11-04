#include "engine/render/shots.h"

#include "engine/geometry/noise.h"

namespace loop {

float Film::length() const {
  float s = 0;
  for (const Shot& sh : shots) s += sh.duration;
  return s;
}

Camera Film::at(float t, const Camera& base, int* shotIndex, float* fade) const {
  Camera c = base;
  if (shots.empty()) return c;
  float start = 0;
  size_t i = 0;
  for (; i + 1 < shots.size() && t >= start + shots[i].duration; ++i) start += shots[i].duration;
  const Shot& s = shots[i];
  float local = clamp(t - start, 0.0f, s.duration);
  float u = static_cast<float>(ease(s.ease, s.duration > 0 ? local / s.duration : 1));
  Framing f;
  const Framing& a = s.from;
  const Framing& b = s.moves ? s.to : s.from;
  f.focalLength = lerp(a.focalLength, b.focalLength, u);
  f.fStop = lerp(a.fStop, b.fStop, u);
  f.focus = lerp(a.focus, b.focus, u);
  f.target = vlerp(a.target, b.target, u);
  if (s.orbit) {
    float ang = lerp(s.angleFrom, s.angleTo, u);
    f.position = s.center + Vec3(std::cos(ang) * s.radius, s.height, std::sin(ang) * s.radius);
    if (!s.moves) f.target = s.center + Vec3(0, s.height * 0.5f, 0);
  } else f.position = vlerp(a.position, b.position, u);
  if (s.shake > 0) {
    // handheld: slow sway plus a little faster jitter, the same every playback
    float k = s.shake;
    Vec3 sway{noise::perlin(Vec3(t * 0.7f, 1.3f, 0)), noise::perlin(Vec3(t * 0.6f, 4.1f, 0)), noise::perlin(Vec3(t * 0.5f, 7.7f, 0))};
    Vec3 jit{noise::perlin(Vec3(t * 4.0f, 2.2f, 1)), noise::perlin(Vec3(t * 4.3f, 5.5f, 1)), 0};
    f.position += (sway * 0.04f + jit * 0.006f) * k;
    f.target += (sway * 0.06f + jit * 0.012f) * k;
  }
  c.position = f.position;
  c.target = f.target;
  c.focalLength = f.focalLength;
  c.fStop = f.fStop;
  c.focusDistance = f.focus;
  float fd = 1;
  if (s.fadeIn > 0 && local < s.fadeIn) fd = std::min(fd, local / s.fadeIn);
  if (s.fadeOut > 0 && local > s.duration - s.fadeOut) fd = std::min(fd, (s.duration - local) / s.fadeOut);
  if (shotIndex) *shotIndex = static_cast<int>(i);
  if (fade) *fade = static_cast<float>(ease(Ease::kSine, fd));
  return c;
}

}  // namespace loop
