// The world words: music (registered by installSceneApi, scene_api.cpp).
#include "engine/script/words_internal.h"

namespace loop::script::words {

void registerMusic(Vm& vm) {
  // ---- music: the engine's own instruments, in time with the world (engine/audio/music.h)
  vm.native("tempo", [](Args& a) {
    Stage& s = stageOf(a.vm);
    s.music.tempo = std::clamp(f32(a.num(0, "bpm", 100)), 20.0f, 300.0f);
    s.music.swing = std::clamp(f32(a.num(-1, "swing", 0)), 0.0f, 0.5f);
    return Value();
  }, "tempo(bpm, swing): how fast the music goes (beats a minute, 4 steps a beat) and its shuffle (0 straight .. 0.5)");
  vm.native("track", [](Args& a) {
    Stage& s = stageOf(a.vm);
    audio::Track t;
    std::string err, ins = a.text(0, "instrument", ""), pat = a.text(1, "pattern", "");
    if (!audio::parseTrack(ins, pat, t, &err)) {
      std::string meant = audio::isInstrument(ins) ? "" : closest(ins, audio::instrumentNames());
      a.vm.fail(err + (meant.empty() ? "" : " (did you mean \"" + meant + "\"?)") + "; instruments: kick, snare, hat, open_hat, clap, tom, bass, lead, pad, pluck, keys, bell");
    }
    t.volume = std::clamp(f32(a.num(-1, "volume", 1)), 0.0f, 2.0f);
    s.music.tracks.push_back(std::move(t));
    return Value();
  }, "track(instrument, pattern, volume): a line of music that loops: drums \"x...X...\" (x a hit, X a loud one), notes \"C2 . - G1\" (. rest, - hold)");
  vm.native("note", [](Args& a) {
    Stage& s = stageOf(a.vm);
    std::string ins = a.text(0, "instrument", ""), pitch = a.text(1, "pitch", "");
    const std::string all = "kick, snare, hat, open_hat, clap, tom, bass, lead, pad, pluck, keys, bell";
    if (!audio::isInstrument(ins)) {
      std::string meant = closest(ins, audio::instrumentNames());
      a.vm.fail("there's no instrument called \"" + ins + "\"" + (meant.empty() ? "" : " (did you mean \"" + meant + "\"?)") + "; instruments: " + all);
    }
    int midi = 60;
    float velocity = 0.8f;
    if (!audio::isDrum(ins)) {
      if (pitch.empty()) a.vm.fail("\"" + ins + "\" needs a pitch, like note \"" + ins + "\", \"C4\" (only the drums take none)");
      bool loud = pitch.back() == '!';
      midi = audio::noteNumber(loud ? pitch.substr(0, pitch.size() - 1) : pitch);
      if (midi < 0) a.vm.fail("\"" + pitch + "\" isn't a note (like C4, Eb3, F#2)");
      if (loud) velocity = 1.0f;
    }
    float volume = std::clamp(f32(a.num(2, "volume", 1)), 0.0f, 2.0f);
    float length = std::max(0.03f, f32(a.num(3, "length", 0.4)));
    // at once, not on the music's step: the same voices a track's step starts
    s.mixer.play(audio::makeNote(ins, static_cast<float>(midi), length, velocity, ++s.musicSeed), Vec3(0), volume, false, false, audio::Category::kMusic);
    return Value();
  }, "note(instrument, pitch, volume, length): sounds one note now, e.g. note \"keys\", \"C4\" (a drum takes no pitch: note \"kick\"); several at once make a chord; length in seconds; past 96 sounds at once the oldest gives way");
}

}  // namespace loop::script::words
