#pragma once
// Models from files: glTF 2.0 binary (.glb), read by our own code. Meshes
// (triangles, any node tree, its transforms baked in), metal/roughness
// materials (colour, metal, roughness, emission with
// KHR_materials_emissive_strength, alpha), and their pictures (JPEG and PNG,
// our own decoders). No skins or animations yet.
#include <cstdint>
#include <string>
#include <vector>

#include "engine/core/image.h"
#include "engine/core/math.h"
#include "engine/geometry/mesh.h"

namespace loop {

struct GltfMaterial {
  std::string name;
  Vec4 baseColor{1, 1, 1, 1};     // linear, alpha in w
  float metallic = 1, roughness = 1;
  Vec3 emissive{0};                // times emissiveStrength already
  int baseTexture = -1, metalRoughTexture = -1, normalTexture = -1, emissiveTexture = -1;  // GltfModel::textures
  enum class Alpha { kOpaque, kMask, kBlend } alpha = Alpha::kOpaque;
  float alphaCutoff = 0.5f;
  bool doubleSided = false;
  float roughnessMin = 0;          // not glTF's: a correction the scene may give
};

struct GltfModel {
  // One mesh per material (every primitive using it, merged), so a model is
  // a few draws however many nodes it had.
  struct Part {
    Mesh mesh;
    int material = -1;  // index into materials, -1 = glTF's default
  };
  std::vector<Part> parts;
  std::vector<GltfMaterial> materials;
  std::vector<Texture> textures;  // one per glTF texture, decoded
  AABB bounds() const;
};

// Reads a .glb from bytes. False with a plain reason.
bool loadGlb(const std::vector<uint8_t>& bytes, GltfModel& out, std::string* error = nullptr);
bool loadGlbFile(const std::string& path, GltfModel& out, std::string* error = nullptr);

}  // namespace loop
