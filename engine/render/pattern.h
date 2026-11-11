#pragma once
// What a material looks like at one point: its colour, roughness and bumped
// normal after its pattern is applied. Shared by the path tracer and the
// texture baker for the GPU.
#include "engine/render/scene.h"

namespace loop {

struct Surface {
  Vec3 base{0.8f};
  float roughness = 0.5f, metallic = 0;
  float alpha = 1;
  Vec3 normal{0, 1, 0};  // shading normal after bumps
  Vec3 glow{1};          // times the material's glow here (a `windows` pattern: lit or dark per window)
};

// `p` world position, `n` shading normal, `uv` mesh coordinates.
// `textures`: the scene's model pictures (may be null).
Surface shadeSurface(const Material& m, const std::vector<Image>& images, Vec3 p, Vec3 n, Vec2 uv, const std::vector<Texture>* textures = nullptr);
// The pattern's mix value (0 = baseColor, 1 = colorB) and height (0..1).
void patternValue(const Pattern& pt, const std::vector<Image>& images, Vec3 p, Vec3 n, Vec2 uv, float& mix, float& height, Vec3& tintOut, Vec3& glowOut);
// The share of a `windows` face's glow that is left on average, for picking light samples.
float windowsMeanGlow(const Pattern& pt);

}  // namespace loop
