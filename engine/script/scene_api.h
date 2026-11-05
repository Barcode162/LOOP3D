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
#include <deque>
#include <map>
#include <memory>
#include <string>
#include <vector>

#include "engine/anim/anim.h"
#include "engine/audio/audio.h"
#include "engine/geometry/terrain.h"
#include "engine/physics/physics.h"
#include "engine/render/scene.h"
#include "engine/render/shots.h"
#include "engine/script/vm.h"

namespace loop::script {

struct RenderRequest {
  std::string file;
  int width = 1920, height = 1080, samples = 256;
  int bounces = 8;
  bool denoise = true, dispersion = true, motionBlur = true;
  bool film = false;  // render every frame of the shots
  float at = 0;       // script time to render at (stills)
};

struct Stage {
  Scene scene;
  Film film;
  std::vector<RenderRequest> renders;
  std::deque<HeightMap> terrains;  // a deque: physics keeps pointers into it
  struct Placed {
    int instance;
    Vec3 position{0}, scale{1};
    Vec3 rotation{0};  // yaw, pitch, roll (rad)
    bool visible = true;
    std::string name;
    // what it is, for physics: 'b' box (size), 's' sphere (radius), 'c' capsule, 'm' other
    char kind = 'm';
    Vec3 size{1};
    int body = -1;     // physics body when solid
    bool useQuat = false;
    Quat quat;
    Vec3 lastVelocity{0};
    bool collide = false;
  };
  // People: a skeleton, its animator and the shapes that make the body.
  struct Person {
    std::unique_ptr<anim::Skeleton> skeleton;
    std::unique_ptr<anim::Animator> animator;
    std::vector<std::pair<int, std::pair<int, int>>> parts;  // placed index, (from bone, to bone)
    Vec3 walkTo{0};
    bool walking = false;
    float speed = 1.4f;
    std::string footSurface;  // "" = from the ground's material
    bool visible = true;
  };
  std::vector<Person> people;
  struct SoundRef {
    int mixerId;
    Vec3 at{0};
    float volume = 1;
  };
  std::vector<SoundRef> sounds;
  physics::World world;
  audio::Mixer mixer;
  std::string groundSurface = "stone";
  bool collidersDirty = false;
  std::map<std::string, int> meshCache;  // shapes already made (shared between copies)
  // Moves the living world on: physics, people, sounds. Call each frame
  // with the script's update.
  void update(float dt);
  void setListener(const Camera& cam);
  std::vector<Placed> placed;   // shape things → instances
  uint64_t version = 0;         // bumps on any change (the live renderer re-reads)
  uint64_t transformsVersion = 0;  // only placements moved
  uint64_t lightsVersion = 0;      // only lamps changed
  std::string title = "Untitled";
  std::string mode = "game";    // "game" or "movie"
  // For the page: lines to show, questions to ask, the next scene to load.
  struct UiMessage {
    std::string kind;     // "line", "ask", "clear"
    std::string speaker, text;
    std::vector<std::string> options;
    float seconds = 0;
  };
  std::vector<UiMessage> ui;     // taken by the player each frame
  std::string nextScene;         // set by go_to
  Vec3 playerPos{0};             // where the player is (the player sets it each frame)
  bool autoplay = false;
  // move_player(): the player is carried somewhere new (the player takes it)
  bool teleport = false;
  Vec3 teleportTo{0}, teleportLook{0, 0, -1};
  bool story = false;            // the script has spoken a line or asked (a story scene)         // `loop play`: the player is always near enough
  void updateTransform(int placedIndex);
};

// Adds every world-making word to `vm`, filling `stage`.
void installSceneApi(Vm& vm, Stage& stage);

}  // namespace loop::script
