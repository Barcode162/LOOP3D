#pragma once
// Looped Script's words for making a world: shapes with real materials,
// land, forests, rocks, lamps, the sun, sky and fog, a physical camera,
// a film look, cinematic shots and renders. A script fills a Stage; the
// live player draws it every frame (things scripts move, move), and the
// `loop` command renders its stills and films with the cinema renderer.
//
//   let oak = material("oak")
//   box size: vec(2, 0.05, 1), at: vec(0, 0.75, 0), material: oak
//   sun elevation: 12 deg, toward: 200 deg
//   camera at: vec(0, 1.6, 4), look_at: vec(0, 0.8, 0), lens: 35 mm
//   look "golden"
//   render file: "table.png", quality: "cinema"
#include <string>
#include <vector>

#include "engine/geometry/terrain.h"
#include "engine/render/scene.h"
#include "engine/render/shots.h"
#include "engine/script/vm.h"

namespace loop::script {

struct RenderRequest {
  std::string file;
  int width = 1920, height = 1080, samples = 256;
  bool film = false;  // render every frame of the shots
  float at = 0;       // script time to render at (stills)
};

struct Stage {
  Scene scene;
  Film film;
  std::vector<RenderRequest> renders;
  std::vector<HeightMap> terrains;
  struct Placed {
    int instance;
    Vec3 position{0}, scale{1};
    Vec3 rotation{0};  // yaw, pitch, roll (rad)
    bool visible = true;
    std::string name;
  };
  std::vector<Placed> placed;   // shape things → instances
  uint64_t version = 0;         // bumps on any change (the live renderer re-reads)
  uint64_t transformsVersion = 0;
  std::string title = "Untitled";
  std::string mode = "game";    // "game" or "movie"
  void updateTransform(int placedIndex);
};

// Adds every world-making word to `vm`, filling `stage`.
void installSceneApi(Vm& vm, Stage& stage);

}  // namespace loop::script
