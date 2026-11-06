#include "engine/audio/music.h"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <sstream>

namespace loop::audio {

namespace {

constexpr float kTwoPiF = 6.28318530718f;
float hz(float midi) { return 440.0f * std::pow(2.0f, (midi - 69.0f) / 12.0f); }

// A small noise source of its own (each note's noise differs by its seed).
struct Noise {
  uint32_t s;
  explicit Noise(uint32_t seed) : s(seed * 747796405u + 2891336453u) {}
  float next() {
    s ^= s << 13, s ^= s >> 17, s ^= s << 5;
    return (s & 0xffffff) / 8388607.5f - 1.0f;
  }
};
// One-pole filters: smooth (low pass) and its remainder (high pass).
struct Lowpass {
  float y = 0, a = 1;
  void set(float cutoff) { a = 1 - std::exp(-kTwoPiF * std::min(cutoff, kRate * 0.45f) / kRate); }
  float run(float x) { return y += a * (x - y); }
};

// A note made sample by sample by a function of time (seconds since it
// started); it ends after `length` seconds.
class Synth : public Voice {
 public:
  Synth(float length, float gain, std::function<float(float)> f) : length_(length), gain_(gain), f_(std::move(f)) {}
  bool render(float* out, int n) override {
    for (int i = 0; i < n; ++i) {
      float t = static_cast<float>(at_++) / kRate;
      if (t >= length_) return false;
      out[i] += f_(t) * gain_;
    }
    return true;
  }

 private:
  float length_, gain_;
  long at_ = 0;
  std::function<float(float)> f_;
};

// attack, hold for `held` seconds, then release: the shape of a held note
float envelope(float t, float attack, float held, float release) {
  if (t < attack) return t / attack;
  if (t < held) return 1;
  float r = (t - held) / release;
  return r >= 1 ? 0 : 1 - r;
}
// a band-limited-ish saw: the naive ramp with its jump smoothed over one sample (polyBLEP)
float blep(float p, float dp) {
  if (p < dp) { p /= dp; return p + p - p * p - 1; }
  if (p > 1 - dp) { p = (p - 1) / dp; return p * p + p + p + 1; }
  return 0;
}

}  // namespace

std::vector<std::string> instrumentNames() { return {"kick", "snare", "hat", "open_hat", "clap", "tom", "bass", "lead", "pad", "pluck", "keys", "bell"}; }
bool isDrum(const std::string& i) { return i == "kick" || i == "snare" || i == "hat" || i == "open_hat" || i == "clap" || i == "tom"; }
bool isInstrument(const std::string& i) {
  auto all = instrumentNames();
  return std::find(all.begin(), all.end(), i) != all.end();
}

std::unique_ptr<Voice> makeNote(const std::string& ins, float midi, float seconds, float velocity, uint32_t seed) {
  float v = std::clamp(velocity, 0.0f, 1.0f);
  if (ins == "kick") {
    // a sine that drops from 150 to 45 Hz, with a click at the start
    auto phase = std::make_shared<float>(0.0f);
    return std::make_unique<Synth>(0.45f, v * 0.95f, [phase](float t) {
      float f = 45 + 105 * std::exp(-t * 28);
      *phase += kTwoPiF * f / kRate;
      return std::sin(*phase) * std::exp(-t * 7.5f) + (t < 0.003f ? 0.5f * (1 - t / 0.003f) : 0);
    });
  }
  if (ins == "snare") {
    auto n = std::make_shared<Noise>(seed);
    auto hp = std::make_shared<Lowpass>();
    hp->set(1800);
    return std::make_unique<Synth>(0.3f, v * 0.6f, [n, hp](float t) {
      float x = n->next(), high = x - hp->run(x);
      return high * std::exp(-t * 18) + 0.6f * std::sin(kTwoPiF * 185 * t) * std::exp(-t * 30);
    });
  }
  if (ins == "hat" || ins == "open_hat") {
    float decay = ins == "hat" ? 55.0f : 9.0f;
    auto n = std::make_shared<Noise>(seed);
    auto lp = std::make_shared<Lowpass>();
    lp->set(7000);
    return std::make_unique<Synth>(ins == "hat" ? 0.12f : 0.5f, v * 0.35f, [n, lp, decay](float t) {
      float x = n->next();
      return (x - lp->run(x)) * std::exp(-t * decay);
    });
  }
  if (ins == "clap") {
    auto n = std::make_shared<Noise>(seed);
    auto lp = std::make_shared<Lowpass>();
    lp->set(2500);
    return std::make_unique<Synth>(0.35f, v * 0.55f, [n, lp](float t) {
      // three quick slaps, then the room
      float x = n->next(), band = x - lp->run(x);
      float e = 0;
      for (int k = 0; k < 3; ++k) {
        float tk = t - k * 0.011f;
        if (tk >= 0) e = std::max(e, std::exp(-tk * 140));
      }
      return band * std::max(e, 0.5f * std::exp(-std::max(0.0f, t - 0.022f) * 14) * (t > 0.022f ? 1.0f : 0.0f));
    });
  }
  if (ins == "tom") {
    auto phase = std::make_shared<float>(0.0f);
    float base = midi > 0 ? hz(midi) : 110;
    return std::make_unique<Synth>(0.5f, v * 0.8f, [phase, base](float t) {
      *phase += kTwoPiF * base * (1 + 0.6f * std::exp(-t * 20)) / kRate;
      return std::sin(*phase) * std::exp(-t * 7);
    });
  }
  float f = hz(midi), held = std::max(0.03f, seconds);
  if (ins == "bass") {
    // a warm saw through a low pass that opens at the start of each note
    auto p = std::make_shared<float>(0.0f);
    auto lp = std::make_shared<Lowpass>();
    return std::make_unique<Synth>(held + 0.08f, v * 0.55f, [p, lp, f, held](float t) {
      float dp = f / kRate;
      *p += dp;
      if (*p >= 1) *p -= 1;
      float saw = 2 * *p - 1 - blep(*p, dp);
      lp->set(300 + 1400 * std::exp(-t * 9));
      return lp->run(saw) * envelope(t, 0.004f, held, 0.08f);
    });
  }
  if (ins == "lead") {
    // a soft square with a little vibrato that comes in late
    auto p = std::make_shared<float>(0.0f);
    auto lp = std::make_shared<Lowpass>();
    lp->set(3200);
    return std::make_unique<Synth>(held + 0.12f, v * 0.3f, [p, lp, f, held](float t) {
      float vib = 1 + 0.004f * std::sin(kTwoPiF * 5.5f * t) * std::min(1.0f, t * 3);
      float dp = f * vib / kRate;
      *p += dp;
      if (*p >= 1) *p -= 1;
      float q = *p + 0.5f >= 1 ? *p - 0.5f : *p + 0.5f;
      float sq = (*p < 0.5f ? 1.0f : -1.0f) + blep(*p, dp) - blep(q, dp);
      return lp->run(sq) * envelope(t, 0.01f, held, 0.12f);
    });
  }
  if (ins == "pad") {
    // three saws slightly apart: wide and slow
    auto ph = std::make_shared<std::vector<float>>(std::vector<float>{0.0f, 0.33f, 0.66f});
    auto lp = std::make_shared<Lowpass>();
    lp->set(1400);
    return std::make_unique<Synth>(held + 0.6f, v * 0.22f, [ph, lp, f, held](float t) {
      const float det[3] = {1.0f, 1.006f, 0.994f};
      float s = 0;
      for (int k = 0; k < 3; ++k) {
        float dp = f * det[k] / kRate;
        float& p = (*ph)[k];
        p += dp;
        if (p >= 1) p -= 1;
        s += 2 * p - 1 - blep(p, dp);
      }
      return lp->run(s / 3) * envelope(t, 0.25f, held, 0.6f);
    });
  }
  if (ins == "pluck") {
    // a plucked string: a burst of noise ringing round a short loop (Karplus-Strong)
    int len = std::max(2, static_cast<int>(kRate / f));
    auto buf = std::make_shared<std::vector<float>>(len);
    Noise n(seed);
    for (float& x : *buf) x = n.next();
    auto at = std::make_shared<int>(0);
    return std::make_unique<Synth>(std::min(2.5f, held + 1.2f), v * 0.5f, [buf, at, len, held](float t) {
      std::vector<float>& b = *buf;
      int i = *at, j = (i + 1) % len;
      float out = b[i];
      b[i] = 0.5f * (b[i] + b[j]) * 0.996f;
      *at = j;
      return out * (t > held + 0.9f ? std::max(0.0f, 1 - (t - held - 0.9f) / 0.3f) : 1.0f);
    });
  }
  if (ins == "keys") {
    // an electric piano: a few sines that fade, the high ones first
    return std::make_unique<Synth>(std::min(3.0f, held + 0.4f), v * 0.4f, [f, held](float t) {
      float s = std::sin(kTwoPiF * f * t) * std::exp(-t * 1.6f) + 0.45f * std::sin(kTwoPiF * f * 2 * t) * std::exp(-t * 3.5f) +
                0.18f * std::sin(kTwoPiF * f * 3.01f * t) * std::exp(-t * 7) + 0.1f * std::sin(kTwoPiF * f * 4.2f * t) * std::exp(-t * 18);
      return s * std::min(1.0f, t / 0.003f) * (t > held ? std::max(0.0f, 1 - (t - held) / 0.4f) : 1.0f);
    });
  }
  // bell: one sine bending another (FM), the bend fading so it rings clean
  return std::make_unique<Synth>(std::min(4.0f, held + 1.8f), v * 0.35f, [f, held](float t) {
    float index = 3.5f * std::exp(-t * 2.5f);
    float s = std::sin(kTwoPiF * f * t + index * std::sin(kTwoPiF * f * 3.5f * t)) * std::exp(-t * 1.2f);
    return s * std::min(1.0f, t / 0.002f) * (t > held + 1.5f ? std::max(0.0f, 1 - (t - held - 1.5f) / 0.3f) : 1.0f);
  });
}

int noteNumber(const std::string& name) {
  if (name.size() < 2) return -1;
  static const int base[7] = {9, 11, 0, 2, 4, 5, 7};  // A B C D E F G
  char l = static_cast<char>(std::toupper(static_cast<unsigned char>(name[0])));
  if (l < 'A' || l > 'G') return -1;
  int n = base[l - 'A'];
  size_t i = 1;
  if (i < name.size() && (name[i] == '#' || name[i] == 's')) ++n, ++i;
  else if (i < name.size() && name[i] == 'b') --n, ++i;
  if (i >= name.size()) return -1;
  bool neg = name[i] == '-';
  if (neg) ++i;
  if (i >= name.size() || !std::isdigit(static_cast<unsigned char>(name[i]))) return -1;
  int oct = 0;
  for (; i < name.size(); ++i) {
    if (!std::isdigit(static_cast<unsigned char>(name[i]))) return -1;
    oct = oct * 10 + (name[i] - '0');
  }
  if (neg) oct = -oct;
  int m = (oct + 1) * 12 + n;
  return m < 0 || m > 127 ? -1 : m;
}

bool parseTrack(const std::string& ins, const std::string& pattern, Track& out, std::string* error) {
  out = Track();
  out.instrument = ins;
  if (!isInstrument(ins)) {
    if (error) *error = "there's no instrument called \"" + ins + "\"";
    return false;
  }
  if (isDrum(ins) && pattern.find(' ') == std::string::npos) {
    // "x...X..." one character a step
    for (char c : pattern) {
      Track::Step s;
      if (c == 'x' || c == 'X' || c == 'o') s.on = true, s.velocity = c == 'X' ? 1.0f : 0.75f;
      else if (c != '.' && c != '-' && c != '_') {
        if (error) *error = std::string("a drum pattern is x (a hit), X (a loud one) and . (nothing), not '") + c + "'";
        return false;
      }
      s.midi = ins == "tom" ? 45 : 60;
      out.steps.push_back(s);
    }
  } else {
    // "C2 . - G1" one word a step
    std::istringstream in(pattern);
    std::string w;
    int lastOn = -1;
    while (in >> w) {
      Track::Step s;
      if (w == ".") lastOn = -1;
      else if (w == "-") {
        if (lastOn >= 0) ++out.steps[lastOn].length;
      } else if (isDrum(ins) && (w == "x" || w == "X")) {
        s.on = true, s.velocity = w == "X" ? 1.0f : 0.75f, lastOn = -1;
      } else {
        bool loud = !w.empty() && w.back() == '!';
        std::string name = loud ? w.substr(0, w.size() - 1) : w;
        int m = noteNumber(name);
        if (m < 0) {
          if (error) *error = "\"" + w + "\" isn't a note (like C4, Eb3, F#2), a rest (.) or a hold (-)";
          return false;
        }
        s.on = true, s.midi = m, s.velocity = loud ? 1.0f : 0.8f;
        lastOn = static_cast<int>(out.steps.size());
      }
      out.steps.push_back(s);
    }
  }
  if (out.steps.empty()) {
    if (error) *error = "the pattern is empty";
    return false;
  }
  return true;
}

void Sequencer::advance(double dt, const std::function<void(const Track&, const Track::Step&, float)>& play) {
  if (dt <= 0 || tempo <= 0) return;
  double stepLen = 60.0 / tempo / 4;
  time_ += dt;
  // every step boundary passed since the last call (swing delays the odd ones)
  for (;;) {
    long long next = step_ + 1;
    double at = next * stepLen + ((next & 1) ? swing * stepLen : 0);
    if (at > time_) break;
    step_ = next;
    for (const Track& t : tracks) {
      if (t.steps.empty()) continue;
      const Track::Step& s = t.steps[static_cast<size_t>(step_ % static_cast<long long>(t.steps.size()))];
      if (s.on) play(t, s, static_cast<float>(s.length * stepLen));
    }
    if (step_ > (1LL << 40)) restart();
  }
}

}  // namespace loop::audio
