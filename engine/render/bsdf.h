#pragma once
// How a surface scatters light: one "principled" model with lobes for
// diffuse, glossy reflection (GGX microfacets), clear coat, sheen, thin
// translucency and glass. Energy never grows: every lobe is weighted so the
// sum reflects at most what came in (checked by tests/render_test.cpp).
#include "engine/core/math.h"
#include "engine/core/random.h"

namespace loop {

namespace ggx {
// alpha = roughness²
inline float D(float nh, float a) {
  float a2 = a * a, d = nh * nh * (a2 - 1) + 1;
  return a2 / (kPi * d * d);
}
inline float lambda(float nv, float a) {
  float c2 = nv * nv, t2 = std::max(0.0f, 1 - c2) / std::max(c2, 1e-8f);
  return 0.5f * (std::sqrt(1 + a * a * t2) - 1);
}
inline float G1(float nv, float a) { return 1 / (1 + lambda(nv, a)); }
inline float G2(float nv, float nl, float a) { return 1 / (1 + lambda(nv, a) + lambda(nl, a)); }
// Heitz 2018: a microfacet normal seen from `v` (local, z up).
inline Vec3 sampleVisible(Vec3 v, float a, Vec2 u) {
  Vec3 vh = normalize(Vec3{a * v.x, a * v.y, v.z});
  float ls = vh.x * vh.x + vh.y * vh.y;
  Vec3 t1 = ls > 0 ? Vec3{-vh.y, vh.x, 0} / std::sqrt(ls) : Vec3{1, 0, 0};
  Vec3 t2 = cross(vh, t1);
  float r = std::sqrt(u.x), phi = kTwoPi * u.y;
  float p1 = r * std::cos(phi), p2 = r * std::sin(phi);
  float s = 0.5f * (1 + vh.z);
  p2 = (1 - s) * std::sqrt(std::max(0.0f, 1 - p1 * p1)) + s * p2;
  Vec3 nh = t1 * p1 + t2 * p2 + vh * std::sqrt(std::max(0.0f, 1 - p1 * p1 - p2 * p2));
  return normalize(Vec3{a * nh.x, a * nh.y, std::max(1e-6f, nh.z)});
}
// pdf of the reflected direction l for visible-normal sampling.
inline float pdfReflect(Vec3 v, Vec3 l, float a) {
  Vec3 h = normalize(v + l);
  if (h.z <= 0 || v.z <= 0) return 0;
  return G1(v.z, a) * D(h.z, a) / (4 * v.z);
}
}  // namespace ggx

// Single-bounce GGX albedo (table), for energy compensation.
float ggxAlbedo(float mu, float roughness);

inline Vec3 schlick(Vec3 f0, float c) {
  float m = std::pow(1 - saturate(c), 5.0f);
  return f0 + (Vec3(1) - f0) * m;
}
// Exact Fresnel reflectance for a dielectric; eta = n_from / n_to.
inline float fresnelDielectric(float cosI, float eta) {
  cosI = saturate(cosI);
  float s2 = eta * eta * (1 - cosI * cosI);
  if (s2 >= 1) return 1;
  float cosT = std::sqrt(1 - s2);
  float rs = (eta * cosI - cosT) / (eta * cosI + cosT), rp = (cosI - eta * cosT) / (cosI + eta * cosT);
  return 0.5f * (rs * rs + rp * rp);
}

// One surface point's scattering, in a local frame where the shading
// normal is +z and the outgoing (view) direction is above it.
struct Bsdf {
  Vec3 t, b, n;  // frame
  Vec3 diffuse{0}, translucent{0}, f0{0.04f}, sheen{0}, glassTint{1};
  float alpha = 0.25f, coat = 0, coatAlpha = 0.01f, glass = 0, glassAlpha = 0, eta = 1 / 1.5f;
  float pDiffuse = 0, pSpec = 0, pCoat = 0, pTranslucent = 0, pGlass = 0;

  Vec3 toLocal(Vec3 v) const { return {dot(v, t), dot(v, b), dot(v, n)}; }
  Vec3 toWorld(Vec3 v) const { return t * v.x + b * v.y + n * v.z; }
  // Lobe choices from the weights (call after filling the fields).
  void finalize(Vec3 woLocal);
  // f·cos for wo → wi (local), and the combined pdf of the lobes that can be
  // evaluated (glass and perfect mirrors are excluded: they're sampled only).
  Vec3 eval(Vec3 wo, Vec3 wi, float& pdf) const;
  struct Sample {
    Vec3 wi;        // local
    Vec3 weight;    // f·cos / pdf
    float pdf = 0;  // combined pdf (0 for delta)
    bool delta = false, transmitted = false;
  };
  bool sample(Vec3 wo, Vec3 u, Sample& s) const;
  bool smoothSpec() const { return alpha < 1e-3f; }
};

}  // namespace loop
