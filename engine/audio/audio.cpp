#include "engine/audio/audio.h"

#include "engine/core/settings.h"

#include <algorithm>
#include <array>
#include <cstring>
#include <functional>

#include "engine/core/file.h"
#include "engine/core/random.h"

namespace loop::audio {

namespace {

// ------------------------------------------------------------ building blocks
struct Biquad {  // RBJ cookbook filters
  float b0 = 1, b1 = 0, b2 = 0, a1 = 0, a2 = 0, z1 = 0, z2 = 0;
  void set(char type, float freq, float q) {
    float w = kTwoPi * clamp(freq, 10.0f, kRate * 0.45f) / kRate, c = std::cos(w), s = std::sin(w), al = s / (2 * q);
    float a0;
    switch (type) {
      case 'l': b0 = (1 - c) / 2, b1 = 1 - c, b2 = (1 - c) / 2, a0 = 1 + al, a1 = -2 * c, a2 = 1 - al; break;
      case 'h': b0 = (1 + c) / 2, b1 = -(1 + c), b2 = (1 + c) / 2, a0 = 1 + al, a1 = -2 * c, a2 = 1 - al; break;
      default: b0 = al, b1 = 0, b2 = -al, a0 = 1 + al, a1 = -2 * c, a2 = 1 - al; break;  // band-pass
    }
    b0 /= a0, b1 /= a0, b2 /= a0, a1 /= a0, a2 /= a0;
  }
  float operator()(float x) {  // transposed direct form II
    float y = b0 * x + z1;
    z1 = b1 * x - a1 * y + z2;
    z2 = b2 * x - a2 * y;
    return y;
  }
};

struct Noise {
  Rng rng;
  float pink[3] = {0, 0, 0};
  explicit Noise(uint32_t s) : rng(s) {}
  float white() { return rng.uniform() * 2 - 1; }
  float pinkish() {  // Paul Kellet's economy pink
    float w = white();
    pink[0] = 0.99765f * pink[0] + w * 0.0990460f;
    pink[1] = 0.96300f * pink[1] + w * 0.2965164f;
    pink[2] = 0.57000f * pink[2] + w * 1.0526913f;
    return (pink[0] + pink[1] + pink[2] + w * 0.1848f) * 0.25f;
  }
  float brown(float& state) {
    state = clamp(state + white() * 0.02f, -1.0f, 1.0f);
    return state;
  }
};

// A sound written as "sample number → value": short and readable recipes.
class FnVoice : public Voice {
 public:
  FnVoice(int length, std::function<float(int)> f) : len_(length), f_(std::move(f)) {}
  bool render(float* out, int n) override {
    for (int i = 0; i < n; ++i) {
      if (len_ >= 0 && t_ >= len_) return false;
      out[i] += f_(t_++);
    }
    return true;
  }

 private:
  int len_, t_ = 0;
  std::function<float(int)> f_;
};

inline float env(float t, float attack, float decay) {  // fast up, exponential down
  return t < attack ? t / attack : std::exp(-(t - attack) / decay);
}

// A struck object: a few decaying partials (modal synthesis) plus a noisy
// click; the partials' ratios and decay say which material it is.
std::unique_ptr<Voice> modal(std::vector<float> freqs, std::vector<float> decays, std::vector<float> amps, float click, float clickTone, float strength, uint32_t seed) {
  struct State {
    Noise n;
    Biquad bp;
    std::vector<float> f, d, a, ph;
  };
  auto st = std::make_shared<State>(State{Noise(seed), {}, freqs, decays, amps, std::vector<float>(freqs.size(), 0)});
  st->bp.set('b', clickTone, 1.2f);
  Rng r(seed * 31 + 7);
  for (auto& f : st->f) f *= 0.96f + 0.08f * r.uniform();  // no two hits the same
  float longest = 0;
  for (float d : decays) longest = std::max(longest, d);
  int len = static_cast<int>((longest * 2.5f + 0.05f) * kRate);  // by then the ring is ~8% of its start
  float g = 0.25f + 0.75f * strength;
  return std::make_unique<FnVoice>(len, [st, click, g](int i) {
    float t = static_cast<float>(i) / kRate, s = 0;
    for (size_t k = 0; k < st->f.size(); ++k) s += st->a[k] * std::sin(kTwoPi * st->f[k] * t) * std::exp(-t / st->d[k]);
    float c = st->bp(st->n.white()) * click * env(t, 0.0005f, 0.006f);
    return (s * std::min(1.0f, t / 0.0008f) + c) * g;
  });
}

}  // namespace

std::string surfaceOf(const std::string& m) {
  auto has = [&](const char* w) { return m.find(w) != std::string::npos; };
  if (has("oak") || has("pine") || has("walnut") || has("cherry") || has("wood") || has("board") || has("bark") || has("plank")) return "wood";
  if (has("grass") || has("moss") || has("leaves") || has("needles") || has("soil")) return "grass";
  if (has("sand") || has("gravel")) return "gravel";
  if (has("snow") || has("ice")) return "snow";
  if (has("water")) return "water";
  if (has("wool") || has("fabric") || has("velvet") || has("carpet") || has("leather") || has("rug")) return "carpet";
  if (has("gold") || has("silver") || has("copper") || has("brass") || has("bronze") || has("iron") || has("steel") || has("chrome") || has("alumin") ||
      has("metal") || has("rust"))
    return "metal";
  if (has("glass") || has("crystal") || has("porcelain")) return "glass";
  if (has("plastic") || has("rubber")) return "plastic";
  return "stone";
}

std::vector<std::string> soundNames() { return {"footstep", "impact", "bell", "click", "whoosh", "thud"}; }
std::vector<std::string> ambienceNames() {
  return {"fire", "wind", "rain", "stream", "birds", "crickets", "hum", "night", "applause", "crowd", "strings", "chains", "synth", "drones", "heartbeat"};
}

std::unique_ptr<Voice> makeSound(const std::string& name, const std::string& variant, float strength, uint32_t seed) {
  strength = saturate(strength);
  if (name == "impact") {
    const std::string& m = variant;
    if (m == "metal") return modal({520, 1340, 2410, 3970}, {0.9f, 0.6f, 0.35f, 0.2f}, {0.3f, 0.25f, 0.18f, 0.1f}, 0.4f, 4000, strength, seed);
    if (m == "glass") return modal({1900, 4100, 6700}, {0.5f, 0.3f, 0.15f}, {0.25f, 0.2f, 0.1f}, 0.5f, 7000, strength, seed);
    if (m == "wood") return modal({220, 610, 1150}, {0.05f, 0.03f, 0.02f}, {0.5f, 0.3f, 0.15f}, 0.8f, 1500, strength, seed);
    if (m == "cloth" || m == "carpet") return modal({90, 180}, {0.02f, 0.015f}, {0.3f, 0.1f}, 0.6f, 500, strength * 0.6f, seed);
    if (m == "plastic") return modal({480, 1250}, {0.03f, 0.02f}, {0.35f, 0.2f}, 0.7f, 2500, strength, seed);
    return modal({140, 390, 870}, {0.04f, 0.025f, 0.015f}, {0.5f, 0.3f, 0.2f}, 1.0f, 2200, strength, seed);  // stone
  }
  if (name == "footstep") {
    // a heel then a toe: two filtered noise bursts, shaped by the surface
    struct P {
      float tone, q, decay, gain, crunch;
    };
    P p = {900, 0.8f, 0.03f, 0.6f, 0};
    const std::string& s = variant;
    if (s == "wood") p = {600, 1.5f, 0.04f, 0.8f, 0};
    else if (s == "grass") p = {3200, 0.5f, 0.06f, 0.35f, 0.6f};
    else if (s == "gravel") p = {2600, 0.6f, 0.08f, 0.55f, 1.0f};
    else if (s == "snow") p = {1200, 0.5f, 0.12f, 0.45f, 0.8f};
    else if (s == "water") p = {1600, 0.7f, 0.15f, 0.5f, 0.3f};
    else if (s == "carpet") p = {350, 0.7f, 0.03f, 0.35f, 0};
    else if (s == "metal") p = {2000, 4.0f, 0.12f, 0.6f, 0};
    struct St {
      Noise n;
      Biquad f, lo;
      float brown = 0;
    };
    auto st = std::make_shared<St>(St{Noise(seed), {}, {}});
    st->f.set('b', p.tone * (0.9f + 0.2f * toUnit(hash32(seed))), p.q);
    st->lo.set('l', 180, 0.7f);
    float g = (0.3f + 0.7f * strength) * p.gain;
    int len = static_cast<int>((0.09f + p.decay * 5) * kRate);
    return std::make_unique<FnVoice>(len, [st, p, g](int i) {
      float t = static_cast<float>(i) / kRate;
      float heel = env(t, 0.002f, p.decay), toe = t > 0.06f ? env(t - 0.06f, 0.003f, p.decay * 0.8f) * 0.6f : 0;
      float grain = p.crunch > 0 ? (st->n.white() > 0.9f - 0.25f * p.crunch ? 1.0f : 0.2f) : 1.0f;
      float body = st->lo(st->n.white()) * 1.5f * env(t, 0.002f, 0.02f);  // the thump of weight
      return (st->f(st->n.white()) * grain + body) * (heel + toe) * g;
    });
  }
  if (name == "bell") return modal({440, 1100, 1780, 2560, 3400}, {3.0f, 2.0f, 1.2f, 0.8f, 0.5f}, {0.3f, 0.2f, 0.15f, 0.1f, 0.06f}, 0.3f, 3000, strength, seed);
  if (name == "click") return modal({2200}, {0.01f}, {0.3f}, 0.4f, 5000, strength * 0.5f, seed);
  if (name == "thud") return modal({60, 110}, {0.08f, 0.05f}, {0.7f, 0.3f}, 0.5f, 300, strength, seed);
  if (name == "whoosh") {
    struct St { Noise n; Biquad f; };
    auto st = std::make_shared<St>(St{Noise(seed), {}});
    int len = static_cast<int>(0.6f * kRate);
    float g = 0.4f * (0.3f + 0.7f * strength);
    return std::make_unique<FnVoice>(len, [st, len, g](int i) {
      float u = static_cast<float>(i) / len;
      st->f.set('b', 300 + 2500 * std::sin(kPi * u), 1.0f);
      return st->f(st->n.white()) * std::sin(kPi * u) * g;
    });
  }
  return nullptr;
}

std::unique_ptr<Voice> makeAmbience(const std::string& name, float k, uint32_t seed) {
  k = std::max(0.0f, k);
  struct St {
    Noise n;
    Biquad a, b, c;
    float brown = 0, lfo = 0, crackle = 0, chirpT = -1, chirpF = 0;
    Rng r;
  };
  auto st = std::make_shared<St>(St{Noise(seed), {}, {}, {}, 0, 0, 0, -1, 0, Rng(seed + 99)});
  if (name == "fire") {
    st->a.set('l', 500, 0.7f);
    st->b.set('b', 2500, 1.0f);
    return std::make_unique<FnVoice>(-1, [st, k](int i) {
      float t = static_cast<float>(i) / kRate;
      float roar = st->a(st->n.brown(st->brown)) * (0.5f + 0.3f * std::sin(t * 0.7f) + 0.2f * std::sin(t * 2.3f));
      // crackles: random little bursts, now and then a pop
      if (st->r.uniform() < 0.00035f * (0.5f + k)) st->crackle = 0.3f + 0.7f * st->r.uniform();
      st->crackle *= 0.995f;
      float pop = st->b(st->n.white()) * st->crackle;
      return (roar * 0.6f + pop * 0.8f) * k;
    });
  }
  if (name == "wind") {
    return std::make_unique<FnVoice>(-1, [st, k](int i) {
      float t = static_cast<float>(i) / kRate;
      float gust = 0.5f + 0.35f * std::sin(t * 0.23f) + 0.15f * std::sin(t * 0.71f + 1);
      if (i % 64 == 0) st->a.set('b', 250 + 600 * gust, 0.9f);
      return st->a(st->n.pinkish()) * gust * 0.9f * k;
    });
  }
  if (name == "rain") {
    st->a.set('h', 1500, 0.7f);
    st->b.set('b', 4000, 2.0f);
    return std::make_unique<FnVoice>(-1, [st, k](int) {
      float hiss = st->a(st->n.pinkish()) * 0.3f;
      if (st->r.uniform() < 0.002f * k) st->crackle = 0.4f + 0.6f * st->r.uniform();
      st->crackle *= 0.97f;
      return (hiss + st->b(st->n.white()) * st->crackle) * k;
    });
  }
  if (name == "stream") {
    return std::make_unique<FnVoice>(-1, [st, k](int i) {
      // a babble: noise through a band that wanders quickly
      if (i % 32 == 0) {
        float t = static_cast<float>(i) / kRate;
        float wander = std::sin(t * 7.3f) * 0.5f + std::sin(t * 13.1f + 1) * 0.3f + std::sin(t * 29.7f + 2) * 0.2f;
        float f = 600 + 1200 * (0.5f + 0.5f * wander);
        st->a.set('b', f, 2.5f);
      }
      return (st->a(st->n.white()) * 0.7f + st->n.pinkish() * 0.08f) * k;
    });
  }
  if (name == "birds" || name == "crickets" || name == "night") {
    bool birds = name == "birds";
    st->a.set('h', 300, 0.7f);
    return std::make_unique<FnVoice>(-1, [st, k, birds, name](int i) {
      float t = static_cast<float>(i) / kRate;
      float s = 0;
      if (birds) {
        // a chirp now and then: a quick rising-falling whistle
        if (st->chirpT < 0 && st->r.uniform() < 0.00004f * (0.5f + k)) st->chirpT = 0, st->chirpF = 2500 + 2000 * st->r.uniform();
        if (st->chirpT >= 0) {
          float u = st->chirpT / 0.12f;
          st->lfo += kTwoPi * (st->chirpF * (1 + 0.3f * std::sin(kPi * u))) / kRate;
          s = std::sin(st->lfo) * std::sin(kPi * std::min(1.0f, u)) * 0.25f;
          st->chirpT += 1.0f / kRate;
          if (u > 1) st->chirpT = st->r.uniform() < 0.5f ? 0.05f : -1;
        }
        s += st->n.pinkish() * 0.01f;
      } else {
        // crickets: a 4.5 kHz tone pulsed in trains
        float train = std::fmod(t * 2.2f, 1.0f) < 0.35f ? 1.0f : 0.0f;
        float pulse = 0.5f + 0.5f * std::sin(kTwoPi * 30 * t);
        s = std::sin(kTwoPi * 4500 * t) * train * pulse * 0.06f;
        if (name == "night") s += st->a(st->n.pinkish()) * 0.03f;
      }
      return s * k;
    });
  }
  if (name == "hum") {
    return std::make_unique<FnVoice>(-1, [k](int i) {
      float t = static_cast<float>(i) / kRate;
      return (std::sin(kTwoPi * 50 * t) * 0.3f + std::sin(kTwoPi * 100 * t) * 0.15f + std::sin(kTwoPi * 150 * t) * 0.05f) * 0.3f * k;
    });
  }
  if (name == "applause") {
    // many hands: each clap a few ms of bright noise, hundreds a second
    st->a.set('b', 1800, 0.8f);
    st->b.set('h', 400, 0.7f);
    return std::make_unique<FnVoice>(-1, [st, k](int) {
      if (st->r.uniform() < 0.012f) st->crackle = std::max(st->crackle, 0.4f + 0.6f * st->r.uniform());
      st->crackle *= 0.992f;
      return st->b(st->a(st->n.white()) * st->crackle + st->n.pinkish() * 0.04f) * 0.9f * k;
    });
  }
  if (name == "crowd") {
    // a room of talk: noise through two vowel bands that keep moving
    return std::make_unique<FnVoice>(-1, [st, k](int i) {
      if (i % 48 == 0) {
        float t = static_cast<float>(i) / kRate;
        st->a.set('b', 500 + 250 * std::sin(t * 5.1f) + 120 * std::sin(t * 11.7f), 4);
        st->b.set('b', 1500 + 500 * std::sin(t * 4.3f + 1) + 200 * std::sin(t * 9.1f), 4);
      }
      float t = static_cast<float>(i) / kRate;
      float swell = 0.7f + 0.2f * std::sin(t * 0.4f) + 0.1f * std::sin(t * 1.7f);
      float x = st->n.pinkish();
      return (st->a(x) + st->b(x) * 0.6f) * swell * 1.4f * k;
    });
  }
  if (name == "strings") {
    // a slow quartet: four bowed voices with vibrato, walking round I-vi-IV-V
    static const float chords[4][4] = {{196.0f, 293.7f, 392.0f, 493.9f}, {164.8f, 261.6f, 329.6f, 440.0f},
                                       {174.6f, 261.6f, 349.2f, 440.0f}, {196.0f, 246.9f, 392.0f, 587.3f}};
    auto ph = std::make_shared<std::array<float, 4>>();
    return std::make_unique<FnVoice>(-1, [ph, k](int i) {
      float t = static_cast<float>(i) / kRate;
      int c = static_cast<int>(t / 3.2f) % 4;
      float u = std::fmod(t, 3.2f) / 3.2f;
      float bow = std::min(1.0f, u * 6) * (1 - 0.35f * u);
      float s = 0;
      for (int v = 0; v < 4; ++v) {
        float f = chords[c][v] * (1 + 0.004f * std::sin(kTwoPi * (5.2f + v * 0.3f) * t));
        (*ph)[v] = std::fmod((*ph)[v] + f / kRate, 1.0f);
        float p = (*ph)[v];
        s += (std::sin(kTwoPi * p) * 0.6f + std::sin(2 * kTwoPi * p) * 0.25f + std::sin(3 * kTwoPi * p) * 0.12f) * (v == 3 ? 0.8f : 1.0f);
      }
      return s * bow * 0.06f * k;
    });
  }
  if (name == "chains") {
    // iron links knocking now and then, far off
    struct Ring {
      float ph[3] = {0, 0, 0}, f = 0, a = 0;
    };
    auto rg = std::make_shared<Ring>();
    return std::make_unique<FnVoice>(-1, [st, rg, k](int) {
      if (st->r.uniform() < 0.00012f) rg->f = 1800 + 1400 * st->r.uniform(), rg->a = 0.5f + 0.5f * st->r.uniform();
      rg->a *= 0.9993f;
      const float ratio[3] = {1.0f, 2.76f, 5.4f};
      float s = 0;
      for (int j = 0; j < 3; ++j) {
        rg->ph[j] = std::fmod(rg->ph[j] + rg->f * ratio[j] / kRate, 1.0f);
        s += std::sin(kTwoPi * rg->ph[j]) / (1 + j);
      }
      return (s * rg->a * 0.12f + st->n.pinkish() * 0.01f) * k;
    });
  }
  if (name == "synth") {
    // music bleeding from a stall: a pulsing bass and a little arpeggio
    static const float notes[8] = {220.0f, 261.6f, 329.6f, 392.0f, 329.6f, 261.6f, 246.9f, 293.7f};
    auto ph = std::make_shared<std::array<float, 2>>();
    st->a.set('l', 1400, 0.9f);
    return std::make_unique<FnVoice>(-1, [st, ph, k](int i) {
      float t = static_cast<float>(i) / kRate;
      float beat = std::fmod(t * 2.0f, 1.0f);
      float bassF = (static_cast<int>(t / 4) % 2 ? 49.0f : 55.0f);
      (*ph)[0] = std::fmod((*ph)[0] + bassF / kRate, 1.0f);
      float bass = ((*ph)[0] * 2 - 1) * std::exp(-beat * 5) * 0.5f;
      float arpF = notes[static_cast<int>(t * 8) % 8] * 2;
      (*ph)[1] = std::fmod((*ph)[1] + arpF / kRate, 1.0f);
      float arp = ((*ph)[1] < 0.5f ? 1.0f : -1.0f) * std::exp(-std::fmod(t * 8, 1.0f) * 4) * 0.12f;
      return st->a(bass + arp) * 0.35f * k;
    });
  }
  if (name == "drones") {
    // small rotors overhead: two whining tones that drift and beat
    return std::make_unique<FnVoice>(-1, [st, k](int i) {
      float t = static_cast<float>(i) / kRate;
      float f1 = 640 + 30 * std::sin(t * 0.7f), f2 = 655 + 40 * std::sin(t * 0.43f + 2);
      st->lfo = std::fmod(st->lfo + f1 / kRate, 1.0f);
      st->chirpF = std::fmod(st->chirpF + f2 / kRate, 1.0f);
      float whine = std::sin(kTwoPi * st->lfo) + std::sin(kTwoPi * st->chirpF);
      float chop = 0.75f + 0.25f * std::sin(kTwoPi * 38 * t);
      return (whine * chop * 0.05f + st->n.pinkish() * 0.03f) * (0.6f + 0.4f * std::sin(t * 0.3f)) * k;
    });
  }
  if (name == "heartbeat") {
    // lub-dub, slow: two low thumps a beat
    return std::make_unique<FnVoice>(-1, [k](int i) {
      float t = static_cast<float>(i) / kRate;
      float u = std::fmod(t, 1.1f);
      float lub = std::exp(-u * 30) * std::sin(kTwoPi * 55 * u);
      float v = u - 0.28f;
      float dub = v > 0 ? std::exp(-v * 35) * std::sin(kTwoPi * 48 * v) * 0.7f : 0;
      return (lub + dub) * 0.6f * k;
    });
  }
  return nullptr;
}

// ------------------------------------------------------------ the mixer
struct Mixer::Impl {
  struct Src {
    int id;
    std::unique_ptr<Voice> v;
    Vec3 at, vel;
    float volume, gain = 1, fade = 0;  // fade: per-sample loss when stopping
    bool spatial, loop, alive = true;
    Biquad air;
    float lastPan = 0.5f, lastGain = 0;
    std::vector<float> delay = std::vector<float>(64, 0.0f);
    int dpos = 0;
    float lastDist = -1;
    Category category = Category::kEffects;
    Vec3 lastAt{0};
    bool moved = false;
    std::vector<float> stretch;  // Doppler: the voice's own samples before resampling
    float carry = 0;             // last sample of the previous block (smooth joins)
  };
  Vec3 lastListener{0};
  bool listenerSeen = false;
  std::vector<Src> srcs;
  int nextId = 1;
  std::vector<float> mono;
  // Freeverb-style reverb: 4 combs + 2 allpasses per side
  struct Comb {
    std::vector<float> buf;
    size_t i = 0;
    float store = 0;
    float process(float x, float fb, float damp) {
      float y = buf[i];
      store = y * (1 - damp) + store * damp;
      buf[i] = x + store * fb;
      i = (i + 1) % buf.size();
      return y;
    }
  };
  struct Allpass {
    std::vector<float> buf;
    size_t i = 0;
    float process(float x) {
      float b = buf[i];
      float y = -x + b;
      buf[i] = x + b * 0.5f;
      i = (i + 1) % buf.size();
      return y;
    }
  };
  Comb combs[2][4];
  Allpass aps[2][2];
  Impl() {
    const int c[4] = {1557, 1617, 1491, 1422}, a[2] = {556, 441};
    for (int s = 0; s < 2; ++s) {
      for (int k = 0; k < 4; ++k) combs[s][k].buf.assign(static_cast<size_t>((c[k] + s * 23) * kRate / 44100), 0.0f);
      for (int k = 0; k < 2; ++k) aps[s][k].buf.assign(static_cast<size_t>((a[k] + s * 23) * kRate / 44100), 0.0f);
    }
  }
  Src* find(int id) {
    for (auto& s : srcs)
      if (s.id == id) return &s;
    return nullptr;
  }
};

Mixer::Mixer() : impl_(std::make_unique<Impl>()) {}
Mixer::~Mixer() = default;

int Mixer::play(std::unique_ptr<Voice> v, Vec3 at, float volume, bool spatial, bool loop, Category category) {
  if (!v) return 0;
  Impl::Src s;
  s.id = impl_->nextId++;
  s.v = std::move(v);
  s.at = at, s.volume = volume, s.spatial = spatial, s.loop = loop, s.category = category;
  s.lastAt = at;
  s.air.set('l', 20000, 0.7f);
  impl_->srcs.push_back(std::move(s));
  return impl_->srcs.back().id;
}
void Mixer::move(int id, Vec3 at, Vec3 vel) {
  if (auto* s = impl_->find(id)) s->at = at, s->vel = vel;
}
void Mixer::setVolume(int id, float v) {
  if (auto* s = impl_->find(id)) s->volume = v;
}
void Mixer::stop(int id, float fade) {
  if (auto* s = impl_->find(id)) s->fade = fade > 0 ? 1.0f / (fade * kRate) : 1.0f;
}
bool Mixer::playing(int id) const {
  for (auto& s : impl_->srcs)
    if (s.id == id) return s.alive;
  return false;
}
size_t Mixer::voices() const { return impl_->srcs.size(); }

void Mixer::render(float* out, int frames) {
  std::memset(out, 0, sizeof(float) * frames * 2);
  Impl& I = *impl_;
  I.mono.assign(frames, 0.0f);
  std::vector<float> wetIn(frames, 0.0f);
  Vec3 right = normalize(cross(listener.forward, listener.up));
  const Config::Audio& cfg = config().audio;
  const float block = static_cast<float>(frames) / kRate;
  // how fast the listener moves (for Doppler), from where it was last block
  Vec3 lv = listener.velocity;
  if (lengthSq(lv) == 0 && I.listenerSeen) lv = (listener.position - I.lastListener) / block;
  I.lastListener = listener.position, I.listenerSeen = true;
  for (auto& s : I.srcs) {
    std::fill(I.mono.begin(), I.mono.end(), 0.0f);
    // Doppler: a sound coming closer is higher, going away lower (speed of sound 343 m/s)
    float ratio = 1;
    if (s.spatial && cfg.doppler) {
      Vec3 sv = lengthSq(s.vel) > 0 ? s.vel : (s.at - s.lastAt) / block;
      Vec3 toL = listener.position - s.at;
      float dl = length(toL);
      if (dl > 1e-3f) {
        Vec3 u = toL / dl;
        float vs = clamp(dot(sv, u), -150.0f, 150.0f), vl = clamp(dot(lv, -u), -150.0f, 150.0f);
        ratio = clamp((343 + vl) / (343 - vs), 0.5f, 2.0f);
      }
    }
    s.lastAt = s.at;
    if (std::fabs(ratio - 1) < 1e-3f) {
      if (!s.v->render(I.mono.data(), frames)) s.alive = false;
    } else {
      int need = static_cast<int>(std::ceil(frames * ratio)) + 1;
      s.stretch.assign(need, 0.0f);
      if (!s.v->render(s.stretch.data() + 1, need - 1)) s.alive = false;
      s.stretch[0] = s.carry;
      for (int i = 0; i < frames; ++i) {
        float x = i * ratio;
        int k = static_cast<int>(x);
        float f = x - k;
        I.mono[i] = s.stretch[std::min(k, need - 1)] * (1 - f) + s.stretch[std::min(k + 1, need - 1)] * f;
      }
      s.carry = s.stretch[need - 1];
    }
    const float cat[] = {cfg.ambience, cfg.effects, cfg.footsteps, cfg.dialogue, cfg.music};
    float pan = 0.5f, gain = s.volume * cat[static_cast<int>(s.category)], itd = 0;
    if (s.spatial) {
      Vec3 d = s.at - listener.position;
      float dist = std::max(length(d), 0.3f);
      Vec3 dir = d / dist;
      float side = dot(dir, right);
      pan = 0.5f + 0.5f * side;
      if (cfg.earDelay) itd = side * 0.00066f * kRate;  // up to ~0.66 ms between the ears
      gain *= 1.0f / (1 + (dist - 0.3f) * 0.6f * cfg.distance);  // gentle inverse-distance
      // far sounds lose their highs (air)
      s.air.set('l', cfg.airAbsorb ? clamp(20000 / (1 + dist * 0.05f), 2500.0f, 20000.0f) : 20000.0f, 0.7f);
    }
    for (int i = 0; i < frames; ++i) {
      if (s.fade > 0) {
        s.gain -= s.fade;
        if (s.gain <= 0) { s.alive = false, s.gain = 0; }
      }
      float g = s.lastGain + (gain * s.gain - s.lastGain) * 0.002f;  // smoothed: no clicks
      float p = s.lastPan + (pan - s.lastPan) * 0.002f;
      s.lastGain = g, s.lastPan = p;
      float x = s.spatial ? s.air(I.mono[i]) : I.mono[i];
      // the far ear hears it a moment later
      s.delay[s.dpos] = x;
      int lag = static_cast<int>(std::fabs(itd));
      float late = s.delay[(s.dpos - lag + 64) % 64];
      s.dpos = (s.dpos + 1) % 64;
      float l = (itd > 0 ? late : x) * std::cos(p * kPi / 2), r = (itd > 0 ? x : late) * std::sin(p * kPi / 2);
      out[i * 2] += l * g, out[i * 2 + 1] += r * g;
      wetIn[i] += x * g;
    }
  }
  I.srcs.erase(std::remove_if(I.srcs.begin(), I.srcs.end(), [](const Impl::Src& s) { return !s.alive && !s.loop; }), I.srcs.end());
  // the room
  float fb = 0.7f + 0.28f * saturate(roomSize), damp = 0.2f + 0.3f * (1 - roomSize);
  float wet = roomWet * cfg.reverb, master = masterVolume * cfg.master;
  for (int i = 0; i < frames; ++i) {
    float in = wetIn[i] * 0.015f;
    for (int side = 0; side < 2; ++side) {
      float acc = 0;
      for (auto& c : I.combs[side]) acc += c.process(in, fb, damp);
      for (auto& a : I.aps[side]) acc = a.process(acc);
      out[i * 2 + side] += acc * wet;
    }
    // a soft limiter so many sounds together never clip harshly (or a hard clip at ±1)
    for (int side = 0; side < 2; ++side)
      out[i * 2 + side] = cfg.limiter ? std::tanh(out[i * 2 + side] * master) : clamp(out[i * 2 + side] * master, -1.0f, 1.0f);
  }
}

bool writeWav(const std::string& path, const std::vector<float>& stereo, int rate) {
  std::vector<uint8_t> d;
  auto u32 = [&](uint32_t v) { for (int i = 0; i < 4; ++i) d.push_back(static_cast<uint8_t>(v >> (8 * i))); };
  auto u16 = [&](uint16_t v) { d.push_back(static_cast<uint8_t>(v)), d.push_back(static_cast<uint8_t>(v >> 8)); };
  uint32_t bytes = static_cast<uint32_t>(stereo.size() * 2);
  d.insert(d.end(), {'R', 'I', 'F', 'F'});
  u32(36 + bytes);
  d.insert(d.end(), {'W', 'A', 'V', 'E', 'f', 'm', 't', ' '});
  u32(16), u16(1), u16(2), u32(static_cast<uint32_t>(rate)), u32(static_cast<uint32_t>(rate * 4)), u16(4), u16(16);
  d.insert(d.end(), {'d', 'a', 't', 'a'});
  u32(bytes);
  for (float s : stereo) u16(static_cast<uint16_t>(static_cast<int16_t>(std::lround(clamp(s, -1.0f, 1.0f) * 32767))));
  return writeFile(path, d.data(), d.size());
}

}  // namespace loop::audio
