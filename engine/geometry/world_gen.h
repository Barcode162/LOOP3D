#pragma once
// World generation like nature (the rebuild's step 12, the picture pipeline's
// phase 10): land made the way nature makes it, in the order it does, on a
// grid (32 m a cell for an 8 km world).
//   1. Plates: the crust in a few plates, each drifting its own way; where two
//      push together the land is lifted into mountains, where they pull apart
//      it sinks into a rift; land plates stand high, sea plates low.
//   2. Rivers carving: while the land rises, rain runs downhill and cuts it
//      (stream power: the more water and the steeper, the faster it cuts),
//      many thousands of years in a few dozen steps (Braun and Willett's
//      implicit method, so it stays stable at any step).
//   3. Slopes rest: nothing steeper than loose rock can stand (talus angle).
//   4. Lakes: where water can't get out, it fills the hollow to its rim.
//   5. Climate: colder higher up (6.5 C a kilometre) and towards one side;
//      wind carries wet air from the sea, which rains out as it climbs the
//      mountains, so their far side is dry (the rain shadow).
//   6. The ground: sea, sand on the shore, grass, forest where it is wet and
//      warm enough, bare rock where steep, snow where cold; and how dense the
//      forest grows.
// Tested against nature's own rules (tests/world_gen_test.cpp): water always
// finds a way down, rivers gather into bigger ones, slopes rest, mountains
// cast rain shadows. Pure code: the script's `world` word streams it
// (engine/geometry/world_stream.h).
#include <cstdint>
#include <vector>

#include "engine/core/math.h"

namespace loop {

struct WorldSettings {
  float size = 8192;      // metres across, centred on 0
  int cells = 257;        // grid points each way
  uint32_t seed = 1;
  int plates = 7;
  float landShare = 0.6f; // of the plates, how many are land
  float mountains = 1;    // how hard the plates push (1: peaks of 1-2 km)
  int ages = 40;          // steps of rivers carving
  float wind = 0;         // where the wind comes from (radians: 0 from -x, the west)
  float warmth = 14;      // C at sea level in the middle
};

struct WorldMap {
  enum Ground : uint8_t { kSea, kLake, kSand, kGrass, kForest, kRock, kSnow };
  int n = 0;
  float size = 0, cell = 0;
  std::vector<float> height;  // metres (sea level 0)
  std::vector<float> water;   // the water's level where there is water (lakes, sea), else the height
  std::vector<float> flow;    // water passing through: the area draining here (m2)
  std::vector<float> rain;    // 0..1
  std::vector<float> warmth;  // C
  std::vector<float> forest;  // 0..1 how dense trees grow
  std::vector<uint8_t> ground;
  std::vector<uint8_t> plate;
  std::vector<int> down;      // where its water goes (a neighbour's index), or -1 (sea, the edge)
  size_t at(int x, int z) const { return static_cast<size_t>(z) * static_cast<size_t>(n) + static_cast<size_t>(x); }
  Vec2 place(int x, int z) const { return {(static_cast<float>(x) / (n - 1) - 0.5f) * size, (static_cast<float>(z) / (n - 1) - 0.5f) * size}; }
  // blended between the grid points, at world x, z
  float heightAt(float x, float z) const;
  float sample(const std::vector<float>& f, float x, float z) const;
  uint8_t groundAt(float x, float z) const;
  float slopeAt(int x, int z) const;  // rise over run, steepest neighbour
};

WorldMap generateWorld(const WorldSettings& s);

}  // namespace loop
