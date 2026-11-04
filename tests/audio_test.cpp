#include "engine/audio/audio.h"
#include "tests/test.h"

using namespace loop;
using namespace loop::audio;

namespace {
struct Level {
  float peak = 0, rmsL = 0, rmsR = 0;
};
Level measure(Mixer& m, int frames) {
  std::vector<float> buf(frames * 2);
  m.render(buf.data(), frames);
  Level l;
  for (int i = 0; i < frames; ++i) {
    l.peak = std::max({l.peak, std::fabs(buf[i * 2]), std::fabs(buf[i * 2 + 1])});
    l.rmsL += buf[i * 2] * buf[i * 2], l.rmsR += buf[i * 2 + 1] * buf[i * 2 + 1];
  }
  l.rmsL = std::sqrt(l.rmsL / frames), l.rmsR = std::sqrt(l.rmsR / frames);
  return l;
}
}  // namespace

TEST(audio_every_sound_plays_and_ends) {
  for (const auto& n : soundNames())
    for (const char* v : {"wood", "metal", "glass", "stone", "grass", "gravel", "snow", "water", "carpet"}) {
      auto voice = makeSound(n, v, 0.8f, 3);
      CHECK(voice != nullptr);
      std::vector<float> buf(kRate * 8, 0.0f);
      bool more = voice->render(buf.data(), static_cast<int>(buf.size()));
      CHECK(!more);  // one-shots finish within 8 s
      float peak = 0;
      for (float x : buf) {
        peak = std::max(peak, std::fabs(x));
        CHECK(std::isfinite(x));
      }
      CHECK(peak > 0.01f && peak < 4.0f);
    }
  for (const auto& n : ambienceNames()) {
    auto voice = makeAmbience(n, 1, 5);
    CHECK(voice != nullptr);
    std::vector<float> buf(kRate * 2, 0.0f);
    CHECK(voice->render(buf.data(), static_cast<int>(buf.size())));  // endless
    double e = 0;
    for (float x : buf) e += x * x;
    CHECK(e > 0);
  }
}

TEST(audio_materials_sound_like_themselves) {
  CHECK(surfaceOf("oak") == "wood");
  CHECK(surfaceOf("floorboards") == "wood");
  CHECK(surfaceOf("brass") == "metal");
  CHECK(surfaceOf("grass") == "grass");
  CHECK(surfaceOf("crystal") == "glass");
  CHECK(surfaceOf("granite") == "stone");
  // metal rings much longer than wood
  auto len = [](const char* m) {
    auto v = makeSound("impact", m, 1, 1);
    std::vector<float> buf(kRate * 10, 0.0f);
    v->render(buf.data(), static_cast<int>(buf.size()));
    int last = 0;
    for (size_t i = 0; i < buf.size(); ++i)
      if (std::fabs(buf[i]) > 0.003f) last = static_cast<int>(i);
    return last;
  };
  CHECK(len("metal") > len("wood") * 5);
}

TEST(audio_space_left_right_and_distance) {
  Mixer m;
  m.roomWet = 0;
  int id = m.play(makeAmbience("wind", 1, 2), {5, 0, 0}, 1, true, true);  // to the right
  measure(m, 4800);
  Level right = measure(m, 9600);
  CHECK(right.rmsR > right.rmsL * 2);
  m.move(id, {-5, 0, 0});
  measure(m, 9600);  // let the pan glide over
  Level left = measure(m, 9600);
  CHECK(left.rmsL > left.rmsR * 2);
  m.move(id, {0, 0, -40});
  measure(m, 9600);
  Level far = measure(m, 9600);
  CHECK(far.rmsL + far.rmsR < (left.rmsL + left.rmsR) * 0.3f);
  m.stop(id, 0.1f);
  measure(m, 9600);
  CHECK(!m.playing(id));
}

TEST(audio_never_clips_harshly) {
  Mixer m;
  for (int i = 0; i < 40; ++i) m.play(makeSound("impact", "metal", 1, i), {0, 0, -1}, 1);
  Level l = measure(m, 9600);
  CHECK(l.peak <= 1.0f);
  std::vector<float> buf(9600, 0.1f);
  CHECK(writeWav("out_audio_test.wav", buf));
  std::remove("out_audio_test.wav");
}
