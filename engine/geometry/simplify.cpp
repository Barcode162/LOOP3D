#include "engine/geometry/simplify.h"

#include <algorithm>
#include <cstring>
#include <queue>
#include <unordered_map>

namespace loop {

std::vector<uint32_t> weldMap(const std::vector<Vec3>& positions) {
  struct Key {
    uint32_t x, y, z;
    bool operator==(const Key& o) const { return x == o.x && y == o.y && z == o.z; }
  };
  struct Hash {
    size_t operator()(const Key& k) const { return (static_cast<size_t>(k.x) * 73856093u) ^ (static_cast<size_t>(k.y) * 19349663u) ^ (static_cast<size_t>(k.z) * 83492791u); }
  };
  std::unordered_map<Key, uint32_t, Hash> first;
  first.reserve(positions.size());
  std::vector<uint32_t> out(positions.size());
  for (size_t i = 0; i < positions.size(); ++i) {
    Vec3 p = positions[i] + Vec3(0.0f);  // -0 and +0 are one place
    Key k;
    std::memcpy(&k.x, &p.x, 4), std::memcpy(&k.y, &p.y, 4), std::memcpy(&k.z, &p.z, 4);
    out[i] = first.emplace(k, static_cast<uint32_t>(i)).first->second;
  }
  return out;
}

namespace {

// The sum of squared distances to a set of planes, as a symmetric 4x4 matrix.
struct Quadric {
  double a2 = 0, ab = 0, ac = 0, ad = 0, b2 = 0, bc = 0, bd = 0, c2 = 0, cd = 0, d2 = 0;
  double weight = 0;  // the area its planes stand for
  void addPlane(Vec3 n, float d, double w) {
    double a = n.x, b = n.y, c = n.z, e = d;
    weight += w;
    a2 += w * a * a, ab += w * a * b, ac += w * a * c, ad += w * a * e;
    b2 += w * b * b, bc += w * b * c, bd += w * b * e;
    c2 += w * c * c, cd += w * c * e, d2 += w * e * e;
  }
  void add(const Quadric& q) {
    a2 += q.a2, ab += q.ab, ac += q.ac, ad += q.ad, b2 += q.b2, bc += q.bc, bd += q.bd, c2 += q.c2, cd += q.cd, d2 += q.d2;
    weight += q.weight;
  }
  double at(Vec3 p) const {
    double x = p.x, y = p.y, z = p.z;
    return a2 * x * x + 2 * ab * x * y + 2 * ac * x * z + 2 * ad * x + b2 * y * y + 2 * bc * y * z + 2 * bd * y + c2 * z * z + 2 * cd * z + d2;
  }
};

// The nearest point of triangle abc to p (Ericson, Real-Time Collision Detection 5.1.5).
Vec3 closestOnTriangle(Vec3 p, Vec3 a, Vec3 b, Vec3 c) {
  Vec3 ab = b - a, ac = c - a, ap = p - a;
  float d1 = dot(ab, ap), d2 = dot(ac, ap);
  if (d1 <= 0 && d2 <= 0) return a;
  Vec3 bp = p - b;
  float d3 = dot(ab, bp), d4 = dot(ac, bp);
  if (d3 >= 0 && d4 <= d3) return b;
  float vc = d1 * d4 - d3 * d2;
  if (vc <= 0 && d1 >= 0 && d3 <= 0) return a + ab * (d1 / (d1 - d3));
  Vec3 cp = p - c;
  float d5 = dot(ab, cp), d6 = dot(ac, cp);
  if (d6 >= 0 && d5 <= d6) return c;
  float vb = d5 * d2 - d1 * d6;
  if (vb <= 0 && d2 >= 0 && d6 <= 0) return a + ac * (d2 / (d2 - d6));
  float va = d3 * d6 - d5 * d4;
  if (va <= 0 && (d4 - d3) >= 0 && (d5 - d6) >= 0) return b + (c - b) * ((d4 - d3) / ((d4 - d3) + (d5 - d6)));
  float den = 1 / (va + vb + vc);
  return a + ab * (vb * den) + ac * (vc * den);
}

struct Candidate {
  double cost;
  uint32_t from, to;    // welded vertices: `from` goes onto `to`
  uint32_t fromStamp, toStamp;
  bool operator>(const Candidate& o) const { return cost > o.cost; }
};

}  // namespace

namespace {
SimplifyResult simplifyCompact(const std::vector<Vec3>& positions, const std::vector<uint32_t>& indices, size_t targetTriangles,
                               const std::vector<uint8_t>& lockedIn, float maxError);
}  // namespace

// Works on just the vertices the triangles use (a small group of a big mesh
// costs only its own size), then gives the original numbers back.
SimplifyResult simplify(const std::vector<Vec3>& positions, const std::vector<uint32_t>& indices, size_t targetTriangles,
                        const std::vector<uint8_t>& locked, float maxError) {
  std::vector<uint32_t> used(indices);
  std::sort(used.begin(), used.end());
  used.erase(std::unique(used.begin(), used.end()), used.end());
  while (!used.empty() && used.back() >= positions.size()) used.pop_back();
  std::vector<Vec3> pos(used.size());
  std::vector<uint8_t> lock(locked.empty() ? 0 : used.size());
  for (size_t k = 0; k < used.size(); ++k) {
    pos[k] = positions[used[k]];
    if (!locked.empty()) lock[k] = used[k] < locked.size() ? locked[used[k]] : 0;  // the list may stop short: the rest are free
  }
  auto local = [&](uint32_t v) {
    auto it = std::lower_bound(used.begin(), used.end(), v);
    return it != used.end() && *it == v ? static_cast<uint32_t>(it - used.begin()) : static_cast<uint32_t>(used.size());
  };
  std::vector<uint32_t> idx(indices.size());
  for (size_t k = 0; k < indices.size(); ++k) idx[k] = local(indices[k]);
  SimplifyResult r = simplifyCompact(pos, idx, targetTriangles, lock, maxError);
  for (uint32_t& v : r.indices) v = used[v];
  return r;
}

namespace {
SimplifyResult simplifyCompact(const std::vector<Vec3>& positions, const std::vector<uint32_t>& indices, size_t targetTriangles,
                               const std::vector<uint8_t>& lockedIn, float maxError) {
  SimplifyResult res;
  const size_t n = positions.size();
  const std::vector<uint32_t> weld = weldMap(positions);
  // the triangles: each corner's original vertex, and its welded place
  std::vector<uint32_t> orig;
  orig.reserve(indices.size());
  for (size_t t = 0; t + 2 < indices.size(); t += 3) {
    uint32_t i0 = indices[t], i1 = indices[t + 1], i2 = indices[t + 2];
    if (i0 >= n || i1 >= n || i2 >= n) continue;
    if (weld[i0] == weld[i1] || weld[i1] == weld[i2] || weld[i0] == weld[i2]) continue;  // already nothing
    orig.insert(orig.end(), {i0, i1, i2});
  }
  const size_t T = orig.size() / 3;
  std::vector<uint32_t> tri(orig.size());
  for (size_t k = 0; k < orig.size(); ++k) tri[k] = weld[orig[k]];
  std::vector<uint8_t> alive(T, 1);
  size_t aliveCount = T;
  if (T <= targetTriangles) {
    res.indices = orig;
    return res;
  }
  // which places can't go: a seam (several originals), an open edge, or locked
  // (a locked number of 2 or more: the place may go, but only onto a place of the same number)
  std::vector<uint32_t> firstOrig(n, ~0u);
  std::vector<uint8_t> fixed(n, 0), slide(n, 0);
  for (size_t k = 0; k < orig.size(); ++k) {
    uint32_t c = tri[k];
    if (firstOrig[c] == ~0u) firstOrig[c] = orig[k];
    else if (firstOrig[c] != orig[k]) fixed[c] = 1;
    const uint8_t L = lockedIn.empty() ? 0 : lockedIn[orig[k]];
    if (L == 1) fixed[c] = 1;
    else if (L >= 2) slide[c] = L;
  }
  // every edge, sorted: an edge not shared by exactly two triangles is open
  // (or crowded), and its ends stay
  auto edgeKey = [](uint32_t a, uint32_t b) { return a < b ? (static_cast<uint64_t>(a) << 32 | b) : (static_cast<uint64_t>(b) << 32 | a); };
  std::vector<uint64_t> edges(T * 3);
  for (size_t t = 0; t < T; ++t)
    for (int e = 0; e < 3; ++e) edges[t * 3 + e] = edgeKey(tri[t * 3 + e], tri[t * 3 + (e + 1) % 3]);
  std::sort(edges.begin(), edges.end());
  for (size_t i = 0; i < edges.size();) {
    size_t j = i;
    while (j < edges.size() && edges[j] == edges[i]) ++j;
    if (j - i != 2) fixed[static_cast<uint32_t>(edges[i] >> 32)] = fixed[static_cast<uint32_t>(edges[i])] = 1;
    i = j;
  }
  // nothing much can move (separate pieces: grass blades, leaf cards): done
  size_t free = 0, used = 0;
  for (size_t v = 0; v < n; ++v)
    if (firstOrig[v] != ~0u) ++used, free += !fixed[v];  // the places in use
  if (free * 4 < used) {
    res.indices = orig;
    return res;
  }
  // each place's triangles, and its quadric (the planes of its triangles, by area)
  std::vector<std::vector<uint32_t>> around(n);
  std::vector<Quadric> quad(n);
  for (size_t t = 0; t < T; ++t) {
    Vec3 p0 = positions[tri[t * 3]], p1 = positions[tri[t * 3 + 1]], p2 = positions[tri[t * 3 + 2]];
    Vec3 c = cross(p1 - p0, p2 - p0);
    float len = length(c);
    if (len < 1e-20f) continue;
    Vec3 nrm = c / len;
    for (int k = 0; k < 3; ++k) {
      around[tri[t * 3 + k]].push_back(static_cast<uint32_t>(t));
      quad[tri[t * 3 + k]].addPlane(nrm, -dot(nrm, p0), 0.5 * len);
    }
  }
  std::vector<uint32_t> stamp(n, 0);
  std::priority_queue<Candidate, std::vector<Candidate>, std::greater<Candidate>> heap;
  // how far sliding place a onto b moves its band's border: a's border then runs straight from b to
  // a's other neighbour of the same kind, so it is a's distance from that line (none on a straight
  // border; along a ring, the chord's sag). With no such neighbour, the whole way slid.
  auto bandShift = [&](uint32_t a, uint32_t b) {
    const Vec3 pa = positions[a], pb = positions[b];
    float best = kInf;
    for (uint32_t t : around[a]) {
      if (!alive[t]) continue;
      for (int k = 0; k < 3; ++k) {
        const uint32_t c = tri[t * 3 + k];
        if (c == a || c == b || slide[c] != slide[a]) continue;
        const Vec3 d = positions[c] - pb;
        const float u = clamp(dot(pa - pb, d) / std::max(lengthSq(d), 1e-20f), 0.0f, 1.0f);
        best = std::min(best, length(pa - (pb + d * u)));
      }
    }
    return best < kInf ? best : length(pa - pb);
  };
  auto offer = [&](uint32_t from, uint32_t to) {
    if (fixed[from] || from == to) return;
    if (slide[from] && slide[to] != slide[from]) return;  // it slides along its own kind only
    Quadric q = quad[from];
    q.add(quad[to]);
    double cost = std::max(0.0, q.at(positions[to]));
    // a slide costs what it moves its band's border (below), weighed as the quadric is, so the least goes first
    if (slide[from]) cost = std::max(cost, (quad[from].weight + quad[to].weight) * static_cast<double>(sqr(bandShift(from, to))));
    heap.push({cost, from, to, stamp[from], stamp[to]});
  };
  for (size_t t = 0; t < T; ++t)
    for (int e = 0; e < 3; ++e) {
      uint32_t a = tri[t * 3 + e], b = tri[t * 3 + (e + 1) % 3];
      offer(a, b), offer(b, a);
    }
  float worst = 0;
  while (aliveCount > targetTriangles && !heap.empty()) {
    Candidate c = heap.top();
    heap.pop();
    uint32_t a = c.from, b = c.to;
    if (c.fromStamp != stamp[a] || c.toStamp != stamp[b] || fixed[a]) continue;
    // the triangles that die (they hold the edge) and those that change
    uint32_t bOrig = ~0u;
    bool sides = true, edgeExists = false;
    for (uint32_t t : around[a]) {
      if (!alive[t]) continue;
      for (int k = 0; k < 3; ++k)
        if (tri[t * 3 + k] == b) {
          edgeExists = true;
          if (bOrig == ~0u) bOrig = orig[t * 3 + k];
          else if (bOrig != orig[t * 3 + k]) sides = false;  // the edge runs along b's seam
        }
    }
    if (!edgeExists || !sides) continue;
    // every changed triangle keeps facing its way (turning no more than about
    // 50 degrees: a steeper turn would raise a fin off the surface) and keeps
    // some size; the
    // surface moves by how far a is from the nearest new triangle
    bool ok = true;
    float moved = kInf;
    Vec3 pa = positions[a];
    for (uint32_t t : around[a]) {
      if (!alive[t]) continue;
      uint32_t v[3] = {tri[t * 3], tri[t * 3 + 1], tri[t * 3 + 2]};
      if (v[0] == b || v[1] == b || v[2] == b) continue;
      Vec3 before = cross(positions[v[1]] - positions[v[0]], positions[v[2]] - positions[v[0]]);
      for (uint32_t& x : v)
        if (x == a) x = b;
      Vec3 after = cross(positions[v[1]] - positions[v[0]], positions[v[2]] - positions[v[0]]);
      float la = length(after), lb = length(before);
      if (la < 1e-12f || dot(after, before) < 0.65f * la * lb) {
        ok = false;
        break;
      }
      // how far a is from the new surface: from the nearest new triangle
      moved = std::min(moved, length(pa - closestOnTriangle(pa, positions[v[0]], positions[v[1]], positions[v[2]])));
    }
    // the error: the typical distance from b to every bit of the original
    // surface it now stands for (its quadric, by area), never less than
    // how far a was from the new triangles
    double w = quad[a].weight + quad[b].weight;
    float e = std::max(moved < kInf ? moved : 0.0f, w > 0 ? static_cast<float>(std::sqrt(c.cost / w)) : 0.0f);
    // a place sliding along its band (a `locked` number of 2 or more) moves the band's border, even
    // where no shape moves (along a flat ring the chord cuts inside it): that counts as the error, so a
    // simpler level that does it is drawn only where it looks small
    if (slide[a]) e = std::max(e, bandShift(a, b));
    if (!ok || e > maxError) continue;
    // collapse: a's triangles go to b (with b's original vertex on this side)
    for (uint32_t t : around[a]) {
      if (!alive[t]) continue;
      bool dies = false;
      for (int k = 0; k < 3; ++k)
        if (tri[t * 3 + k] == b) dies = true;
      if (dies) {
        alive[t] = 0, --aliveCount;
        continue;
      }
      for (int k = 0; k < 3; ++k)
        if (tri[t * 3 + k] == a) tri[t * 3 + k] = b, orig[t * 3 + k] = bOrig;
      around[b].push_back(t);
    }
    around[a].clear();
    quad[b].add(quad[a]);
    worst = std::max(worst, e);
    ++stamp[a], ++stamp[b];
    fixed[a] = 1;  // gone
    // only b's quadric changed: just its edges again, at their new costs
    // (the stamps of a and b retire every older offer that involves them)
    std::vector<uint32_t>& ab = around[b];
    ab.erase(std::remove_if(ab.begin(), ab.end(), [&](uint32_t t) { return !alive[t]; }), ab.end());
    for (uint32_t t : ab)
      for (int k = 0; k < 3; ++k) {
        uint32_t x = tri[t * 3 + k];
        if (x != b) offer(x, b), offer(b, x);
      }
  }
  res.indices.reserve(aliveCount * 3);
  for (size_t t = 0; t < T; ++t)
    if (alive[t]) res.indices.insert(res.indices.end(), {orig[t * 3], orig[t * 3 + 1], orig[t * 3 + 2]});
  res.error = worst;
  return res;
}
}  // namespace

}  // namespace loop
