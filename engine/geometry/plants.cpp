#include "engine/geometry/plants.h"

#include "engine/core/random.h"
#include "engine/geometry/noise.h"

namespace loop {

namespace {
struct Grower {
  const TreeShape& s;
  Rng rng;
  TreeMeshes out;

  // Every detail grows the full set of leaves (the same random numbers, so
  // the rest of the tree comes out the same) and keeps an even share of them,
  // bigger, so the crown stays as full: the same leaf area with fewer cards.
  void leafCluster(Vec3 at, Vec3 dir, float scale) {
    const int full = std::max(2, s.leavesPerTip);
    const float share = s.leafShare > 0 ? std::min(s.leafShare, 1.0f) : std::sqrt(std::min(s.detail, 1.0f));
    const int keep = std::min(full, std::max(1, static_cast<int>(std::lround(s.leavesPerTip * share))));
    const float grow = std::sqrt(static_cast<float>(full) / static_cast<float>(keep));
    for (int i = 0; i < full; ++i) {
      Vec3 off = uniformSphere(rng.uniform2()) * (s.leafSize * 3 * scale);
      Vec3 c = at + off + dir * (s.leafSize * rng.uniform());
      Vec3 n = normalize(off + Vec3(0, 0.6f, 0));
      Vec3 t, b;
      orthonormalBasis(n, t, b);
      float a = rng.uniform() * kTwoPi;
      Vec3 u = t * std::cos(a) + b * std::sin(a), v = cross(n, u);
      float w = s.leafSize * (0.7f + 0.6f * rng.uniform()) * grow, h = w * 1.6f;
      if ((i + 1) * keep / full == i * keep / full) continue;  // not one of the kept share
      uint32_t base = static_cast<uint32_t>(out.leaves.positions.size());
      out.leaves.addVertex(c - u * (w / 2), n, {0, 1});
      out.leaves.addVertex(c + u * (w / 2), n, {1, 1});
      out.leaves.addVertex(c + u * (w / 2) + v * h, n, {1, 0});
      out.leaves.addVertex(c - u * (w / 2) + v * h, n, {0, 0});
      out.leaves.addQuad(base, base + 1, base + 2, base + 3);
    }
  }

  // The branch's path is always grown in full (the same random numbers at
  // every detail, so the same tree); lower detail draws its tube through
  // fewer of the path's points, with fewer sides.
  void branch(Vec3 start, Vec3 dir, float len, float radius, int level) {
    const int full = std::max(2, 8 - level);
    std::vector<Vec3> path = {start};
    std::vector<float> radii = {radius};
    Vec3 p = start, d = dir;
    for (int i = 1; i <= full; ++i) {
      d = normalize(d + uniformSphere(rng.uniform2()) * (s.gnarl * 0.3f) + Vec3(0, s.upward * 0.15f, 0));
      p = p + d * (len / full);
      path.push_back(p);
      radii.push_back(radius * (1 - 0.7f * i / full));
    }
    const int steps = std::min(full, std::max(2, static_cast<int>(std::lround((8 - level) * std::min(s.detail, 1.0f)))));
    const int sides = std::max(3, static_cast<int>(std::lround((12 - level * 2) * std::min(s.detail, 1.0f))));
    if (level > s.barkLevels) {
      // too thin to see from where this version is drawn: no bark, the leaves still grow
    } else if (steps == full) {
      out.bark.append(shapes::tube(path, radii, sides, false));
    } else {
      std::vector<Vec3> tp;
      std::vector<float> tr;
      for (int i = 0; i <= steps; ++i) {
        size_t k = static_cast<size_t>(std::lround(static_cast<double>(i) * full / steps));
        tp.push_back(path[k]), tr.push_back(radii[k]);
      }
      out.bark.append(shapes::tube(tp, tr, sides, false));
    }
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

namespace {
// a card (two triangles, uv 0..1) from `base` along `along`, `width` wide, facing `n`
void card(Mesh& m, Vec3 base, Vec3 along, Vec3 side, Vec3 n, float width) {
  uint32_t b = static_cast<uint32_t>(m.positions.size());
  m.addVertex(base - side * (width / 2), n, {0, 1});
  m.addVertex(base + side * (width / 2), n, {1, 1});
  m.addVertex(base + side * (width / 2) + along, n, {1, 0});
  m.addVertex(base - side * (width / 2) + along, n, {0, 0});
  m.addQuad(b, b + 1, b + 2, b + 3);
}
}  // namespace

Mesh makeFern(float size, int fronds, uint32_t seed) {
  Mesh m;
  Rng rng(seed);
  for (int f = 0; f < fronds; ++f) {
    float az = kTwoPi * f / fronds + rng.uniform() * 0.4f;
    float len = size * (0.45f + 0.25f * rng.uniform());
    float rise = 0.9f + 0.4f * rng.uniform();  // how high it arches before falling
    Vec3 out{std::cos(az), 0, std::sin(az)}, side{-out.z, 0, out.x};
    const int seg = 12;
    std::vector<Vec3> path;
    for (int i = 0; i <= seg; ++i) {
      float t = static_cast<float>(i) / seg;
      // an arch: up and out, then curling down at the tip
      path.push_back(out * (len * t) + Vec3(0, len * rise * (t * 0.9f - t * t * 0.75f), 0));
    }
    std::vector<float> radii(path.size());
    for (size_t i = 0; i < path.size(); ++i) radii[i] = size * 0.006f * (1 - 0.8f * i / path.size());
    m.append(shapes::tube(path, radii, 4, false));
    // leaflets in pairs along the stem, longest in the middle
    for (int i = 1; i < seg; ++i) {
      float t = static_cast<float>(i) / seg;
      Vec3 at = path[i], dir = normalize(path[i + 1] - path[i - 1]);
      float l = len * 0.28f * std::sin(kPi * std::min(1.0f, t * 1.1f)) * (0.85f + 0.3f * rng.uniform());
      if (l < 0.01f) continue;
      for (int sgn : {-1, 1}) {
        Vec3 sd = normalize(side * static_cast<float>(sgn) + dir * 0.35f - Vec3(0, 0.15f, 0));
        Vec3 n = normalize(cross(sd, dir) * static_cast<float>(sgn) + Vec3(0, 0.5f, 0));
        card(m, at, sd * l, dir, n, l * 0.42f);
      }
    }
  }
  return m;
}

FlowerMeshes makeFlower(const FlowerShape& s) {
  FlowerMeshes o;
  Rng rng(s.seed);
  // the stem leans and bends a little
  std::vector<Vec3> path;
  Vec3 lean{rng.uniform() - 0.5f, 0, rng.uniform() - 0.5f};
  for (int i = 0; i <= 6; ++i) {
    float t = i / 6.0f;
    path.push_back(Vec3(0, s.height * t, 0) + lean * (s.height * 0.25f * t * t));
  }
  std::vector<float> radii(path.size(), s.height * 0.012f + 0.0015f);
  o.stem = shapes::tube(path, radii, 5, false);
  // two small leaves low on the stem
  for (int k = 0; k < 2; ++k) {
    float a = kTwoPi * (k * 0.5f + rng.uniform() * 0.2f);
    Vec3 d{std::cos(a), 0.5f, std::sin(a)};
    d = normalize(d);
    Vec3 side = normalize(cross(d, Vec3(0, 1, 0)));
    o.stem.append([&] { Mesh c; card(c, path[1], d * (s.height * 0.3f), side, normalize(cross(side, d)), s.height * 0.08f); return c; }());
  }
  Vec3 head = path.back(), up = normalize(path.back() - path[path.size() - 2]);
  Vec3 t, b;
  orthonormalBasis(up, t, b);
  for (int i = 0; i < s.petals; ++i) {
    float a = kTwoPi * i / s.petals + rng.uniform() * 0.15f;
    Vec3 radial = t * std::cos(a) + b * std::sin(a);
    // cup: petals tilt up from flat (daisy) to closed (bell)
    Vec3 dir = normalize(radial * (1 - s.cup) + up * (s.cup * 1.4f + 0.08f));
    Vec3 side = normalize(cross(up, radial));
    Vec3 n = normalize(cross(side, dir));
    card(o.petals, head + radial * (s.headSize * 0.25f), dir * s.headSize, side, n, s.headSize * 0.55f);
  }
  o.centre = shapes::icosphere(s.headSize * 0.32f, 1);
  o.centre.transform(Mat4::translation(head + up * (s.headSize * 0.05f)));
  return o;
}

Mesh makeColumn(const ColumnShape& s) {
  Mesh m;
  Rng rng(s.seed);
  const int seg = s.flutes * 4, rings = 16;
  float shaftR = s.radius;
  float baseH = s.radius * 0.6f, capH = s.broken ? 0 : s.radius * 0.6f;
  float shaftH = s.height - baseH - capH;
  // a broken top: snapped at a slant, ragged
  float breakTilt = s.broken ? s.radius * (0.6f + 0.8f * rng.uniform()) : 0;
  float phase = rng.uniform() * kTwoPi;
  // `height` is how tall it stood; broken, it snapped somewhere up its shaft
  const float snap = shaftH * (0.6f + 0.25f * rng.uniform());
  auto topAt = [&](float a) {
    if (!s.broken) return shaftH;
    float rag = 0.12f * s.radius * (std::sin(a * 7 + phase) + 0.5f * std::sin(a * 13 + phase * 2));
    return snap - breakTilt * (0.5f + 0.5f * std::sin(a + phase)) + rag;
  };
  auto rAt = [&](float a, float t) {
    float fl = 0.5f - 0.5f * std::cos(a * s.flutes);  // 0 at the ridge, 1 in the flute
    float taper = 1 - 0.12f * t;                     // entasis: a little thinner up top
    return shaftR * taper * (1 - s.fluteDepth * fl * fl);
  };
  // the shaft
  uint32_t first = static_cast<uint32_t>(m.positions.size());
  for (int j = 0; j <= rings; ++j)
    for (int i = 0; i <= seg; ++i) {
      float a = kTwoPi * i / seg, t = static_cast<float>(j) / rings;
      float y = baseH + topAt(a) * t, r = rAt(a, y / s.height);
      m.addVertex({r * std::cos(a), y, -r * std::sin(a)}, {std::cos(a), 0, -std::sin(a)}, {static_cast<float>(i) / seg, t});
    }
  for (int j = 0; j < rings; ++j)
    for (int i = 0; i < seg; ++i) {
      uint32_t a = first + j * (seg + 1) + i, c = a + seg + 1;
      m.addQuad(a, a + 1, c + 1, c);
    }
  // the top: a capital, or a rough broken face
  uint32_t ctr = m.addVertex({0, baseH + (s.broken ? snap - breakTilt * 0.5f : shaftH), 0}, {0, 1, 0}, {0.5f, 0.5f});
  for (int i = 0; i <= seg; ++i) {
    float a = kTwoPi * i / seg, y = baseH + topAt(a), r = rAt(a, y / s.height);
    m.addVertex({r * std::cos(a), y, -r * std::sin(a)}, {0, 1, 0}, {0.5f + 0.5f * std::cos(a), 0.5f - 0.5f * std::sin(a)});
  }
  for (int i = 0; i < seg; ++i) m.addTriangle(ctr, ctr + 1 + i, ctr + 2 + i);
  // base (a plinth and a torus-like moulding) and capital (moulding and a square abacus)
  Mesh plinth = shapes::roundedBox({shaftR * 2.6f, baseH * 0.55f, shaftR * 2.6f}, shaftR * 0.05f, 2);
  plinth.transform(Mat4::translation({0, baseH * 0.275f, 0}));
  m.append(plinth);
  Mesh moulding = shapes::lathe({{shaftR * 1.22f, baseH * 0.55f}, {shaftR * 1.25f, baseH * 0.7f}, {shaftR * 1.12f, baseH * 0.9f}, {shaftR * 1.0f, baseH}}, seg);
  m.append(moulding);
  if (!s.broken) {
    float y0 = baseH + shaftH;
    m.append(shapes::lathe({{shaftR * 0.88f, y0}, {shaftR * 1.12f, y0 + capH * 0.25f}, {shaftR * 1.3f, y0 + capH * 0.45f}}, seg));
    Mesh abacus = shapes::roundedBox({shaftR * 2.7f, capH * 0.55f, shaftR * 2.7f}, shaftR * 0.04f, 2);
    abacus.transform(Mat4::translation({0, y0 + capH * 0.725f, 0}));
    m.append(abacus);
  }
  m.computeNormals();
  return m;
}

LogMeshes makeLog(float length, float radius, uint32_t seed) {
  LogMeshes o;
  Rng rng(seed);
  noise::Fractal f;
  f.octaves = 3;
  f.seed = seed;
  std::vector<Vec3> path;
  std::vector<float> radii;
  const int seg = 16;
  for (int i = 0; i <= seg; ++i) {
    float t = static_cast<float>(i) / seg;
    path.push_back({length * (t - 0.5f), radius + radius * 0.25f * std::sin(t * kPi), radius * 0.3f * std::sin(t * 3.1f + seed)});
    radii.push_back(radius * (1 - 0.15f * t) * (0.9f + 0.2f * noise::fbm(Vec3(t * 4, 0, 0), f)));
  }
  o.bark = shapes::tube(path, radii, 14, false);
  // knobbly bark
  o.bark.displace([&](Vec3 p, Vec3) { return radius * 0.06f * noise::fbm(p * (6 / std::max(radius, 0.01f)), f); });
  o.bark.computeNormals();
  for (int e : {0, seg}) {
    Vec3 dir = normalize(e == 0 ? path[0] - path[1] : path[seg] - path[seg - 1]);
    Mesh d = shapes::cylinder(radii[e] * 0.98f, 0.01f, 14, true);
    // the cylinder stands on Y: turn it to face along the log
    Mat4 xf = Mat4::trs(path[e], Quat::fromTo({0, 1, 0}, dir), Vec3(1));
    o.ends.append(d, xf);
  }
  return o;
}

Mesh makeWing(float span) {
  Mesh m;
  // a rounded triangle-ish card: wide at the outer edge, narrow at the hinge
  card(m, {0, 0, 0}, {span, 0, 0}, {0, 0, 1}, {0, 1, 0}, span * 1.3f);
  // and its back face, so it shows from below
  card(m, {0, 0, 0}, {span, 0, 0}, {0, 0, -1}, {0, -1, 0}, span * 1.3f);
  return m;
}

Mesh makeMote(float radius) { return shapes::icosphere(radius, 1); }

}  // namespace loop
