#include "engine/geometry/plants.h"

#include "engine/core/random.h"
#include "engine/geometry/noise.h"

namespace loop {

namespace {
struct Grower {
  const TreeShape& s;
  Rng rng;
  TreeMeshes out;

  void leafCluster(Vec3 at, Vec3 dir, float scale) {
    for (int i = 0; i < s.leavesPerTip; ++i) {
      Vec3 off = uniformSphere(rng.uniform2()) * (s.leafSize * 3 * scale);
      Vec3 c = at + off + dir * (s.leafSize * rng.uniform());
      Vec3 n = normalize(off + Vec3(0, 0.6f, 0));
      Vec3 t, b;
      orthonormalBasis(n, t, b);
      float a = rng.uniform() * kTwoPi;
      Vec3 u = t * std::cos(a) + b * std::sin(a), v = cross(n, u);
      float w = s.leafSize * (0.7f + 0.6f * rng.uniform()), h = w * 1.6f;
      uint32_t base = static_cast<uint32_t>(out.leaves.positions.size());
      out.leaves.addVertex(c - u * (w / 2), n, {0, 1});
      out.leaves.addVertex(c + u * (w / 2), n, {1, 1});
      out.leaves.addVertex(c + u * (w / 2) + v * h, n, {1, 0});
      out.leaves.addVertex(c - u * (w / 2) + v * h, n, {0, 0});
      out.leaves.addQuad(base, base + 1, base + 2, base + 3);
    }
  }

  void branch(Vec3 start, Vec3 dir, float len, float radius, int level) {
    const int steps = std::max(3, 8 - level);
    std::vector<Vec3> path = {start};
    std::vector<float> radii = {radius};
    Vec3 p = start, d = dir;
    for (int i = 1; i <= steps; ++i) {
      d = normalize(d + uniformSphere(rng.uniform2()) * (s.gnarl * 0.3f) + Vec3(0, s.upward * 0.15f, 0));
      p = p + d * (len / steps);
      path.push_back(p);
      radii.push_back(radius * (1 - 0.7f * i / steps));
    }
    out.bark.append(shapes::tube(path, radii, std::max(5, 12 - level * 2), false));
    if (level >= s.levels) {
      leafCluster(p, d, 1.0f);
      return;
    }
    int n = s.conifer && level == 0 ? s.branches * 3 : s.branches;
    for (int k = 0; k < n; ++k) {
      float t = s.conifer && level == 0 ? 0.25f + 0.73f * k / n : 0.35f + 0.6f * rng.uniform();
      size_t idx = std::min(path.size() - 2, static_cast<size_t>(t * (path.size() - 1)));
      float f = t * (path.size() - 1) - idx;
      Vec3 at = vlerp(path[idx], path[idx + 1], f);
      Vec3 pd = normalize(path[idx + 1] - path[idx]);
      Vec3 tt, bb;
      orthonormalBasis(pd, tt, bb);
      float az = kTwoPi * (k * 0.618034f) + rng.uniform() * 0.5f;
      float spread = s.conifer && level == 0 ? 1.35f : s.spread;
      Vec3 cd = normalize(pd * std::cos(spread) + (tt * std::cos(az) + bb * std::sin(az)) * std::sin(spread));
      float cl = len * s.lengthFalloff * (0.8f + 0.4f * rng.uniform());
      if (s.conifer && level == 0) cl = len * 0.42f * (1.05f - t);
      branch(at, cd, cl, radius * s.radiusFalloff * radii[idx] / radius, level + 1);
    }
  }
};
}  // namespace

TreeMeshes growTree(const TreeShape& s) {
  Grower g{s, Rng(s.seed), {}};
  float trunk = s.conifer ? s.height : s.height * 0.45f;
  g.branch({0, 0, 0}, {0, 1, 0}, trunk, s.trunkRadius, 0);
  g.out.bark.computeNormals();
  return g.out;
}

Mesh makeRock(const RockShape& s) {
  Mesh m = shapes::icosphere(s.size * 0.5f, 4);
  noise::Fractal f;
  f.octaves = 5;
  f.seed = s.seed;
  Rng rng(s.seed);
  Vec3 squash{1 + 0.3f * rng.uniform(), s.flatten + 0.2f * rng.uniform(), 1 + 0.3f * rng.uniform()};
  // A few big flat facets (cut planes) make it read as stone, not a blob.
  std::vector<Plane> cuts;
  for (int i = 0; i < 6; ++i) {
    Vec3 n = uniformSphere(rng.uniform2());
    cuts.push_back({n, -s.size * 0.5f * (0.62f + 0.25f * rng.uniform())});
  }
  for (size_t i = 0; i < m.positions.size(); ++i) {
    Vec3 p = m.positions[i] * squash;
    float n = noise::fbm(p * (2.5f / s.size), f);
    p += normalize(p) * (n * s.roughness * s.size * 0.5f);
    for (const Plane& c : cuts) {
      float d = c.distance(p);
      if (d > 0) p -= c.n * d * 0.85f;
    }
    m.positions[i] = p;
  }
  float minY = m.bounds().lo.y;
  for (auto& p : m.positions) p.y -= minY * 0.8f;
  m.computeNormals();
  return m;
}

Mesh makeGrassTuft(int blades, float height, uint32_t seed) {
  Mesh m;
  Rng rng(seed);
  for (int b = 0; b < blades; ++b) {
    float a = rng.uniform() * kTwoPi, r = 0.08f * std::sqrt(rng.uniform());
    Vec3 root{std::cos(a) * r, 0, std::sin(a) * r};
    float h = height * (0.6f + 0.6f * rng.uniform()), w = 0.012f + 0.01f * rng.uniform();
    float face = rng.uniform() * kTwoPi, lean = 0.2f + 0.5f * rng.uniform();
    Vec3 side{std::cos(face), 0, std::sin(face)}, out{-side.z, 0, side.x};
    const int seg = 4;
    uint32_t base = static_cast<uint32_t>(m.positions.size());
    for (int i = 0; i <= seg; ++i) {
      float t = static_cast<float>(i) / seg;
      Vec3 c = root + Vec3(0, h * t, 0) + out * (lean * h * t * t);
      float ww = w * (1 - t);
      Vec3 n = normalize(cross(side, Vec3(0, 1, 0) + out * (2 * lean * t)));
      m.addVertex(c - side * ww, n, {0, 1 - t});
      m.addVertex(c + side * ww, n, {1, 1 - t});
    }
    for (int i = 0; i < seg; ++i) m.addQuad(base + i * 2, base + i * 2 + 1, base + i * 2 + 3, base + i * 2 + 2);
  }
  return m;
}

}  // namespace loop
