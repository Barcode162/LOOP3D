#pragma once
// Landscapes: a height map grown from noise, carved by water (droplet
// hydraulic erosion) and slumped by gravity (thermal erosion), then turned
// into a mesh. Also Poisson-disk scattering for trees, rocks and grass.
#include <cstdint>
#include <functional>
#include <vector>

#include "engine/core/math.h"
#include "engine/geometry/mesh.h"

namespace loop {

class HeightMap {
 public:
  HeightMap() = default;
  HeightMap(int res, float size) : res_(res), size_(size), h_(static_cast<size_t>(res) * res, 0.0f) {}
  int resolution() const { return res_; }
  float size() const { return size_; }  // metres across, centred on 0
  float& at(int x, int z) { return h_[static_cast<size_t>(z) * res_ + x]; }
  float at(int x, int z) const { return h_[static_cast<size_t>(clamp(z, 0, res_ - 1)) * res_ + clamp(x, 0, res_ - 1)]; }
  // Height and normal at world x, z (bilinear).
  float height(float x, float z) const;
  Vec3 normal(float x, float z) const;
  float slope(float x, float z) const { return 1 - normal(x, z).y; }

  struct Shape {
    float height = 120;       // metres from lowest valley to highest peak
    float featureSize = 600;  // metres per base noise cell
    float ridges = 0.55f;     // 0 rolling hills .. 1 sharp mountain ridges
    float warp = 0.6f;        // domain warping
    uint32_t seed = 1;
  };
  void generate(const Shape& s);
  struct Erosion {
    int droplets = 120000;
    float inertia = 0.05f, capacity = 4, deposition = 0.3f, erosion = 0.3f, evaporation = 0.01f, gravity = 4;
    int lifetime = 40, radius = 3;
    uint32_t seed = 7;
  };
  void erodeHydraulic(const Erosion& e);
  void erodeThermal(int iterations, float talusDegrees = 34, float amount = 0.5f);
  // Mesh with `step` samples per vertex (1 = full resolution).
  Mesh toMesh(int step = 1) const;
  float minHeight() const;
  float maxHeight() const;

 private:
  int res_ = 0;
  float size_ = 0;
  std::vector<float> h_;
  Vec2 gradientAt(float gx, float gz, float& h) const;
};

// Points at least `spacing` apart over a square of `size` (centred),
// kept where accept(x, z) says (Bridson).
std::vector<Vec2> poissonDisk(float size, float spacing, uint32_t seed,
                              const std::function<bool(float, float)>& accept = nullptr, int tries = 20);

}  // namespace loop
