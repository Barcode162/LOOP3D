// The world words: materials (registered by installSceneApi, scene_api.cpp).
#include "engine/script/words_internal.h"

namespace loop::script::words {

void registerMaterials(Vm& vm) {
  // ---- materials
  vm.native("material", [](Args& a) {
    Stage& s = stageOf(a.vm);
    Material m;
    std::string base = a.text(0, "like", "");
    if (!base.empty() && !materialByName(base, m)) {
      std::string c = closest(base, materialNames());
      a.vm.fail("there's no material called \"" + base + "\"", c.empty() ? "" : "did you mean \"" + c + "\"?");
    }
    m.name = a.text(-1, "name", base.empty() ? "material " + std::to_string(s.scene.materials.size()) : base);
    for (auto& [k, v] : a.named)
      if (k != "like" && k != "name" && !applyMaterialSetting(a.vm, m, k, v)) {
        std::vector<std::string> opts = {"color", "roughness", "metal", "glass", "ior", "dispersion", "glow", "clearcoat", "sheen", "subsurface",
                                         "translucent", "opacity", "pattern", "pattern_color", "pattern_scale", "bump", "leaf"};
        std::string c = closest(k, opts);
        a.vm.fail("a material has no setting `" + k + "`", c.empty() ? "" : "did you mean `" + c + "`?");
      }
    return Value::object(a.vm.newThing(&kMaterialClass, s.scene.addMaterial(m)));
  }, "material(\"oak\", roughness: 0.3) or material(color: rgb(…), metal: 1, …)");
  vm.native("materials", [](Args& a) {
    List* l = a.vm.newList();
    for (auto& n : materialNames()) l->items.push_back(a.vm.text(n));
    return Value::object(l);
  }, "materials(): every material name");
}

}  // namespace loop::script::words
