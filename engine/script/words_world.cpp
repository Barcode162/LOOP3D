// The world words: a whole world, made the way nature makes it, streamed round the player (registered by installSceneApi, scene_api.cpp).
#include "engine/script/words_internal.h"

namespace loop::script::words {

void registerWorld(Vm& vm) {
  // ---- a whole world, made the way nature makes it, streamed round the player
  vm.native("world", [](Args& a) {
    Stage& s = stageOf(a.vm);
    WorldSettings ws;
    ws.size = f32(a.num(-1, "size", 8192));
    ws.seed = static_cast<uint32_t>(a.num(-1, "seed", 1));
    ws.mountains = f32(a.num(-1, "mountains", 1));
    ws.warmth = f32(a.num(-1, "warmth", 14));
    ws.wind = f32(a.num(-1, "wind", 0));
    ws.cells = std::clamp(static_cast<int>(a.num(-1, "cells", 257)), 33, 513);
    // an open world 8 km across needs no light cache (its probes would be over
    // 100 m apart, and every new chunk would start its bake again: your phone's
    // recording, 0.6-0.8 s stalls while walking): the sky's light outdoors
    setSetting(a.vm, "render.lightCache", Value::boolean(false));
    Vec3 start = makeStreamedWorld(a.vm, s, ws);
    ++s.version;
    return Value::vec(start);
  }, "world(seed, size: 8192 m, mountains: 1, warmth: 14 C, wind: 0 (from the west)): a world grown like nature (plates lift mountains, rivers carve, lakes fill, rain falls on the windward side; sand, grass, forest, rock, snow), its land and trees made round the player as they go; gives back a good place to start (for move_player)");
}

}  // namespace loop::script::words
