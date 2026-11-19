#pragma once
// The light bake (docs/LIGHT_LAB.md, round 3): the light falling on every
// still surface, worked out once by the film renderer's path tracer (every
// lamp with its own shadow, the sky, glows, the light bounced between them)
// and kept, so the phone reads it instead of adding up the lamps.
//
// No unwrapping and no light-map coordinates: the light is kept in cells of
// space a few centimetres wide, one per way a surface can face (the six
// directions), found by a hash of the cell (a "surface cache", after the
// world-space hash grids of GI-1.0, Boissé 2023). A surface reads the four
// cells round it in its own plane, blended. Opposite faces of a thin thing
// (a lamp's shade, a shelf) face opposite ways, so they never share a cell:
// light can't leak through them.
#include <cstdint>
#include <vector>
#include "engine/core/math.h"
#include "engine/render/scene.h"

namespace loop {
class PathTracer;

struct SurfaceLight {
  bool on = false;
  Vec3 origin;       // the low corner of cell (0, 0, 0)
  float cell = 0.1f; // m
  // per cell: its key (see key()), the point and facing its light is baked at
  // (the average of the surface found in it), the light (irradiance), rays so far
  std::vector<uint32_t> keys;
  std::vector<Vec3> at, normal, light;
  std::vector<int> rays;
  // the hash table: per slot a cell's number, or -1 (a power of two long)
  std::vector<int> slots;
  int cells() const { return static_cast<int>(keys.size()); }
  static constexpr uint32_t kEmpty = 0xFFFFFFFFu;
  // The cell (x, y, z) facing way f (0..5: +x -x +y -y +z -z) as one number: x 10 bits, y 9, z 10, f 3.
  static uint32_t key(int x, int y, int z, int f);
  static uint32_t hash(uint32_t key);  // the same sums as the shader's
  int find(uint32_t key) const;        // the cell's number, or -1
  // The light on a surface at p facing n, blended as the shader blends it;
  // `found`: false where no cell is near (that surface is lit live).
  Vec3 irradiance(Vec3 p, Vec3 n, bool* found = nullptr) const;
  // What the GPU reads: two words a slot (the key, the light as RGB9E5), table-long.
  std::vector<uint32_t> packed() const;
  // The table made again from the keys (after load).
  void index();
  // As a file, made ahead by `loop bake` and shipped beside the scene, so the
  // phone only reads it: "LBK1", the scene's fingerprint, the cells' corner and
  // size, then each cell's key and light (no table: made again on loading).
  std::vector<uint8_t> save(const std::vector<float>& signature) const;
  bool load(const uint8_t* data, size_t size, std::vector<float>* signature);
};

// What the bake depends on, so a file baked from another version of the scene
// is never used: each still shape's points, triangles and box in the world,
// each lamp's kind, place, lumens and colour. Kept as numbers, not a hash: the
// phone works a scene out a hair differently from a computer (its own sines),
// so two signatures are the same scene when every number is within a
// centimetre (or a thousandth of itself).
std::vector<float> bakeSignature(const Scene& scene);
bool sameScene(const std::vector<float>& a, const std::vector<float>& b);
// The cells to bake, nearest `eye` first.
std::vector<int> bakeOrder(const SurfaceLight& bake, Vec3 eye);

// Cells over the still surfaces (not skins, not glass) within `box`, `cell`
// apart (wider when the box is too large for the key, or more than `maxCells`
// would be made).
SurfaceLight layOutSurfaceLight(const Scene& scene, const AABB& box, float cell = 0.1f, int maxCells = 1 << 18);
// `rays` more estimates into cell i (the path tracer was given the same scene).
void bakeCell(SurfaceLight& bake, int i, PathTracer& pt, int rays = 64, int bounces = 2, uint32_t seed = 1);
// Light as one word (three 9-bit parts sharing a 5-bit exponent), and back.
uint32_t packRgb9e5(Vec3 c);
Vec3 unpackRgb9e5(uint32_t w);
}  // namespace loop
