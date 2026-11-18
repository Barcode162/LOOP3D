#include "engine/geometry/edit_mesh.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <set>
#include <sstream>
#include <tuple>

namespace loop {

namespace {

using Edge = std::pair<uint32_t, uint32_t>;
Edge edgeKey(uint32_t a, uint32_t b) { return a < b ? Edge{a, b} : Edge{b, a}; }

Vec3 lerp3(Vec3 a, Vec3 b, float t) { return a + (b - a) * t; }
Vec2 lerp2(Vec2 a, Vec2 b, float t) { return a + (b - a) * t; }
Vec4 lerp4(Vec4 a, Vec4 b, float t) { return a + (b - a) * t; }

EditMesh::Corner lerpCorner(const EditMesh::Corner& a, const EditMesh::Corner& b, float t) {
  EditMesh::Corner c;
  c.uv = lerp2(a.uv, b.uv, t);
  c.color = lerp4(a.color, b.color, t);
  return c;
}

// The bones of several points, each with a share: the pulls add up per bone, the strongest 8 stay, and
// they are scaled to sum to one again.
Pulls mixPulls(const std::vector<std::pair<const Pulls*, float>>& parts) {
  std::map<uint16_t, float> acc;
  for (auto& [p, w] : parts)
    for (int i = 0; i < 8; ++i)
      if (p->pull[i] > 0) acc[p->bone[i]] += p->pull[i] * w;
  std::vector<std::pair<float, uint16_t>> v;
  for (auto& [bone, w] : acc) v.push_back({w, bone});
  std::sort(v.begin(), v.end(), [](auto& a, auto& b) { return a.first != b.first ? a.first > b.first : a.second < b.second; });
  if (v.size() > 8) v.resize(8);
  float total = 0;
  for (auto& e : v) total += e.first;
  Pulls out;
  for (size_t i = 0; i < v.size() && total > 0; ++i) out.bone[i] = v[i].second, out.pull[i] = v[i].first / total;
  return out;
}

bool samePulls(const Pulls& a, const Pulls& b) {
  for (int i = 0; i < 8; ++i)
    if (a.bone[i] != b.bone[i] || a.pull[i] != b.pull[i]) return false;
  return true;
}

}  // namespace

// ---------------------------------------------------------------- counting, finding

size_t EditMesh::pointCount() const {
  size_t n = 0;
  for (auto& p : points) n += p.alive;
  return n;
}
size_t EditMesh::faceCount() const {
  size_t n = 0;
  for (auto& f : faces) n += f.alive;
  return n;
}
int EditMesh::pointByName(uint32_t name) const {
  for (size_t i = 0; i < points.size(); ++i)
    if (points[i].alive && points[i].name == name) return static_cast<int>(i);
  return -1;
}
int EditMesh::faceByName(uint32_t name) const {
  for (size_t i = 0; i < faces.size(); ++i)
    if (faces[i].alive && faces[i].name == name) return static_cast<int>(i);
  return -1;
}
int EditMesh::findEdge(uint32_t a, uint32_t b) const {
  for (size_t f = 0; f < faces.size(); ++f) {
    if (!faces[f].alive) continue;
    const auto& c = faces[f].corners;
    for (size_t k = 0; k < c.size(); ++k) {
      uint32_t p = c[k].point, q = c[(k + 1) % c.size()].point;
      if ((p == a && q == b) || (p == b && q == a)) return static_cast<int>(f);
    }
  }
  return -1;
}
float EditMesh::creaseOf(uint32_t a, uint32_t b) const {
  auto it = creases.find(edgeKey(a, b));
  return it == creases.end() ? 0.0f : it->second;
}
void EditMesh::setCrease(uint32_t a, uint32_t b, float sharpness) {
  if (findEdge(a, b) < 0) return;
  if (sharpness <= 0) creases.erase(edgeKey(a, b));
  else creases[edgeKey(a, b)] = sharpness;
}

// ---------------------------------------------------------------- building

uint32_t EditMesh::addPoint(Vec3 pos) {
  Point p;
  p.pos = pos, p.name = nextName++;
  points.push_back(p);
  return static_cast<uint32_t>(points.size() - 1);
}

int EditMesh::addFace(const std::vector<uint32_t>& loop) {
  if (loop.size() < 3) return -1;
  std::set<uint32_t> seen;
  for (uint32_t p : loop)
    if (p >= points.size() || !points[p].alive || !seen.insert(p).second) return -1;
  // each edge of the new face must run the way no other face runs it
  for (size_t k = 0; k < loop.size(); ++k) {
    uint32_t a = loop[k], b = loop[(k + 1) % loop.size()];
    for (auto& f : faces) {
      if (!f.alive) continue;
      for (size_t i = 0; i < f.corners.size(); ++i)
        if (f.corners[i].point == a && f.corners[(i + 1) % f.corners.size()].point == b) return -1;
    }
  }
  // and an edge may have only two faces
  for (size_t k = 0; k < loop.size(); ++k) {
    uint32_t a = loop[k], b = loop[(k + 1) % loop.size()];
    int count = 0;
    for (auto& f : faces) {
      if (!f.alive) continue;
      for (size_t i = 0; i < f.corners.size(); ++i) {
        uint32_t p = f.corners[i].point, q = f.corners[(i + 1) % f.corners.size()].point;
        if ((p == a && q == b) || (p == b && q == a)) ++count;
      }
    }
    if (count >= 2) return -1;
  }
  Face f;
  f.name = nextName++;
  for (uint32_t p : loop) {
    Corner c;
    c.point = p;
    f.corners.push_back(c);
  }
  faces.push_back(std::move(f));
  return static_cast<int>(faces.size() - 1);
}

void EditMesh::tidy() {
  std::vector<char> used(points.size(), 0);
  std::set<Edge> edges;
  for (auto& f : faces) {
    if (!f.alive) continue;
    for (size_t k = 0; k < f.corners.size(); ++k) {
      used[f.corners[k].point] = 1;
      edges.insert(edgeKey(f.corners[k].point, f.corners[(k + 1) % f.corners.size()].point));
    }
  }
  for (size_t i = 0; i < points.size(); ++i)
    if (!used[i]) points[i].alive = false;
  for (auto it = creases.begin(); it != creases.end();) {
    if (!edges.count(it->first) || it->second <= 0) it = creases.erase(it);
    else ++it;
  }
}

void EditMesh::compact() {
  std::vector<uint32_t> remap(points.size(), 0);
  std::vector<Point> np;
  for (size_t i = 0; i < points.size(); ++i)
    if (points[i].alive) remap[i] = static_cast<uint32_t>(np.size()), np.push_back(points[i]);
  std::vector<Face> nf;
  for (auto& f : faces) {
    if (!f.alive) continue;
    Face g = f;
    for (auto& c : g.corners) c.point = remap[c.point];
    nf.push_back(std::move(g));
  }
  std::map<Edge, float> nc;
  for (auto& [k, v] : creases) {
    if (k.first >= points.size() || k.second >= points.size() || !points[k.first].alive || !points[k.second].alive) continue;
    nc[edgeKey(remap[k.first], remap[k.second])] = v;
  }
  points = std::move(np), faces = std::move(nf), creases = std::move(nc);
}

std::string EditMesh::check() const {
  std::set<uint32_t> pointNames, faceNames;
  for (auto& p : points)
    if (p.alive && !pointNames.insert(p.name).second) return "two points share a name";
  std::vector<char> used(points.size(), 0);
  std::map<std::pair<uint32_t, uint32_t>, int> directed;
  std::set<Edge> edges;
  for (auto& f : faces) {
    if (!f.alive) continue;
    if (!faceNames.insert(f.name).second) return "two faces share a name";
    if (f.corners.size() < 3) return "a face has fewer than 3 corners";
    std::set<uint32_t> seen;
    for (size_t k = 0; k < f.corners.size(); ++k) {
      uint32_t a = f.corners[k].point, b = f.corners[(k + 1) % f.corners.size()].point;
      if (a >= points.size() || b >= points.size() || !points[a].alive || !points[b].alive) return "a face uses a removed point";
      if (!seen.insert(a).second) return "a face uses a point twice";
      used[a] = 1;
      if (++directed[{a, b}] > 1) return "an edge is run the same way by two faces";
      edges.insert(edgeKey(a, b));
    }
  }
  for (size_t i = 0; i < points.size(); ++i)
    if (points[i].alive && !used[i]) return "a point is used by no face";
  for (auto& [k, v] : creases) {
    if (!(k.first < k.second)) return "a crease key is not in order";
    if (!edges.count(k)) return "a crease lies on no edge";
    if (!(v > 0)) return "a crease has no sharpness";
  }
  return "";
}

EditMesh::HalfEdges EditMesh::halfEdges() const {
  HalfEdges h;
  h.start.assign(faces.size(), -1);
  std::map<std::pair<uint32_t, uint32_t>, int> at;
  for (size_t f = 0; f < faces.size(); ++f) {
    if (!faces[f].alive) continue;
    int s = static_cast<int>(h.he.size());
    h.start[f] = s;
    int n = static_cast<int>(faces[f].corners.size());
    for (int k = 0; k < n; ++k) {
      HalfEdge e;
      e.face = static_cast<int>(f), e.corner = k;
      e.from = faces[f].corners[k].point, e.to = faces[f].corners[(k + 1) % n].point;
      e.twin = -1, e.next = s + (k + 1) % n, e.prev = s + (k + n - 1) % n;
      at[{e.from, e.to}] = static_cast<int>(h.he.size());
      h.he.push_back(e);
    }
  }
  for (auto& e : h.he) {
    auto it = at.find({e.to, e.from});
    if (it != at.end()) e.twin = it->second;
  }
  return h;
}

// ---------------------------------------------------------------- the four moves

int EditMesh::splitEdge(uint32_t a, uint32_t b, float t) {
  if (a == b || a >= points.size() || b >= points.size() || !points[a].alive || !points[b].alive) return -1;
  if (!(t > 0 && t < 1)) return -1;
  if (findEdge(a, b) < 0) return -1;
  Point np;
  np.pos = lerp3(points[a].pos, points[b].pos, t);
  np.mask = points[a].mask + (points[b].mask - points[a].mask) * t;
  np.pulls = mixPulls({{&points[a].pulls, 1 - t}, {&points[b].pulls, t}});
  np.name = nextName++;
  points.push_back(np);
  uint32_t n = static_cast<uint32_t>(points.size() - 1);
  for (auto& f : faces) {
    if (!f.alive) continue;
    size_t cnt = f.corners.size();
    for (size_t k = 0; k < cnt; ++k) {
      uint32_t p = f.corners[k].point, q = f.corners[(k + 1) % cnt].point;
      bool forward = (p == a && q == b), back = (p == b && q == a);
      if (!forward && !back) continue;
      Corner c = lerpCorner(f.corners[k], f.corners[(k + 1) % cnt], forward ? t : 1 - t);
      c.point = n;
      f.corners.insert(f.corners.begin() + static_cast<long>(k) + 1, c);
      break;
    }
  }
  auto it = creases.find(edgeKey(a, b));
  if (it != creases.end()) {
    float s = it->second;
    creases.erase(it);
    creases[edgeKey(a, n)] = s;
    creases[edgeKey(n, b)] = s;
  }
  return static_cast<int>(n);
}

int EditMesh::splitFace(uint32_t face, uint32_t i, uint32_t j) {
  if (face >= faces.size() || !faces[face].alive) return -1;
  if (i > j) std::swap(i, j);
  size_t n = faces[face].corners.size();
  if (j >= n || j - i < 2 || (i == 0 && j == n - 1)) return -1;
  if (findEdge(faces[face].corners[i].point, faces[face].corners[j].point) >= 0) return -1;  // the edge is there already
  Face other;
  other.name = nextName++;
  const auto& c = faces[face].corners;
  for (size_t k = j; k < n; ++k) other.corners.push_back(c[k]);
  for (size_t k = 0; k <= i; ++k) other.corners.push_back(c[k]);
  std::vector<Corner> mine(c.begin() + static_cast<long>(i), c.begin() + static_cast<long>(j) + 1);
  faces[face].corners = std::move(mine);
  faces.push_back(std::move(other));
  return static_cast<int>(faces.size() - 1);
}

bool EditMesh::joinPoints(uint32_t a, uint32_t b) {
  if (a == b || a >= points.size() || b >= points.size() || !points[a].alive || !points[b].alive) return false;
  EditMesh w = *this;
  for (auto& f : w.faces) {
    if (!f.alive) continue;
    for (auto& c : f.corners)
      if (c.point == b) c.point = a;
    for (bool again = true; again && f.corners.size() >= 2;) {  // a corner next to its own point folds away
      again = false;
      for (size_t k = 0; k < f.corners.size(); ++k) {
        size_t nx = (k + 1) % f.corners.size();
        if (f.corners[k].point == f.corners[nx].point) {
          f.corners.erase(f.corners.begin() + static_cast<long>(nx));
          again = true;
          break;
        }
      }
    }
    if (f.corners.size() < 3) f.alive = false;
  }
  w.points[a].pos = lerp3(points[a].pos, points[b].pos, 0.5f);
  w.points[a].mask = (points[a].mask + points[b].mask) * 0.5f;
  w.points[b].alive = false;
  std::map<Edge, float> nc;
  for (auto& [k, v] : creases) {
    uint32_t p = k.first == b ? a : k.first, q = k.second == b ? a : k.second;
    if (p == q) continue;
    float& s = nc[edgeKey(p, q)];
    s = std::max(s, v);
  }
  w.creases = std::move(nc);
  w.tidy();
  if (!w.check().empty()) return false;
  *this = std::move(w);
  return true;
}

bool EditMesh::removeFace(uint32_t face) {
  if (face >= faces.size() || !faces[face].alive) return false;
  faces[face].alive = false;
  tidy();
  return true;
}

// ---------------------------------------------------------------- smoothing

namespace {

EditMesh smoothOnce(EditMesh src) {
  src.compact();
  const size_t P = src.points.size(), F = src.faces.size();
  struct EdgeInfo {
    int faces = 0;
    int f[2] = {-1, -1};
    uint32_t point = 0;  // the edge's new point
  };
  std::map<Edge, EdgeInfo> edges;
  for (size_t f = 0; f < F; ++f) {
    auto& c = src.faces[f].corners;
    for (size_t k = 0; k < c.size(); ++k) {
      EdgeInfo& e = edges[edgeKey(c[k].point, c[(k + 1) % c.size()].point)];
      if (e.faces < 2) e.f[e.faces] = static_cast<int>(f);
      ++e.faces;
    }
  }

  EditMesh out;
  out.nextName = src.nextName;
  out.points = src.points;  // the old points first, in place, keeping their names

  // face points
  std::vector<uint32_t> facePt(F);
  for (size_t f = 0; f < F; ++f) {
    auto& c = src.faces[f].corners;
    Vec3 sum(0);
    float mask = 0;
    std::vector<std::pair<const Pulls*, float>> parts;
    for (auto& cc : c) {
      sum += src.points[cc.point].pos;
      mask += src.points[cc.point].mask;
      parts.push_back({&src.points[cc.point].pulls, 1.0f / c.size()});
    }
    EditMesh::Point p;
    p.pos = sum / static_cast<float>(c.size());
    p.mask = mask / c.size();
    p.pulls = mixPulls(parts);
    p.name = out.nextName++;
    facePt[f] = static_cast<uint32_t>(out.points.size());
    out.points.push_back(p);
  }
  // edge points
  for (auto& [key, e] : edges) {
    Vec3 A = src.points[key.first].pos, B = src.points[key.second].pos, mid = (A + B) * 0.5f;
    float s = e.faces == 2 ? src.creaseOf(key.first, key.second) : 1e9f;
    Vec3 pos = mid;
    if (e.faces == 2 && s < 1) {
      Vec3 smooth = (A + B + out.points[facePt[e.f[0]]].pos + out.points[facePt[e.f[1]]].pos) * 0.25f;
      pos = s > 0 ? lerp3(smooth, mid, s) : smooth;
    }
    EditMesh::Point p;
    p.pos = pos;
    p.mask = (src.points[key.first].mask + src.points[key.second].mask) * 0.5f;
    p.pulls = mixPulls({{&src.points[key.first].pulls, 0.5f}, {&src.points[key.second].pulls, 0.5f}});
    p.name = out.nextName++;
    e.point = static_cast<uint32_t>(out.points.size());
    out.points.push_back(p);
  }
  // the old points move
  std::vector<std::vector<uint32_t>> facesOf(P);
  std::vector<std::vector<Edge>> edgesOf(P);
  for (size_t f = 0; f < F; ++f)
    for (auto& c : src.faces[f].corners) facesOf[c.point].push_back(static_cast<uint32_t>(f));
  for (auto& [key, e] : edges) {
    edgesOf[key.first].push_back(key);
    edgesOf[key.second].push_back(key);
  }
  for (size_t v = 0; v < P; ++v) {
    Vec3 Pv = src.points[v].pos;
    std::vector<uint32_t> sharpEnds;
    for (auto& key : edgesOf[v]) {
      const EdgeInfo& e = edges[key];
      if (e.faces < 2 || src.creaseOf(key.first, key.second) >= 1) sharpEnds.push_back(key.first == v ? key.second : key.first);
    }
    if (sharpEnds.size() == 2) {
      out.points[v].pos = (Pv * 6 + src.points[sharpEnds[0]].pos + src.points[sharpEnds[1]].pos) * 0.125f;
    } else if (sharpEnds.size() >= 3) {
      out.points[v].pos = Pv;
    } else {
      float n = static_cast<float>(edgesOf[v].size());
      Vec3 Favg(0), Ravg(0);
      for (uint32_t f : facesOf[v]) Favg += out.points[facePt[f]].pos;
      Favg = Favg / static_cast<float>(facesOf[v].size());
      for (auto& key : edgesOf[v]) Ravg += (src.points[key.first].pos + src.points[key.second].pos) * 0.5f;
      Ravg = Ravg / n;
      out.points[v].pos = (Favg + Ravg * 2 + Pv * (n - 3)) / n;
    }
  }
  // faces: one four-sided face for each corner of each old face
  for (size_t f = 0; f < F; ++f) {
    auto& c = src.faces[f].corners;
    size_t n = c.size();
    EditMesh::Corner mid;
    mid.uv = Vec2(0), mid.color = Vec4(0, 0, 0, 0);
    for (auto& cc : c) mid.uv = mid.uv + cc.uv, mid.color = mid.color + cc.color;
    mid.uv = mid.uv / static_cast<float>(n), mid.color = mid.color * (1.0f / n);
    mid.point = facePt[f];
    for (size_t k = 0; k < n; ++k) {
      const auto& cur = c[k];
      const auto& nxt = c[(k + 1) % n];
      const auto& prv = c[(k + n - 1) % n];
      EditMesh::Face g;
      g.name = k == 0 ? src.faces[f].name : out.nextName++;
      EditMesh::Corner vc = cur;
      EditMesh::Corner e1 = lerpCorner(cur, nxt, 0.5f), e0 = lerpCorner(prv, cur, 0.5f);
      e1.point = edges[edgeKey(cur.point, nxt.point)].point;
      e0.point = edges[edgeKey(prv.point, cur.point)].point;
      g.corners = {vc, e1, mid, e0};
      out.faces.push_back(std::move(g));
    }
  }
  // sharpness carries down one level
  for (auto& [key, s] : src.creases) {
    if (s <= 1) continue;
    uint32_t e = edges[key].point;
    out.creases[edgeKey(key.first, e)] = s - 1;
    out.creases[edgeKey(e, key.second)] = s - 1;
  }
  return out;
}

}  // namespace

EditMesh EditMesh::smoothed(int levels) const {
  EditMesh m = *this;
  for (int i = 0; i < levels; ++i) m = smoothOnce(std::move(m));
  return m;
}

// ---------------------------------------------------------------- to and from a Mesh

EditMesh EditMesh::fromMesh(const Mesh& m) {
  EditMesh e;
  std::map<std::array<int64_t, 3>, uint32_t> byPlace;
  std::vector<uint32_t> pointOf(m.positions.size());
  for (size_t i = 0; i < m.positions.size(); ++i) {
    Vec3 p = m.positions[i];
    std::array<int64_t, 3> key = {static_cast<int64_t>(std::floor(p.x * 1e5 + 0.5)), static_cast<int64_t>(std::floor(p.y * 1e5 + 0.5)),
                                  static_cast<int64_t>(std::floor(p.z * 1e5 + 0.5))};
    auto it = byPlace.find(key);
    if (it == byPlace.end()) {
      uint32_t id = e.addPoint(p);
      if (m.pulls.size() == m.positions.size()) e.points[id].pulls = m.pulls[i];
      it = byPlace.emplace(key, id).first;
    }
    pointOf[i] = it->second;
  }
  struct Tri {
    uint32_t v[3];  // mesh vertices
    uint32_t p[3];  // points
  };
  std::vector<Tri> tris;
  for (size_t i = 0; i + 2 < m.indices.size(); i += 3) {
    Tri t;
    for (int k = 0; k < 3; ++k) t.v[k] = m.indices[i + k], t.p[k] = pointOf[t.v[k]];
    if (t.p[0] == t.p[1] || t.p[1] == t.p[2] || t.p[0] == t.p[2]) continue;  // a sliver with no area
    tris.push_back(t);
  }
  std::map<Edge, int> edgeUse;
  for (auto& t : tris)
    for (int k = 0; k < 3; ++k) ++edgeUse[edgeKey(t.p[k], t.p[(k + 1) % 3])];
  auto cornerOf = [&](uint32_t v) {
    Corner c;
    c.point = pointOf[v];
    if (v < m.uvs.size()) c.uv = m.uvs[v];
    return c;
  };
  auto normalOf = [&](const Tri& t) {
    return normalize(cross(e.points[t.p[1]].pos - e.points[t.p[0]].pos, e.points[t.p[2]].pos - e.points[t.p[0]].pos));
  };
  std::set<std::pair<uint32_t, uint32_t>> directed;
  auto push = [&](std::vector<Corner> cs) {
    std::set<uint32_t> seen;
    for (auto& c : cs)
      if (!seen.insert(c.point).second) return;
    for (size_t k = 0; k < cs.size(); ++k)
      if (directed.count({cs[k].point, cs[(k + 1) % cs.size()].point})) return;  // would not be whole: leave it out
    for (size_t k = 0; k < cs.size(); ++k) directed.insert({cs[k].point, cs[(k + 1) % cs.size()].point});
    Face f;
    f.name = e.nextName++;
    f.corners = std::move(cs);
    e.faces.push_back(std::move(f));
  };
  for (size_t t = 0; t < tris.size(); ++t) {
    if (t + 1 < tris.size()) {
      // a quad cut in two is (a,b,c) then (a,c,d), or the first turned round: (c,a,b) then (a,c,d) and so on
      const Tri &A = tris[t], &B = tris[t + 1];
      bool joined = false;
      for (int r = 0; r < 3 && !joined; ++r) {
        uint32_t p0 = A.p[r], p1 = A.p[(r + 1) % 3], p2 = A.p[(r + 2) % 3];
        bool pattern = B.p[0] == p0 && B.p[1] == p2 && B.p[2] != p0 && B.p[2] != p1 && B.p[2] != p2;
        if (pattern && edgeUse[edgeKey(p0, p2)] == 2 && dot(normalOf(A), normalOf(B)) > 0.7f) {
          push({cornerOf(A.v[r]), cornerOf(A.v[(r + 1) % 3]), cornerOf(A.v[(r + 2) % 3]), cornerOf(B.v[2])});
          joined = true;
        }
      }
      if (joined) {
        ++t;
        continue;
      }
    }
    push({cornerOf(tris[t].v[0]), cornerOf(tris[t].v[1]), cornerOf(tris[t].v[2])});
  }
  e.tidy();
  return e;
}

Mesh EditMesh::toMesh() const {
  Mesh m;
  std::map<std::tuple<uint32_t, uint32_t, uint32_t>, uint32_t> made;
  bool skin = false;
  for (auto& p : points)
    if (p.alive && p.pulls.pull[0] > 0) skin = true;
  auto vertex = [&](const Corner& c) {
    uint32_t ux, uy;
    std::memcpy(&ux, &c.uv.x, 4);
    std::memcpy(&uy, &c.uv.y, 4);
    auto key = std::make_tuple(c.point, ux, uy);
    auto it = made.find(key);
    if (it != made.end()) return it->second;
    uint32_t id = m.addVertex(points[c.point].pos, Vec3(0, 1, 0), c.uv);
    if (skin) m.pulls.push_back(points[c.point].pulls);
    made[key] = id;
    return id;
  };
  for (auto& f : faces) {
    if (!f.alive) continue;
    size_t n = f.corners.size();
    size_t first = 0;
    if (n == 4) {  // cut along the shorter diagonal
      float d02 = length(points[f.corners[0].point].pos - points[f.corners[2].point].pos);
      float d13 = length(points[f.corners[1].point].pos - points[f.corners[3].point].pos);
      if (d13 < d02) first = 1;
    }
    uint32_t v0 = vertex(f.corners[first]);
    for (size_t k = 1; k + 1 < n; ++k) m.addTriangle(v0, vertex(f.corners[(first + k) % n]), vertex(f.corners[(first + k + 1) % n]));
  }
  m.computeNormals();
  return m;
}

// ---------------------------------------------------------------- the text form

namespace {

void putFloat(std::string& s, float v) {
  uint32_t b;
  std::memcpy(&b, &v, 4);
  char buf[16];
  std::snprintf(buf, sizeof buf, " %08x", b);
  s += buf;
}
void putInt(std::string& s, uint64_t v) { s += " " + std::to_string(v); }

bool getFloat(std::istringstream& in, float& v) {
  std::string t;
  if (!(in >> t) || t.size() != 8) return false;
  char* end = nullptr;
  unsigned long b = std::strtoul(t.c_str(), &end, 16);
  if (*end) return false;
  uint32_t u = static_cast<uint32_t>(b);
  std::memcpy(&v, &u, 4);
  return true;
}

}  // namespace

std::string EditMesh::toText() const {
  EditMesh c = *this;
  c.compact();
  std::string s = "loop-editmesh 1\n";
  s += "points " + std::to_string(c.points.size()) + "\n";
  s += "faces " + std::to_string(c.faces.size()) + "\n";
  s += "creases " + std::to_string(c.creases.size()) + "\n";
  s += "next-name " + std::to_string(c.nextName) + "\n";
  s += "# a point: name x y z mask, the number of pull slots used, then per slot: slot bone pull\n";
  s += "# a face: name corners, then per corner: point u v r g b a\n";
  s += "# a crease: point point sharpness. Decimals are whole numbers; the others are the 8 hex digits of a float.\n";
  for (auto& p : c.points) {
    std::string line = std::to_string(p.name);
    putFloat(line, p.pos.x), putFloat(line, p.pos.y), putFloat(line, p.pos.z), putFloat(line, p.mask);
    int used = 0;
    for (int i = 0; i < 8; ++i) used += (p.pulls.bone[i] != 0 || p.pulls.pull[i] != 0);
    putInt(line, used);
    for (int i = 0; i < 8; ++i) {
      if (p.pulls.bone[i] == 0 && p.pulls.pull[i] == 0) continue;
      putInt(line, i), putInt(line, p.pulls.bone[i]), putFloat(line, p.pulls.pull[i]);
    }
    s += "p " + line + "\n";
  }
  for (auto& f : c.faces) {
    std::string line = std::to_string(f.name);
    putInt(line, f.corners.size());
    for (auto& k : f.corners) {
      putInt(line, k.point);
      putFloat(line, k.uv.x), putFloat(line, k.uv.y);
      putFloat(line, k.color.x), putFloat(line, k.color.y), putFloat(line, k.color.z), putFloat(line, k.color.w);
    }
    s += "f " + line + "\n";
  }
  for (auto& [k, v] : c.creases) {
    std::string line = std::to_string(k.first);
    putInt(line, k.second);
    putFloat(line, v);
    s += "c " + line + "\n";
  }
  return s;
}

bool EditMesh::fromText(const std::string& text, EditMesh& out, std::string* error) {
  auto fail = [&](const std::string& why) {
    if (error) *error = why;
    return false;
  };
  std::istringstream all(text);
  std::string line;
  auto next = [&](std::string& l) {
    while (std::getline(all, l))
      if (!l.empty() && l[0] != '#') return true;
    return false;
  };
  if (!next(line) || line != "loop-editmesh 1") return fail("not an editable mesh (first line)");
  size_t np = 0, nf = 0, nc = 0;
  uint32_t nextName = 1;
  for (int h = 0; h < 4; ++h) {
    if (!next(line)) return fail("the header is cut short");
    std::istringstream in(line);
    std::string key;
    unsigned long long v = 0;
    if (!(in >> key >> v)) return fail("bad header line: " + line);
    if (key == "points") np = v;
    else if (key == "faces") nf = v;
    else if (key == "creases") nc = v;
    else if (key == "next-name") nextName = static_cast<uint32_t>(v);
    else return fail("unknown header word: " + key);
  }
  EditMesh m;
  m.nextName = nextName;
  for (size_t i = 0; i < np; ++i) {
    if (!next(line)) return fail("fewer points than the header says");
    std::istringstream in(line);
    std::string tag;
    Point p;
    size_t used = 0;
    if (!(in >> tag >> p.name) || tag != "p") return fail("bad point line");
    if (!getFloat(in, p.pos.x) || !getFloat(in, p.pos.y) || !getFloat(in, p.pos.z) || !getFloat(in, p.mask)) return fail("bad point numbers");
    if (!(in >> used) || used > 8) return fail("bad pull count");
    for (size_t u = 0; u < used; ++u) {
      int slot = 0;
      unsigned bone = 0;
      if (!(in >> slot >> bone) || slot < 0 || slot > 7) return fail("bad pull");
      p.pulls.bone[slot] = static_cast<uint16_t>(bone);
      if (!getFloat(in, p.pulls.pull[slot])) return fail("bad pull number");
    }
    m.points.push_back(p);
  }
  for (size_t i = 0; i < nf; ++i) {
    if (!next(line)) return fail("fewer faces than the header says");
    std::istringstream in(line);
    std::string tag;
    Face f;
    size_t n = 0;
    if (!(in >> tag >> f.name >> n) || tag != "f" || n < 3 || n > 100000) return fail("bad face line");
    for (size_t k = 0; k < n; ++k) {
      Corner c;
      if (!(in >> c.point) || c.point >= np) return fail("a face names a point that is not there");
      if (!getFloat(in, c.uv.x) || !getFloat(in, c.uv.y) || !getFloat(in, c.color.x) || !getFloat(in, c.color.y) ||
          !getFloat(in, c.color.z) || !getFloat(in, c.color.w))
        return fail("bad corner numbers");
      f.corners.push_back(c);
    }
    m.faces.push_back(std::move(f));
  }
  for (size_t i = 0; i < nc; ++i) {
    if (!next(line)) return fail("fewer creases than the header says");
    std::istringstream in(line);
    std::string tag;
    uint32_t a = 0, b = 0;
    float v = 0;
    if (!(in >> tag >> a >> b) || tag != "c" || !getFloat(in, v)) return fail("bad crease line");
    m.creases[{a, b}] = v;
  }
  std::string why = m.check();
  if (!why.empty()) return fail("the mesh is not whole: " + why);
  out = std::move(m);
  return true;
}

bool EditMesh::operator==(const EditMesh& o) const {
  if (points.size() != o.points.size() || faces.size() != o.faces.size() || creases != o.creases || nextName != o.nextName) return false;
  for (size_t i = 0; i < points.size(); ++i) {
    const Point &a = points[i], &b = o.points[i];
    if (a.name != b.name || a.pos != b.pos || a.mask != b.mask || a.alive != b.alive || !samePulls(a.pulls, b.pulls)) return false;
  }
  for (size_t i = 0; i < faces.size(); ++i) {
    const Face &a = faces[i], &b = o.faces[i];
    if (a.name != b.name || a.alive != b.alive || a.corners.size() != b.corners.size()) return false;
    for (size_t k = 0; k < a.corners.size(); ++k) {
      const Corner &x = a.corners[k], &y = b.corners[k];
      if (x.point != y.point || x.uv.x != y.uv.x || x.uv.y != y.uv.y || x.color.x != y.color.x || x.color.y != y.color.y ||
          x.color.z != y.color.z || x.color.w != y.color.w)
        return false;
    }
  }
  return true;
}

}  // namespace loop
