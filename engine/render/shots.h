#pragma once
// Cinematography: a film is a list of shots; each shot moves the camera
// from one framing to another over its length (dolly, crane, push in, pull
// out), or orbits a subject, with the lens, aperture and focus changing too
// (a focus pull), eased so it starts and stops like a real rig, plus an
// optional handheld wobble and fades. Evaluated at any time for the player
// (live) and for the cinema renderer (frame by frame).
#include <string>
#include <vector>

#include "engine/core/ease.h"
#include "engine/render/scene.h"

namespace loop {

struct Framing {
  Vec3 position{0, 1.6f, 5}, target{0, 1, 0};
  float focalLength = 35, fStop = 2.8f, focus = 0;  // focus 0 = on the target
};

struct Shot {
  std::string name;
  float duration = 4;
  Framing from, to;
  bool moves = false;      // false: holds `from`
  Ease ease = Ease::kInOut;
  // orbit: circle `center` at `radius`, `height`, from angle a0 to a1 (rad)
  bool orbit = false;
  Vec3 center{0};
  float radius = 5, height = 1.6f, angleFrom = 0, angleTo = 1;
  float shake = 0;         // handheld: 0 still .. 1 running with the camera
  float fadeIn = 0, fadeOut = 0;  // seconds of fade from / to black
  std::string look;        // a look for this shot only ("" keeps the film's)
};

struct Film {
  std::vector<Shot> shots;
  float fps = 24;
  float length() const;
  // The camera at time t; `shotIndex` and `fade` (1 = clear, 0 = black).
  Camera at(float t, const Camera& base, int* shotIndex = nullptr, float* fade = nullptr) const;
};

}  // namespace loop
