#include "engine/geometry/mesh.h"

#include <unordered_map>

namespace loop {

AABB Mesh::bounds() const {
  AABB b;
  for (const Vec3& p : positions) b.grow(p);
  return b;
}

uint32_t Mesh::addVertex(Vec3 p, Vec3 n, Vec2 uv) {
  positions.push_back(p);
  normals.push_back(n);
  uvs.push_back(uv);
  return static_cast<uint32_t>(positions.size() - 1);
}

void Mesh::append(const Mesh& m, const Mat4& xf) {
  uint32_t base = static_cast<uint32_t>(positions.size());
  Mat3 nm = xf.linear().inverse().transposed();
  bool hasT = !m.tangents.empty() && tangents.size() == positions.size();
  for (size_t i = 0; i < m.positions.size(); ++i) {
    positions.push_back(xf.point(m.positions[i]));
    normals.push_back(i < m.normals.size() ? normalize(nm * m.normals[i]) : Vec3{0, 1, 0});
    uvs.push_back(i < m.uvs.size() ? m.uvs[i] : Vec2{});
    if (hasT) {
      Vec3 t = normalize(xf.vector(m.tangents[i].xyz()));
      tangents.push_back({t, m.tangents[i].w});
    }
  }
  if (!hasT) tangents.clear();
  for (uint32_t i : m.indices) indices.push_back(base + i);
}

void Mesh::transform(const Mat4& xf) {
  Mat3 nm = xf.linear().inverse().transposed();
  for (auto& p : positions) p = xf.point(p);
  for (auto& n : normals) n = normalize(nm * n);
  for (auto& t : tangents) t = {normalize(xf.vector(t.xyz())), t.w};
  if (xf.linear().determinant() < 0)
    for (size_t i = 0; i + 2 < indices.size(); i += 3) std::swap(indices[i + 1], indices[i + 2]);
}

void Mesh::computeNormals() {
  normals.assign(positions.size(), Vec3{});
  for (size_t i = 0; i + 2 < indices.size(); i += 3) {
    uint32_t a = indices[i], b = indices[i + 1], c = indices[i + 2];
    Vec3 n = cross(positions[b] - positions[a], positions[c] - positions[a]);
    normals[a] += n, normals[b] += n, normals[c] += n;
  }
  for (auto& n : normals) {
    n = normalize(n);
    if (lengthSq(n) == 0) n = {0, 1, 0};
  }
}

void Mesh::computeTangents() {
  std::vector<Vec3> t(positions.size()), b(positions.size());
  if (uvs.size() != positions.size()) uvs.resize(positions.size());
  for (size_t i = 0; i + 2 < indices.size(); i += 3) {
    uint32_t i0 = indices[i], i1 = indices[i + 1], i2 = indices[i + 2];
    Vec3 e1 = positions[i1] - positions[i0], e2 = positions[i2] - positions[i0];
    Vec2 d1 = uvs[i1] - uvs[i0], d2 = uvs[i2] - uvs[i0];
    float r = d1.x * d2.y - d2.x * d1.y;
    if (std::fabs(r) < 1e-12f) continue;
    r = 1 / r;
    Vec3 sd = (e1 * d2.y - e2 * d1.y) * r, td = (e2 * d1.x - e1 * d2.x) * r;
    for (uint32_t k : {i0, i1, i2}) t[k] += sd, b[k] += td;
  }
  tangents.resize(positions.size());
  for (size_t i = 0; i < positions.size(); ++i) {
    Vec3 n = i < normals.size() ? normals[i] : Vec3{0, 1, 0};
    Vec3 tt = t[i] - n * dot(n, t[i]);
    if (lengthSq(tt) < 1e-20f) {
      Vec3 bb;
      orthonormalBasis(n, tt, bb);
    }
    tt = normalize(tt);
    float w = dot(cross(n, tt), b[i]) < 0 ? -1.0f : 1.0f;
    tangents[i] = {tt, w};
  }
}

void Mesh::flatShade() {
  Mesh out;
  for (size_t i = 0; i + 2 < indices.size(); i += 3) {
    Vec3 a = positions[indices[i]], b = positions[indices[i + 1]], c = positions[indices[i + 2]];
    Vec3 n = normalize(cross(b - a, c - a));
    uint32_t base = static_cast<uint32_t>(out.positions.size());
    for (int k = 0; k < 3; ++k) {
      uint32_t s = indices[i + k];
      out.addVertex(positions[s], n, s < uvs.size() ? uvs[s] : Vec2{});
    }
    out.addTriangle(base, base + 1, base + 2);
  }
  *this = std::move(out);
}

void Mesh::displace(const std::function<float(Vec3, Vec3)>& f) {
  if (normals.size() != positions.size()) computeNormals();
  for (size_t i = 0; i < positions.size(); ++i) positions[i] += normals[i] * f(positions[i], normals[i]);
}

void Mesh::weld(float eps) {
  struct Key { int64_t x, y, z; bool operator==(const Key&) const = default; };
  struct KH { size_t operator()(const Key& k) const { return static_cast<size_t>(k.x * 73856093 ^ k.y * 19349663 ^ k.z * 83492791); } };
  std::unordered_map<Key, uint32_t, KH> seen;
  std::vector<uint32_t> remap(positions.size());
  Mesh out;
  for (size_t i = 0; i < positions.size(); ++i) {
    Key k{std::llround(positions[i].x / eps), std::llround(positions[i].y / eps), std::llround(positions[i].z / eps)};
    auto it = seen.find(k);
    if (it != seen.end()) { remap[i] = it->second; continue; }
    uint32_t v = out.addVertex(positions[i], i < normals.size() ? normals[i] : Vec3{}, i < uvs.size() ? uvs[i] : Vec2{});
    seen.emplace(k, v);
    remap[i] = v;
  }
  for (size_t i = 0; i + 2 < indices.size(); i += 3) {
    uint32_t a = remap[indices[i]], b = remap[indices[i + 1]], c = remap[indices[i + 2]];
    if (a != b && b != c && a != c) out.addTriangle(a, b, c);
  }
  *this = std::move(out);
}

float Mesh::surfaceArea() const {
  float s = 0;
  for (size_t i = 0; i + 2 < indices.size(); i += 3) {
    Vec3 a = positions[indices[i]], b = positions[indices[i + 1]], c = positions[indices[i + 2]];
    s += 0.5f * length(cross(b - a, c - a));
  }
  return s;
}

float Mesh::volume() const {
  float v = 0;
  for (size_t i = 0; i + 2 < indices.size(); i += 3)
    v += dot(positions[indices[i]], cross(positions[indices[i + 1]], positions[indices[i + 2]])) / 6.0f;
  return v;
}

// ------------------------------------------------------------------ shapes
namespace shapes {

Mesh box(Vec3 size) {
  Mesh m;
  Vec3 h = size * 0.5f;
  const Vec3 n[6] = {{1, 0, 0}, {-1, 0, 0}, {0, 1, 0}, {0, -1, 0}, {0, 0, 1}, {0, 0, -1}};
  for (int f = 0; f < 6; ++f) {
    Vec3 nn = n[f], u, v;
    // u, v such that u x v = n
    if (f < 2) u = {0, 0, -nn.x}, v = {0, 1, 0};
    else if (f < 4) u = {1, 0, 0}, v = {0, 0, -nn.y};
    else u = {nn.z, 0, 0}, v = {0, 1, 0};
    Vec3 c = nn * h;
    Vec3 du = u * dot(vabs(u), h), dv = v * dot(vabs(v), h);
    float su = 2 * dot(vabs(u), h), sv = 2 * dot(vabs(v), h);
    uint32_t a = m.addVertex(c - du - dv, nn, {0, sv});
    uint32_t b = m.addVertex(c + du - dv, nn, {su, sv});
    uint32_t cc = m.addVertex(c + du + dv, nn, {su, 0});
    uint32_t d = m.addVertex(c - du + dv, nn, {0, 0});
    m.addQuad(a, b, cc, d);
  }
  return m;
}

Mesh roundedBox(Vec3 size, float radius, int segments) {
  radius = std::min(radius, minComp(size) * 0.5f);
  if (radius <= 0) return box(size);
  // Each face is a grid projected onto the rounded box: points are clamped to the inner box and pushed
  // out by the radius along the offset. The grid lines sit only where they are needed: `steps` equal
  // angles across each rounded edge, and none across the flat middle, so a small rounding stays cheap.
  // The normals are exact, so even one step shades round.
  Vec3 inner = size * 0.5f - Vec3(radius);
  int steps = std::max(1, segments);
  // One axis' grid lines as offsets from the middle, in units of the half size H (-1 .. 1).
  auto lines = [&](float H, float I) {
    std::vector<float> l;
    for (int k = steps; k >= 1; --k) l.push_back(-(I + radius * std::tan(0.25f * kPi * k / steps)));
    l.push_back(-I);
    l.push_back(I);
    for (int k = 1; k <= steps; ++k) l.push_back(I + radius * std::tan(0.25f * kPi * k / steps));
    for (float& x : l) x = std::max(-1.0f, std::min(1.0f, x / H));
    return l;
  };
  Mesh m;
  const Vec3 n[6] = {{1, 0, 0}, {-1, 0, 0}, {0, 1, 0}, {0, -1, 0}, {0, 0, 1}, {0, 0, -1}};
  for (int f = 0; f < 6; ++f) {
    Vec3 nn = n[f], u, v;
    if (f < 2) u = {0, 0, -nn.x}, v = {0, 1, 0};
    else if (f < 4) u = {1, 0, 0}, v = {0, 0, -nn.y};
    else u = {nn.z, 0, 0}, v = {0, 1, 0};
    uint32_t base = static_cast<uint32_t>(m.positions.size());
    Vec3 h = size * 0.5f;
    float hu = dot(vabs(u), h), hv = dot(vabs(v), h);
    std::vector<float> lu = lines(hu, dot(vabs(u), inner)), lv = lines(hv, dot(vabs(v), inner));
    int nu = static_cast<int>(lu.size()), nv = static_cast<int>(lv.size());
    for (int j = 0; j < nv; ++j)
      for (int i = 0; i < nu; ++i) {
        float a = lu[i], b = lv[j];
        Vec3 p = nn * dot(vabs(nn), h) + u * (a * hu) + v * (b * hv);
        Vec3 c = vmin(vmax(p, -inner), inner);
        Vec3 d = normalize(p - c);
        if (lengthSq(d) == 0) d = nn;
        Vec3 q = c + d * radius;
        m.addVertex(q, d, {(a + 1) * 0.5f * dot(vabs(u), size), (1 - b) * 0.5f * dot(vabs(v), size)});
      }
    for (int j = 0; j + 1 < nv; ++j)
      for (int i = 0; i + 1 < nu; ++i) {
        uint32_t a = base + j * nu + i;
        m.addQuad(a, a + 1, a + nu + 1, a + nu);
      }
  }
  return m;
}

Mesh sphere(float r, int seg, int rings) {
  Mesh m;
  for (int j = 0; j <= rings; ++j) {
    float v = static_cast<float>(j) / rings, th = v * kPi;
    for (int i = 0; i <= seg; ++i) {
      float u = static_cast<float>(i) / seg, ph = u * kTwoPi;
      Vec3 n{std::sin(th) * std::cos(ph), std::cos(th), -std::sin(th) * std::sin(ph)};
      m.addVertex(n * r, n, {u, v});
    }
  }
  for (int j = 0; j < rings; ++j)
    for (int i = 0; i < seg; ++i) {
      uint32_t a = j * (seg + 1) + i, b = a + seg + 1;
      if (j != 0) m.addTriangle(a, b, a + 1);
      if (j != rings - 1) m.addTriangle(a + 1, b, b + 1);
    }
  return m;
}

Mesh icosphere(float r, int sub) {
  const float t = (1 + std::sqrt(5.0f)) / 2;
  std::vector<Vec3> v = {{-1, t, 0}, {1, t, 0}, {-1, -t, 0}, {1, -t, 0}, {0, -1, t}, {0, 1, t}, {0, -1, -t}, {0, 1, -t}, {t, 0, -1}, {t, 0, 1}, {-t, 0, -1}, {-t, 0, 1}};
  for (auto& p : v) p = normalize(p);
  std::vector<uint32_t> f = {0, 11, 5, 0, 5, 1, 0, 1, 7, 0, 7, 10, 0, 10, 11, 1, 5, 9, 5, 11, 4, 11, 10, 2, 10, 7, 6, 7, 1, 8,
                             3, 9, 4, 3, 4, 2, 3, 2, 6, 3, 6, 8, 3, 8, 9, 4, 9, 5, 2, 4, 11, 6, 2, 10, 8, 6, 7, 9, 8, 1};
  for (int s = 0; s < sub; ++s) {
    std::unordered_map<uint64_t, uint32_t> mid;
    auto midpoint = [&](uint32_t a, uint32_t b) {
      uint64_t k = a < b ? (uint64_t(a) << 32) | b : (uint64_t(b) << 32) | a;
      auto it = mid.find(k);
      if (it != mid.end()) return it->second;
      v.push_back(normalize(v[a] + v[b]));
      return mid[k] = static_cast<uint32_t>(v.size() - 1);
    };
    std::vector<uint32_t> nf;
    for (size_t i = 0; i < f.size(); i += 3) {
      uint32_t a = midpoint(f[i], f[i + 1]), b = midpoint(f[i + 1], f[i + 2]), c = midpoint(f[i + 2], f[i]);
      nf.insert(nf.end(), {f[i], a, c, f[i + 1], b, a, f[i + 2], c, b, a, b, c});
    }
    f.swap(nf);
  }
  Mesh m;
  for (auto& p : v) m.addVertex(p * r, p, {0.5f + std::atan2(p.z, p.x) / kTwoPi, std::acos(clamp(p.y, -1.0f, 1.0f)) / kPi});
  m.indices = f;
  return m;
}

Mesh lathe(const std::vector<Vec2>& prof, int seg) {
  Mesh m;
  if (prof.size() < 2) return m;
  std::vector<float> len(prof.size(), 0);
  for (size_t i = 1; i < prof.size(); ++i) len[i] = len[i - 1] + length(prof[i] - prof[i - 1]);
  for (size_t j = 0; j < prof.size(); ++j) {
    Vec2 d = j + 1 < prof.size() ? prof[j + 1] - prof[j] : prof[j] - prof[j - 1];
    if (j > 0 && j + 1 < prof.size()) d = prof[j + 1] - prof[j - 1];
    Vec2 n2 = normalize(Vec2{d.y, -d.x});  // outward for a profile going up
    for (int i = 0; i <= seg; ++i) {
      float u = static_cast<float>(i) / seg, a = u * kTwoPi;
      float c = std::cos(a), s = std::sin(a);
      m.addVertex({prof[j].x * c, prof[j].y, -prof[j].x * s}, normalize(Vec3{n2.x * c, n2.y, -n2.x * s}), {u, len[j] / std::max(len.back(), 1e-6f)});
    }
  }
  for (size_t j = 0; j + 1 < prof.size(); ++j)
    for (int i = 0; i < seg; ++i) {
      uint32_t a = static_cast<uint32_t>(j * (seg + 1) + i), b = a + seg + 1;
      m.addQuad(a, a + 1, b + 1, b);
    }
  return m;
}

static void disk(Mesh& m, float r, float y, int seg, bool up) {
  Vec3 n{0, up ? 1.0f : -1.0f, 0};
  uint32_t c = m.addVertex({0, y, 0}, n, {0.5f, 0.5f});
  for (int i = 0; i <= seg; ++i) {
    float a = kTwoPi * i / seg;
    m.addVertex({r * std::cos(a), y, -r * std::sin(a)}, n, {0.5f + 0.5f * std::cos(a), 0.5f - 0.5f * std::sin(a)});
  }
  for (int i = 0; i < seg; ++i) {
    if (up) m.addTriangle(c, c + 1 + i, c + 2 + i);
    else m.addTriangle(c, c + 2 + i, c + 1 + i);
  }
}

Mesh cylinder(float r, float h, int seg, bool caps) {
  Mesh m = lathe({{r, -h / 2}, {r, h / 2}}, seg);
  if (caps) disk(m, r, h / 2, seg, true), disk(m, r, -h / 2, seg, false);
  return m;
}

Mesh cone(float r, float h, int seg) {
  Mesh m;
  float slope = r / h;
  for (int i = 0; i < seg; ++i) {
    float a0 = kTwoPi * i / seg, a1 = kTwoPi * (i + 1) / seg, am = (a0 + a1) / 2;
    auto nrm = [&](float a) { return normalize(Vec3{std::cos(a), slope, -std::sin(a)}); };
    uint32_t t = m.addVertex({0, h / 2, 0}, nrm(am), {0.5f, 0});
    uint32_t p0 = m.addVertex({r * std::cos(a0), -h / 2, -r * std::sin(a0)}, nrm(a0), {static_cast<float>(i) / seg, 1});
    uint32_t p1 = m.addVertex({r * std::cos(a1), -h / 2, -r * std::sin(a1)}, nrm(a1), {static_cast<float>(i + 1) / seg, 1});
    m.addTriangle(t, p0, p1);
  }
  disk(m, r, -h / 2, seg, false);
  return m;
}

Mesh capsule(float r, float h, int seg, int rings) {
  std::vector<Vec2> prof;
  float half = std::max(0.0f, h / 2 - r);
  for (int i = 0; i <= rings; ++i) {
    float a = -kPi / 2 + (kPi / 2) * i / rings;
    prof.push_back({r * std::cos(a), -half + r * std::sin(a)});
  }
  for (int i = 0; i <= rings; ++i) {
    float a = (kPi / 2) * i / rings;
    prof.push_back({r * std::cos(a), half + r * std::sin(a)});
  }
  prof.front().x = 0, prof.back().x = 0;
  Mesh m = lathe(prof, seg);
  m.computeNormals();
  return m;
}

Mesh torus(float R, float r, int seg, int sides) {
  Mesh m;
  for (int j = 0; j <= sides; ++j) {
    float v = static_cast<float>(j) / sides, b = v * kTwoPi;
    for (int i = 0; i <= seg; ++i) {
      float u = static_cast<float>(i) / seg, a = u * kTwoPi;
      Vec3 c{R * std::cos(a), 0, -R * std::sin(a)};
      Vec3 n{std::cos(b) * std::cos(a), std::sin(b), -std::cos(b) * std::sin(a)};
      m.addVertex(c + n * r, n, {u, v});
    }
  }
  for (int j = 0; j < sides; ++j)
    for (int i = 0; i < seg; ++i) {
      uint32_t a = j * (seg + 1) + i, b = a + seg + 1;
      m.addQuad(a, a + 1, b + 1, b);
    }
  return m;
}

Mesh plane(Vec2 size, int dx, int dz) {
  return heightfield(size, dx, dz, [](float, float) { return 0.0f; });
}

Mesh heightfield(Vec2 size, int rx, int rz, const std::function<float(float, float)>& height) {
  Mesh m;
  rx = std::max(1, rx), rz = std::max(1, rz);
  for (int j = 0; j <= rz; ++j)
    for (int i = 0; i <= rx; ++i) {
      float x = (static_cast<float>(i) / rx - 0.5f) * size.x, z = (static_cast<float>(j) / rz - 0.5f) * size.y;
      m.addVertex({x, height(x, z), z}, {0, 1, 0}, {static_cast<float>(i) / rx * size.x, static_cast<float>(j) / rz * size.y});
    }
  for (int j = 0; j < rz; ++j)
    for (int i = 0; i < rx; ++i) {
      uint32_t a = j * (rx + 1) + i, b = a + rx + 1;
      m.addQuad(a, b, b + 1, a + 1);
    }
  m.computeNormals();
  return m;
}

Mesh tube(const std::vector<Vec3>& path, const std::vector<float>& radius, int sides, bool caps) {
  Mesh m;
  if (path.size() < 2) return m;
  // Parallel-transport frames so the tube never twists.
  Vec3 t0 = normalize(path[1] - path[0]), n0, b0;
  orthonormalBasis(t0, n0, b0);
  Vec3 n = n0, prevT = t0;
  float along = 0;
  for (size_t i = 0; i < path.size(); ++i) {
    Vec3 t = i + 1 < path.size() ? path[i + 1] - path[i] : path[i] - path[i - 1];
    if (i > 0 && i + 1 < path.size()) t = path[i + 1] - path[i - 1];
    t = normalize(t);
    if (i > 0) {
      Quat q = Quat::fromTo(prevT, t);
      n = normalize(q.rotate(n));
      along += length(path[i] - path[i - 1]);
    }
    prevT = t;
    Vec3 b = cross(t, n);
    float r = radius[std::min(i, radius.size() - 1)];
    for (int s = 0; s <= sides; ++s) {
      float a = kTwoPi * s / sides;
      Vec3 d = n * std::cos(a) + b * std::sin(a);
      m.addVertex(path[i] + d * r, d, {static_cast<float>(s) / sides, along});
    }
  }
  for (size_t i = 0; i + 1 < path.size(); ++i)
    for (int s = 0; s < sides; ++s) {
      uint32_t a = static_cast<uint32_t>(i * (sides + 1) + s), c = a + sides + 1;
      m.addQuad(a, a + 1, c + 1, c);
    }
  if (caps) {
    for (int end = 0; end < 2; ++end) {
      size_t i = end ? path.size() - 1 : 0;
      Vec3 t = end ? normalize(path[i] - path[i - 1]) : normalize(path[0] - path[1]);
      uint32_t c = m.addVertex(path[i], t, {0.5f, 0.5f});
      uint32_t ring = static_cast<uint32_t>(i * (sides + 1));
      for (int s = 0; s < sides; ++s) {
        uint32_t a = m.addVertex(m.positions[ring + s], t, {}), b = m.addVertex(m.positions[ring + s + 1], t, {});
        if (end) m.addTriangle(c, b, a);
        else m.addTriangle(c, a, b);
      }
    }
  }
  return m;
}

}  // namespace shapes
}  // namespace loop
