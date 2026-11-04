#include "engine/render/bsdf.h"

#include "engine/render/scene.h"

namespace loop {

namespace {
// Directional albedo E(cos, alpha) of a white single-bounce GGX surface,
// worked out once. Light a real rough surface sends back after bouncing
// between its own micro-bumps is restored with 1 + F0 (1/E - 1) (Kulla &
// Conty 2017, the cheap form), so rough metals don't go dark.
struct AlbedoTable {
  static constexpr int N = 24;
  float e[N][N];
  AlbedoTable() {
    for (int i = 0; i < N; ++i)
      for (int j = 0; j < N; ++j) {
        float mu = (i + 0.5f) / N, rough = (j + 0.5f) / N, a = rough * rough;
        Vec3 wo{std::sqrt(1 - mu * mu), 0, mu};
        double sum = 0;
        const int n = 512;
        for (int k = 0; k < n; ++k) {
          Vec3 m = ggx::sampleVisible(wo, a, r2(k));
          Vec3 wi = reflect(-wo, m);
          if (wi.z > 0) sum += ggx::G2(wo.z, wi.z, a) / ggx::G1(wo.z, a);
        }
        e[i][j] = static_cast<float>(sum / n);
      }
  }
  float at(float mu, float rough) const {
    float x = clamp(mu * N - 0.5f, 0.0f, N - 1.001f), y = clamp(rough * N - 0.5f, 0.0f, N - 1.001f);
    int i = static_cast<int>(x), j = static_cast<int>(y);
    float tx = x - i, ty = y - j;
    return lerp(lerp(e[i][j], e[i + 1][j], tx), lerp(e[i][j + 1], e[i + 1][j + 1], tx), ty);
  }
};
const AlbedoTable& albedoTable() {
  static AlbedoTable t;
  return t;
}
}  // namespace

float ggxAlbedo(float mu, float roughness) { return albedoTable().at(mu, roughness); }

void Bsdf::finalize(Vec3 wo) {
  float c = std::max(wo.z, 1e-4f);
  float wd = luminance(diffuse), ws = luminance(schlick(f0, c)), wc = coat * 0.25f, wt = luminance(translucent), wg = glass;
  if (ws > 0 || wd > 0) ws = std::max(ws, 0.08f);
  float sum = wd + ws + wc + wt + wg;
  if (sum <= 0) { pSpec = 1; return; }
  pDiffuse = wd / sum, pSpec = ws / sum, pCoat = wc / sum, pTranslucent = wt / sum, pGlass = wg / sum;
}

Vec3 Bsdf::eval(Vec3 wo, Vec3 wi, float& pdf) const {
  pdf = 0;
  Vec3 f{0};
  if (wo.z <= 0) return f;
  if (wi.z > 0) {
    Vec3 h = normalize(wo + wi);
    float vh = dot(wo, h);
    // coat sits on top: what it reflects doesn't reach the base
    float coatF = coat > 0 ? coat * (0.04f + 0.96f * std::pow(1 - saturate(vh), 5.0f)) : 0;
    float base = 1 - coatF;
    Vec3 fs = schlick(f0, vh);
    float diffuseScale = (1 - luminance(schlick(f0, wo.z))) * base;
    f += diffuse * (kInvPi * wi.z * diffuseScale);
    if (luminance(sheen) > 0) f += sheen * (std::pow(1 - saturate(vh), 5.0f) * kInvPi * wi.z * base);
    pdf += pDiffuse * wi.z * kInvPi;
    if (!smoothSpec()) {
      float E = std::max(albedoTable().at(wo.z, std::sqrt(alpha)), 0.05f);
      Vec3 comp = Vec3(1) + f0 * (1 / E - 1);
      f += fs * comp * (ggx::D(h.z, alpha) * ggx::G2(wo.z, wi.z, alpha) / (4 * wo.z) * base);
      pdf += pSpec * ggx::pdfReflect(wo, wi, alpha);
    }
    if (coat > 0) {
      f += Vec3(coatF * ggx::D(h.z, coatAlpha) * ggx::G2(wo.z, wi.z, coatAlpha) / (4 * wo.z));
      pdf += pCoat * ggx::pdfReflect(wo, wi, coatAlpha);
    }
  } else if (wi.z < 0 && luminance(translucent) > 0) {
    f += translucent * (kInvPi * -wi.z);
    pdf += pTranslucent * -wi.z * kInvPi;
  }
  return f;
}

bool Bsdf::sample(Vec3 wo, Vec3 u, Sample& s) const {
  if (wo.z <= 0) return false;
  float x = u.z;
  Vec2 uv{u.x, u.y};
  s.delta = false;
  s.transmitted = false;
  if (x < pGlass) {
    // glass: a microfacet (or the flat surface), then Fresnel picks reflect / refract
    Vec3 m = glassAlpha > 1e-3f ? ggx::sampleVisible(wo, glassAlpha, uv) : Vec3{0, 0, 1};
    float c = dot(wo, m);
    float F = fresnelDielectric(c, eta);
    float pick = x / pGlass;
    Vec3 wi;
    Vec3 w{1};
    if (pick < F) {
      wi = reflect(-wo, m);
      if (wi.z <= 0) return false;
    } else {
      if (!refract(-wo, m, eta, wi)) return false;
      if (wi.z >= 0) return false;
      w = glassTint;
      s.transmitted = true;
    }
    if (glassAlpha > 1e-3f) w *= ggx::G2(wo.z, std::fabs(wi.z), glassAlpha) / ggx::G1(wo.z, glassAlpha);
    s.wi = wi;
    s.weight = w / pGlass;
    s.pdf = 0;
    s.delta = true;
    return true;
  }
  x -= pGlass;
  if (smoothSpec() && x < pSpec) {
    s.wi = {-wo.x, -wo.y, wo.z};
    s.weight = schlick(f0, wo.z) / pSpec;
    s.pdf = 0;
    s.delta = true;
    return true;
  }
  if (x < pDiffuse) s.wi = cosineHemisphere(uv);
  else if ((x -= pDiffuse) < pSpec) s.wi = reflect(-wo, ggx::sampleVisible(wo, alpha, uv));
  else if ((x -= pSpec) < pCoat) s.wi = reflect(-wo, ggx::sampleVisible(wo, coatAlpha, uv));
  else {
    Vec3 d = cosineHemisphere(uv);
    s.wi = {d.x, d.y, -d.z};
  }
  if (std::fabs(s.wi.z) < 1e-6f) return false;
  float pdf;
  Vec3 f = eval(wo, s.wi, pdf);
  if (pdf <= 0) return false;
  s.weight = f / pdf;
  s.pdf = pdf;
  s.transmitted = s.wi.z < 0;
  return true;
}

}  // namespace loop
