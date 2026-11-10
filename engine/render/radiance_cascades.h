#pragma once
// Radiance cascades (Sannikov 2023), the picture pipeline's phase 9
// experiment: tested against the light cache (engine/render/light_cache.h)
// on the same rooms, with the same ray oracle (the film renderer's), for the
// same job: the soft light falling on a surface.
//
// Light from near things changes quickly from place to place but needs few
// directions; light from far things needs many directions but changes slowly
// from place to place. So level 0 has probes close together, each looking a
// short way in few directions; each level up has probes twice as far apart,
// looking twice as far, in four times the directions. Each level's rays cover
// only their own stretch of distance; a ray that meets nothing in its stretch
// takes the light the level above found farther on, that way (merging), so
// level 0 ends up holding all of the light, from near and far.
//
// Directions are an octahedral grid (N x N at level 0, 2N x 2N the next), so a
// direction's four children above are its 2 x 2 texels. CPU only: an
// experiment measured with the error and the rays it costs (`loop research`).
#include <cstdint>
#include <vector>

#include "engine/core/math.h"
#include "engine/geometry/bvh.h"

namespace loop {

class PathTracer;

struct RadianceCascades {
  struct Level {
    Vec3 origin;
    Vec3 step{1};  // between probes (each way: the grid fits the box exactly)
    int nx = 0, ny = 0, nz = 0;
    int dirs = 4;          // octahedral grid side: dirs x dirs directions
    float t0 = 0, t1 = 1;  // the stretch of distance its rays cover
    std::vector<Vec3> light;      // per probe, per direction: the light along it (merged: all of it)
    std::vector<uint8_t> through; // per probe, per direction: met nothing in its stretch
    int probes() const { return nx * ny * nz; }
    Vec3 probePos(int x, int y, int z) const { return origin + Vec3(static_cast<float>(x), static_cast<float>(y), static_cast<float>(z)) * step; }
    size_t at(int x, int y, int z, int d) const { return (static_cast<size_t>((z * ny + y) * nx + x)) * static_cast<size_t>(dirs * dirs) + static_cast<size_t>(d); }
  };
  std::vector<Level> levels;
  uint64_t rays = 0;  // what building it cost
  // The light falling on a surface at p facing n (from level 0, blended between its probes).
  Vec3 irradiance(Vec3 p, Vec3 n) const;
};

// Levels over the box: level 0's probes `spacing` apart with dirs x dirs
// directions, its rays out to `reach`; each level up twice the spacing and
// the reach, four times the directions; the last one's rays go on forever.
RadianceCascades buildCascades(PathTracer& pt, const AABB& box, float spacing, int levels, int dirs, float reach, int bounces = 2,
                               uint32_t seed = 1);

// The octahedral grid's direction at texel (u, v) of an n x n grid (its middle), and its solid angle.
Vec3 octDirection(int u, int v, int n);
float octSolidAngle(int u, int v, int n);

}  // namespace loop
