#pragma once
// A physical sky: sunlight scattered by air (Rayleigh, why the sky is blue),
// haze (Mie, the bright glow round the sun) and absorbed by ozone (the deep
// blue at dusk), integrated through a spherical atmosphere. The sun reddens
// and dims as it sets because more air is in the way, not because a colour
// was picked. Baked into a table for fast lookups.
#include <vector>

#include "engine/core/math.h"
#include "engine/render/scene.h"

namespace loop {

class SkyModel {
 public:
  // `sunLux`: illuminance of the sun with it overhead, on the ground.
  void build(const Sky& sky, Vec3 sunDir, float sunLux, float sunAngularRadius = 0.00465f);
  // Sky radiance (nits) seen looking along `dir` (without the sun's disk).
  Vec3 radiance(Vec3 dir) const;
  // Including the sun's disk when `dir` falls on it.
  Vec3 radianceWithSun(Vec3 dir) const;
  // The sun as a light at the ground: colour × illuminance (lux), already
  // dimmed and reddened by the air.
  Vec3 sunIlluminance() const { return sunIlluminance_; }
  Vec3 sunDirection() const { return sunDir_; }
  float sunAngularRadius() const { return sunRadius_; }
  Vec3 sunDiskRadiance() const { return sunDisk_; }
  // Average light from the whole sky on an upward-facing surface (lux).
  Vec3 ambientIrradiance() const { return ambient_; }
  // Spherical-harmonic (order 2) projection of the sky, for the GPU.
  void shCoefficients(Vec3 out[9]) const;
  bool valid() const { return !table_.empty() || kind_ != Sky::Kind::kPhysical; }

 private:
  Vec3 integrate(Vec3 dir) const;
  Vec3 transmittanceToSpace(Vec3 p, Vec3 dir) const;
  Sky::Kind kind_ = Sky::Kind::kPhysical;
  Vec3 flat_{0};
  float stars_ = 0;
  Vec3 sunDir_{0, 1, 0}, sunIlluminance_{0}, sunDisk_{0}, ambient_{0}, ground_{0.3f};
  float sunRadius_ = 0.00465f, turbidity_ = 2.5f, sunTop_ = 0;  // sun illuminance above the air
  static constexpr int kW = 192, kH = 96;
  std::vector<Vec3> table_;
};

}  // namespace loop
