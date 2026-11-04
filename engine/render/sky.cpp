#include "engine/render/sky.h"

#include "engine/core/jobs.h"
#include "engine/core/random.h"

namespace loop {

namespace {
constexpr float kEarth = 6360e3f, kTop = 6420e3f;
const Vec3 kRayleigh{5.802e-6f, 13.558e-6f, 33.1e-6f};
constexpr float kRayleighH = 8000, kMieH = 1200;
const Vec3 kOzone{0.650e-6f, 1.881e-6f, 0.085e-6f};

struct Air {
  float turbidity;
  Vec3 mieScatter() const { return Vec3(3.996e-6f * turbidity / 2.5f); }
  Vec3 mieExtinct() const { return Vec3(4.44e-6f * turbidity / 2.5f); }
  // Extinction and the two scattering parts at height h (m).
  void at(float h, Vec3& ext, Vec3& ray, Vec3& mie) const {
    float dr = std::exp(-h / kRayleighH), dm = std::exp(-h / kMieH);
    float doz = std::max(0.0f, 1 - std::fabs(h - 25000) / 15000);
    ray = kRayleigh * dr;
    mie = mieScatter() * dm;
    ext = ray + mieExtinct() * dm + kOzone * doz;
  }
};

// Distance along `d` from `p` to the sphere of radius r (outside hit), -1 none.
float sphereExit(Vec3 p, Vec3 d, float r) {
  float b = dot(p, d), c = dot(p, p) - r * r, disc = b * b - c;
  if (disc < 0) return -1;
  return -b + std::sqrt(disc);
}
float sphereEnter(Vec3 p, Vec3 d, float r) {
  float b = dot(p, d), c = dot(p, p) - r * r, disc = b * b - c;
  if (disc < 0) return -1;
  float t = -b - std::sqrt(disc);
  return t > 0 ? t : -1;
}
inline Vec3 vexp(Vec3 v) { return {std::exp(v.x), std::exp(v.y), std::exp(v.z)}; }
float phaseRayleigh(float c) { return 3.0f / (16 * kPi) * (1 + c * c); }
float phaseMie(float c, float g = 0.8f) {  // Cornette-Shanks
  float g2 = g * g;
  return 3.0f / (8 * kPi) * ((1 - g2) * (1 + c * c)) / ((2 + g2) * std::pow(1 + g2 - 2 * g * c, 1.5f));
}

// Table layout: azimuth x, and elevation with more rows near the horizon.
inline float elevToV(float elev) {  // elev in [-pi/2, pi/2] -> v in [0,1]
  float s = elev / (kPi / 2);
  return 0.5f + 0.5f * std::copysign(std::sqrt(std::fabs(s)), s);
}
inline float vToElev(float v) {
  float s = (v - 0.5f) * 2;
  return std::copysign(s * s, s) * (kPi / 2);
}
}  // namespace

Vec3 SkyModel::transmittanceToSpace(Vec3 p, Vec3 d) const {
  Air air{turbidity_};
  if (sphereEnter(p, d, kEarth) > 0) return Vec3(0);
  float len = sphereExit(p, d, kTop);
  if (len <= 0) return Vec3(1);
  const int n = 24;
  Vec3 od{0};
  for (int i = 0; i < n; ++i) {
    Vec3 q = p + d * ((i + 0.5f) * len / n);
    Vec3 e, r, m;
    air.at(length(q) - kEarth, e, r, m);
    od += e * (len / n);
  }
  return vexp(-od);
}

Vec3 SkyModel::integrate(Vec3 dir) const {
  Air air{turbidity_};
  Vec3 origin{0, kEarth + 2, 0};
  float tGround = sphereEnter(origin, dir, kEarth);
  float len = tGround > 0 ? tGround : sphereExit(origin, dir, kTop);
  const int n = 40;
  Vec3 sum{0}, od{0};
  float c = dot(dir, sunDir_);
  float pr = phaseRayleigh(c), pm = phaseMie(c);
  float prev = 0;
  for (int i = 0; i < n; ++i) {
    // denser steps near the eye
    float t1 = len * sqr((i + 1.0f) / n);
    float dt = t1 - prev, tm = (prev + t1) * 0.5f;
    prev = t1;
    Vec3 q = origin + dir * tm;
    Vec3 e, r, m;
    air.at(length(q) - kEarth, e, r, m);
    Vec3 tView = vexp(-(od + e * (dt * 0.5f)));
    od += e * dt;
    Vec3 tSun = transmittanceToSpace(q, sunDir_);
    // single scattering + a cheap isotropic term for light scattered many times
    Vec3 single = (r * pr + m * pm) * tSun;
    Vec3 multi = (r + m) * (0.06f * std::max(0.0f, sunDir_.y + 0.1f));
    sum += tView * (single + multi) * dt;
  }
  Vec3 L = sum * sunTop_;
  if (tGround > 0) {
    // the ground far below: lit by the sun through the air
    Vec3 tv = vexp(-od);
    Vec3 tsun = transmittanceToSpace(origin, sunDir_);
    L += tv * ground_ * tsun * (sunTop_ * std::max(0.0f, sunDir_.y) / kPi);
  }
  return L;
}

void SkyModel::build(const Sky& sky, Vec3 sunDir, float sunLux, float sunRadius) {
  kind_ = sky.kind;
  sunDir_ = normalize(sunDir);
  sunRadius_ = std::max(sunRadius, 0.001f);
  turbidity_ = sky.turbidity;
  ground_ = sky.groundAlbedo;
  stars_ = sky.starNits;
  flat_ = kind_ == Sky::Kind::kColor ? sky.color * sky.colorNits * sky.intensity : Vec3(0);
  table_.clear();
  if (kind_ != Sky::Kind::kPhysical) {
    sunIlluminance_ = Vec3(0);
    sunDisk_ = Vec3(0);
    ambient_ = flat_ * kPi;
    return;
  }
  // Calibrate: `sunLux` is what the sun gives at noon overhead.
  Vec3 tz = transmittanceToSpace({0, kEarth + 2, 0}, {0, 1, 0});
  sunTop_ = sunLux / std::max(luminance(tz), 1e-6f) * sky.intensity;
  Vec3 ts = transmittanceToSpace({0, kEarth + 2, 0}, sunDir_);
  sunIlluminance_ = ts * sunTop_;
  float solid = kTwoPi * (1 - std::cos(sunRadius_));
  sunDisk_ = sunIlluminance_ / solid;
  table_.assign(static_cast<size_t>(kW) * kH, Vec3(0));
  JobSystem::shared().parallelFor(kH, [&](int y) {
    float elev = vToElev((y + 0.5f) / kH);
    for (int x = 0; x < kW; ++x) {
      float az = (x + 0.5f) / kW * kTwoPi;
      Vec3 d{std::cos(elev) * std::cos(az), std::sin(elev), std::cos(elev) * std::sin(az)};
      table_[static_cast<size_t>(y) * kW + x] = integrate(d);
    }
  });
  // Irradiance on an upward surface from the dome.
  Vec3 e{0};
  Rng rng(5);
  const int n = 2048;
  for (int i = 0; i < n; ++i) {
    Vec3 d = cosineHemisphere(r2(i));
    e += radiance({d.x, d.z, d.y});
  }
  ambient_ = e * (kPi / n);
}

Vec3 SkyModel::radiance(Vec3 d) const {
  if (kind_ == Sky::Kind::kNone) return Vec3(0);
  Vec3 L = flat_;
  if (kind_ == Sky::Kind::kPhysical && !table_.empty()) {
    float elev = std::asin(clamp(d.y, -1.0f, 1.0f));
    float az = std::atan2(d.z, d.x);
    if (az < 0) az += kTwoPi;
    float fx = az / kTwoPi * kW - 0.5f, fy = elevToV(elev) * kH - 0.5f;
    int x0 = static_cast<int>(std::floor(fx)), y0 = clamp(static_cast<int>(std::floor(fy)), 0, kH - 1);
    int y1 = std::min(y0 + 1, kH - 1);
    float tx = fx - std::floor(fx), ty = clamp(fy - y0, 0.0f, 1.0f);
    int xa = (x0 % kW + kW) % kW, xb = (xa + 1) % kW;
    auto T = [&](int x, int y) { return table_[static_cast<size_t>(y) * kW + x]; };
    L = vlerp(vlerp(T(xa, y0), T(xb, y0), tx), vlerp(T(xa, y1), T(xb, y1), tx), ty);
  }
  if (stars_ > 0 && d.y > 0) {
    // a fixed field of stars: hash the direction on a fine grid
    Vec3 g = d * 400.0f;
    uint32_t h = hash32(static_cast<uint32_t>(std::floor(g.x)) * 73856093U ^ static_cast<uint32_t>(std::floor(g.y)) * 19349663U ^ static_cast<uint32_t>(std::floor(g.z)) * 83492791U);
    if ((h & 1023) < 3) {
      float b = toUnit(hash32(h));
      L += kelvin(3000 + 9000 * toUnit(hash32(h + 1))) * (stars_ * b * b * b * 40);
    }
    L += Vec3(0.04f, 0.05f, 0.09f) * stars_ * 0.02f;  // airglow
  }
  return L;
}

Vec3 SkyModel::radianceWithSun(Vec3 d) const {
  Vec3 L = radiance(d);
  if (kind_ == Sky::Kind::kPhysical && dot(d, sunDir_) > std::cos(sunRadius_)) L += sunDisk_;
  return L;
}

void SkyModel::shCoefficients(Vec3 out[9]) const {
  for (int i = 0; i < 9; ++i) out[i] = Vec3(0);
  const int n = 4096;
  for (int i = 0; i < n; ++i) {
    Vec3 d = uniformSphere(r2(i, {0.3f, 0.7f}));
    Vec3 L = radiance(d);
    float x = d.x, y = d.y, z = d.z;
    float b[9] = {0.282095f, 0.488603f * y, 0.488603f * z, 0.488603f * x, 1.092548f * x * y, 1.092548f * y * z,
                  0.315392f * (3 * z * z - 1), 1.092548f * x * z, 0.546274f * (x * x - y * y)};
    for (int k = 0; k < 9; ++k) out[k] += L * (b[k] * 4 * kPi / n);
  }
}

}  // namespace loop
