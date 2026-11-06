#pragma once
// Engine settings: every value the editor, a project or a scene's script can
// tune, by one name each ("render.shadowDistance", "physics.iterations").
// The same names are used in the editor (web/editor/settings_schema.js), in
// project.json ("settings": {...}) and in Looped Script (`setting "…", v`).
//
// Three layers, highest first: the editor (live changes while editing), the
// scene (its script asked), the project (project.json). Without any of them a
// setting has its default. Engine code reads the plain fields in config().
#include <functional>
#include <string>
#include <vector>

#include "engine/core/json.h"
#include "engine/core/math.h"

namespace loop {

struct Config {
  struct Render {
    int targetFps = 60, framesInFlight = 2;
    bool pauseHidden = true;
    float res = 0;  // 0 automatic, else held at this share of full size
    float ratioPhone = 1, ratioHigh = 1.5f, ratioUltra = 1;
    float floor = 0.5f, floorUltra = 0.35f;
    float slowGpu = 110, climbUnder = 100;  // % of the frame budget
    float calm = 3;                          // seconds steady before climbing
    int msaa = 4;
    bool fxaa = false;
    bool filmGrain = false;
    bool cull = true;  // skip what a pass can't see
    bool clusters = false;  // draw from the cook's cluster trees (the rebuild's step 4, off until it's done)
    float clusterError = 1;  // pixels: the most a simpler cluster may look different from the full shape
    bool sunShadows = true;
    int sunMapPhone = 1024, sunMapHigh = 2048;
    float shadowDistance = 0;  // m half-width; 0 = the quality's own (25 / 40)
    float shadowBias = 0.0015f, shadowSoftness = 1.8f;
    int lamps = -1, rays = -1;
    bool lampShadows = true;
    int maxLights = 256;
    int textureSize = 256;  // model pictures on the GPU, each this many pixels square
    float skyLight = 1, occlusion = 1;
    bool passScene = true, passExposure = true, passBloom = true, passFilm = true, passShadow = true, passDraw = true;
    int prepass = 2;  // depth pre-pass: 0 off, 1 on, 2 automatic (many lamps)
    int view = 0;
  } render;
  struct Ultra {
    int bounces = 5, spp = 1, history = 0;  // history 0 = keep sharpening forever
    float clamp = 4;
    bool denoise = true;
    int denoisePasses = 4;
    bool glossy = true, glass = true, dof = true, sunShadows = true, lampShadows = true;
  } ultra;
  struct Cinema {
    std::string quality = "";  // "" = each word's own (render: good, film: good)
    int width = 0, height = 0, samples = 0, bounces = 10, fps = 24, threads = 0;  // 0 = the word's own / from quality / all
    bool denoise = true, dispersion = true, motionBlur = true;
    std::string folder = "frames";
  } cinema;
  struct Camera {
    float adaptSpeed = 1, minEv = -12, maxEv = 23;
    float shake = 0, smoothing = 18;
    bool portraitWide = true;  // an upright screen keeps the lens's width
  } camera;
  struct World {
    float drawDistance = 4000, patternFade = 1, foliageDensity = 1, terrainRes = 1;
    float lodBias = 0;  // levels of detail: + full detail farther, - simpler sooner
  } world;
  struct Physics {
    Vec3 gravity{0, -9.81f, 0};
    int rate = 120, iterations = 16;
    float sleep = 0.05f, friction = 0.6f, bounce = 0.2f;
    float charRadius = 0.25f, charStep = 0.3f, charSlope = 53, charHeight = 1.1f;
    int clothIterations = 8;
    Vec3 wind{0};
  } physics;
  struct Player {
    float walk = 1.6f, run = 4.5f, look = 1, eye = 1.6f, bob = 0.025f, accel = 7;
    bool invertY = false;
    float touchLook = 1.6f, stickSize = 110, deadzone = 0.15f;
    std::string kForward = "KeyW", kBack = "KeyS", kLeft = "KeyA", kRight = "KeyD", kRun = "ShiftLeft", kUse = "KeyE";
  } player;
  struct Anim {
    bool ik = true, breathing = true, armSwing = true;
    float blend = 1.0f / 6;
    int maxPeople = 200;
  } anim;
  struct Audio {
    float master = 1, ambience = 1, effects = 1, footsteps = 1, dialogue = 1, music = 1;
    float reverb = 1, distance = 1;
    bool earDelay = true, airAbsorb = true, doppler = false, limiter = true;
    int buffer = 2048;
  } audio;
  struct Script {
    int tick = 120;
    float catchUp = 8;
    int maxSteps = 12;
    float timeScale = 1;
    std::string onError = "continue";  // continue | stop | pause
    bool autoplay = false;
    std::string choices = "0";
  } script;
  struct Story {
    float textSpeed = 15, lineMin = 2, subtitleSize = 1;
    bool speakerNames = true;
    std::string speakerColor = "#ffd27a";
    float choiceTimeout = 0;
    int timeoutPick = 0;
    float fade = 0.6f;
    std::string loadingText = "Loading {scene}…";
    bool hud = true;
  } story;
  struct Perf {
    float hitch = 25;
  } perf;
  struct Project {
    std::string mode = "";  // a scene without `mode` uses this ("" = game)
  } project;
};

// The values in effect (read these; set them through Settings).
Config& config();

class Settings {
 public:
  enum class Type { kBool, kInt, kFloat, kVec3, kText };
  enum Layer { kProject = 0, kScene = 1, kEditor = 2 };
  struct Entry {
    std::string id;
    Type type;
    void* field;
    Json def;
    double min = -1e30, max = 1e30;
    std::string help;
    Json layers[3];  // null = not set in that layer
  };

  static Settings& get();
  // Sets one setting in a layer (a null Json clears it there). False, with a
  // reason, for an unknown name ("did you mean"), a value of the wrong kind, or
  // a number outside the setting's safe range (nothing is changed then).
  bool set(Layer layer, const std::string& id, const Json& value, std::string* error = nullptr);
  // The same checks without setting anything.
  bool check(const std::string& id, const Json& value, std::string* error = nullptr) const;
  void clear(Layer layer);                      // every value in that layer
  bool setMany(Layer layer, const Json& object, std::string* error = nullptr);
  Json value(const std::string& id) const;      // in effect
  const Entry* find(const std::string& id) const;
  std::string suggest(const std::string& id) const;
  const std::vector<Entry>& entries() const { return entries_; }
  Json list() const;                            // [{id, type, default, value, min, max, help, layers}]
  // Called after a setting changes (its id); `prefix` matches the start of ids.
  int watch(const std::string& prefix, std::function<void(const std::string& id)> fn);
  void unwatch(int handle);
  void resetAll();                               // every layer cleared, defaults back

 private:
  Settings();
  void apply(Entry& e);
  std::vector<Entry> entries_;
  struct Watcher { int handle; std::string prefix; std::function<void(const std::string&)> fn; };
  std::vector<Watcher> watchers_;
  int nextHandle_ = 1;
};

}  // namespace loop
