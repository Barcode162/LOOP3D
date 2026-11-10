// The world words: settings (registered by installSceneApi, scene_api.cpp).
#include "engine/script/words_internal.h"

namespace loop::script::words {

void registerSettings(Vm& vm) {
  // ---- settings: the same names as the editor and project.json
  vm.native("setting", [](Args& a) {
    std::string id = a.text(0, "name", "");
    const Value* v = a.get(1, "value");
    if (id.empty() || !v) a.vm.fail("setting needs a name and a value, like setting \"render.lamps\", 4");
    setSetting(a.vm, id, *v);
    return Value();
  }, "setting \"physics.iterations\", 24: changes an engine setting for this scene (names as in the editor)");
  vm.native("settings", [](Args& a) {
    Table* t = a.count ? a.pos[0].as<Table>() : nullptr;
    if (!t) a.vm.fail("settings needs a table, like settings {render_lamps: 4, story_textSpeed: 20}");
    for (auto& [k, v] : t->entries) setSetting(a.vm, k, v);
    return Value();
  }, "settings {story_textSpeed: 20, audio_reverb: 1.5}: several settings at once (_ or . between the parts)");
  vm.native("get_setting", [](Args& a) {
    std::string id = a.text(0, "name", "");
    if (!Settings::get().find(id)) {
      std::string s = Settings::get().suggest(id);
      a.vm.fail("there's no setting \"" + id + "\"", s.empty() ? "" : "did you mean \"" + s + "\"?");
    }
    Json j = Settings::get().value(id);
    if (j.isNumber()) return Value::number(j.number());
    if (j.type() == Json::Type::kBool) return Value::boolean(j.boolean());
    if (j.isArray()) return Value::vec(Vec3(static_cast<float>(j[0].number()), static_cast<float>(j[1].number()), static_cast<float>(j[2].number())));
    return a.vm.text(j.string());
  }, "get_setting(\"player.walk\"): a setting's value in effect now");

  vm.native("title", [](Args& a) {
    stageOf(a.vm).title = a.text(0, "name", "Untitled");
    return Value();
  }, "title \"The Last Ember\"");
  vm.native("mode", [](Args& a) {
    std::string m = a.text(0, "kind", "game");
    if (m != "game" && m != "movie") a.vm.fail("mode is \"game\" or \"movie\"");
    stageOf(a.vm).mode = m;
    return Value();
  }, "mode \"movie\": plays its shots; \"game\": you walk around");
}

}  // namespace loop::script::words
