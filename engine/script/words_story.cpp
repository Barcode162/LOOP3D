// The world words: story (registered by installSceneApi, scene_api.cpp).
#include "engine/script/words_internal.h"

namespace loop::script::words {

void registerStory(Vm& vm) {
  // ---- story: lines on screen, questions, the next scene
  vm.native("line", [](Args& a) {
    Stage& s = stageOf(a.vm);
    Stage::UiMessage m;
    m.kind = "line";
    s.story = true;
    if (a.count >= 2) m.speaker = a.vm.show(a.pos[0]), m.text = a.vm.show(a.pos[1]);
    else m.text = a.text(0, "text", "");
    m.speaker = a.text(-1, "speaker", m.speaker);
    // long enough to read: story.textSpeed characters a second (15), at least story.lineMin (2 s)
    const Config::Story& st = config().story;
    size_t chars = 0;
    for (unsigned char ch : m.text) chars += (ch & 0xC0) != 0x80;  // letters, not bytes
    m.seconds = f32(a.num(-1, "seconds", std::max<double>(st.lineMin, chars / std::max(st.textSpeed, 1.0f) + 1)));
    s.ui.push_back(m);
    return Value::number(m.seconds);
  }, "line(\"Mira\", \"The ember has gone out.\"): a line of dialogue on screen; gives back how long it shows");
  vm.native("ask", [](Args& a) {
    Stage& s = stageOf(a.vm);
    Stage::UiMessage m;
    m.kind = "ask";
    s.story = true;
    m.text = a.text(0, "question", "");
    const Value* o = a.get(1, "options");
    List* l = o ? o->as<List>() : nullptr;
    if (!l || l->items.empty()) a.vm.fail("ask() needs a list of options, like ask(\"Which way?\", [\"The woods\", \"The river\"])");
    for (auto& v : l->items) m.options.push_back(a.vm.show(v));
    s.ui.push_back(m);
    return Value();
  }, "ask(question, [options]): buttons appear; `when \"choice\"` gets the number picked (0 = the first)");
  // what the story remembers from scene to scene (survives go_to)
  vm.native("remember", [](Args& a) {
    StoryValue v;
    const Value* x = a.get(1, "value");
    if (!x || x->isNothing()) v.kind = 0;
    else if (x->isNumber()) v.kind = 1, v.n = x->n;
    else if (x->isBool()) v.kind = 2, v.n = x->b ? 1 : 0;
    else v.kind = 3, v.s = a.vm.show(*x);
    storyMemory()[a.text(0, "key", "")] = v;
    return Value();
  }, "remember(\"helped nia\", yes): kept for the rest of the story");
  vm.native("recall", [](Args& a) {
    auto& m = storyMemory();
    auto it = m.find(a.text(0, "key", ""));
    if (it == m.end()) return a.get(1, "otherwise") ? *a.get(1, "otherwise") : Value();
    const StoryValue& v = it->second;
    if (v.kind == 1) return Value::number(v.n);
    if (v.kind == 2) return Value::boolean(v.n != 0);
    if (v.kind == 3) return a.vm.text(v.s);
    return Value();
  }, "recall(\"helped nia\", otherwise: no): what the story remembered");
  vm.native("forget_story", [](Args&) {
    storyMemory().clear();
    return Value();
  }, "forget_story(): start the story's memory afresh");
  vm.native("player", [](Args& a) { return Value::vec(stageOf(a.vm).playerPos); }, "player(): where the player is");
  vm.native("near", [](Args& a) {
    if (stageOf(a.vm).autoplay) return Value::boolean(true);
    Vec3 p = stageOf(a.vm).playerPos, q = a.vec(0, "place", {});
    float r = f32(a.num(1, "within", 2));
    return Value::boolean(sqr(p.x - q.x) + sqr(p.z - q.z) < r * r);
  }, "near(vec(…), within: 2 m): is the player that close (on the ground)?");
  vm.native("move_player", [](Args& a) {
    Stage& s = stageOf(a.vm);
    s.teleportTo = a.vec(0, "at", s.playerPos);
    s.teleportLook = a.vec(1, "look_at", s.teleportTo + Vec3(0, 1.6f, -1));
    s.teleport = true;
    s.playerPos = s.teleportTo;
    return Value();
  }, "move_player(at: vec(…), look_at: vec(…)): carry the player somewhere new (feet at `at`)");
  vm.native("go_to", [](Args& a) {
    stageOf(a.vm).nextScene = a.text(0, "scene", "");
    return Value();
  }, "go_to(\"the woods\"): load the scene file scenes/the woods.loop next");
  vm.native("clear_lines", [](Args& a) {
    Stage::UiMessage m;
    m.kind = "clear";
    stageOf(a.vm).ui.push_back(m);
    return Value();
  }, "clear_lines(): takes every line of dialogue off the screen");
}

}  // namespace loop::script::words
