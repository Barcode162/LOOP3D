#pragma once
// Sound, made by the engine itself (no recordings, so nothing to license):
// oscillators, noise, envelopes and filters build every sound; a mixer
// places them in 3D (left/right, the delay between the ears, distance and
// air softening far sounds) and a reverb
// gives the room its size.
//
// Universal sounds, by name, for any scene: footsteps on wood, stone, grass,
// gravel, snow, water and carpet; knocks and clinks of every material
// (wood, metal, glass, stone, cloth); fire, wind, rain, a stream, birds,
// crickets, a bell, and soft interface clicks.
#include <cstdint>
#include <memory>
#include <string>
#include <vector>

#include "engine/core/math.h"

namespace loop::audio {

constexpr int kRate = 48000;

// A sound that makes samples (mono, -1..1) until it's done.
class Voice {
 public:
  virtual ~Voice() = default;
  // Adds `n` samples to `out`; false when finished.
  virtual bool render(float* out, int n) = 0;
};

// One-shot sounds by name; `strength` 0..1 (how hard), `seed` varies it.
// Footsteps: "footstep" with surface wood|stone|grass|gravel|snow|water|carpet|metal.
// Impacts: "impact" with material wood|metal|glass|stone|cloth|plastic.
// Others: "bell", "click", "whoosh", "thud".
std::unique_ptr<Voice> makeSound(const std::string& name, const std::string& variant, float strength, uint32_t seed);
// Endless sounds: "fire", "wind", "rain", "stream", "birds", "crickets", "hum", "night".
std::unique_ptr<Voice> makeAmbience(const std::string& name, float intensity, uint32_t seed);
std::vector<std::string> soundNames();
std::vector<std::string> ambienceNames();
// The surface/material word a material name sounds like ("oak" → "wood").
std::string surfaceOf(const std::string& materialName);

// What a sound is, for its volume setting (audio.ambience, audio.effects, …).
enum class Category { kAmbience, kEffects, kFootsteps, kDialogue, kMusic };

class Mixer {
 public:
  Mixer();
  ~Mixer();
  struct Listener {
    Vec3 position{0}, forward{0, 0, -1}, up{0, 1, 0}, velocity{0};
  } listener;
  float masterVolume = 0.8f;
  float roomSize = 0.3f, roomWet = 0.15f;  // reverb (0 outside .. 1 a cathedral)

  // Plays a voice at a place (or everywhere when `spatial` is false).
  // Returns an id for moving it, changing its volume or stopping it.
  // The engine's audio settings (config().audio) scale it by category and
  // turn ear delay, air, Doppler and the limiter on or off.
  int play(std::unique_ptr<Voice> v, Vec3 at, float volume = 1, bool spatial = true, bool loop = false, Category category = Category::kEffects);
  void move(int id, Vec3 at, Vec3 velocity = {});
  void setVolume(int id, float volume);
  void stop(int id, float fadeSeconds = 0.3f);
  bool playing(int id) const;
  size_t voices() const;

  // Interleaved stereo frames.
  void render(float* stereo, int frames);

 private:
  struct Impl;
  std::unique_ptr<Impl> impl_;
};

// 16-bit PCM WAV of interleaved stereo.
bool writeWav(const std::string& path, const std::vector<float>& stereo, int rate = kRate);

}  // namespace loop::audio
