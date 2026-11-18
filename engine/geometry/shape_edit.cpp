#include "engine/geometry/shape_edit.h"

#include <cmath>
#include <cstdio>
#include <map>
#include <utility>

namespace loop::shapeedit {

float tidy(float x) {
  float y = std::round(x * 10000.0f) / 10000.0f;
  return y == 0 ? 0.0f : y;  // never -0
}
Vec3 tidy(Vec3 v) { return {tidy(v.x), tidy(v.y), tidy(v.z)}; }

std::string stepLine(const Step& s) {
  auto num = [](float x) {
    char buf[32];
    std::snprintf(buf, sizeof buf, "%.4f", static_cast<double>(x));
    std::string t = buf;
    while (!t.empty() && t.back() == '0') t.pop_back();
    if (!t.empty() && t.back() == '.') t.pop_back();
    return t == "-0" || t.empty() ? std::string("0") : t;
  };
  if (s.kind == Step::Extrude) return "extrude face: " + std::to_string(s.face) + ", by: " + num(s.out);
  return "pull point: " + std::to_string(s.point) + ", by: vec(" + num(s.by.x) + ", " + num(s.by.y) + ", " + num(s.by.z) + ")";
}

std::string pull(EditMesh& m, uint32_t point, Vec3 by) {
  int i = m.pointByName(point);
  if (i < 0 || !m.points[i].alive) {
    uint32_t most = 0;
    for (auto& p : m.points)
      if (p.alive && p.name > most) most = p.name;
    return "this shape has no point " + std::to_string(point) + " (its points are 1 to " + std::to_string(most) + ")";
  }
  m.points[i].pos = m.points[i].pos + by;
  return "";
}

namespace {
std::string noFace(const EditMesh& m, uint32_t face) {
  std::string names;
  for (auto& f : m.faces)
    if (f.alive) names += (names.empty() ? "" : ", ") + std::to_string(f.name);
  return "this shape has no face " + std::to_string(face) + " (its faces are " + names + ")";
}
}  // namespace

bool faceFrame(const EditMesh& m, uint32_t face, Vec3& middle, Vec3& facing) {
  int fi = m.faceByName(face);
  if (fi < 0 || !m.faces[fi].alive) return false;
  const auto& cs = m.faces[fi].corners;
  Vec3 sum(0), n(0);
  for (size_t i = 0; i < cs.size(); ++i) {  // Newell's way: right for a face of any sides, even one not quite flat
    const Vec3 a = m.points[cs[i].point].pos, b = m.points[cs[(i + 1) % cs.size()].point].pos;
    sum = sum + a;
    n = n + Vec3((a.y - b.y) * (a.z + b.z), (a.z - b.z) * (a.x + b.x), (a.x - b.x) * (a.y + b.y));
  }
  if (cs.empty() || length(n) < 1e-12f) return false;
  middle = sum * (1.0f / static_cast<float>(cs.size()));
  facing = n * (1.0f / length(n));
  return true;
}

std::string extrude(EditMesh& m, uint32_t face, float out) {
  Vec3 mid, n;
  if (!faceFrame(m, face, mid, n)) return noFace(m, face);
  const EditMesh keep = m;
  const int fi = m.faceByName(face);
  const size_t k = m.faces[fi].corners.size();
  std::vector<uint32_t> was(k), now(k);
  for (size_t i = 0; i < k; ++i) {
    was[i] = m.faces[fi].corners[i].point;
    now[i] = m.addPoint(m.points[was[i]].pos + n * out);
    m.points[now[i]].pulls = m.points[was[i]].pulls;
  }
  for (size_t i = 0; i < k; ++i) m.faces[fi].corners[i].point = now[i];  // the face keeps its name and unwrap
  for (size_t i = 0; i < k; ++i) {  // the walls: each runs its old edge the face's way, so it meets the face beside
    const size_t j = (i + 1) % k;
 if (m.addFace({was[i], was[j], now[j], now[i]}) < 0) return m = keep, "face " + std::to_string(face) + " could not be extruded";
  }
  return "";
}

Shape open(const Mesh& recipe) {
  Shape s;
  s.base = EditMesh::fromMesh(recipe);
  s.now = s.base;
  return s;
}

std::string apply(Shape& s, const Step& step) {
  std::string why = step.kind == Step::Extrude ? extrude(s.now, step.face, step.out) : pull(s.now, step.point, step.by);
  if (why.empty()) s.steps.push_back(step);
  return why;
}

Mesh drawn(const EditMesh& m) {
  Mesh out = m.toMesh();
  out.flatShade();
  return out;
}

namespace {
std::map<std::pair<const void*, int>, Shape>& records() {
  static std::map<std::pair<const void*, int>, Shape> r;
  return r;
}
}  // namespace

void remember(const void* owner, int mesh, const Shape& s) { records()[{owner, mesh}] = s; }
Shape* recall(const void* owner, int mesh) {
  auto it = records().find({owner, mesh});
  return it == records().end() ? nullptr : &it->second;
}

}  // namespace loop::shapeedit
