#include "engine/audio/audio.h"
#include "engine/audio/music.h"
#include "engine/script/scene_api.h"
#include "tests/test.h"

#include <algorithm>
#include <cmath>
#include <cstdio>

using namespace loop;
using namespace loop::audio;
using loop::script::Stage;
using loop::script::Vm;
using loop::script::installSceneApi;
using loop::script::installStandardLibrary;

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

// Music: notes by name, patterns, the sequencer's timing, every instrument sounds.
TEST(music_notes_patterns_and_time) {
  CHECK(audio::noteNumber("C4") == 60 && audio::noteNumber("A4") == 69);
  CHECK(audio::noteNumber("Eb2") == 39 && audio::noteNumber("F#3") == 54 && audio::noteNumber("H2") == -1 && audio::noteNumber("C") == -1);
  audio::Track drums, bass;
  CHECK(audio::parseTrack("kick", "x...X...", drums));
  CHECK(drums.steps.size() == 8 && drums.steps[0].on && !drums.steps[1].on && drums.steps[4].velocity > drums.steps[0].velocity);
  CHECK(audio::parseTrack("bass", "C2 - - . G1 . . .", bass));
  CHECK(bass.steps.size() == 8 && bass.steps[0].on && bass.steps[0].midi == 36 && bass.steps[0].length == 3 && bass.steps[4].midi == 31);
  std::string err;
  CHECK(!audio::parseTrack("kick", "x.y.", drums, &err) && err.find("'y'") != std::string::npos);
  CHECK(!audio::parseTrack("tuba", "C2", drums, &err));
  CHECK(!audio::parseTrack("bass", "C2 Q7", bass, &err) && err.find("Q7") != std::string::npos);
  // 120 beats a minute: a step every 0.125 s; one second plays 8 steps (0..7)
  audio::Sequencer seq;
  seq.tempo = 120;
  audio::Track k;
  audio::parseTrack("kick", "x.x.", k);
  audio::parseTrack("bass", "C2 - - - . . . .", bass);
  seq.tracks = {k, bass};
  int kicks = 0, notes = 0;
  float held = 0;
  for (int i = 0; i < 100; ++i)
    seq.advance(0.01, [&](const audio::Track& t, const audio::Track::Step&, float seconds) {
      if (t.instrument == "kick") ++kicks; else { ++notes; held = seconds; }
    });
  CHECK(seq.step() == 8);         // steps 0..8 started by t = 1.0 s
  CHECK(kicks == 5);              // steps 0, 2, 4, 6, 8
  CHECK(notes == 2);              // the bass loops every 8 steps: 0 and 8
  CHECK_NEAR(held, 0.5f, 1e-4f);  // held for 4 steps
  // every instrument makes sound, and a bass note sings at its pitch
  for (const std::string& ins : audio::instrumentNames()) {
    auto v = audio::makeNote(ins, 48, 0.3f, 0.8f, 7);
    std::vector<float> buf(audio::kRate / 2, 0.0f);
    v->render(buf.data(), static_cast<int>(buf.size()));
    float peak = 0;
    for (float x : buf) peak = std::max(peak, std::fabs(x));
    if (!(peak > 0.02f && peak < 1.5f)) std::printf("  %s peak %f\n", ins.c_str(), peak);
    CHECK(peak > 0.02f && peak < 1.5f);
  }
  auto b = audio::makeNote("keys", 57, 0.5f, 1, 1);  // A3: 220 Hz
  std::vector<float> buf(audio::kRate / 4, 0.0f);
  b->render(buf.data(), static_cast<int>(buf.size()));
  int crossings = 0;
  for (size_t i = 1; i < buf.size(); ++i) crossings += (buf[i - 1] < 0) != (buf[i] < 0);
  CHECK_NEAR(crossings / 2.0f / 0.25f, 220.0f, 25.0f);
}

// The script's words: tempo and tracks reach the stage, a mistake says what's wrong.
TEST(music_words) {
  Vm vm;
  Stage st;
  installStandardLibrary(vm);
  installSceneApi(vm, st);
  CHECK(vm.load("tempo 96, swing: 0.2\ntrack \"kick\", \"x...x...\"\ntrack \"bass\", \"C2 . G1 .\", volume: 0.7\n", "music.loop"));
  CHECK_NEAR(st.music.tempo, 96.0f, 1e-4f);
  CHECK(st.music.tracks.size() == 2 && st.music.tracks[1].volume < 0.71f);
  Vm vm2;
  Stage st2;
  installStandardLibrary(vm2);
  installSceneApi(vm2, st2);
  CHECK(!vm2.load("track \"kik\", \"x...\"\n", "bad.loop"));
  CHECK(!vm2.problems().empty() && vm2.problems()[0].what.find("kick") != std::string::npos);
  // the stage plays the notes into its mixer as time goes on
  for (int i = 0; i < 60; ++i) st.update(1.0f / 60);
  CHECK(st.mixer.voices() > 0);
  // with nobody listening, one-off sounds don't pile up (they'd all play at once later)
  for (int i = 0; i < 6000; ++i) st.update(1.0f / 60);
  CHECK(st.mixer.voices() <= 96);
}
