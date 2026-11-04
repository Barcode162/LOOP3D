#include "engine/render/film.h"

#include <algorithm>

#include "engine/core/jobs.h"
#include "engine/core/random.h"

namespace loop {

float meterLuminance(const Image& hdr) {
  std::vector<float> l;
  l.reserve(hdr.px.size() / 4 + 1);
  for (size_t i = 0; i < hdr.px.size(); i += 4) l.push_back(std::log(std::max(luminance(hdr.px[i].xyz()), 1e-4f)));
  if (l.empty()) return 1;
  std::sort(l.begin(), l.end());
  size_t lo = l.size() * 3 / 100, hi = l.size() * 97 / 100;
  double s = 0;
  for (size_t i = lo; i < hi; ++i) s += l[i];
  return static_cast<float>(std::exp(s / std::max<size_t>(1, hi - lo)));
}

// ------------------------------------------------------------- tone curves
Vec3 tonemapAgX(Vec3 c) {
  // Troy Sobotka's AgX, with Benjamin Wrensch's polynomial fit of the curve.
  const Mat3 in = [] {
    Mat3 m;
    m.c[0] = {0.842479062253094f, 0.0423282422610123f, 0.0423756549057051f};
    m.c[1] = {0.0784335999999992f, 0.878468636469772f, 0.0784336f};
    m.c[2] = {0.0792237451477643f, 0.0791661274605434f, 0.879142973793104f};
    return m;
  }();
  const Mat3 out = [] {
    Mat3 m;
    m.c[0] = {1.19687900512017f, -0.0528968517574562f, -0.0529716355144438f};
    m.c[1] = {-0.0980208811401368f, 1.15190312990417f, -0.0980434501171241f};
    m.c[2] = {-0.0990297440797205f, -0.0989611768448433f, 1.15107367264116f};
    return m;
  }();
  const float minEv = -12.47393f, maxEv = 4.026069f;
  c = in * vmax(c, Vec3(1e-10f));
  Vec3 r;
  for (int i = 0; i < 3; ++i) {
    float x = saturate((std::log2(c[i]) - minEv) / (maxEv - minEv));
    float x2 = x * x, x4 = x2 * x2;
    r[i] = 15.5f * x4 * x2 - 40.14f * x4 * x + 31.96f * x4 - 6.868f * x2 * x + 0.4298f * x2 + 0.1191f * x - 0.00232f;
  }
  r = out * r;
  // the curve's output is display-encoded (gamma 2.2); return linear
  return {std::pow(saturate(r.x), 2.2f), std::pow(saturate(r.y), 2.2f), std::pow(saturate(r.z), 2.2f)};
}

Vec3 tonemapAces(Vec3 c) {
  // Stephen Hill's fit of the ACES RRT + sRGB ODT.
  Mat3 inM, outM;
  inM.c[0] = {0.59719f, 0.07600f, 0.02840f};
  inM.c[1] = {0.35458f, 0.90834f, 0.13383f};
  inM.c[2] = {0.04823f, 0.01566f, 0.83777f};
  outM.c[0] = {1.60475f, -0.10208f, -0.00327f};
  outM.c[1] = {-0.53108f, 1.10813f, -0.07276f};
  outM.c[2] = {-0.07367f, -0.00605f, 1.07602f};
  Vec3 v = inM * c;
  Vec3 a = v * (v + Vec3(0.0245786f)) - Vec3(0.000090537f);
  Vec3 b = v * (v * 0.983729f + Vec3(0.4329510f)) + Vec3(0.238081f);
  return vmax(vmin(outM * (a / b), Vec3(1)), Vec3(0));
}

Vec3 tonemapFilmic(Vec3 c) {  // John Hable's, white point 11.2
  auto h = [](float x) {
    const float A = 0.22f, B = 0.30f, C = 0.10f, D = 0.20f, E = 0.01f, F = 0.30f;
    return ((x * (A * x + C * B) + D * E) / (x * (A * x + B) + D * F)) - E / F;
  };
  float w = 1 / h(11.2f);
  c = c * 2.0f;
  return {saturate(h(c.x) * w), saturate(h(c.y) * w), saturate(h(c.z) * w)};
}

Vec3 whiteBalance(float temperature, float tint) {
  // As if the picture were lit by a warmer (or colder) white: multiply by
  // that white's colour relative to daylight, then nudge green / magenta.
  Vec3 g = kelvin(6500 - temperature * 3000) / kelvin(6500);
  if (temperature < 0) g = kelvin(6500 / (1 + temperature * 0.6f)) / kelvin(6500);
  g = g * Vec3(1 + tint * 0.08f, 1 - tint * 0.12f, 1 + tint * 0.08f);
  return g / luminance(g);
}

Vec3 gradePixel(Vec3 c, const Look& L) {
  // contrast around middle grey, in log space
  if (L.contrast != 1) {
    for (int i = 0; i < 3; ++i) {
      float v = std::max(c[i], 1e-7f);
      c[i] = 0.18f * std::pow(v / 0.18f, L.contrast);
    }
  }
  float y = luminance(c);
  c = vmax(Vec3(0), Vec3(y) + (c - Vec3(y)) * L.saturation);
  Vec3 t;
  switch (L.tonemap) {
    case Look::Tonemap::kAgX: t = tonemapAgX(c); break;
    case Look::Tonemap::kAces: t = tonemapAces(c); break;
    case Look::Tonemap::kFilmic: t = tonemapFilmic(c); break;
    default: t = vmin(c, Vec3(1)); break;
  }
  // display-referred grading (lift / gamma / gain, split toning, faded black)
  float ty = luminance(t);
  t += L.shadowTint * (1 - smoothstep(0.0f, 0.5f, ty)) + L.highlightTint * smoothstep(0.4f, 1.0f, ty);
  for (int i = 0; i < 3; ++i) {
    float v = saturate(t[i]);
    v = v * L.gain[i] + L.lift[i] * (1 - v);
    v = std::pow(std::max(v, 0.0f), 1.0f / std::max(L.gamma[i], 0.01f));
    t[i] = L.blackLevel + v * (1 - L.blackLevel);
  }
  if (L.monochrome) {
    float m = 0.25f * t.x + 0.65f * t.y + 0.1f * t.z;  // a panchromatic stock's response
    t = Vec3(m);
  }
  return vmax(vmin(t, Vec3(1)), Vec3(0));
}

// ------------------------------------------------------------- image passes
namespace {

Image downsample(const Image& src) {
  Image d(std::max(1, src.width / 2), std::max(1, src.height / 2));
  for (int y = 0; y < d.height; ++y)
    for (int x = 0; x < d.width; ++x) {
      Vec4 s{};
      for (int dy = 0; dy < 2; ++dy)
        for (int dx = 0; dx < 2; ++dx) s = s + src.at(std::min(src.width - 1, x * 2 + dx), std::min(src.height - 1, y * 2 + dy));
      d.at(x, y) = s * 0.25f;
    }
  return d;
}

void blur(Image& img, float sigma) {
  if (sigma <= 0.3f) return;
  int r = static_cast<int>(std::ceil(sigma * 2.5f));
  std::vector<float> k(2 * r + 1);
  float sum = 0;
  for (int i = -r; i <= r; ++i) sum += k[i + r] = std::exp(-0.5f * i * i / (sigma * sigma));
  for (float& v : k) v /= sum;
  Image tmp(img.width, img.height);
  JobSystem::shared().parallelFor(img.height, [&](int y) {
    for (int x = 0; x < img.width; ++x) {
      Vec4 s{};
      for (int i = -r; i <= r; ++i) s = s + img.at(clamp(x + i, 0, img.width - 1), y) * k[i + r];
      tmp.at(x, y) = s;
    }
  });
  JobSystem::shared().parallelFor(img.height, [&](int y) {
    for (int x = 0; x < img.width; ++x) {
      Vec4 s{};
      for (int i = -r; i <= r; ++i) s = s + tmp.at(x, clamp(y + i, 0, img.height - 1)) * k[i + r];
      img.at(x, y) = s;
    }
  });
}

// A wide, natural glow: several blurred copies at falling resolutions, each
// a bit wider, summed (like light scattering in glass and the eye).
Image glow(const Image& src, float radiusFrac, int levels = 6) {
  Image acc(src.width, src.height, {0, 0, 0, 0});
  Image level = src;
  float totalW = 0;
  for (int l = 0; l < levels; ++l) {
    level = downsample(level);
    Image b = level;
    blur(b, std::max(1.0f, radiusFrac * b.width * 0.35f));
    float w = 1.0f / (1 + l * 0.5f);
    totalW += w;
    JobSystem::shared().parallelFor(acc.height, [&](int y) {
      for (int x = 0; x < acc.width; ++x)
        acc.at(x, y) = acc.at(x, y) + b.sample((x + 0.5f) / acc.width, (y + 0.5f) / acc.height) * w;
    });
    if (level.width < 4 || level.height < 4) break;
  }
  for (auto& p : acc.px) p = p * (1 / totalW);
  return acc;
}

Vec4 sampleClamp(const Image& img, float x, float y) {
  x = clamp(x, 0.0f, img.width - 1.001f), y = clamp(y, 0.0f, img.height - 1.001f);
  int x0 = static_cast<int>(x), y0 = static_cast<int>(y);
  float tx = x - x0, ty = y - y0;
  Vec4 a = img.at(x0, y0) * (1 - tx) + img.at(x0 + 1, y0) * tx;
  Vec4 b = img.at(x0, y0 + 1) * (1 - tx) + img.at(x0 + 1, y0 + 1) * tx;
  return a * (1 - ty) + b * ty;
}

}  // namespace

Image develop(const Image& hdr, const Camera& cam, const Look& L, uint32_t frame, FilmResult* info) {
  const int W = hdr.width, H = hdr.height;
  FilmResult res;
  res.averageLuminance = meterLuminance(hdr);
  if (cam.autoExposure) res.exposure = 0.18f / std::max(res.averageLuminance, 1e-6f);
  else res.exposure = exposureFromEv100(cam.ev100());
  res.exposure *= std::exp2(cam.exposureCompensation);
  res.ev100 = std::log2(1.0f / (1.2f * res.exposure));
  if (info) *info = res;

  // 1. Exposure and the lens: barrel distortion and colour fringes.
  Image img(W, H);
  const float cx = W * 0.5f, cy = H * 0.5f, diag = std::sqrt(cx * cx + cy * cy);
  JobSystem::shared().parallelFor(H, [&](int y) {
    for (int x = 0; x < W; ++x) {
      float dx = (x + 0.5f - cx) / diag, dy = (y + 0.5f - cy) / diag;
      float r2 = dx * dx + dy * dy;
      float k = 1 + L.distortion * 0.15f * r2;
      float ca = L.chromaticAberration / diag;
      Vec3 c;
      for (int ch = 0; ch < 3; ++ch) {
        float s = k * (1 + (ch - 1) * ca * std::sqrt(r2));
        Vec4 v = sampleClamp(hdr, cx + dx * diag * s - 0.5f, cy + dy * diag * s - 0.5f);
        c[ch] = v[ch];
      }
      img.at(x, y) = {vmax(c, Vec3(0)) * res.exposure, 1};
    }
  });

  // 2. Bloom and halation (film's red glow from light bouncing off its backing).
  if (L.bloom > 0 || L.halation > 0) {
    // Glow from at most ~40x middle grey per pixel: beyond that a real lens
    // and sensor are saturated, and the sun alone would flood the frame.
    Image capped = img;
    for (auto& p : capped.px) {
      float y = luminance(p.xyz());
      if (y > 8) p = Vec4(p.xyz() * (8 / y), 1);
    }
    Image g = glow(capped, L.bloomRadius);
    Image h;
    if (L.halation > 0) {
      Image bright = capped;
      for (auto& p : bright.px) {
        float y = luminance(p.xyz());
        p = Vec4(p.xyz() * saturate((y - 0.9f) / 2.0f), 1);
      }
      h = glow(bright, L.bloomRadius * 0.5f, 4);
    }
    JobSystem::shared().parallelFor(H, [&](int y) {
      for (int x = 0; x < W; ++x) {
        Vec3 c = img.at(x, y).xyz();
        c = c + (g.at(x, y).xyz() - vmin(c, Vec3(8))) * L.bloom;
        if (L.halation > 0) c += h.at(x, y).xyz() * Vec3(1.0f, 0.25f, 0.05f) * (L.halation * 2);
        img.at(x, y) = {c, 1};
      }
    });
  }

  // 3. White balance, vignette, grade and tone curve, grain, bars.
  const Vec3 wb = whiteBalance(L.temperature, L.tint);
  const float aspect = static_cast<float>(W) / H;
  const int bar = L.letterbox > aspect ? static_cast<int>(std::round((H - W / L.letterbox) * 0.5f)) : 0;
  Image out(W, H);
  JobSystem::shared().parallelFor(H, [&](int y) {
    Rng rng(hashCombine(frame * 7919u + 1, static_cast<uint32_t>(y)));
    for (int x = 0; x < W; ++x) {
      if (y < bar || y >= H - bar) { out.at(x, y) = {0, 0, 0, 1}; continue; }
      Vec3 c = img.at(x, y).xyz() * wb;
      float dx = (x + 0.5f - cx) / diag, dy = (y + 0.5f - cy) / diag;
      float r2 = (dx * dx + dy * dy) * 2;
      float cos4 = 1 / sqr(1 + r2 * 0.25f);  // natural lens fall-off
      c *= lerp(1.0f, cos4 * (1 - 0.4f * r2), L.vignette);
      Vec3 t = gradePixel(c, L);
      if (L.grain > 0) {
        // grain is clumped (grainSize) and strongest in the mid tones
        uint32_t gx = static_cast<uint32_t>(x / std::max(1.0f, L.grainSize)), gy = static_cast<uint32_t>(y / std::max(1.0f, L.grainSize));
        Rng g(hashCombine(hashCombine(gx, gy), frame));
        float n = g.gaussian();
        float ty = luminance(t);
        float amt = L.grain * (0.4f + 2.4f * ty * (1 - ty));
        t = vmax(Vec3(0), t + Vec3(n * amt));
      }
      Vec3 enc{linearToSrgb(t.x), linearToSrgb(t.y), linearToSrgb(t.z)};
      enc = vmax(Vec3(0), vmin(Vec3(1), enc + Vec3((rng.uniform() - 0.5f) / 255.0f)));  // dither: no banding in skies
      out.at(x, y) = {enc, 1};
    }
  });

  // 4. Sharpen (unsharp mask on the finished picture).
  if (L.sharpen > 0) {
    Image soft = out;
    blur(soft, 1.0f);
    for (size_t i = 0; i < out.px.size(); ++i) {
      Vec3 d = out.px[i].xyz() - soft.px[i].xyz();
      out.px[i] = {vmax(Vec3(0), vmin(Vec3(1), out.px[i].xyz() + d * (L.sharpen * 1.5f))), 1};
    }
  }
  return out;
}

}  // namespace loop
