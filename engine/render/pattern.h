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

// What a surface recipe needs about a point beyond what a flat material does
// (docs/SURFACES.md section 7): where it is and which way it faces in the
// recipe's own space (the thing's as it was made, or the world's), the way back
// to the world for the bumped facing, the two marks blended across the
// triangle, and how far away it is.
struct SurfaceContext {
  const SurfaceRecipe* recipe = nullptr;
  Vec3 place{0}, facing{0, 1, 0};
  Mat3 toWorld;            // a facing in the recipe's space to the world's (normals)
  float edge = 0, hollow = 0, distance = 0;
};

// `p` world position, `n` shading normal, `uv` mesh coordinates.
// `textures`: the scene's model pictures (may be null).
// `surface`: for a material made by `surface(...)`, where the point is on its
// thing; without it such a material is its base, flat.
Surface shadeSurface(const Material& m, const std::vector<Image>& images, Vec3 p, Vec3 n, Vec2 uv, const std::vector<Texture>* textures = nullptr,
                     const SurfaceContext* surface = nullptr);
// The pattern's mix value (0 = baseColor, 1 = colorB) and height (0..1).
void patternValue(const Pattern& pt, const std::vector<Image>& images, Vec3 p, Vec3 n, Vec2 uv, float& mix, float& height, Vec3& tintOut, Vec3& glowOut);
// The share of a `windows` face's glow that is left on average, for picking light samples.
float windowsMeanGlow(const Pattern& pt);

}  // namespace loop
