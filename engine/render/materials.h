#pragma once
// A library of real materials by name (measured colours and roughness of
// real surfaces, linear light), so a story can say "made of oak" or
// "brass" and get something believable: woods, stones, metals, glass and
// liquids, fabrics, skin, plants, the ground, glowing things.
#include <string>
#include <vector>

#include "engine/render/scene.h"

namespace loop {
bool materialByName(const std::string& name, Material& out);
std::vector<std::string> materialNames();
}  // namespace loop
