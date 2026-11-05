#include "engine/render/scene.h"

namespace loop {

float Camera::fovY() const { return 2 * std::atan(sensor.y / (2 * focalLength)); }

float Camera::ev100() const {
  // EV100 = log2(N² / t · 100 / S)
  return std::log2(fStop * fStop / std::max(shutter, 1e-6f) * 100.0f / std::max(iso, 1.0f));
}

Vec3 Scene::sunDirection() const {
  for (const Light& l : lights)
    if (l.kind == Light::Kind::kSun) return normalize(-l.direction);
  return {0, 1, 0};
}

int Scene::findMaterial(const std::string& name) const {
  for (size_t i = 0; i < materials.size(); ++i)
    if (materials[i].name == name) return static_cast<int>(i);
  return -1;
}

Vec3 kelvin(float k) {
  // Planckian locus (Kim et al. cubic fit) → CIE xy → XYZ → linear sRGB.
  double t = clamp(static_cast<double>(k), 1000.0, 40000.0), t2 = t * t, t3 = t2 * t;
  double x = t <= 4000 ? -0.2661239e9 / t3 - 0.2343589e6 / t2 + 0.8776956e3 / t + 0.179910
                       : -3.0258469e9 / t3 + 2.1070379e6 / t2 + 0.2226347e3 / t + 0.240390;
  double x2 = x * x, x3 = x2 * x;
  double y = t <= 2222   ? -1.1063814 * x3 - 1.34811020 * x2 + 2.18555832 * x - 0.20219683
             : t <= 4000 ? -0.9549476 * x3 - 1.37418593 * x2 + 2.09137015 * x - 0.16748867
                         : 3.0817580 * x3 - 5.87338670 * x2 + 3.75112997 * x - 0.37001483;
  double X = x / y, Z = (1 - x - y) / y;
  Vec3 c{static_cast<float>(3.2404542 * X - 1.5371385 - 0.4985314 * Z), static_cast<float>(-0.9692660 * X + 1.8760108 + 0.0415560 * Z),
         static_cast<float>(0.0556434 * X - 0.2040259 + 1.0572252 * Z)};
  c = vmax(c, Vec3(0));
  float l = luminance(c);
  return l > 0 ? c / l : Vec3(1);
}

// ------------------------------------------------------------------ looks
std::vector<std::string> lookNames() {
  return {"natural", "blockbuster", "noir", "golden", "bleach", "seventies", "neon", "dream", "documentary", "horror", "arctic", "western"};
}

bool lookByName(const std::string& name, Look& o) {
  o = Look();
  o.name = name;
  if (name == "natural") return true;
  if (name == "blockbuster") {  // teal shadows, warm skin, punchy, wide bars
    o.contrast = 1.15f, o.saturation = 1.1f;
    o.shadowTint = {-0.02f, 0.01f, 0.035f}, o.highlightTint = {0.03f, 0.012f, -0.02f};
    o.bloom = 0.06f, o.vignette = 0.35f, o.grain = 0.015f, o.letterbox = 2.39f, o.chromaticAberration = 0.8f;
    return true;
  }
  if (name == "noir") {
    o.monochrome = true, o.contrast = 1.45f, o.lift = Vec3(-0.02f), o.gain = Vec3(1.1f);
    o.vignette = 0.6f, o.grain = 0.05f, o.grainSize = 1.4f, o.bloom = 0.05f, o.halation = 0.1f;
    return true;
  }
  if (name == "golden") {  // late sun, honey highlights
    o.temperature = 0.35f, o.saturation = 1.1f, o.contrast = 1.05f;
    o.highlightTint = {0.04f, 0.02f, -0.03f}, o.bloom = 0.09f, o.halation = 0.15f, o.vignette = 0.3f;
    return true;
  }
  if (name == "bleach") {  // bleach bypass: silver, hard, low colour
    o.saturation = 0.45f, o.contrast = 1.4f, o.gain = Vec3(1.05f), o.grain = 0.03f, o.vignette = 0.4f;
    return true;
  }
  if (name == "seventies") {  // faded film stock
    o.tonemap = Look::Tonemap::kFilmic;
    o.temperature = 0.25f, o.tint = 0.08f, o.saturation = 0.85f, o.contrast = 0.9f, o.blackLevel = 0.045f;
    o.shadowTint = {0.01f, 0.02f, 0.0f}, o.highlightTint = {0.03f, 0.015f, -0.02f};
    o.grain = 0.06f, o.grainSize = 1.6f, o.halation = 0.35f, o.bloom = 0.07f, o.vignette = 0.45f, o.chromaticAberration = 1.2f;
    return true;
  }
  if (name == "neon") {  // cyberpunk night: magenta and cyan, glow
    o.saturation = 1.3f, o.contrast = 1.15f, o.tint = 0.15f, o.temperature = -0.2f;
    o.shadowTint = {0.0f, 0.02f, 0.05f}, o.highlightTint = {0.05f, -0.01f, 0.04f};
    o.bloom = 0.16f, o.bloomRadius = 0.035f, o.chromaticAberration = 2.0f, o.vignette = 0.4f, o.grain = 0.02f;
    return true;
  }
  if (name == "dream") {
    o.contrast = 0.85f, o.saturation = 0.9f, o.lift = Vec3(0.03f), o.bloom = 0.28f, o.bloomRadius = 0.05f;
    o.temperature = 0.1f, o.tint = 0.06f, o.vignette = 0.3f;
    return true;
  }
  if (name == "documentary") {
    o.tonemap = Look::Tonemap::kAces, o.saturation = 0.95f, o.grain = 0.02f, o.vignette = 0.15f, o.sharpen = 0.3f;
    return true;
  }
  if (name == "horror") {
    o.saturation = 0.55f, o.contrast = 1.3f, o.temperature = -0.15f, o.tint = -0.18f;
    o.shadowTint = {-0.01f, 0.02f, 0.01f}, o.lift = Vec3(-0.015f), o.vignette = 0.7f, o.grain = 0.045f;
    return true;
  }
  if (name == "arctic") {
    o.temperature = -0.4f, o.saturation = 0.8f, o.contrast = 1.05f, o.gain = Vec3(1.05f, 1.07f, 1.1f), o.bloom = 0.06f;
    return true;
  }
  if (name == "western") {
    o.temperature = 0.45f, o.saturation = 0.75f, o.contrast = 1.25f, o.highlightTint = {0.05f, 0.03f, -0.03f};
    o.grain = 0.035f, o.vignette = 0.5f, o.letterbox = 2.39f;
    return true;
  }
  return false;
}

bool sensorByName(const std::string& name, Vec2& out) {
  if (name == "ff" || name == "full frame") out = {36, 24};
  else if (name == "s35" || name == "super 35") out = {24.89f, 18.66f};
  else if (name == "apsc" || name == "aps-c") out = {23.6f, 15.6f};
  else if (name == "m43" || name == "micro 4/3") out = {17.3f, 13};
  else if (name == "imax" || name == "imax 65") out = {70.41f, 52.63f};
  else return false;
  return true;
}

}  // namespace loop
