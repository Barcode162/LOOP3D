// The world words: materials (registered by installSceneApi, scene_api.cpp).
#include <filesystem>
#include <set>

#include "engine/project/library.h"
#include "engine/project/project.h"
#include "engine/script/words_internal.h"

namespace loop::script::words {

namespace {

namespace fs = std::filesystem;

// Where `surface(...)` keeps its checks: one problem, named plainly.
using Kind = SurfacePattern::Kind;

Kind patternKind(Vm& vm, const std::string& where, const Value& v) {
  static const std::pair<const char*, Kind> kinds[] = {
      {"none", Kind::kNone},       {"noise", Kind::kNoise},   {"speckle", Kind::kSpeckle}, {"cells", Kind::kCells},
      {"pores", Kind::kPores},     {"cracks", Kind::kCracks}, {"veins", Kind::kVeins},     {"strata", Kind::kStrata},
      {"grain", Kind::kGrain},     {"planks", Kind::kPlanks}, {"bricks", Kind::kBricks},   {"tiles", Kind::kTiles},
      {"weave", Kind::kWeave},     {"knit", Kind::kKnit},     {"brushed", Kind::kBrushed}, {"checker", Kind::kChecker},
      {"grid", Kind::kGrid}};
  if (!v.isString()) vm.fail("`" + where + ".pattern` should be a name, like \"grain\" or \"bricks\"");
  std::vector<std::string> names;
  for (auto& [k, kind] : kinds) {
    names.push_back(k);
    if (v.str() == k) return kind;
  }
  std::string c = closest(v.str(), names);
  vm.fail("there's no pattern `" + v.str() + "`", c.empty() ? "patterns: noise speckle cells pores cracks veins strata grain planks bricks tiles weave knit brushed checker grid" : "did you mean `" + c + "`?");
}

// One of a few words: returns its place in `names`.
int oneOf(Vm& vm, const std::string& where, const Value& v, const std::vector<std::string>& names) {
  if (v.isString())
    for (size_t i = 0; i < names.size(); ++i)
      if (v.str() == names[i]) return static_cast<int>(i);
  std::string list, shown = v.isString() ? v.str() : vm.show(v);
  for (auto& n : names) list += (list.empty() ? "\"" : ", \"") + n + "\"";
  std::string c = v.isString() ? closest(v.str(), names) : "";
  vm.fail("`" + where + "` can't be \"" + shown + "\"", c.empty() ? "it is one of " + list : "did you mean \"" + c + "\"? (one of " + list + ")");
}

float number(Vm& vm, const std::string& where, const Value& v) {
  if (!v.isNumber()) vm.fail("`" + where + "` should be a number");
  return static_cast<float>(v.n);
}
Vec3 colour(Vm& vm, const std::string& where, const Value& v) {
  if (v.isVec()) return v.asVec();
  if (v.isNumber()) return Vec3(static_cast<float>(v.n));
  vm.fail("`" + where + "` should be a colour, like rgb(0.8, 0.2, 0.1)");
}

// The settings each layer takes (docs/SURFACES.md, section 4).
const std::vector<std::string> kPatternSettings = {"pattern", "size", "stretch", "contrast", "sharp", "piece", "twill"};
std::vector<std::string> join(std::vector<std::string> a, const std::vector<std::string>& b) {
  a.insert(a.end(), b.begin(), b.end());
  return a;
}

Table& layerMap(Vm& vm, const std::string& layer, const Value& v) {
  Table* t = v.as<Table>();
  if (!t) vm.fail("`" + layer + "` should be a map of settings, like `" + layer + ": {color: rgb(0.4, 0.3, 0.2)}`");
  return *t;
}

void unknownSetting(Vm& vm, const std::string& layer, const std::string& k, const std::vector<std::string>& options) {
  std::string c = closest(k, options);
  std::string list;
  for (auto& o : options) list += (list.empty() ? "" : ", ") + o;
  vm.fail("`" + layer + "` has no setting `" + k + "`", c.empty() ? layer + " takes: " + list : "did you mean `" + c + "`?");
}

// Reads the pattern settings every patterned layer shares. `sized` says the
// layer named a pattern; `hasSize` that it also gave its size.
bool readPattern(Vm& vm, const std::string& layer, const std::string& k, const Value& v, SurfacePattern& p, bool& named, bool& hasSize) {
  if (k == "pattern") { p.kind = patternKind(vm, layer, v); named = p.kind != Kind::kNone; }
  else if (k == "size") { p.size = number(vm, layer + ".size", v); hasSize = true; }
  else if (k == "stretch") p.stretch = number(vm, layer + ".stretch", v);
  else if (k == "contrast") p.contrast = number(vm, layer + ".contrast", v);
  else if (k == "sharp") p.sharp = number(vm, layer + ".sharp", v);
  else if (k == "piece") p.piece = number(vm, layer + ".piece", v);
  else if (k == "twill") p.twill = static_cast<int>(number(vm, layer + ".twill", v));
  else return false;
  return true;
}

void checkPattern(Vm& vm, const std::string& layer, bool named, bool hasSize, const SurfacePattern& p) {
  if (named && !hasSize) vm.fail("`" + layer + "` has a pattern but no `size`", "say how big its feature is, like `size: 6 mm`");
  if (named && !(p.size > 0)) vm.fail("`" + layer + ".size` should be more than 0");
}

// base, coat and coat2 (wear and dirt are the same layer without a pattern).
void readLayer(Vm& vm, const std::string& name, const Value& value, SurfaceLayer& l, bool isBase) {
  Table& t = layerMap(vm, name, value);
  bool coat = name.compare(0, 4, "coat") == 0, worn = name == "wear";
  std::vector<std::string> options;
  if (isBase) options = join({"color", "color2", "roughness", "roughness2", "metal", "bump"}, kPatternSettings);
  else if (coat) options = join({"color", "roughness", "metal", "cover", "sits", "facing", "bump"}, kPatternSettings);
  else if (worn) options = {"color", "roughness", "metal", "amount", "ragged", "soft"};
  else options = {"color", "roughness", "amount", "ragged", "soft"};
  l.on = true;
  bool named = false, hasSize = false;
  for (auto& [k, v] : t.entries) {
    std::string where = name + "." + k;
    if (std::find(options.begin(), options.end(), k) == options.end()) unknownSetting(vm, name, k, options);
    if (k == "color") l.color = colour(vm, where, v), l.color2 = isBase && !t.find("color2") ? l.color : l.color2;
    else if (k == "color2") l.color2 = colour(vm, where, v);
    else if (k == "roughness") l.roughness = number(vm, where, v), l.roughness2 = isBase && !t.find("roughness2") ? l.roughness : l.roughness2;
    else if (k == "roughness2") l.roughness2 = number(vm, where, v);
    else if (k == "metal") l.metal = number(vm, where, v);
    else if (k == "bump") l.bump = number(vm, where, v);
    else if (k == "cover") l.cover = saturate(number(vm, where, v));
    else if (k == "amount") l.amount = saturate(number(vm, where, v));
    else if (k == "ragged") l.ragged = saturate(number(vm, where, v));
    else if (k == "soft") l.soft = saturate(number(vm, where, v));
    else if (k == "sits") l.sits = static_cast<SurfaceLayer::Sits>(oneOf(vm, where, v, {"any", "high", "low"}));
    else if (k == "facing") l.facing = static_cast<SurfaceLayer::Facing>(oneOf(vm, where, v, {"any", "up", "down", "side"}));
    else readPattern(vm, name, k, v, l.pattern, named, hasSize);
  }
  if (coat && !t.find("pattern")) l.pattern.kind = Kind::kNoise;  // a coat lies where a noise says, unless told
  checkPattern(vm, name, named, hasSize, l.pattern);
  if (l.metal != 0 && l.metal != 1) vm.fail("`" + name + ".metal` is " + vm.show(Value::number(l.metal)) + ": a layer is a metal or it is not", "use `metal: 0` or `metal: 1`");
  if (isBase && l.metal == 0)
    {  // the first colour, as stored (linear: what CRAFT's 0.03 to 0.9 means); section 3's own oak has a darker second one, the late wood
      float b = luminance(l.color);
      if (b < 0.03f || b > 0.9f)
        vm.fail("`base` colour is too " + std::string(b < 0.03f ? "dark" : "bright") + " for a non-metal (" + vm.show(Value::number(b)) + ")", "a real surface reflects between 0.03 and 0.9 of the light (docs/CRAFT.md)");
    }
}

void readVary(Vm& vm, const Value& value, SurfaceRecipe& r) {
  Table& t = layerMap(vm, "vary", value);
  const std::vector<std::string> options = {"large", "amount", "tint", "roughness", "small", "small_amount"};
  for (auto& [k, v] : t.entries) {
    std::string where = "vary." + k;
    if (k == "large") r.varyLarge = number(vm, where, v);
    else if (k == "amount") r.varyAmount = number(vm, where, v);
    else if (k == "tint") r.varyTint = colour(vm, where, v);
    else if (k == "roughness") r.varyRoughness = number(vm, where, v);
    else if (k == "small") r.varySmall = number(vm, where, v);
    else if (k == "small_amount") r.varySmallAmount = number(vm, where, v);
    else unknownSetting(vm, "vary", k, options);
  }
}

void readDetail(Vm& vm, const Value& value, SurfaceRecipe& r) {
  Table& t = layerMap(vm, "detail", value);
  const std::vector<std::string> options = join({"bump", "roughness", "tile", "fade"}, kPatternSettings);
  bool named = false, hasSize = false;
  for (auto& [k, v] : t.entries) {
    std::string where = "detail." + k;
    if (k == "bump") r.detailBump = number(vm, where, v);
    else if (k == "roughness") r.detailRoughness = number(vm, where, v);
    else if (k == "tile") r.detailTile = number(vm, where, v);
    else if (k == "fade") r.detailFade = number(vm, where, v);
    else if (!readPattern(vm, "detail", k, v, r.detail, named, hasSize)) unknownSetting(vm, "detail", k, options);
  }
  checkPattern(vm, "detail", named, hasSize, r.detail);
}

// The library's surfaces folder for this scene, "" if there is none.
std::string surfacesFolder(Stage& s) {
  std::string lib = findLibrary(s.folder);
  if (lib.empty()) return "";
  std::error_code ec;
  fs::path p = fs::path(lib) / "surfaces";
  return fs::is_directory(p, ec) ? p.string() : "";
}

}  // namespace

// `material("oak")`, decision 11 of docs/SURFACES.md: the library's
// surfaces/oak.loop is brought in as `use` does (in a little script of its
// own, on this stage), its function `oak()` called once, and the material it
// gives kept for the scene. Gives its number, or -1 if there is no such entry.
int surfaceFromLibrary(Vm& vm, const std::string& name) {
  Stage& s = stageOf(vm);
  if (name.empty() || name.find('/') != std::string::npos || name.find("..") != std::string::npos) return -1;
  std::string folder = surfacesFolder(s);
  if (folder.empty()) return -1;
  std::string file = (fs::path(folder) / (name + ".loop")).string(), src;
  std::error_code ec;
  if (!fs::is_regular_file(file, ec)) return -1;
  std::vector<std::string> seen{file};
  if (!readText(file, src)) return -1;
  src = expandUses(src, directoryOf(file), seen);
  Vm sub;
  installStandardLibrary(sub);
  stages()[&sub] = &s;
  registerMaterials(sub);  // what an entry may use: the material words (a surface is one)
  bool ok = sub.load(src, file);
  int made = -1;
  std::string why;
  if (!ok && !sub.problems().empty()) why = sub.problems()[0].text();
  else if (!sub.hasGlobal(name)) why = "it has no function `" + name + "()`";
  else {
    Value m = sub.call(sub.global(name), {});
    Thing* t = m.as<Thing>();
    if (!sub.problems().empty()) why = sub.problems()[0].text();
    else if (!t || t->cls != &kMaterialClass) why = "`" + name + "()` should give a surface";
    else made = t->id;
  }
  stages().erase(&sub);
  if (made < 0) vm.fail("the library surface \"" + name + "\" won't load: " + why);
  s.scene.materials[made].name = name;
  return made;
}

void registerMaterials(Vm& vm) {
  // ---- materials
  vm.native("material", [](Args& a) {
    Stage& s = stageOf(a.vm);
    Material m;
    std::string base = a.text(0, "like", "");
    int found = -1;  // a surface made in this scene, then one from the library (decision 11)
    for (size_t i = 0; i < s.scene.materials.size() && !base.empty(); ++i)
      if (s.scene.materials[i].surface >= 0 && s.scene.materials[i].name == base) found = static_cast<int>(i);
    if (found < 0 && !base.empty()) found = surfaceFromLibrary(a.vm, base);
    if (found >= 0) m = s.scene.materials[found];  // a surface made in this scene, or one from the library
    else if (!base.empty() && !materialByName(base, m)) {
      std::string c = closest(base, materialNames());
      a.vm.fail("there's no material called \"" + base + "\"", c.empty() ? "" : "did you mean \"" + c + "\"?");
    }
    m.name = a.text(-1, "name", base.empty() ? "material " + std::to_string(s.scene.materials.size()) : base);
    for (auto& [k, v] : a.named)
      if (k != "like" && k != "name" && !applyMaterialSetting(a.vm, m, k, v)) {
        std::vector<std::string> opts = {"color", "roughness", "metal", "glass", "ior", "dispersion", "glow", "clearcoat", "sheen", "subsurface",
                                         "translucent", "opacity", "pattern", "pattern_color", "pattern_scale", "bump", "leaf", "floor", "bay", "window", "lit", "seed"};
        std::string c = closest(k, opts);
        a.vm.fail("a material has no setting `" + k + "`", c.empty() ? "" : "did you mean `" + c + "`?");
      }
    return Value::object(a.vm.newThing(&kMaterialClass, s.scene.addMaterial(m)));
  }, "material(\"oak\", roughness: 0.3) or material(color: rgb(…), metal: 1, …) or material(pattern: \"windows\", lit: 0.35, glow: 40, …)");
  vm.native("surface", [](Args& a) {
    Stage& s = stageOf(a.vm);
    std::string name = a.text(0, "name", "");
    if (name.empty()) a.vm.fail("a surface needs a name", "surface(\"oak\", base: {color: rgb(0.42, 0.27, 0.14), roughness: 0.55})");
    SurfaceRecipe r;
    r.name = name;
    Material m;
    static const std::vector<std::string> layers = {"base", "vary", "coat", "coat2", "wear", "dirt", "detail"};
    static const std::vector<std::string> beside = {"tile", "project", "space", "along", "seed", "sheen", "clearcoat", "subsurface", "subsurface_color", "translucent", "ior"};
    std::vector<std::string> all = join(join({"name"}, layers), beside);
    bool coatFacing[2] = {false, false};
    for (auto& [k, v] : a.named) {
      if (k == "name") continue;
      if (k == "base") readLayer(a.vm, k, v, r.base, true);
      else if (k == "coat") readLayer(a.vm, k, v, r.coat[0], false), coatFacing[0] = r.coat[0].facing != SurfaceLayer::Facing::kAny;
      else if (k == "coat2") readLayer(a.vm, k, v, r.coat[1], false), coatFacing[1] = r.coat[1].facing != SurfaceLayer::Facing::kAny;
      else if (k == "wear") readLayer(a.vm, k, v, r.wear, false);
      else if (k == "dirt") readLayer(a.vm, k, v, r.dirt, false);
      else if (k == "vary") readVary(a.vm, v, r);
      else if (k == "detail") readDetail(a.vm, v, r);
      else if (k == "tile") {
        r.tile = number(a.vm, k, v);
        if (!(r.tile > 0)) a.vm.fail("`tile` should be more than 0", "the side of the square that repeats, like `tile: 2 m`");
      } else if (k == "project") r.project = static_cast<SurfaceRecipe::Project>(oneOf(a.vm, k, v, {"box", "top", "uv"}));
      else if (k == "space") r.worldSpace = oneOf(a.vm, k, v, {"thing", "world"}) == 1;
      else if (k == "along") r.along = oneOf(a.vm, k, v, {"long", "x", "y", "z"}) - 1;
      else if (k == "seed") r.seed = static_cast<uint32_t>(number(a.vm, k, v));
      else if (std::find(beside.begin(), beside.end(), k) == beside.end() || !applyMaterialSetting(a.vm, m, k, v)) {
        std::string c = closest(k, all);
        a.vm.fail("a surface has no layer or setting `" + k + "`", c.empty() ? "layers: base, vary, coat, coat2, wear, dirt, detail" : "did you mean `" + c + "`?");
      }
    }
    if (!r.base.on) a.vm.fail("a surface needs a `base`", "base: {color: rgb(0.42, 0.27, 0.14), roughness: 0.55}");
    if (coatFacing[0] && coatFacing[1]) a.vm.fail("a surface may have only one coat with a `facing`", "the other coat can have `facing: \"any\"` (it is baked into the tile)");
    // the material: the base's colour, roughness and metal, and the settings beside the layers
    m.name = name;
    m.baseColor = r.base.color;
    m.roughness = r.base.roughness;
    m.metallic = r.base.metal;
    m.surface = static_cast<int>(s.scene.surfaces.size());
    s.scene.surfaces.push_back(r);
    return Value::object(a.vm.newThing(&kMaterialClass, s.scene.addMaterial(m)));
  }, "surface(name, tile, project, space, along, seed, base: {…}, vary: {…}, coat: {…}, coat2: {…}, wear: {…}, dirt: {…}, detail: {…}, sheen, clearcoat, subsurface, …): a material written as layers in real sizes");
  vm.native("materials", [](Args& a) {
    Stage& s = stageOf(a.vm);
    std::vector<std::string> names;
    std::set<std::string> seen;
    auto add = [&](const std::string& n) {
      if (seen.insert(n).second) names.push_back(n);
    };
    for (auto& r : s.scene.surfaces) add(r.name);  // made in this scene,
    std::string folder = surfacesFolder(s);        // then the library's,
    if (!folder.empty()) {
      std::error_code ec;
      std::vector<std::string> lib;
      for (const auto& e : fs::directory_iterator(folder, ec))
        if (e.is_regular_file() && e.path().extension() == ".loop") lib.push_back(e.path().stem().string());
      std::sort(lib.begin(), lib.end());
      for (auto& n : lib) add(n);
    }
    for (auto& n : materialNames()) add(n);  // then the built-in ones
    List* l = a.vm.newList();
    for (auto& n : names) l->items.push_back(a.vm.text(n));
    return Value::object(l);
  }, "materials(): every material name (surfaces made here, the library's, the built-in ones)");
}

}  // namespace loop::script::words
