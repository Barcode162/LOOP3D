#include "engine/render/score.h"

#include <algorithm>
#include <cmath>
#include <vector>

namespace loop {

namespace {

// Half as big: each pixel the average of four.
Image half(const Image& a) {
  Image h(std::max(a.width / 2, 1), std::max(a.height / 2, 1));
  for (int y = 0; y < h.height; ++y)
    for (int x = 0; x < h.width; ++x) {
      Vec3 s(0);
      for (int dy = 0; dy < 2; ++dy)
        for (int dx = 0; dx < 2; ++dx) s = s + a.at(std::min(2 * x + dx, a.width - 1), std::min(2 * y + dy, a.height - 1)).xyz();
      h.at(x, y) = Vec4(s * 0.25f, 1);
    }
  return h;
}

float luminance(Vec3 c) { return 0.2126f * c.x + 0.7152f * c.y + 0.0722f * c.z; }

}  // namespace

Vec3 linearSrgbToLab(Vec3 c) {
  // linear sRGB to XYZ, relative to the D65 white
  float X = (0.4124f * c.x + 0.3576f * c.y + 0.1805f * c.z) / 0.95047f;
  float Y = 0.2126f * c.x + 0.7152f * c.y + 0.0722f * c.z;
  float Z = (0.0193f * c.x + 0.1192f * c.y + 0.9505f * c.z) / 1.08883f;
  auto f = [](float t) { return t > 0.008856f ? std::cbrt(t) : 7.787f * t + 16.0f / 116.0f; };
  float fx = f(std::max(X, 0.0f)), fy = f(std::max(Y, 0.0f)), fz = f(std::max(Z, 0.0f));
  return {116 * fy - 16, 500 * (fx - fy), 200 * (fy - fz)};
}

float deltaE(Vec3 a, Vec3 b) { return length(a - b); }

PictureScore scorePictures(const Image& live, const Image& cinema) {
  PictureScore s;
  if (live.empty() || live.width != cinema.width || live.height != cinema.height) return s;
  Image l = half(live), c = half(cinema);
  double yl = 0, yc = 0;
  for (size_t i = 0; i < l.px.size(); ++i) yl += luminance(l.px[i].xyz()), yc += luminance(c.px[i].xyz());
  s.brightness = yc > 0 ? static_cast<float>(yl / yc) : 1.0f;
  const float gain = 1.0f / std::clamp(s.brightness, 0.125f, 8.0f);
  // pixels black in both (a film's bars) say nothing about the picture: left out
  std::vector<float> d;
  d.reserve(l.px.size());
  s.difference = Image(l.width, l.height);
  for (size_t i = 0; i < l.px.size(); ++i) {
    if (luminance(l.px[i].xyz()) < 1e-5f && luminance(c.px[i].xyz()) < 1e-5f) continue;
    float e = deltaE(linearSrgbToLab(l.px[i].xyz() * gain), linearSrgbToLab(c.px[i].xyz()));
    d.push_back(e);
    float g = std::min(e / 20.0f, 1.0f);
    s.difference.px[i] = Vec4(g * g, g * g, g * g, 1);  // linear: shown about as g after the sRGB curve
  }
  if (d.empty()) return s;
  double sum = 0;
  for (float e : d) sum += e;
  s.score = static_cast<float>(sum / static_cast<double>(d.size()));
  size_t k = d.size() * 9 / 10;
  std::nth_element(d.begin(), d.begin() + static_cast<long>(k), d.end());
  s.worstTenth = d[k];
  return s;
}

}  // namespace loop
