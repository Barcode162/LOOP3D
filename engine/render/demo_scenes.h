#pragma once
// Scenes built in C++ to show and test the renderer. (Scripts build their
// own with Looped Script; these stay as the renderer's reference shots.)
#include <string>

#include "engine/render/scene.h"

namespace loop {
bool buildDemoScene(const std::string& name, Scene& s);
}
