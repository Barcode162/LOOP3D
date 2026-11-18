#include "engine/geometry/shape_edit.h"

#include <cmath>
#include <cstdio>
#include <map>
#include <utility>

namespace loop::shapeedit {

Vec3 tidy(Vec3 v) {
  auto r = [](float x) {
    float y = std::round(x * 10000.0f) / 10000.0f;
    return y == 0 ? 0.0f : y;  // never -0
  };
  return {r(v.x), r(v.y), r(v.z)};
}

std::string stepLine(const Step& s) {
  auto num = [](float x) {
    char buf[32];
    std::snprintf(buf, sizeof buf, "%.4f", static_cast<double>(x));
    std::string t = buf;
    while (!t.empty() && t.back() == '0') t.pop_back();
    if (!t.empty() && t.back() == '.') t.pop_back();
    return t == "-0" || t.empty() ? std::string("0") : t;
  };
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

Shape open(const Mesh& recipe) {
  Shape s;
  s.base = EditMesh::fromMesh(recipe);
  s.now = s.base;
  return s;
}

std::string apply(Shape& s, const Step& step) {
  std::string why = pull(s.now, step.point, step.by);
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
