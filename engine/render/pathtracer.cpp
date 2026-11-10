#include "engine/render/pathtracer.h"

#include <atomic>

#include "engine/core/jobs.h"
#include "engine/core/random.h"
#include "engine/core/time.h"
#include "engine/render/bsdf.h"
#include "engine/render/pattern.h"
#include "engine/render/scene_bvh.h"

namespace loop {

namespace {

// Visible light from 380 to 720 nm as linear sRGB weights (a smooth fit of
// the CIE matching functions through sRGB), normalised at start-up so that
// averaging over all wavelengths gives back white.
Vec3 wavelengthRaw(float nm) {
  auto g = [](float x, float mu, float s1, float s2) {
    float t = (x - mu) / (x < mu ? s1 : s2);
    return std::exp(-0.5f * t * t);
  };
  float X = 1.056f * g(nm, 599.8f, 37.9f, 31.0f) + 0.362f * g(nm, 442.0f, 16.0f, 26.7f) - 0.065f * g(nm, 501.1f, 20.4f, 26.2f);
  float Y = 0.821f * g(nm, 568.8f, 46.9f, 40.5f) + 0.286f * g(nm, 530.9f, 16.3f, 31.1f);
  float Z = 1.217f * g(nm, 437.0f, 11.8f, 36.0f) + 0.681f * g(nm, 459.0f, 26.0f, 13.8f);
  // A single wavelength lies outside sRGB, so one of the three comes out below
  // zero. Light below zero is thrown away further on (addLight), which made
  // every ray through dispersive glass black; kept at zero instead, and the
  // normalising below still makes all wavelengths together white.
  return vmax(Vec3{3.2404542f * X - 1.5371385f * Y - 0.4985314f * Z, -0.9692660f * X + 1.8760108f * Y + 0.0415560f * Z,
                   0.0556434f * X - 0.2040259f * Y + 1.0572252f * Z},
              Vec3(0));
}

struct Spectrum {
  Vec3 norm{1};
  Spectrum() {
    Vec3 s{0};
    const int n = 340;
    for (int i = 0; i < n; ++i) s += wavelengthRaw(380 + (i + 0.5f));
    s = s / n;
    norm = {1 / s.x, 1 / s.y, 1 / s.z};
  }
  Vec3 weight(float nm) const { return wavelengthRaw(nm) * norm; }
};
const Spectrum& spectrum() {
  static Spectrum s;
  return s;
}

struct Emitter {
  float area;
  Vec3 radiance;
  Vec3 v0, e1, e2, ng;
};

struct LightData {
  Light light;
  Vec3 radiance;  // of the bulb / panel surface (nits)
  float cosInner = 1, cosOuter = 0;
  Vec3 rectU, rectV, rectN;
};

inline float hgPhase(float c, float g) {
  float d = 1 + g * g - 2 * g * c;
  return (1 - g * g) / (4 * kPi * d * std::sqrt(d));
}
inline Vec3 sampleHg(Vec3 wo, float g, Vec2 u) {
  // wo points away from the scatter point (back toward the previous vertex);
  // returns a direction continuing the light's travel.
  float c;
  if (std::fabs(g) < 1e-3f) c = 1 - 2 * u.x;
  else {
    float s = (1 - g * g) / (1 - g + 2 * g * u.x);
    c = (1 + g * g - s * s) / (2 * g);
  }
  float sn = std::sqrt(std::max(0.0f, 1 - c * c)), phi = kTwoPi * u.y;
  Vec3 fwd = -wo, t, b;
  orthonormalBasis(fwd, t, b);
  return normalize(t * (sn * std::cos(phi)) + b * (sn * std::sin(phi)) + fwd * c);
}

}  // namespace

struct PathTracer::Impl {
  const Scene* scene = nullptr;
  SceneBvh geom;
  std::vector<int> emitBase;  // per instance: its first emitter, or -1
  std::vector<Emitter> emitters;
  std::vector<float> emitterCdf;
  float emitterPower = 0;
  std::vector<LightData> lights;
  int sunIndex = -1;
  Vec3 sunRadiance{0};
  float sunCos = 1, sunSolid = 0;
  SkyModel sky;
  bool hasCutouts = false, hasGlass = false;
  RenderSettings settings;
  float clampLum = kInf;
  std::atomic<uint64_t> rays{0};
  bool probeFirst = false;  // probeLight: the first stretch skips lamps and the sun seen straight on
  std::vector<char> stoodFor;  // per instance: a lamp stands for its glow (Light::standsFor), so a probe's first stretch skips it too
  std::vector<float> lampWeights;  // probeLight's lamp picking

  // camera
  Vec3 camPos, camF, camR, camU, camDelta{0};
  float tanX = 1, tanY = 1, lensR = 0, focusDist = 1;
  int blades = 0;
  float bladeRot = 0;

  void build(const Scene& s);
  void setupCamera(const Camera& c, int w, int h);
  Ray cameraRay(float px, float py, int w, int h, Rng& rng, Vec2 pixelJitter) const;
  Vec3 trace(Ray ray, Rng& rng, Vec3* albedoOut, Vec3* normalOut);
  // Fraction of light passing from `p` along `d` for `dist` (blockers,
  // cut-outs, thin glass, fog).
  Vec3 transmittance(Vec3 p, Vec3 d, float dist, bool glassBlocks = false);
  // phase 9: light followed from the bulbs through glass and mirrors to the
  // first rough surface, then to the camera; added to `out` (nits)
  bool lightPathsOn = false;
  void lightPass(Image& out, int W, int H, uint64_t paths, uint32_t seed);
  float fogDistance(Vec3 o, Vec3 d, float tmax, float& t0, float& t1) const;
  bool surfaceAlphaCut(int inst, uint32_t prim, float u, float v, Vec3 p) const;
  // Next event estimation from a surface (bsdf) or a fog point (phase g).
  Vec3 sampleLights(Vec3 p, Vec3 nOffset, const Bsdf* bsdf, Vec3 woLocal, Vec3 woWorld, float phaseG, Rng& rng);
  // Light reached along a BSDF-sampled ray that missed the scene / hit a lamp.
  Vec3 lampHit(const Ray& r, float tmax, float lastPdf, bool lastDelta, bool allowLamps, float& tHit) const;
};

PathTracer::PathTracer() : impl_(std::make_unique<Impl>()) {}
PathTracer::~PathTracer() = default;
const SkyModel& PathTracer::sky() const { return impl_->sky; }
void PathTracer::setScene(const Scene& s) { impl_->build(s); }

void PathTracer::Impl::build(const Scene& s) {
  scene = &s;
  emitters.clear();
  emitterCdf.clear();
  lights.clear();
  hasCutouts = hasGlass = false;
  for (const Material& M : s.materials) {
    // glowing things never block a lamp's light (a bulb's glass around its own
    // light, a neon tube): shadow rays look at each hit, like for cutouts
    if (M.opacity < 1 || M.leafShape || M.alphaCutoff > 0 || luminance(M.emission) > 0) hasCutouts = true;
    if (M.transmission > 0) hasGlass = true;
  }
  geom.build(s);
  emitBase.assign(geom.instanceCount(), -1);
  for (size_t i = 0; i < geom.instanceCount(); ++i) {
    int mat = geom.materialOf(static_cast<int>(i));
    if (mat < 0 || luminance(s.materials[mat].emission) <= 0) continue;
    emitBase[i] = static_cast<int>(emitters.size());
    for (uint32_t k = 0; k < geom.triangleCount(static_cast<int>(i)); ++k) {
      TriInfo t = geom.triangle(static_cast<int>(i), k);
      float area = 0.5f * length(cross(t.e1, t.e2));
      emitters.push_back({std::max(area, 1e-12f), s.materials[mat].emission, t.v0, t.e1, t.e2, t.ng});
    }
  }
  emitterPower = 0;
  for (const Emitter& e : emitters) {
    emitterPower += e.area * luminance(e.radiance);
    emitterCdf.push_back(emitterPower);
  }
  // lamps
  sunIndex = -1;
  float sunLux = 0, sunAngle = 0.00465f;
  Vec3 sunDir{0, 1, 0}, sunColor{1};
  // a lamp that stands for a glowing thing: the thing's own triangles (the
  // emitters above) give its light here, so the lamp is left out
  stoodFor.assign(geom.instanceCount(), 0);
  {
    std::vector<int> geomOf(s.instances.size(), -1);  // SceneBvh skips instances without triangles
    int g = 0;
    for (size_t i = 0; i < s.instances.size(); ++i) {
      const int m = s.instances[i].mesh;
      if (m < 0 || m >= static_cast<int>(s.meshes.size()) || s.meshes[m].triangleCount() == 0) continue;
      geomOf[i] = g++;
    }
    for (const Light& l : s.lights)
      if (l.standsFor >= 0 && l.standsFor < static_cast<int>(geomOf.size()) && geomOf[l.standsFor] >= 0 &&
          geomOf[l.standsFor] < static_cast<int>(stoodFor.size()))
        stoodFor[geomOf[l.standsFor]] = 1;
  }
  for (const Light& l : s.lights) {
    if (l.standsFor >= 0) continue;
    LightData d;
    d.light = l;
    d.light.direction = normalize(l.direction);
    float r = std::max(l.radius, 1e-3f);
    switch (l.kind) {
      case Light::Kind::kPoint: d.radiance = l.color * (l.lumens / (4 * kPi) / (kPi * r * r)); break;
      case Light::Kind::kSpot: {
        d.cosInner = std::cos(l.innerAngle), d.cosOuter = std::cos(std::max(l.outerAngle, l.innerAngle + 1e-3f));
        float cone = kTwoPi * (1 - 0.5f * (d.cosInner + d.cosOuter));
        d.radiance = l.color * (l.lumens / cone / (kPi * r * r));
        break;
      }
      case Light::Kind::kRect: {
        d.rectN = d.light.direction;
        Vec3 t, b;
        orthonormalBasis(d.rectN, t, b);
        if (std::fabs(d.rectN.y) < 0.99f) {
          t = normalize(cross(Vec3{0, 1, 0}, d.rectN));
          b = cross(d.rectN, t);
        }
        d.rectU = t * l.size.x, d.rectV = b * l.size.y;
        d.radiance = l.color * (l.lumens / (kPi * std::max(l.size.x * l.size.y, 1e-6f)));
        break;
      }
      case Light::Kind::kSun:
        if (sunIndex >= 0) continue;  // one sun
        sunIndex = static_cast<int>(lights.size());
        sunLux = l.lux, sunAngle = std::max(l.radius > 0.05f ? 0.00465f : l.radius, 0.0025f);
        sunDir = normalize(-l.direction), sunColor = l.color;
        break;
    }
    lights.push_back(d);
  }
  sky.build(s.sky, sunDir, sunIndex >= 0 ? sunLux : 0, sunAngle);
  sunCos = std::cos(sunAngle);
  sunSolid = kTwoPi * (1 - sunCos);
  if (sunIndex >= 0) {
    if (s.sky.kind == Sky::Kind::kPhysical) sunRadiance = sky.sunDiskRadiance() * sunColor;
    else sunRadiance = sunColor * (sunLux / sunSolid);
  }
}

void PathTracer::Impl::setupCamera(const Camera& c, int w, int h) {
  camPos = c.position;
  camF = normalize(c.target - c.position);
  camR = normalize(cross(camF, c.up));
  camU = cross(camR, camF);
  tanY = std::tan(c.fovY() * 0.5f);
  tanX = tanY * static_cast<float>(w) / h;
  focusDist = c.focusDistance > 0 ? c.focusDistance : length(c.target - c.position);
  lensR = (c.focalLength / 1000.0f) / (2 * std::max(c.fStop, 0.5f));
  blades = c.blades;
  bladeRot = c.bladeRotation;
  camDelta = c.positionEnd.x < kInf * 0.5f ? c.positionEnd - c.position : Vec3(0);
}

Ray PathTracer::Impl::cameraRay(float px, float py, int w, int h, Rng& rng, Vec2 jit) const {
  float sx = ((px + jit.x) / w * 2 - 1) * tanX, sy = (1 - (py + jit.y) / h * 2) * tanY;
  Vec3 dir = normalize(camF + camR * sx + camU * sy);
  Vec3 o = camPos + camDelta * rng.uniform();
  Vec3 focus = o + dir * (focusDist / dot(dir, camF));
  Vec2 l = polygonAperture(rng.uniform2(), blades, bladeRot) * lensR;
  Vec3 lo = o + camR * l.x + camU * l.y;
  return {lo, normalize(focus - lo)};
}

float PathTracer::Impl::fogDistance(Vec3 o, Vec3 d, float tmax, float& t0, float& t1) const {
  // The part of [0, tmax] inside the fog: its box, below its top.
  const Fog& f = scene->fog;
  t0 = 0, t1 = std::min(tmax, 1e7f);
  if (f.density <= 0) return 0;
  AABB box = f.box;
  box.hi.y = std::min(box.hi.y, f.top);
  for (int a = 0; a < 3; ++a) {
    if (std::fabs(d[a]) < 1e-12f) {
      if (o[a] < box.lo[a] || o[a] > box.hi[a]) return 0;
      continue;
    }
    float ta = (box.lo[a] - o[a]) / d[a], tb = (box.hi[a] - o[a]) / d[a];
    if (ta > tb) std::swap(ta, tb);
    t0 = std::max(t0, ta), t1 = std::min(t1, tb);
  }
  return std::max(0.0f, t1 - t0);
}

bool PathTracer::Impl::surfaceAlphaCut(int inst, uint32_t prim, float u, float v, Vec3 p) const {
  int mat = geom.materialOf(inst);
  if (mat < 0) return false;
  const Material& m = scene->materials[mat];
  bool texCut = m.alphaCutoff > 0 && m.baseTexture >= 0 && m.baseTexture < static_cast<int>(scene->textures.size());
  if (m.opacity >= 1 && !m.leafShape && !texCut) return false;
  TriInfo td = geom.triangle(inst, prim);
  Vec2 uv = td.uv[0] * (1 - u - v) + td.uv[1] * u + td.uv[2] * v;
  if (texCut && scene->textures[m.baseTexture].sample(uv.x, uv.y, false).w < m.alphaCutoff) return true;
  if (m.leafShape) {
    // a pointed leaf: widest a third of the way up, a tip at the top
    float y = 1 - uv.y, x = (uv.x - 0.5f) * 2;
    float width = std::sin(kPi * std::pow(saturate(y), 0.75f)) * 0.95f;
    if (std::fabs(x) > width) return true;
  }
  if (m.opacity < 0.5f && m.pattern.kind == Pattern::Kind::kGrid) {
    // a see-through grid: only its lines are there
    float mix, height;
    Vec3 tint;
    patternValue(m.pattern, scene->images, p, td.ng, uv, mix, height, tint);
    return mix < 0.5f;
  }
  if (m.opacity < 1) {
    // dithered opacity: deterministic per point so it's a stable pattern
    uint32_t h = hash32(static_cast<uint32_t>(p.x * 4096) ^ hash32(static_cast<uint32_t>(p.y * 4096)) ^ hash32(static_cast<uint32_t>(p.z * 4096) + 17));
    if (toUnit(h) > m.opacity) return true;
  }
  return false;
}

Vec3 PathTracer::Impl::transmittance(Vec3 p, Vec3 d, float dist, bool glassBlocks) {
  Vec3 T{1};
  const Fog& f = scene->fog;
  if (f.density > 0) {
    float t0, t1;
    float inside = fogDistance(p, d, dist, t0, t1);
    T *= std::exp(-f.density * inside);
  }
  if (!hasCutouts && !hasGlass) {
    rays++;
    return geom.occluded({p, d}, 1e-4f, dist * 0.9999f) ? Vec3(0) : T;
  }
  float t = 1e-4f;
  for (int i = 0; i < 16; ++i) {
    rays++;
    SceneHit h = geom.closest({p, d}, t, dist * 0.9999f);
    if (!h.valid()) return T;
    Vec3 hp = p + d * h.t;
    if (surfaceAlphaCut(h.inst, h.prim, h.u, h.v, hp)) { t = h.t + 1e-4f; continue; }
    int mat = geom.materialOf(h.inst);
    if (mat >= 0) {
      const Material& m = scene->materials[mat];
      if (luminance(m.emission) > 0) {  // glowing things never block light (as live)
        t = h.t + 1e-4f;
        continue;
      }
      if (m.transmission > 0 && !glassBlocks) {
        // shadows through glass: its tint, minus what the surface reflects
        // (no refraction: true caustics come from `caustics` paths)
        T *= m.baseColor * (0.92f * m.transmission);
        if (luminance(T) < 1e-5f) return Vec3(0);
        t = h.t + 1e-4f;
        continue;
      }
    }
    return Vec3(0);
  }
  return T;
}

Vec3 PathTracer::Impl::sampleLights(Vec3 p, Vec3 nOff, const Bsdf* bsdf, Vec3 woL, Vec3 woW, float g, Rng& rng) {
  Vec3 sum{0};
  auto scatter = [&](Vec3 wi, float& pdf) -> Vec3 {
    if (bsdf) return bsdf->eval(woL, bsdf->toLocal(wi), pdf);
    float ph = hgPhase(dot(-woW, wi), g);
    pdf = ph;
    return Vec3(ph);
  };
  auto origin = [&](Vec3 wi) { return bsdf ? p + nOff * (dot(wi, nOff) >= 0 ? 1.0f : -1.0f) : p; };
  // one lamp's light (mis: weighed against the bsdf's own rays finding it)
  auto one = [&](const LightData& L, bool mis) -> Vec3 {
    const Light& l = L.light;
    Vec3 wi, Li;
    float dist, lightPdf;
    switch (l.kind) {
      case Light::Kind::kSun: {
        Vec3 sd = sky.sunDirection(), t, b;
        orthonormalBasis(sd, t, b);
        Vec3 c = uniformCone(rng.uniform2(), sunCos);
        wi = t * c.x + b * c.y + sd * c.z;
        Li = sunRadiance;
        dist = 1e7f;
        lightPdf = 1 / sunSolid;
        break;
      }
      case Light::Kind::kRect: {
        Vec2 u = rng.uniform2();
        Vec3 q = l.position + L.rectU * (u.x - 0.5f) + L.rectV * (u.y - 0.5f);
        Vec3 d = q - p;
        dist = length(d);
        wi = d / dist;
        float cl = dot(-wi, L.rectN);
        if (cl <= 0) return Vec3(0);
        Li = L.radiance;
        lightPdf = dist * dist / (cl * std::max(l.size.x * l.size.y, 1e-6f));
        break;
      }
      default: {  // point / spot as a small glowing ball
        Vec3 d = l.position - p;
        float d2 = lengthSq(d), r = std::max(l.radius, 1e-3f);
        if (d2 <= r * r) return Vec3(0);
        float cd = std::sqrt(d2);
        float cosMax = std::sqrt(std::max(0.0f, 1 - r * r / d2));
        Vec3 axis = d / cd, t, b;
        orthonormalBasis(axis, t, b);
        Vec3 c = uniformCone(rng.uniform2(), cosMax);
        wi = t * c.x + b * c.y + axis * c.z;
        dist = cd - r;
        lightPdf = 1 / (kTwoPi * (1 - cosMax));
        Li = L.radiance;
        if (l.kind == Light::Kind::kSpot) Li *= smoothstep(L.cosOuter, L.cosInner, dot(-wi, l.direction));
        if (1 - cosMax < 1e-7f) {  // too far / small to sample as a cone: treat as a point
          lightPdf = 0;
          Li = Li * (kPi * r * r) / d2;
        }
        break;
      }
    }
    if (luminance(Li) <= 0) return Vec3(0);
    float bp;
    Vec3 f = scatter(wi, bp);
    if (luminance(f) <= 0) return Vec3(0);
    // a bulb's light through glass comes from the light paths (lightPass) when they run
    const bool bulb = l.kind == Light::Kind::kPoint || l.kind == Light::Kind::kSpot;
    Vec3 T = l.castShadows ? transmittance(origin(wi), wi, dist, lightPathsOn && bulb) : Vec3(1);
    if (luminance(T) <= 0) return Vec3(0);
    if (lightPdf == 0 || !mis) return lightPdf == 0 ? f * Li * T : f * Li * T / lightPdf;
    return f * Li * T * (powerHeuristic(lightPdf, bp) / lightPdf);
    };
  if (probeFirst && lights.size() > 8) {
    // the light cache's probes: a few lamps picked by how much light they
    // could give here, each weighed by its chance (the sun always); the
    // bsdf's rays don't count lamps they hit, so no mixing weights
    lampWeights.resize(lights.size());
    float total = 0;
    for (size_t i = 0; i < lights.size(); ++i) {
      const Light& l = lights[i].light;
      float w = 0;
      if (l.kind != Light::Kind::kSun) w = std::max(l.lumens, 0.0f) / std::max(lengthSq(l.position - p), 0.25f);
      else sum += one(lights[i], false);
      total += w;
      lampWeights[i] = total;
    }
    const int picks = 4;
    if (total > 0)
      for (int k = 0; k < picks; ++k) {
        size_t i = std::lower_bound(lampWeights.begin(), lampWeights.end(), rng.uniform() * total) - lampWeights.begin();
        i = std::min(i, lights.size() - 1);
        const float pick = (lampWeights[i] - (i ? lampWeights[i - 1] : 0.0f)) / total;
        if (pick > 0) sum += one(lights[i], false) / (pick * picks);
      }
  } else {
    for (const LightData& L : lights) sum += one(L, true);
  }
  // one glowing triangle, picked by power
  if (!emitters.empty() && emitterPower > 0) {
    float x = rng.uniform() * emitterPower;
    size_t k = std::lower_bound(emitterCdf.begin(), emitterCdf.end(), x) - emitterCdf.begin();
    k = std::min(k, emitters.size() - 1);
    const Emitter& e = emitters[k];
    Vec2 bu = uniformTriangle(rng.uniform2());
    Vec3 q = e.v0 + e.e1 * bu.x + e.e2 * bu.y;
    Vec3 d = q - p;
    float dist = length(d);
    if (dist > 1e-5f) {
      Vec3 wi = d / dist;
      // a glowing face lights only what it faces (as when a ray meets it: its
      // front); its back gave light too, so a closed glowing shape lit twice
      float cl = -dot(wi, e.ng);
      if (cl > 1e-6f) {
        float pSel = e.area * luminance(e.radiance) / emitterPower;
        float lightPdf = pSel * dist * dist / (cl * e.area);
        float bp;
        Vec3 f = scatter(wi, bp);
        if (luminance(f) > 0) {
          Vec3 T = transmittance(origin(wi), wi, dist * 0.999f);
          sum += f * e.radiance * T * (powerHeuristic(lightPdf, bp) / lightPdf);
        }
      }
    }
  }
  return sum;
}


Vec3 PathTracer::Impl::lampHit(const Ray& r, float tmax, float lastPdf, bool lastDelta, bool allowLamps, float& tHit) const {
  // Lamps are drawn as glowing balls / panels the camera and reflections see.
  Vec3 out{0};
  tHit = tmax;
  if (!allowLamps) return out;
  for (const LightData& L : lights) {
    const Light& l = L.light;
    float lightPdf = 0, t = kInf;
    Vec3 Le{0};
    if (l.kind == Light::Kind::kRect) {
      float dn = dot(r.d, L.rectN);
      if (dn >= 0) continue;  // panels shine one way
      t = dot(l.position - r.o, L.rectN) / dn;
      if (t <= 1e-4f || t >= tHit) continue;
      Vec3 q = r.at(t) - l.position;
      float su = dot(q, L.rectU) / lengthSq(L.rectU), sv = dot(q, L.rectV) / lengthSq(L.rectV);
      if (std::fabs(su) > 0.5f || std::fabs(sv) > 0.5f) continue;
      Le = L.radiance;
      lightPdf = t * t / (-dn * std::max(l.size.x * l.size.y, 1e-6f));
    } else if (l.kind == Light::Kind::kPoint || l.kind == Light::Kind::kSpot) {
      float rad = std::max(l.radius, 1e-3f);
      Vec3 oc = r.o - l.position;
      float b = dot(oc, r.d), c = dot(oc, oc) - rad * rad, disc = b * b - c;
      if (disc < 0 || c <= 0) continue;
      t = -b - std::sqrt(disc);
      if (t <= 1e-4f || t >= tHit) continue;
      Le = L.radiance;
      if (l.kind == Light::Kind::kSpot) Le *= smoothstep(L.cosOuter, L.cosInner, dot(-r.d, l.direction));
      float d2 = dot(oc, oc), cosMax = std::sqrt(std::max(0.0f, 1 - rad * rad / d2));
      lightPdf = 1 / (kTwoPi * std::max(1 - cosMax, 1e-9f));
    } else continue;
    tHit = t;
    float w = lastDelta ? 1.0f : powerHeuristic(lastPdf, lightPdf);
    out = Le * w;
  }
  return out;
}

Vec3 PathTracer::Impl::trace(Ray ray, Rng& rng, Vec3* albedoOut, Vec3* normalOut) {
  const Scene& S = *scene;
  Vec3 L{0}, beta{1};
  float lastPdf = 0;
  bool lastDelta = true, sawDiffuse = false, firstHit = true;
  float wavelength = 0;  // chosen on the first dispersive glass
  const Vec3 eye = ray.o;
  Vec3 firstPoint{0};  // where the sight line first meets a surface (for the haze)
  bool hazed = false;
  Vec3 insideAbsorb{0};
  int bounces = 0;
  auto addLight = [&](Vec3 c) {
    if (!(c.x >= 0 && c.y >= 0 && c.z >= 0) || !isFinite(c)) return;
    if (bounces > 0) {
      float y = luminance(c);
      if (y > clampLum) c *= clampLum / y;
    }
    L += c;
  };
  for (int guard = 0; guard < settings.maxBounces + 24; ++guard) {
    rays++;
    SceneHit h = geom.closest(ray);
    float tSurf = h.valid() ? h.t : kInf;
    const bool skipDirect = probeFirst && ((bounces == 0 && firstHit) || lights.size() > 8);  // many lamps: their light by sampleLights alone
    bool allowLamps = (settings.caustics || !sawDiffuse || !lastDelta) && !skipDirect;
    float tLamp;
    Vec3 lamp = lampHit(ray, tSurf, lastPdf, lastDelta, allowLamps, tLamp);

    // fog: does the light scatter before reaching the surface / lamp?
    const Fog& fog = S.fog;
    // a light-cache probe gathers no fog: the live renderer draws the fog's
    // glow along each view itself
    if (fog.density > 0 && !probeFirst) {
      float t0, t1;
      float span = fogDistance(ray.o, ray.d, std::min(tLamp, tSurf), t0, t1);
      if (span > 0) {
        float td = -std::log(1 - rng.uniform()) / fog.density;
        if (td < span) {
          Vec3 p = ray.at(t0 + td);
          beta *= fog.albedo;
          if (firstHit) {
            if (albedoOut) *albedoOut = fog.albedo;
            if (normalOut) *normalOut = -ray.d;
            firstHit = false;
          }
          addLight(beta * sampleLights(p, {}, nullptr, {}, -ray.d, fog.anisotropy, rng));
          Vec3 nd = sampleHg(-ray.d, fog.anisotropy, rng.uniform2());
          lastPdf = hgPhase(dot(ray.d, nd), fog.anisotropy);
          lastDelta = false;
          sawDiffuse = true;
          ray = {p, nd};
          if (++bounces > settings.maxBounces) break;
          continue;
        }
      }
    }
    if (tLamp < tSurf) {
      addLight(beta * lamp);
      break;
    }
    if (!h.valid()) {
      Vec3 env = sky.radiance(ray.d);
      if (sunIndex >= 0 && !skipDirect && dot(ray.d, sky.sunDirection()) > sunCos && (settings.caustics || !sawDiffuse || !lastDelta)) {
        float w = lastDelta ? 1.0f : powerHeuristic(lastPdf, 1 / sunSolid);
        env += sunRadiance * w;
      }
      addLight(beta * env);
      if (firstHit) {
        if (albedoOut) *albedoOut = Vec3(1);
        if (normalOut) *normalOut = -ray.d;
      }
      break;
    }
    Vec3 p = ray.at(h.t);
    if (luminance(insideAbsorb) > 0) {
      beta *= Vec3(std::exp(-insideAbsorb.x * h.t), std::exp(-insideAbsorb.y * h.t), std::exp(-insideAbsorb.z * h.t));
    }
    if (surfaceAlphaCut(h.inst, h.prim, h.u, h.v, p)) {
      ray.o = p + ray.d * 1e-4f;
      continue;
    }
    TriInfo td = geom.triangle(h.inst, h.prim);
    int emissiveIndex = emitBase[h.inst] >= 0 ? emitBase[h.inst] + static_cast<int>(h.prim) : -1;
    static const Material kDefault;
    const Material& M = td.material >= 0 ? S.materials[td.material] : kDefault;
    float w0 = 1 - h.u - h.v;
    Vec3 ns = normalize(td.n[0] * w0 + td.n[1] * h.u + td.n[2] * h.v);
    Vec2 uv = td.uv[0] * w0 + td.uv[1] * h.u + td.uv[2] * h.v;
    Vec3 ng = td.ng;
    bool front = dot(ray.d, ng) < 0;
    if (!front) ng = -ng, ns = -ns;
    if (dot(ns, ng) < 0) ns = ng;
    Surface sf = shadeSurface(M, S.images, p, ns, uv, &S.textures);
    Vec3 n = sf.normal;
    Vec3 wo = -ray.d;
    if (dot(n, wo) <= 1e-4f) n = normalize(n + ng * (1e-4f - dot(n, wo) + 0.01f));

    // glowing surfaces
    // (a light-cache probe looking straight at a glow a lamp stands for skips
    // it, as it skips lamps: the live renderer lights with that lamp)
    if (luminance(M.emission) > 0 && front && !(probeFirst && bounces == 0 && firstHit && stoodFor[h.inst])) {
      if (lastDelta || emissiveIndex < 0) {
        if (settings.caustics || !sawDiffuse || !lastDelta) addLight(beta * M.emission);
      } else {
        const Emitter& e = emitters[emissiveIndex];
        float cl = std::fabs(dot(ray.d, td.ng));
        float lightPdf = (e.area * luminance(e.radiance) / emitterPower) * h.t * h.t / std::max(cl * e.area, 1e-9f);
        addLight(beta * M.emission * powerHeuristic(lastPdf, lightPdf));
      }
    }

    // the surface's scattering
    Bsdf b;
    b.n = n;
    orthonormalBasis(n, b.t, b.b);
    Vec3 base = sf.base;
    float metal = sf.metallic, rough = std::max(sf.roughness, 0.0f);
    float glass = M.transmission * (1 - metal);
    float transl = M.translucency * (1 - metal) * (1 - glass);
    float sss = M.subsurface * (1 - metal) * (1 - glass);
    b.alpha = rough * rough;
    b.f0 = vlerp(Vec3(0.08f * M.specular), base, metal);
    b.diffuse = base * ((1 - metal) * (1 - glass) * (1 - transl) * (1 - 0.5f * sss));
    b.translucent = base * transl + base * M.subsurfaceColor * (0.5f * sss);
    b.sheen = Vec3(M.sheen) * vlerp(Vec3(1), base, 0.5f) * (1 - metal);
    b.coat = M.clearcoat;
    b.coatAlpha = std::max(sqr(M.clearcoatRoughness), 1e-4f);
    b.glass = glass;
    b.glassAlpha = rough < 0.03f ? 0 : rough * rough;
    float ior = M.ior;
    if (glass > 0 && M.dispersion > 0 && settings.dispersion) {
      if (wavelength == 0) {
        wavelength = 380 + 340 * rng.uniform();
        beta *= spectrum().weight(wavelength);
      }
      // Cauchy's law: shorter wavelengths bend more
      float B = 0.0042f * M.dispersion * 4;
      float lum = wavelength / 1000.0f;
      ior = (ior - B / (0.5893f * 0.5893f)) + B / (lum * lum);
    }
    b.eta = front ? 1 / ior : ior;
    b.glassTint = glass > 0 && luminance(M.absorption) <= 0 ? base : Vec3(1);
    if (glass > 0 && b.smoothSpec()) b.f0 = Vec3(0);  // the glass lobe does its own Fresnel
    if (glass > 0) b.f0 = b.f0 * (1 - glass);
    Vec3 woL = b.toLocal(wo);
    b.finalize(woL);

    if (firstHit) {
      firstPoint = p, hazed = true;
      Vec3 alb = b.diffuse + b.translucent + b.f0 * metal + Vec3(glass);
      if (albedoOut) *albedoOut = vmin(vmax(alb, Vec3(0.02f)), Vec3(1));
      if (normalOut) *normalOut = n;
      firstHit = false;
    }

    // lamps, sun, glowing surfaces (not for perfectly smooth lobes alone)
    bool onlyDelta = b.pDiffuse + b.pCoat + b.pTranslucent + (b.smoothSpec() ? 0 : b.pSpec) <= 1e-6f;
    if (!onlyDelta) addLight(beta * sampleLights(p, ng * 1e-4f + ng * (h.t * 1e-6f), &b, woL, wo, 0, rng));

    Bsdf::Sample sm;
    if (!b.sample(woL, {rng.uniform(), rng.uniform(), rng.uniform()}, sm)) break;
    Vec3 wi = b.toWorld(sm.wi);
    beta *= sm.weight;
    if (!isFinite(beta) || luminance(beta) <= 0) break;
    lastPdf = sm.pdf;
    lastDelta = sm.delta;
    if (!sm.delta) sawDiffuse = true;
    bool goesIn = dot(wi, ng) < 0;
    if (sm.transmitted && glass > 0) insideAbsorb = front ? M.absorption : Vec3(0);
    ray = {p + ng * (goesIn ? -1e-4f : 1e-4f), wi};
    if (++bounces > settings.maxBounces) break;
    // Russian roulette: stop dim paths early, without bias
    if (bounces > 3) {
      float q = std::min(0.95f, maxComp(beta));
      if (rng.uniform() > q) break;
      beta /= q;
    }
  }
  // haze: the air along the sight line to the first thing it met (a light-cache
  // probe is not an eye, and the sky seen straight is left as it is)
  if (hazed && !probeFirst && S.fog.haze.visibility > 0) L = hazeAt(L, eye, firstPoint, sky, S.fog.haze);
  return L;
}

Vec3 PathTracer::tracePixel(float px, float py, int w, int h, uint32_t i) {
  Impl& I = *impl_;
  I.setupCamera(I.scene->camera, w, h);
  Rng rng(hashCombine(static_cast<uint32_t>(px) * 9781 + static_cast<uint32_t>(py) * 6271, i));
  Ray r = I.cameraRay(px, py, w, h, rng, rng.uniform2());
  return I.trace(r, rng, nullptr, nullptr);
}

Vec3 PathTracer::probeInterval(Vec3 p, Vec3 d, float t0, float t1, uint32_t seed, int bounces, bool* through) {
  Impl& I = *impl_;
  const Ray r{p + d * t0, d};
  ++I.rays;
  const SceneHit h = I.geom.closest(r);
  if (t1 >= 1e4f) {  // the last stretch goes on forever: the sky too
    *through = false;
    return probeLight(r.o, d, seed, bounces, nullptr);
  }
  if (!h.valid() || h.t > t1 - t0) {
    *through = true;
    return Vec3(0);
  }
  *through = false;
  return probeLight(r.o, d, seed, bounces, nullptr);
}

bool PathTracer::clear(Vec3 a, Vec3 b) {
  const Vec3 d = b - a;
  const float len = length(d);
  if (len < 1e-6f) return true;
  ++impl_->rays;
  return !impl_->geom.occluded({a, d / len}, 1e-4f, len);
}

Vec3 PathTracer::probeLight(Vec3 p, Vec3 d, uint32_t seed, int bounces, bool* back) {
  Impl& I = *impl_;
  Ray r{p, d};
  if (back) {
    SceneHit h = I.geom.closest(r);
    *back = h.valid() && dot(d, I.geom.triangle(h.inst, h.prim).ng) > 0;
  }
  Rng rng(seed);
  I.settings.maxBounces = std::max(bounces, 1);
  I.probeFirst = true;
  Vec3 c = I.trace(r, rng, nullptr, nullptr);
  I.probeFirst = false;
  return isFinite(c) ? c : Vec3(0);
}

RenderOutput PathTracer::render(const RenderSettings& s) {
  Impl& I = *impl_;
  Stopwatch sw;
  I.settings = s;
  I.rays = 0;
  const int W = s.width, H = s.height;
  I.setupCamera(I.scene->camera, W, H);
  // light paths only where there is glass for a bulb's light to go through
  I.lightPathsOn = false;
  if (s.lightPaths && I.hasGlass)
    for (const LightData& L : I.lights)
      if (L.light.kind == Light::Kind::kPoint || L.light.kind == Light::Kind::kSpot) I.lightPathsOn = true;
  // Auto clamp: a quick low-resolution look at the scene, then bounces may be
  // at most `clamp` times brighter than its average (kills fireflies).
  I.clampLum = kInf;
  if (s.clamp >= 0) {
    const int pw = 48, ph = std::max(8, 48 * H / W);
    double sum = 0;
    int n = 0;
    for (int y = 0; y < ph; ++y)
      for (int x = 0; x < pw; ++x) {
        Rng rng(hashCombine(x * 31 + 7, y * 17 + 3));
        I.setupCamera(I.scene->camera, pw, ph);
        Vec3 c = I.trace(I.cameraRay(x + 0.5f, y + 0.5f, pw, ph, rng, {0, 0}), rng, nullptr, nullptr);
        sum += std::log(std::max(1e-3f, luminance(c)));
        ++n;
      }
    float avg = static_cast<float>(std::exp(sum / std::max(1, n)));
    I.clampLum = avg * (s.clamp > 0 ? s.clamp : 60.0f);
    I.setupCamera(I.scene->camera, W, H);
  }
  RenderOutput out;
  out.color = Image(W, H, {0, 0, 0, 1});
  out.albedo = Image(W, H, {0, 0, 0, 1});
  out.normal = Image(W, H, {0, 0, 0, 1});
  Image variance(W, H, {0, 0, 0, 1});
  const int tile = 16, tx = (W + tile - 1) / tile, ty = (H + tile - 1) / tile;
  std::atomic<int> done{0};
  std::atomic<bool> stop{false};
  JobSystem::shared().parallelFor(tx * ty, [&](int t) {
    if (stop) return;
    int x0 = (t % tx) * tile, y0 = (t / tx) * tile;
    for (int y = y0; y < std::min(H, y0 + tile); ++y)
      for (int x = x0; x < std::min(W, x0 + tile); ++x) {
        uint32_t ph = hashCombine(static_cast<uint32_t>(x) * 73856093u ^ static_cast<uint32_t>(y) * 19349663u, s.seed);
        Vec2 rot{toUnit(ph), toUnit(hash32(ph))};
        Vec3 sum{0}, alb{0}, nrm{0};
        double m = 0, m2 = 0;
        for (int i = 0; i < s.samples; ++i) {
          Rng rng(hashCombine(ph, static_cast<uint32_t>(i)));
          Vec3 a{0}, nn{0};
          Ray r = I.cameraRay(x, y, W, H, rng, r2(static_cast<uint32_t>(i), rot));
          Vec3 c = I.trace(r, rng, &a, &nn);
          sum += c, alb += a, nrm += nn;
          double l = luminance(c);
          m += l, m2 += l * l;
        }
        float inv = 1.0f / s.samples;
        out.color.at(x, y) = {sum * inv, 1};
        out.albedo.at(x, y) = {alb * inv, 1};
        Vec3 nn = normalize(nrm);
        out.normal.at(x, y) = {nn, 1};
        double mean = m * inv, var = std::max(0.0, m2 * inv - mean * mean) / s.samples;
        variance.at(x, y) = {Vec3(static_cast<float>(var)), 1};
      }
    int d = ++done;
    if (s.progress && (d % 16 == 0 || d == tx * ty))
      if (!s.progress(static_cast<float>(d) / (tx * ty))) stop = true;
  }, 1);
  // the bulbs' light through glass, followed from the bulbs
  if (I.lightPathsOn && !stop) I.lightPass(out.color, W, H, static_cast<uint64_t>(W) * H * static_cast<uint64_t>(std::max(1, s.samples / 2)), s.seed);
  out.noisy = out.color;
  if (s.denoise && s.samples < 4096) out.color = denoise(out.color, out.albedo, out.normal, variance);
  out.seconds = sw.seconds();
  out.rays = I.rays;
  return out;
}

// ------------------------------------------------------------- light paths
// Light tracing, the other half of bidirectional path tracing: each path
// leaves a bulb (picked by its power), passes through glass (bent by Fresnel
// and Snell, tinted) and off smooth metal, and where it first lands on a rough
// surface after at least one of those, that spot is joined to the camera's
// pupil. The camera's own paths can't find these (a bulb is too small to hit
// by chance after a bounce), so nothing is counted twice: the bulbs' shadow
// rays stop at glass while this runs. The landing surface is taken as matte.
void PathTracer::Impl::lightPass(Image& out, int W, int H, uint64_t paths, uint32_t seed) {
  const Scene& S = *scene;
  std::vector<int> bulbs;
  std::vector<float> cdf;
  float total = 0;
  for (size_t i = 0; i < lights.size(); ++i) {
    const Light& l = lights[i].light;
    if (l.kind != Light::Kind::kPoint && l.kind != Light::Kind::kSpot) continue;
    total += std::max(l.lumens, 0.0f) * luminance(l.color);
    bulbs.push_back(static_cast<int>(i)), cdf.push_back(total);
  }
  if (bulbs.empty() || total <= 0 || paths == 0) return;
  const float A = 4 * tanX * tanY;  // the picture's plane, a metre in front of the pupil
  const float scale = static_cast<float>(W) * static_cast<float>(H) / static_cast<float>(paths);
  const int chunks = 64;
  std::vector<Image> parts(static_cast<size_t>(chunks));
  JobSystem::shared().parallelFor(chunks, [&](int c) {
    Image& img = parts[static_cast<size_t>(c)];
    img = Image(W, H, {0, 0, 0, 0});
    const uint64_t a = paths * static_cast<uint64_t>(c) / chunks, b = paths * static_cast<uint64_t>(c + 1) / chunks;
    for (uint64_t k = a; k < b; ++k) {
      Rng rng(hashCombine(seed * 2654435761u + 17u, static_cast<uint32_t>(k) ^ static_cast<uint32_t>(k >> 32)));
      // a bulb, by its power, and a direction out of it
      const float x = rng.uniform() * total;
      const size_t pick = std::min(static_cast<size_t>(std::lower_bound(cdf.begin(), cdf.end(), x) - cdf.begin()), bulbs.size() - 1);
      const LightData& L = lights[static_cast<size_t>(bulbs[pick])];
      const Light& l = L.light;
      const float pPick = (cdf[pick] - (pick ? cdf[pick - 1] : 0.0f)) / total;
      const Vec2 u = rng.uniform2();
      const float z = 1 - 2 * u.x, r = std::sqrt(std::max(0.0f, 1 - z * z)), ph = kTwoPi * u.y;
      Vec3 d(r * std::cos(ph), z, r * std::sin(ph));
      // its strength that way (lumens over the sphere, or over a spot's cone), over the chances
      const float rad = std::max(l.radius, 1e-3f);
      Vec3 I = L.radiance * (kPi * rad * rad);
      if (l.kind == Light::Kind::kSpot) I *= smoothstep(L.cosOuter, L.cosInner, dot(d, l.direction));
      Vec3 beta = I * (4 * kPi) / pPick;
      if (luminance(beta) <= 0) continue;
      Ray ray{l.position + d * (rad * 1.01f), d};
      bool bent = false;
      for (int bounce = 0; bounce < 12; ++bounce) {
        ++rays;
        const SceneHit h = geom.closest(ray);
        if (!h.valid()) break;
        const Vec3 p = ray.at(h.t);
        if (surfaceAlphaCut(h.inst, h.prim, h.u, h.v, p)) {
          ray.o = p + ray.d * 1e-4f;
          --bounce;
          continue;
        }
        const TriInfo td = geom.triangle(h.inst, h.prim);
        static const Material kDefault;
        const Material& M = td.material >= 0 ? S.materials[td.material] : kDefault;
        const float w0 = 1 - h.u - h.v;
        Vec3 ns = normalize(td.n[0] * w0 + td.n[1] * h.u + td.n[2] * h.v);
        const Vec2 uv = td.uv[0] * w0 + td.uv[1] * h.u + td.uv[2] * h.v;
        Vec3 ng = td.ng;
        const bool front = dot(ray.d, ng) < 0;
        if (!front) ng = -ng, ns = -ns;
        if (dot(ns, ng) < 0) ns = ng;
        const Surface sf = shadeSurface(M, S.images, p, ns, uv, &S.textures);
        const Vec3 n = sf.normal;
        const float metal = sf.metallic, glass = M.transmission * (1 - metal);
        if (glass > 0.5f && sf.roughness < 0.05f) {
          // smooth glass: reflected or bent through, by Fresnel
          const float eta = front ? 1 / std::max(M.ior, 1.0f) : std::max(M.ior, 1.0f);
          const float cosI = -dot(ray.d, n);
          const float F = fresnelDielectric(cosI, eta);
          if (rng.uniform() < F) {
            ray = {p + ng * 1e-4f, normalize(ray.d + n * (2 * cosI))};
          } else {
            const float s2 = eta * eta * (1 - cosI * cosI);
            const Vec3 t = ray.d * eta + n * (eta * cosI - std::sqrt(std::max(0.0f, 1 - s2)));
            ray = {p - ng * 1e-4f, normalize(t)};
            beta *= sf.base;  // its tint
          }
          bent = true;
          continue;
        }
        if (metal > 0.5f && sf.roughness < 0.05f) {
          // a mirror
          ray = {p + ng * 1e-4f, normalize(ray.d - n * (2 * dot(ray.d, n)))};
          beta *= sf.base;
          bent = true;
          continue;
        }
        if (!bent) break;  // straight from the bulb: the camera's paths have it
        // landed: joined to the camera's pupil, if it sees this spot
        const Vec3 v = p - camPos;
        const float zc = dot(v, camF);
        if (zc <= 1e-4f) break;
        const float sx = dot(v, camR) / zc / tanX, sy = dot(v, camU) / zc / tanY;
        if (std::fabs(sx) >= 1 || std::fabs(sy) >= 1) break;
        const float dist = length(v);
        const Vec3 toCam = -v / dist;
        const float cosX = dot(n, toCam);
        if (cosX <= 0) break;  // the camera sees the other side
        ++rays;
        if (geom.occluded({p + ng * 1e-4f, toCam}, 1e-4f, dist * 0.9999f)) break;
        const float cosC = zc / dist;
        const Vec3 f = sf.base * ((1 - metal) * (1 - glass) / kPi);
        const Vec3 c = beta * f * (cosX / (dist * dist * A * cosC * cosC * cosC)) * scale;
        if (!isFinite(c)) break;
        const int px = std::clamp(static_cast<int>((sx * 0.5f + 0.5f) * static_cast<float>(W)), 0, W - 1);
        const int py = std::clamp(static_cast<int>((0.5f - sy * 0.5f) * static_cast<float>(H)), 0, H - 1);
        Vec4& o = img.at(px, py);
        o.x += c.x, o.y += c.y, o.z += c.z;
        break;
      }
    }
  }, 1);
  for (const Image& img : parts)
    for (size_t i = 0; i < out.px.size(); ++i) out.px[i].x += img.px[i].x, out.px[i].y += img.px[i].y, out.px[i].z += img.px[i].z;
}

// ------------------------------------------------------------- denoiser
Image denoise(const Image& color, const Image& albedo, const Image& normal, const Image& variance, int iterations) {
  const int W = color.width, H = color.height;
  // Work on light per unit of surface colour, so textures aren't blurred.
  Image cur(W, H), var = variance;
  for (size_t i = 0; i < cur.px.size(); ++i) {
    Vec3 a = vmax(albedo.px[i].xyz(), Vec3(0.02f));
    cur.px[i] = {color.px[i].xyz() / a, 1};
    var.px[i].x = variance.px[i].x / std::max(luminance(a) * luminance(a), 1e-4f);
  }
  const float kernel[5] = {1.0f / 16, 1.0f / 4, 3.0f / 8, 1.0f / 4, 1.0f / 16};
  // A pixel's own noise estimate is itself noisy (a pixel whose few samples
  // all missed the light thinks it's certain): spread it over its
  // neighbours, and keep a floor relative to the local brightness.
  auto spreadVariance = [&](Image& v, const Image& c) {
    Image o(W, H);
    JobSystem::shared().parallelFor(H, [&](int y) {
      for (int x = 0; x < W; ++x) {
        float s = 0, m = 0;
        int n = 0;
        for (int j = -2; j <= 2; ++j)
          for (int i = -2; i <= 2; ++i) {
            int xx = clamp(x + i, 0, W - 1), yy = clamp(y + j, 0, H - 1);
            s += v.at(xx, yy).x, m += luminance(c.at(xx, yy).xyz()), ++n;
          }
        float mean = m / n;
        o.at(x, y) = {Vec3(std::max(s / n, sqr(0.08f * mean))), 1};
      }
    });
    v = std::move(o);
  };
  spreadVariance(var, cur);
  for (int it = 0; it < iterations; ++it) {
    int step = 1 << it;
    Image next(W, H), nextVar(W, H);
    JobSystem::shared().parallelFor(H, [&](int y) {
      for (int x = 0; x < W; ++x) {
        Vec3 c0 = cur.at(x, y).xyz(), n0 = normal.at(x, y).xyz(), a0 = albedo.at(x, y).xyz();
        float l0 = luminance(c0);
        float sigmaL = 4.0f * std::sqrt(std::max(var.at(x, y).x, 0.0f)) + 1e-3f * (l0 + 1e-3f);
        Vec3 sum{0};
        float wsum = 0, vsum = 0;
        for (int j = -2; j <= 2; ++j)
          for (int i = -2; i <= 2; ++i) {
            int xx = x + i * step, yy = y + j * step;
            if (xx < 0 || yy < 0 || xx >= W || yy >= H) continue;
            Vec3 c = cur.at(xx, yy).xyz();
            float wn = std::pow(std::max(0.0f, dot(n0, normal.at(xx, yy).xyz())), 32.0f);
            Vec3 da = a0 - albedo.at(xx, yy).xyz();
            float wa = std::exp(-dot(da, da) / 0.02f);
            float wl = std::exp(-std::fabs(luminance(c) - l0) / sigmaL);
            float w = kernel[i + 2] * kernel[j + 2] * wn * wa * wl;
            sum += c * w;
            wsum += w;
            vsum += w * w * var.at(xx, yy).x;
          }
        next.at(x, y) = {wsum > 0 ? sum / wsum : c0, 1};
        nextVar.at(x, y) = {Vec3(wsum > 0 ? vsum / (wsum * wsum) : var.at(x, y).x), 1};
      }
    });
    cur = std::move(next);
    var = std::move(nextVar);
  }
  for (size_t i = 0; i < cur.px.size(); ++i) cur.px[i] = {cur.px[i].xyz() * vmax(albedo.px[i].xyz(), Vec3(0.02f)), 1};
  return cur;
}

}  // namespace loop
