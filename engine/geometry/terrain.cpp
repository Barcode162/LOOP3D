#include "engine/geometry/terrain.h"

#include "engine/core/random.h"
#include "engine/geometry/noise.h"

namespace loop {

float HeightMap::height(float x, float z) const {
  if (res_ < 2) return 0;
  float gx = (x / size_ + 0.5f) * (res_ - 1), gz = (z / size_ + 0.5f) * (res_ - 1);
  int x0 = static_cast<int>(std::floor(gx)), z0 = static_cast<int>(std::floor(gz));
  float tx = gx - x0, tz = gz - z0;
  return lerp(lerp(at(x0, z0), at(x0 + 1, z0), tx), lerp(at(x0, z0 + 1), at(x0 + 1, z0 + 1), tx), tz);
}

Vec3 HeightMap::normal(float x, float z) const {
  float e = size_ / (res_ - 1);
  float hx = height(x + e, z) - height(x - e, z), hz = height(x, z + e) - height(x, z - e);
  return normalize(Vec3{-hx, 2 * e, -hz});
}

void HeightMap::generate(const Shape& s) {
  noise::Fractal f;
  f.octaves = 9;
  f.seed = s.seed;
  for (int z = 0; z < res_; ++z)
    for (int x = 0; x < res_; ++x) {
      Vec2 p{(static_cast<float>(x) / (res_ - 1) - 0.5f) * size_ / s.featureSize, (static_cast<float>(z) / (res_ - 1) - 0.5f) * size_ / s.featureSize};
      float hills = noise::warped(p, s.warp * 2, f) * 0.5f + 0.5f;
      float ridge = noise::ridged(p * 0.7f + Vec2(3.3f, 7.1f), f);
      float h = lerp(hills, ridge, s.ridges);
      // shape the curve so valleys flatten and peaks sharpen
      h = std::pow(saturate(h), 1.6f);
      at(x, z) = h * s.height;
    }
}

Vec2 HeightMap::gradientAt(float gx, float gz, float& h) const {
  int x0 = static_cast<int>(gx), z0 = static_cast<int>(gz);
  float u = gx - x0, v = gz - z0;
  float a = at(x0, z0), b = at(x0 + 1, z0), c = at(x0, z0 + 1), d = at(x0 + 1, z0 + 1);
  h = a * (1 - u) * (1 - v) + b * u * (1 - v) + c * (1 - u) * v + d * u * v;
  return {(b - a) * (1 - v) + (d - c) * v, (c - a) * (1 - u) + (d - b) * u};
}

// Droplet erosion after Hans Theobald Beyer (2015).
void HeightMap::erodeHydraulic(const Erosion& e) {
  if (res_ < 4) return;
  // Brush weights for spreading erosion over a radius.
  std::vector<std::pair<int, int>> offs;
  std::vector<float> wts;
  float wsum = 0;
  for (int dz = -e.radius; dz <= e.radius; ++dz)
    for (int dx = -e.radius; dx <= e.radius; ++dx) {
      float d = std::sqrt(static_cast<float>(dx * dx + dz * dz));
      if (d > e.radius) continue;
      offs.push_back({dx, dz});
      wts.push_back(1 - d / e.radius);
      wsum += wts.back();
    }
  for (float& w : wts) w /= wsum;
  float cell = size_ / (res_ - 1);
  Rng rng(e.seed);
  for (int n = 0; n < e.droplets; ++n) {
    float px = rng.range(0, res_ - 2.001f), pz = rng.range(0, res_ - 2.001f);
    float dx = 0, dz = 0, speed = 1, water = 1, sediment = 0;
    for (int life = 0; life < e.lifetime; ++life) {
      int nx = static_cast<int>(px), nz = static_cast<int>(pz);
      float u = px - nx, v = pz - nz;
      float h;
      Vec2 g = gradientAt(px, pz, h);
      dx = dx * e.inertia - g.x * (1 - e.inertia);
      dz = dz * e.inertia - g.y * (1 - e.inertia);
      float len = std::sqrt(dx * dx + dz * dz);
      if (len < 1e-8f) break;
      dx /= len, dz /= len;
      px += dx, pz += dz;
      if (px < 0 || pz < 0 || px >= res_ - 2 || pz >= res_ - 2) break;
      float nh;
      gradientAt(px, pz, nh);
      float dh = nh - h;
      float cap = std::max(-dh / cell, 0.01f) * speed * water * e.capacity * cell;
      if (sediment > cap || dh > 0) {
        float dep = dh > 0 ? std::min(dh, sediment) : (sediment - cap) * e.deposition;
        sediment -= dep;
        at(nx, nz) += dep * (1 - u) * (1 - v);
        at(nx + 1, nz) += dep * u * (1 - v);
        at(nx, nz + 1) += dep * (1 - u) * v;
        at(nx + 1, nz + 1) += dep * u * v;
      } else {
        float ero = std::min((cap - sediment) * e.erosion, -dh);
        for (size_t k = 0; k < offs.size(); ++k) {
          int ex = nx + offs[k].first, ez = nz + offs[k].second;
          if (ex < 0 || ez < 0 || ex >= res_ || ez >= res_) continue;
          float take = std::min(at(ex, ez), ero * wts[k]);
          at(ex, ez) -= take;
          sediment += take;
        }
      }
      speed = std::sqrt(std::max(0.0f, speed * speed - dh / cell * e.gravity));
      water *= 1 - e.evaporation;
    }
  }
}

void HeightMap::erodeThermal(int iterations, float talusDeg, float amount) {
  float cell = size_ / (res_ - 1);
  float talus = std::tan(radians(talusDeg)) * cell;
  std::vector<float> delta(h_.size());
  const int ox[4] = {1, -1, 0, 0}, oz[4] = {0, 0, 1, -1};
  for (int it = 0; it < iterations; ++it) {
    std::fill(delta.begin(), delta.end(), 0.0f);
    for (int z = 1; z < res_ - 1; ++z)
      for (int x = 1; x < res_ - 1; ++x) {
        float h = at(x, z), maxd = 0, total = 0;
        float d[4];
        for (int k = 0; k < 4; ++k) {
          d[k] = h - at(x + ox[k], z + oz[k]);
          if (d[k] > talus) total += d[k], maxd = std::max(maxd, d[k]);
        }
        if (total <= 0) continue;
        float move = amount * (maxd - talus) * 0.5f;
        for (int k = 0; k < 4; ++k)
          if (d[k] > talus) {
            float m = move * d[k] / total;
            delta[static_cast<size_t>(z + oz[k]) * res_ + x + ox[k]] += m;
            delta[static_cast<size_t>(z) * res_ + x] -= m;
          }
      }
    for (size_t i = 0; i < h_.size(); ++i) h_[i] += delta[i];
  }
}

Mesh HeightMap::toMesh(int step) const {
  step = std::max(1, step);
  int n = (res_ - 1) / step;
  Mesh m = shapes::heightfield({size_, size_}, n, n, [&](float x, float z) { return height(x, z); });
  for (size_t i = 0; i < m.positions.size(); ++i) m.normals[i] = normal(m.positions[i].x, m.positions[i].z);
  return m;
}

float HeightMap::minHeight() const { return h_.empty() ? 0 : *std::min_element(h_.begin(), h_.end()); }
float HeightMap::maxHeight() const { return h_.empty() ? 0 : *std::max_element(h_.begin(), h_.end()); }

std::vector<Vec2> poissonDisk(float size, float r, uint32_t seed, const std::function<bool(float, float)>& accept, int tries) {
  std::vector<Vec2> pts;
  if (r <= 0 || size <= 0) return pts;
  float cell = r / std::sqrt(2.0f);
  int gw = static_cast<int>(std::ceil(size / cell));
  std::vector<int> grid(static_cast<size_t>(gw) * gw, -1);
  std::vector<int> active;
  Rng rng(seed);
  auto gi = [&](Vec2 p) { return std::pair<int, int>{clamp(static_cast<int>(p.x / cell), 0, gw - 1), clamp(static_cast<int>(p.y / cell), 0, gw - 1)}; };
  auto fits = [&](Vec2 p) {
    if (p.x < 0 || p.y < 0 || p.x >= size || p.y >= size) return false;
    auto [cx, cy] = gi(p);
    for (int y = std::max(0, cy - 2); y <= std::min(gw - 1, cy + 2); ++y)
      for (int x = std::max(0, cx - 2); x <= std::min(gw - 1, cx + 2); ++x) {
        int k = grid[static_cast<size_t>(y) * gw + x];
        if (k >= 0 && lengthSq(Vec3(pts[k].x - p.x, pts[k].y - p.y, 0)) < r * r) return false;
      }
    return true;
  };
  auto add = [&](Vec2 p) {
    pts.push_back(p);
    auto [cx, cy] = gi(p);
    grid[static_cast<size_t>(cy) * gw + cx] = static_cast<int>(pts.size() - 1);
    active.push_back(static_cast<int>(pts.size() - 1));
  };
  add({rng.uniform() * size, rng.uniform() * size});
  while (!active.empty()) {
    int ai = rng.below(static_cast<int>(active.size()));
    Vec2 c = pts[active[ai]];
    bool found = false;
    for (int t = 0; t < tries; ++t) {
      float a = rng.uniform() * kTwoPi, d = r * (1 + rng.uniform());
      Vec2 p{c.x + std::cos(a) * d, c.y + std::sin(a) * d};
      if (fits(p)) { add(p); found = true; break; }
    }
    if (!found) active[ai] = active.back(), active.pop_back();
  }
  std::vector<Vec2> out;
  for (Vec2 p : pts) {
    Vec2 q{p.x - size / 2, p.y - size / 2};
    if (!accept || accept(q.x, q.y)) out.push_back(q);
  }
  return out;
}

}  // namespace loop
