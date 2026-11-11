#pragma once
// Triangle meshes and the shapes the engine makes itself: boxes (rounded),
// spheres, cylinders, cones, capsules, tori, grids, tubes along curves,
// lathes, height maps. Plus the operations every tool needs: transform,
// merge, normals, tangents, bounds, welding, displacement.
#include <cstdint>
#include <functional>
#include <vector>

#include "engine/core/math.h"

namespace loop {

struct Mesh {
  std::vector<Vec3> positions, normals;
  std::vector<Vec2> uvs;
  std::vector<Vec4> tangents;  // xyz + handedness
  std::vector<uint32_t> indices;

  size_t vertexCount() const { return positions.size(); }
  size_t triangleCount() const { return indices.size() / 3; }
  AABB bounds() const;
  uint32_t addVertex(Vec3 p, Vec3 n, Vec2 uv);
  void addTriangle(uint32_t a, uint32_t b, uint32_t c) { indices.insert(indices.end(), {a, b, c}); }
  void addQuad(uint32_t a, uint32_t b, uint32_t c, uint32_t d) { addTriangle(a, b, c), addTriangle(a, c, d); }
  void append(const Mesh& m, const Mat4& xf = Mat4());
  void transform(const Mat4& xf);
  void computeNormals();   // area-weighted smooth normals
  void computeTangents();  // per-vertex from uvs (Lengyel)
  void flatShade();        // unshare vertices so every face is flat
  // Moves each vertex along its normal by f(position, normal).
  void displace(const std::function<float(Vec3, Vec3)>& f);
  // Merges vertices closer than `eps` (positions only; recompute normals).
  void weld(float eps = 1e-5f);
  float surfaceArea() const;
  float volume() const;  // signed, for closed meshes
};

namespace shapes {
Mesh box(Vec3 size);
// A box with edges rounded to `radius` (`segments` steps across each rounded edge;
// the flat middle of a face is one cell, so a small rounding costs little).
Mesh roundedBox(Vec3 size, float radius, int segments = 4);
Mesh sphere(float radius, int segments = 48, int rings = 24);
Mesh icosphere(float radius, int subdivisions = 3);
Mesh cylinder(float radius, float height, int segments = 32, bool caps = true);
Mesh cone(float radius, float height, int segments = 32);
Mesh capsule(float radius, float height, int segments = 24, int rings = 8);
Mesh torus(float major, float minor, int segments = 48, int sides = 24);
Mesh plane(Vec2 size, int divX = 1, int divZ = 1);  // in XZ, facing +Y
// A shape spun around Y from a profile of (radius, height) points.
Mesh lathe(const std::vector<Vec2>& profile, int segments = 48);
// A tube following `path`, its radius at each point.
Mesh tube(const std::vector<Vec3>& path, const std::vector<float>& radius, int sides = 12, bool caps = true);
// A grid over [0,size.x]x[0,size.y] (centred), height from the callback.
Mesh heightfield(Vec2 size, int resX, int resZ, const std::function<float(float, float)>& height);
}  // namespace shapes

}  // namespace loop
