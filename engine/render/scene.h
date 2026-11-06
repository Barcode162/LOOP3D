#pragma once
// What a picture is made of: meshes with materials, lights in real units,
// a sky, fog, a physical camera and a film look. The same scene feeds the
// cinematic path tracer (render/pathtracer.h) and the live GPU renderer
// (gpu/), so a shot looks the same in both, only cleaner offline.
//
// Units: metres, seconds; lamps in lumens and kelvin; the sun in lux;
// glowing surfaces in nits (cd/m²); the camera in mm, f-stops, seconds, ISO.
#include <array>
#include <cstdint>
#include <string>
#include <vector>

#include "engine/core/image.h"
#include "engine/core/math.h"
#include "engine/geometry/mesh.h"

namespace loop {

// A pattern computed at the hit point (no image files needed): colour,
// roughness and bumps that vary across a surface.
struct Pattern {
  enum class Kind { kNone, kChecker, kTiles, kBricks, kWood, kMarble, kNoise, kGround, kPlanks, kImage };
  Kind kind = Kind::kNone;
  Vec3 colorB{0.2f};          // second colour (grout, dark grain, veins...)
  float scale = 1;            // pattern cells per metre
  float contrast = 1;         // how strongly it varies
  float bump = 0;             // bump height in mm
  float roughnessVariation = 0;
  int image = -1;             // Scene::images index for kImage
  bool worldSpace = true;     // world position (true) or mesh UVs
};

struct Material {
  std::string name = "default";
  Vec3 baseColor{0.8f};
  float metallic = 0, roughness = 0.5f;
  float specular = 0.5f;      // dielectric reflectance (0.5 → 4%)
  float ior = 1.5f;
  float transmission = 0;     // 1 = glass / water
  float dispersion = 0;       // 0 none .. 1 strong rainbow fringes
  Vec3 absorption{0};         // inside the glass, per metre (tinted glass)
  float subsurface = 0;       // light that soaks in and comes out softened (skin, wax, marble)
  Vec3 subsurfaceColor{1, 0.4f, 0.25f};
  float clearcoat = 0, clearcoatRoughness = 0.04f;
  float sheen = 0;            // cloth and velvet rim
  float translucency = 0;     // thin sheets: leaves, paper, lampshades
  Vec3 emission{0};           // nits (cd/m²)
  float opacity = 1;          // < 0.5 cut away (in pattern/image alpha)
  bool leafShape = false;     // cut the mesh's UV square to a leaf outline
  Pattern pattern;
  // Pictures from a model (Scene::textures, -1 none), read at the mesh's UVs:
  // colour (sRGB, alpha too) times baseColor, roughness (G) and metal (B)
  // times theirs, a normal map (tangent space), glow times emission.
  int baseTexture = -1, metalRoughTexture = -1, normalTexture = -1, emissiveTexture = -1;
  float alphaCutoff = 0;      // > 0: cut away where the colour picture's alpha is below this
  float roughnessMin = 0;     // never smoother than this (a model's real value; its pictures may say less)
};

struct Light {
  enum class Kind { kPoint, kSpot, kRect, kSun };
  Kind kind = Kind::kPoint;
  Vec3 position{0, 3, 0};
  Vec3 direction{0, -1, 0};   // spot / sun / rect facing (towards where the light goes)
  Vec3 color{1};              // unit-luminance colour (see kelvin())
  float lumens = 800;         // point, spot, rect
  float lux = 100000;         // sun: light on the ground facing it
  float radius = 0.02f;       // point/spot bulb size; sun: angular radius (rad)
  float innerAngle = 0.4f, outerAngle = 0.6f;  // spot, half-angles (rad)
  Vec2 size{1, 1};            // rect
  bool castShadows = true;
};

struct Sky {
  enum class Kind { kPhysical, kColor, kNone };
  Kind kind = Kind::kPhysical;
  Vec3 color{0.4f, 0.6f, 1.0f};
  float colorNits = 3000;     // brightness of a flat colour sky
  float turbidity = 2.5f;     // haze: 2 clear .. 10 smoggy
  float intensity = 1;
  Vec3 groundAlbedo{0.3f};
  float starNits = 0;         // > 0 adds a starry night
};

struct Fog {
  float density = 0;          // extinction per metre (0.01 = a light haze)
  Vec3 albedo{0.9f};
  float anisotropy = 0.6f;    // Henyey-Greenstein g: forward-scattering god rays
  float top = 1e6f;           // fog only below this height
  AABB box{Vec3(-1e6f), Vec3(1e6f)};  // and only inside this box (a room, a valley)
};

struct Camera {
  Vec3 position{0, 1.6f, 5}, target{0, 1, 0}, up{0, 1, 0};
  Vec3 positionEnd{kInf};     // where the camera is when the shutter closes (motion blur); kInf = still
  float focalLength = 35;     // mm
  Vec2 sensor{36, 24};        // mm (full frame)
  float fStop = 2.8f;
  float focusDistance = 0;    // metres; 0 = the distance to `target`
  float shutter = 1.0f / 48;  // seconds (180° at 24 fps)
  float iso = 100;
  int blades = 7;             // aperture blades (0 = round)
  float bladeRotation = 0.2f;
  bool autoExposure = true;   // meter the scene, like a camera on auto
  float exposureCompensation = 0;  // stops
  float fovY() const;         // vertical field of view (rad)
  float ev100() const;        // exposure value of the camera's settings
};

// The film: how the light becomes a picture. Every setting is "what a
// colourist would touch".
struct Look {
  std::string name = "natural";
  enum class Tonemap { kAgX, kAces, kFilmic, kLinear } tonemap = Tonemap::kAgX;
  float contrast = 1, saturation = 1;
  float temperature = 0, tint = 0;            // -1..1 (blue..amber, green..magenta)
  Vec3 lift{0}, gamma{1}, gain{1};            // shadows / mids / highlights
  Vec3 shadowTint{0}, highlightTint{0};       // split toning
  float blackLevel = 0;                       // faded blacks
  float bloom = 0.04f, bloomRadius = 0.02f;   // strength, size (fraction of width)
  float halation = 0;                         // red glow round highlights (film)
  float vignette = 0.25f;
  float grain = 0, grainSize = 1;
  float chromaticAberration = 0;              // pixels at the corners
  float distortion = 0;                       // + barrel, - pincushion
  float sharpen = 0;
  float letterbox = 0;                        // aspect ratio of the bars (2.39), 0 none
  bool monochrome = false;
};
// The built-in looks by name: natural, blockbuster, noir, golden, bleach,
// seventies, neon, dream, documentary, horror, arctic, western.
bool lookByName(const std::string& name, Look& out);
// Camera sensors by name (mm): ff (full frame 36×24), s35, apsc, m43, imax.
bool sensorByName(const std::string& name, Vec2& out);
std::vector<std::string> lookNames();

struct MeshInstance {
  int mesh = 0, material = 0;
  Mat4 transform;
};

struct Scene {
  std::vector<Mesh> meshes;
  std::vector<Material> materials;
  std::vector<MeshInstance> instances;
  std::vector<Light> lights;
  std::vector<Image> images;
  std::vector<Texture> textures;  // model pictures (8-bit), see Material::baseTexture
  Sky sky;
  Fog fog;
  Camera camera;
  Look look;
  Vec3 sunDirection() const;  // towards the sun (from the first sun light), or straight up
  int addMesh(Mesh m) { meshes.push_back(std::move(m)); return static_cast<int>(meshes.size() - 1); }
  int addMaterial(Material m) { materials.push_back(std::move(m)); return static_cast<int>(materials.size() - 1); }
  void add(int mesh, int material, const Mat4& xf = Mat4()) { instances.push_back({mesh, material, xf}); }
  int findMaterial(const std::string& name) const;
  // Simpler versions of a mesh, for when it's small on screen (levels of
  // detail): lods[mesh] = {simpler, simplest}, -1 for none. The live renderer
  // picks one per copy each frame (lodLevel in render/frame_cost.h); the
  // cinema renderer always uses the full mesh.
  std::vector<std::array<int, 2>> lods;
  void setLods(int mesh, int simpler, int simplest);
  int lodMesh(int mesh, int level) const;  // the mesh to draw at level 0, 1 or 2
};

// ---- Light helpers ----
// A glowing body's colour at `k` kelvin, luminance 1 (1000-40000 K).
Vec3 kelvin(float k);
inline float luminance(Vec3 c) { return 0.2126f * c.x + 0.7152f * c.y + 0.0722f * c.z; }
// The camera's exposure multiplier for an EV100.
inline float exposureFromEv100(float ev) { return 1.0f / (1.2f * std::exp2(ev)); }

}  // namespace loop
