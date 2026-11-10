// The world words: sound (registered by installSceneApi, scene_api.cpp).
#include "engine/script/words_internal.h"

namespace loop::script::words {

void registerSound(Vm& vm) {
  // ---- sound
  vm.native("sound", [](Args& a) {
    Stage& s = stageOf(a.vm);
    std::string name = a.text(0, "name", "fire");
    std::unique_ptr<audio::Voice> v;
    auto amb = audio::ambienceNames();
    bool endless = std::find(amb.begin(), amb.end(), name) != amb.end();
    if (endless) v = audio::makeAmbience(name, f32(a.num(-1, "intensity", 1)), static_cast<uint32_t>(s.sounds.size() + 1));
    else v = audio::makeSound(name, a.text(1, "variant", "stone"), f32(a.num(-1, "strength", 0.7)), static_cast<uint32_t>(s.sounds.size() + 1));
    if (!v) {
      std::vector<std::string> all = amb;
      for (auto& n : audio::soundNames()) all.push_back(n);
      std::string c = closest(name, all);
      a.vm.fail("there's no sound called \"" + name + "\"", c.empty() ? "" : "did you mean \"" + c + "\"?");
    }
    Stage::SoundRef r;
    r.at = a.vec(-1, "at", {});
    r.volume = f32(a.num(-1, "volume", 1));
    bool everywhere = !a.has(-1, "at");
    r.mixerId = s.mixer.play(std::move(v), r.at, r.volume, !everywhere, endless, endless ? audio::Category::kAmbience : audio::Category::kEffects);
    s.sounds.push_back(r);
    return Value::object(a.vm.newThing(&kSoundClass, static_cast<int>(s.sounds.size() - 1)));
  }, "sound(\"fire\", at: vec(…), volume) for endless sounds; sound(\"impact\", \"glass\", at: …) for one-offs");
  vm.native("room", [](Args& a) {
    Stage& s = stageOf(a.vm);
    s.mixer.roomSize = f32(a.num(0, "size", 0.3));
    s.mixer.roomWet = f32(a.num(-1, "echo", 0.15));
    return Value();
  }, "room(size: 0 outdoors .. 1 a cathedral, echo)");
}

}  // namespace loop::script::words
