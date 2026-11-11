#pragma once
// The light cache (the picture pipeline's phase 6, docs/PIPELINE.md): a grid
// of probes over the scene, each holding the light arriving at it from every
// direction except the lamps and the sun seen straight on, which the live
// renderer works out itself: the light bounced off walls, floors and things,
// and the sky's light where it really gets in. A surface reads the probes
// round it (blended between them) for its soft light, so a closed room is lit
// by its own walls instead of the open sky.
//
// Each probe is baked by the film renderer's path tracer (engine/render/
// pathtracer.h: "cinema bakes the light cache"), a few probes at a time, so it
// can run in slices between frames; it knows nothing of the graphics chip.
// A probe stores the light falling on six faces (+x -x +y -y +z -z): exact
// for floors, walls and ceilings, blended between them by the surface's
// direction for the rest. (The first two orders of the light's spread were
// tried first: they overstate light arriving from the side, which turned the
// courtyard's shaded walls red.) A probe inside something (most of its rays
// hit the backs of surfaces) takes its neighbours' light instead.
#include <array>
#include <cstdint>
#include <vector>

#include "engine/core/math.h"
#include "engine/geometry/bvh.h"

namespace loop {

class PathTracer;

struct LightCache {
  bool on = false;
  Vec3 origin;          // the first probe's place
  float spacing = 1;    // m between probes
  int nx = 0, ny = 0, nz = 0;
  // per probe: the light falling on a face turned +x, -x, +y, -y, +z, -z
  std::vector<std::array<Vec3, 6>> face;
  // per probe and face: how much of what the face looks at is open sky (1: all
  // of it, 0: none, a closed room). The live renderer's glossy surfaces mirror
  // the sky only by this share, and the room's own light for the rest.
  std::vector<std::array<float, 6>> sky;
  enum State : uint8_t { kWaiting, kBaked, kInside };
  std::vector<uint8_t> state;
  // The rooms (the picture pipeline's phase 10, typed relationships): per
  // probe, which of its six neighbours it was found to see (bit d) and which
  // were tested at all (bit d of `tested`); light spreads only along links
  // that are open, so a lamp's change relights its own room (and through an
  // open door, the next), not the room behind the wall.
  std::vector<uint8_t> open, tested;
  int probes() const { return nx * ny * nz; }
  Vec3 probePos(int i) const;
  int probeAt(int x, int y, int z) const { return (z * ny + y) * nx + x; }
  // The light on a surface at p facing n, blended between the probes round it (tests, tools).
  Vec3 irradiance(Vec3 p, Vec3 n) const;
};

// Probes over the box, `spacing` apart (at least `minSpacing`, wider when
// more than `maxProbes` would be needed).
LightCache layOutLightCache(const AABB& box, int maxProbes = 2048, float minSpacing = 1.0f);

// Bakes probe i with `rays` rays through the scene the path tracer was given.
void bakeProbe(LightCache& cache, int i, PathTracer& pt, int rays = 64, int bounces = 3, uint32_t seed = 1);

// Probes inside things take the average of their baked neighbours (spread
// outward until every probe has light). Probes not yet baked are left alone.
void fillInside(LightCache& cache);

// Blends each lit probe a little with its lit neighbours (two fifths its own,
// a tenth each of up to six): light that has bounced changes slowly, so this
// mostly takes away the noise of each probe's few rays.
LightCache smoothed(const LightCache& cache);

// The order to bake in: nearest the eye first.
std::vector<int> bakeOrder(const LightCache& cache, Vec3 eye);

// Something changed in `box` (a lamp's reach, a thing that moved): its probes
// are to be baked again (they keep their light meanwhile, marked waiting).
// Returns how many were marked.
int markNear(LightCache& cache, const AABB& box);
// A lamp at `at` changed, its light reaching `box`: the probes its light can
// get to (from the probes round it, along open links, inside the box) are to
// be baked again, and the probes inside things beside them. Links not yet
// tested count as open (as markNear). Returns how many were marked.
int markReached(LightCache& cache, const AABB& box, Vec3 at);
// The rooms: probes joined by open links (and not inside things) numbered
// alike; -1 for probes inside things.
std::vector<int> rooms(const LightCache& cache);
// The probes waiting to be baked, nearest the eye first.
std::vector<int> waitingOrder(const LightCache& cache, Vec3 eye);

}  // namespace loop
