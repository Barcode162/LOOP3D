#include "engine/render/materials.h"

namespace loop {

namespace {
struct Entry {
  const char* name;
  void (*make)(Material&);
};

Vec3 srgb(float r, float g, float b) { return {srgbToLinear(r), srgbToLinear(g), srgbToLinear(b)}; }
void wood(Material& m, Vec3 light, Vec3 dark, Pattern::Kind k = Pattern::Kind::kWood) {
  m.baseColor = light, m.roughness = 0.55f;
  m.pattern.kind = k, m.pattern.colorB = dark, m.pattern.bump = 0.25f, m.pattern.roughnessVariation = 0.25f, m.pattern.scale = 1;
}
void metal(Material& m, Vec3 c, float rough) { m.baseColor = c, m.metallic = 1, m.roughness = rough; }

const Entry kLibrary[] = {
    // ---- woods
    {"oak", [](Material& m) { wood(m, srgb(0.70f, 0.52f, 0.33f), srgb(0.45f, 0.30f, 0.17f)); }},
    {"pine", [](Material& m) { wood(m, srgb(0.86f, 0.70f, 0.48f), srgb(0.66f, 0.45f, 0.25f)); }},
    {"walnut", [](Material& m) { wood(m, srgb(0.42f, 0.28f, 0.18f), srgb(0.22f, 0.13f, 0.08f)); }},
    {"cherry", [](Material& m) { wood(m, srgb(0.62f, 0.33f, 0.20f), srgb(0.40f, 0.18f, 0.10f)); }},
    {"floorboards", [](Material& m) { wood(m, srgb(0.66f, 0.47f, 0.30f), srgb(0.36f, 0.22f, 0.12f), Pattern::Kind::kPlanks); }},
    {"old wood", [](Material& m) { wood(m, srgb(0.48f, 0.42f, 0.34f), srgb(0.27f, 0.23f, 0.19f)); m.roughness = 0.85f; m.pattern.bump = 0.6f; }},
    {"bark", [](Material& m) {
       m.baseColor = srgb(0.36f, 0.27f, 0.20f), m.roughness = 0.95f;
       m.pattern.kind = Pattern::Kind::kNoise, m.pattern.colorB = srgb(0.18f, 0.13f, 0.10f), m.pattern.scale = 9, m.pattern.bump = 3, m.pattern.contrast = 2;
     }},
    // ---- stone and building
    {"marble", [](Material& m) {
       m.baseColor = srgb(0.93f, 0.92f, 0.90f), m.roughness = 0.12f, m.subsurface = 0.25f, m.subsurfaceColor = {1, 0.92f, 0.85f};
       m.pattern.kind = Pattern::Kind::kMarble, m.pattern.colorB = srgb(0.45f, 0.46f, 0.48f), m.pattern.scale = 2;
     }},
    {"granite", [](Material& m) {
       m.baseColor = srgb(0.62f, 0.60f, 0.58f), m.roughness = 0.4f;
       m.pattern.kind = Pattern::Kind::kNoise, m.pattern.colorB = srgb(0.25f, 0.24f, 0.24f), m.pattern.scale = 60, m.pattern.contrast = 3;
     }},
    {"stone", [](Material& m) {
       m.baseColor = srgb(0.55f, 0.53f, 0.50f), m.roughness = 0.85f;
       m.pattern.kind = Pattern::Kind::kNoise, m.pattern.colorB = srgb(0.38f, 0.37f, 0.35f), m.pattern.scale = 3, m.pattern.bump = 2.5f;
     }},
    {"brick", [](Material& m) {
       m.baseColor = srgb(0.60f, 0.30f, 0.22f), m.roughness = 0.9f;
       m.pattern.kind = Pattern::Kind::kBricks, m.pattern.colorB = srgb(0.70f, 0.68f, 0.64f), m.pattern.bump = 3;
     }},
    {"concrete", [](Material& m) {
       m.baseColor = srgb(0.62f, 0.62f, 0.60f), m.roughness = 0.9f;
       m.pattern.kind = Pattern::Kind::kNoise, m.pattern.colorB = srgb(0.52f, 0.52f, 0.50f), m.pattern.scale = 4, m.pattern.bump = 0.6f;
     }},
    {"plaster", [](Material& m) {
       m.baseColor = srgb(0.90f, 0.88f, 0.84f), m.roughness = 0.9f;
       m.pattern.kind = Pattern::Kind::kNoise, m.pattern.colorB = srgb(0.84f, 0.82f, 0.78f), m.pattern.scale = 3, m.pattern.bump = 0.3f;
     }},
    {"tiles", [](Material& m) {
       m.baseColor = srgb(0.92f, 0.92f, 0.90f), m.roughness = 0.15f, m.clearcoat = 0.4f;
       m.pattern.kind = Pattern::Kind::kTiles, m.pattern.colorB = srgb(0.55f, 0.54f, 0.52f), m.pattern.scale = 5, m.pattern.bump = 1;
     }},
    {"porcelain", [](Material& m) { m.baseColor = srgb(0.95f, 0.95f, 0.94f), m.roughness = 0.05f, m.clearcoat = 1, m.subsurface = 0.2f; }},
    {"clay", [](Material& m) { m.baseColor = srgb(0.70f, 0.42f, 0.30f), m.roughness = 0.8f; }},
    // ---- metals (measured reflectance, linear)
    {"gold", [](Material& m) { metal(m, {1.0f, 0.766f, 0.336f}, 0.18f); }},
    {"silver", [](Material& m) { metal(m, {0.972f, 0.960f, 0.915f}, 0.12f); }},
    {"copper", [](Material& m) { metal(m, {0.955f, 0.637f, 0.538f}, 0.25f); }},
    {"brass", [](Material& m) { metal(m, {0.910f, 0.778f, 0.423f}, 0.3f); }},
    {"bronze", [](Material& m) { metal(m, {0.80f, 0.55f, 0.35f}, 0.35f); }},
    {"iron", [](Material& m) { metal(m, {0.560f, 0.570f, 0.580f}, 0.5f); }},
    {"steel", [](Material& m) { metal(m, {0.63f, 0.62f, 0.60f}, 0.3f); }},
    {"chrome", [](Material& m) { metal(m, {0.55f, 0.556f, 0.554f}, 0.03f); }},
    {"aluminium", [](Material& m) { metal(m, {0.913f, 0.922f, 0.924f}, 0.35f); }},
    {"rust", [](Material& m) {
       m.baseColor = srgb(0.45f, 0.20f, 0.10f), m.roughness = 0.9f;
       m.pattern.kind = Pattern::Kind::kNoise, m.pattern.colorB = srgb(0.25f, 0.12f, 0.07f), m.pattern.scale = 8, m.pattern.bump = 1.5f;
     }},
    // ---- glass, water, ice
    {"glass", [](Material& m) { m.baseColor = Vec3(1), m.transmission = 1, m.roughness = 0, m.ior = 1.52f, m.dispersion = 0.3f; }},
    {"crystal", [](Material& m) { m.baseColor = Vec3(1), m.transmission = 1, m.roughness = 0, m.ior = 1.6f, m.dispersion = 1; }},
    {"frosted glass", [](Material& m) { m.baseColor = Vec3(1), m.transmission = 1, m.roughness = 0.3f, m.ior = 1.5f; }},
    {"green glass", [](Material& m) { m.baseColor = srgb(0.55f, 0.85f, 0.55f), m.transmission = 1, m.roughness = 0, m.ior = 1.52f; }},
    {"water", [](Material& m) { m.baseColor = srgb(0.85f, 0.95f, 0.95f), m.transmission = 1, m.roughness = 0.02f, m.ior = 1.333f; }},
    {"ice", [](Material& m) { m.baseColor = srgb(0.9f, 0.96f, 1.0f), m.transmission = 0.9f, m.roughness = 0.1f, m.ior = 1.31f, m.subsurface = 0.2f; }},
    // ---- soft things
    {"fabric", [](Material& m) { m.baseColor = srgb(0.55f, 0.55f, 0.58f), m.roughness = 0.9f, m.sheen = 0.5f; }},
    {"velvet", [](Material& m) { m.baseColor = srgb(0.30f, 0.05f, 0.08f), m.roughness = 0.8f, m.sheen = 1; }},
    {"wool", [](Material& m) { m.baseColor = srgb(0.80f, 0.76f, 0.68f), m.roughness = 1, m.sheen = 0.6f; }},
    {"leather", [](Material& m) { m.baseColor = srgb(0.38f, 0.22f, 0.13f), m.roughness = 0.5f, m.clearcoat = 0.2f, m.clearcoatRoughness = 0.3f; }},
    {"rubber", [](Material& m) { m.baseColor = srgb(0.08f, 0.08f, 0.08f), m.roughness = 0.75f, m.specular = 0.3f; }},
    {"plastic", [](Material& m) { m.baseColor = srgb(0.8f, 0.8f, 0.8f), m.roughness = 0.35f; }},
    {"car paint", [](Material& m) { m.baseColor = srgb(0.55f, 0.05f, 0.05f), m.roughness = 0.4f, m.clearcoat = 1, m.clearcoatRoughness = 0.02f; }},
    {"paper", [](Material& m) { m.baseColor = srgb(0.92f, 0.90f, 0.85f), m.roughness = 0.9f, m.translucency = 0.3f; }},
    {"wax", [](Material& m) { m.baseColor = srgb(0.96f, 0.92f, 0.82f), m.roughness = 0.45f, m.subsurface = 0.8f, m.subsurfaceColor = {1, 0.75f, 0.45f}; }},
    {"skin", [](Material& m) { m.baseColor = srgb(0.80f, 0.60f, 0.50f), m.roughness = 0.45f, m.subsurface = 0.6f, m.subsurfaceColor = {1, 0.35f, 0.25f}; }},
    {"hair", [](Material& m) { m.baseColor = srgb(0.25f, 0.15f, 0.08f), m.roughness = 0.35f, m.sheen = 0.5f; }},
    // ---- nature
    {"leaves", [](Material& m) { m.baseColor = srgb(0.25f, 0.40f, 0.12f), m.roughness = 0.6f, m.translucency = 0.45f, m.leafShape = true; }},
    {"needles", [](Material& m) { m.baseColor = srgb(0.14f, 0.26f, 0.12f), m.roughness = 0.7f, m.translucency = 0.35f, m.leafShape = true; }},
    {"autumn leaves", [](Material& m) { m.baseColor = srgb(0.75f, 0.35f, 0.08f), m.roughness = 0.6f, m.translucency = 0.5f, m.leafShape = true; }},
    {"grass", [](Material& m) {
       m.baseColor = srgb(0.30f, 0.42f, 0.14f), m.roughness = 0.85f;
       m.pattern.kind = Pattern::Kind::kGround, m.pattern.colorB = srgb(0.45f, 0.38f, 0.28f), m.pattern.bump = 1.5f;
     }},
    {"soil", [](Material& m) {
       m.baseColor = srgb(0.36f, 0.27f, 0.19f), m.roughness = 0.95f;
       m.pattern.kind = Pattern::Kind::kNoise, m.pattern.colorB = srgb(0.25f, 0.19f, 0.14f), m.pattern.scale = 5, m.pattern.bump = 2;
     }},
    {"sand", [](Material& m) {
       m.baseColor = srgb(0.84f, 0.74f, 0.56f), m.roughness = 0.9f;
       m.pattern.kind = Pattern::Kind::kNoise, m.pattern.colorB = srgb(0.76f, 0.66f, 0.50f), m.pattern.scale = 30, m.pattern.bump = 0.5f;
     }},
    {"snow", [](Material& m) { m.baseColor = srgb(0.95f, 0.96f, 0.98f), m.roughness = 0.6f, m.subsurface = 0.5f, m.subsurfaceColor = {0.7f, 0.85f, 1}; }},
    {"moss", [](Material& m) {
       m.baseColor = srgb(0.30f, 0.38f, 0.12f), m.roughness = 1, m.sheen = 0.4f;
       m.pattern.kind = Pattern::Kind::kNoise, m.pattern.colorB = srgb(0.20f, 0.27f, 0.08f), m.pattern.scale = 10, m.pattern.bump = 1;
     }},
    // ---- light givers (nits)
    {"flame", [](Material& m) { m.baseColor = Vec3(0), m.emission = kelvin(1850) * 30000.0f; }},
    {"ember", [](Material& m) { m.baseColor = srgb(0.1f, 0.03f, 0.02f), m.roughness = 0.9f, m.emission = kelvin(1200) * 800.0f; }},
    {"neon", [](Material& m) { m.baseColor = Vec3(0), m.emission = Vec3(1, 0.2f, 0.6f) * 2500.0f; }},
    {"screen", [](Material& m) { m.baseColor = Vec3(0.02f), m.roughness = 0.1f, m.emission = kelvin(6500) * 300.0f; }},
    {"lamp shade", [](Material& m) { m.baseColor = srgb(0.95f, 0.88f, 0.75f), m.roughness = 0.9f, m.translucency = 0.6f; }},
    // ---- plain colours
    {"white", [](Material& m) { m.baseColor = srgb(0.9f, 0.9f, 0.9f), m.roughness = 0.6f; }},
    {"black", [](Material& m) { m.baseColor = srgb(0.04f, 0.04f, 0.04f), m.roughness = 0.6f; }},
    {"grey", [](Material& m) { m.baseColor = srgb(0.5f, 0.5f, 0.5f), m.roughness = 0.6f; }},
};
}  // namespace

bool materialByName(const std::string& name, Material& out) {
  for (const Entry& e : kLibrary)
    if (name == e.name) {
      out = Material();
      out.name = e.name;
      e.make(out);
      return true;
    }
  return false;
}

std::vector<std::string> materialNames() {
  std::vector<std::string> n;
  for (const Entry& e : kLibrary) n.push_back(e.name);
  return n;
}

}  // namespace loop
