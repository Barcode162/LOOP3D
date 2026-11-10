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
}

}  // namespace loop::script::words
