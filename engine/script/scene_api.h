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
#include "engine/anim/clip.h"
#include "engine/core/json.h"
#include "engine/audio/audio.h"
#include "engine/audio/music.h"
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

struct StreamedWorld;  // a generated world, its land and trees streamed round the player (scene_api.cpp)

struct Stage {
  Scene scene;
  std::shared_ptr<StreamedWorld> streamed;  // world(): made a chunk at a time where the player goes
  void streamWorld();
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
    std::vector<int> moreInstances;  // a model's other parts, moved with it
    int line = 0;  // the script line that placed it (0: the engine made it, like a person's limbs)
    // The one clock: where it is drawn now (its last placement) and where it
    // was at the end of the step before it last moved, so the drawing can
    // blend between the two; the step it last moved in.
    Vec3 drawnPos{0}, drawnScale{1}, prevPos{0}, prevScale{1};
    Quat drawnQuat, prevQuat;
    uint64_t movedAt = ~0ull;
    bool queued = false;  // in `moved`, waiting for the drawing to take it
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
  // A model of several parts (a tree's bark and leaves, a flower, a log, a
  // whole meadow of grass): one handle moves or hides them all.
  // The word `group` makes one too: everything placed in its block (things,
  // lamps, the groups inside it), measured from its place and turn.
  struct Group {
    std::vector<int> parts;  // placed indices
    Vec3 position{0};
    bool visible = true;
    float turn = 0;               // about the upright axis (rad)
    std::vector<int> lights;      // lamps that move, turn and go dark with it
    std::vector<int> children;    // groups made inside it
    std::vector<float> litLumens; // the lamps' lumens while it is hidden
    std::string name;
  };
  std::vector<Group> groups;
  // Clips of keys playing on things, lamps and groups (the words `clip` and
  // `key`; engine/anim/clip.h). Each step a playing clip moves on and puts
  // its target where its keys say; the same in film and live.
  struct Playing {
    anim::Clip clip;
    char kind = 't';    // 't' a thing, 'l' a lamp, 'g' a group, 'n' a clip by name, on nothing yet
    int target = -1;    // which one (placed, light or group index)
    int line = 0;       // the script line of its `clip`
    double moment = 0;  // the clip's own time, in seconds
    double speed = 1;   // 1 forwards, -1 backwards
    bool playing = true;
    bool moving = false;  // it changed its target in the last step
  };
  std::deque<Playing> clips;
  std::vector<int> clipsBeingRead;  // clips whose lines are being read: `key` adds to the last
  // Puts a clip's target where the clip is at its moment. True when something changed.
  bool showClip(int clip);
  // Sets a clip's moment and shows it, without the world's time moving (scrubbing).
  void seekClip(int clip, double moment);
  void updateClips(float dt);
  // Moves and turns a group about its own place: its things, lamps and inner groups.
  void placeGroup(int group, Vec3 position, float turn);
  void showGroup(int group, bool visible);
  // Small creatures the engine moves itself each step (no script needed):
  // butterflies (a body and two flapping wings) and fireflies (glowing motes).
  struct Critter {
    char kind = 'b';            // 'b' butterfly, 'f' firefly
    std::vector<int> parts;     // placed: body, left wing, right wing (butterfly) or the mote
    Vec3 home{0}, pos{0}, vel{0}, target{0};
    float radius = 2, height = 1, phase = 0, speed = 1, size = 1, wait = 0;
    uint32_t seed = 1;
    bool visible = true;
  };
  std::vector<Critter> critters;
  void updateCritters(float dt);
  struct SoundRef {
    int mixerId;
    Vec3 at{0};
    float volume = 1;
  };
  std::vector<SoundRef> sounds;
  physics::World world;
  audio::Mixer mixer;
  audio::Sequencer music;   // tempo and tracks (engine/audio/music.h), in time with the world
  uint32_t musicSeed = 1;
  std::string groundSurface = "stone";
  bool collidersDirty = false;
  std::map<std::string, int> meshCache;  // shapes already made (shared between copies)
  // Models already read (file → its parts as mesh and material), so 276
  // bottles cost one read and share their meshes.
  std::map<std::string, std::vector<std::pair<int, int>>> modelCache;
  std::map<uint64_t, int> textureByContent;
  std::map<std::string, Json> materialTables;  // models' materials.json, by path
  // Invisible things to bump into (blocker): a box mesh and where it stands.
  std::vector<std::pair<int, Mat4>> blockers;  // the same picture in two models is kept once
  std::string folder;  // where the scene's own files are (models): set by the host, "" = here
  // Moves the living world on by one step of the fixed clock: physics,
  // people, critters, sounds. Call after the script's update for the same
  // step; `steps` counts them.
  void update(float dt);
  uint64_t steps = 0;
  // What moved since the drawing last asked, and where to draw it: every
  // instance of those things with its transform, blended `alpha` (0..1) of
  // the way from where it was at the step before the last to where it is
  // (alpha = how far the clock is into the next step). Things still blending
  // come back next time with their final place. Clears the list.
  void takeMoves(float alpha, std::vector<int>& instances, std::vector<Mat4>& transforms);
  void setListener(const Camera& cam);
  // Decodes the models' pictures still waiting, all at once on every core
  // (a scene's script places its models first). Call before the scene is
  // drawn or rendered; cheap when none wait. Returns how many couldn't be read.
  int decodePictures(std::string* error = nullptr) { return decodeTextures(scene.textures, error); }
  std::vector<Placed> placed;   // shape things → instances
  std::vector<int> moved;       // placed things moved since the drawing took them (see takeMoves)
  std::vector<int> blending_;   // placed things drawn part-way last time
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
  bool autoplay = false;         // `loop play` and script.autoplay: the player is always near enough
  // move_player(): the player is carried somewhere new (the player takes it)
  bool teleport = false;
  Vec3 teleportTo{0}, teleportLook{0, 0, -1};
  bool story = false;            // the script has spoken a line or asked (a story scene)
  void updateTransform(int placedIndex);
  // What a ray meets first: the placed thing (its index) whose triangles it
  // hits nearest, or -1. Hidden things are passed through. For the editor's
  // tap-to-select; `distance` gets how far along the ray.
  int pick(const Ray& ray, float* distance = nullptr) const;
};

// Adds every world-making word to `vm`, filling `stage`.
void installSceneApi(Vm& vm, Stage& stage);

}  // namespace loop::script
