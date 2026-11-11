// Examples of testing a word with runScene (tests/test.h): run a script, look at the stage.
#include <filesystem>
#include <fstream>

#include "tests/test.h"

TEST(words_box_and_light_land_on_the_stage) {
  auto r = runScene(R"(
box size: vec(1, 2, 1), at: vec(0, 1, 0), material: "oak"
light(vec(0, 3, 0), lumens: 500, kelvin: 2700)
camera at: vec(0, 2, 6), look_at: vec(0, 1, 0), lens: 50 mm
)");
  CHECK(r->ok);
  CHECK(r->problems().empty());
  CHECK(r->stage.scene.instances.size() == 1);
  CHECK(r->stage.scene.lights.size() == 1);
  CHECK_NEAR(r->stage.scene.camera.focalLength, 50, 1e-3);
}

TEST(words_a_misspelt_name_gets_a_hint) {
  auto r = runScene("box size: 1, material: \"oke\"\n");
  CHECK(r->hasProblem("did you mean \"oak\""));
  CHECK(!runScene("look \"nior\"\n")->problems().empty());
}

TEST(words_lathe_spins_a_vase_from_six_points) {
  auto r = runScene(R"(
lathe [vec(0, 0), vec(0.12, 0), vec(0.2, 0.2), vec(0.3, 0.45), vec(0.15, 0.7), vec(0.12, 0.8)], at: vec(1, 0, 2), name: "vase"
)");
  CHECK(r->ok);
  CHECK(r->problems().empty());
  CHECK(r->stage.scene.instances.size() == 1);
  const auto& inst = r->stage.scene.instances[0];
  loop::AABB bx = r->stage.scene.meshes[inst.mesh].bounds();
  CHECK_NEAR(bx.lo.y, 0, 1e-3);
  CHECK_NEAR(bx.hi.y, 0.8, 1e-3);
  CHECK_NEAR(bx.hi.x, 0.3, 1e-3);
  CHECK_NEAR(bx.lo.x, -0.3, 1e-3);
  CHECK_NEAR(r->stage.placed[0].position.x, 1, 1e-6);
  // the same outline twice shares one mesh
  auto r2 = runScene("lathe [vec(0, 0), vec(0.3, 0.4)]\nlathe [vec(0, 0), vec(0.3, 0.4)], at: vec(1, 0, 0)\n");
  CHECK(r2->stage.scene.meshes.size() == 1);
}

TEST(words_lathe_names_its_problems) {
  CHECK(runScene("lathe [vec(0.3, 0)]\n")->hasProblem("at least two points"));
  CHECK(runScene("lathe [vec(0.3, 0), vec(-0.1, 1)]\n")->hasProblem("negative radius"));
  CHECK(runScene("lathe 5\n")->hasProblem("needs a profile"));
}

// group: what its block places is measured from its place and turn
TEST(words_group_places_and_turns_as_one) {
  auto r = runScene(R"(
to pane(at: vec(0, 0, 0), width: 4)
  box size: vec(width, 2, 0.02), at: at + vec(0, 1, 0)
let g = group(at: vec(0, 0, 0), turn: 90 deg)
  box size: vec(0.2, 0.2, 0.2), at: vec(1, 0, 0)
  light(vec(2, 1, 0), lumens: 500, aim: vec(2, 0, 0))
let wall = group(at: vec(10, 0, 0), turn: 90 deg)
  pane(at: vec(0, 0, 0), width: 4)
let outer = group(at: vec(0, 5, 0))
  group at: vec(1, 0, 0), turn: 90 deg
    box size: vec(0.1, 0.1, 0.1), at: vec(1, 0, 0)
g.position = vec(0, 0, 3)
)");
  CHECK(r->ok);
  CHECK(r->problems().empty());
  auto& st = r->stage;
  if (st.placed.size() < 3 || st.scene.lights.empty()) return;
  // the box at vec(1, 0, 0) in a group turned 90 deg lands at vec(0, 0, -1); then the group moved 3 along z
  CHECK_NEAR(st.placed[0].position.x, 0, 1e-3);
  CHECK_NEAR(st.placed[0].position.z, 2, 1e-3);
  // its lamp turned and moved with it, still aimed straight down
  CHECK_NEAR(st.scene.lights[0].position.x, 0, 1e-3);
  CHECK_NEAR(st.scene.lights[0].position.z, 1, 1e-3);
  CHECK_NEAR(st.scene.lights[0].direction.y, -1, 1e-3);
  // a recipe called inside a turned group lands in the group: its sheet (4 wide along x) now runs along z
  const loop::Mat4& gx = st.scene.instances[st.placed[1].instance].transform;
  loop::Vec3 along = gx.vector({1, 0, 0});
  CHECK_NEAR(std::fabs(along.z), 1, 1e-3);
  CHECK_NEAR(st.placed[1].position.x, 10, 1e-3);
  // a group in a group: (1,0,0) turned 90 deg is (0,0,-1), moved by (1,0,0), then by (0,5,0)
  const auto& last = st.placed.back();
  CHECK_NEAR(last.position.x, 1, 1e-3);
  CHECK_NEAR(last.position.y, 5, 1e-3);
  CHECK_NEAR(last.position.z, -1, 1e-3);
}

TEST(words_group_hides_its_things_and_lamps) {
  auto r = runScene(R"(
let g = group(at: vec(0, 0, 0))
  box size: vec(1, 1, 1), at: vec(0, 0.5, 0)
  light(vec(0, 2, 0), lumens: 500)
g.visible = no
g.turn = 180 deg
g.visible = yes
)");
  CHECK(r->ok);
  CHECK(r->problems().empty());
  if (r->stage.placed.empty() || r->stage.scene.lights.empty()) return;
  CHECK(r->stage.placed[0].visible);
  CHECK_NEAR(r->stage.scene.lights[0].lumens, 500, 1e-3);
  CHECK(!runScene("group at: vec(0, 0, 0)\n")->problems().empty());  // no lines under it
}

TEST(words_a_strip_in_a_group_turns_with_it) {
  auto r = runScene(R"(
let g = group(at: vec(0, 1, 0), turn: 90 deg)
  strip(from: vec(0, 0, 0), to: vec(1, 0, 0), lumens: 300)
)");
  CHECK(r->ok);
  CHECK(r->problems().empty());
  auto& st = r->stage;
  if (st.scene.lights.size() < 4) return;
  // four lamps along x from 0 to 1 now lie along -z, one metre up, still facing down
  float zMin = 0, zMax = -9;
  for (const auto& l : st.scene.lights) {
    CHECK_NEAR(l.position.x, 0, 1e-3);
    CHECK_NEAR(l.position.y, 1, 2e-2);
    CHECK_NEAR(l.direction.y, -1, 1e-3);
    zMin = std::min(zMin, l.position.z), zMax = std::max(zMax, l.position.z);
  }
  CHECK(zMin < -0.8f && zMax < 0.01f && zMax > -0.2f);
}

TEST(words_shadows_no_lands_on_every_lamp) {
  auto r = runScene(R"(
light at: vec(0, 2, 0)
light at: vec(1, 2, 0), shadows: no
light at: vec(2, 2, 0), aim: vec(2, 0, 0), shadows: no
panel_light at: vec(0, 3, 0), shadows: no
panel_light at: vec(1, 3, 0)
)");
  CHECK(r->ok);
  CHECK(r->problems().empty());
  const auto& ls = r->stage.scene.lights;
  CHECK(ls.size() == 5);
  if (ls.size() < 5) return;
  CHECK(ls[0].castShadows);
  CHECK(!ls[1].castShadows);
  CHECK(!ls[2].castShadows && ls[2].kind == loop::Light::Kind::kSpot);
  CHECK(!ls[3].castShadows);
  CHECK(ls[4].castShadows);
}

// ---- surfaces (docs/SURFACES.md): the word `surface` and where a material's name is looked up

static const char* kOak = R"(
let oak = surface("oak",
  tile: 2 m, project: "box", along: "long", seed: 3, sheen: 0.2,
  base:   {color: rgb(0.42, 0.27, 0.14), color2: rgb(0.27, 0.16, 0.08), roughness: 0.55, roughness2: 0.7,
           pattern: "grain", size: 6 mm, stretch: 40, bump: 0.15 mm},
  vary:   {large: 1.2 m, amount: 0.12, tint: rgb(0.50, 0.30, 0.13), roughness: 0.08,
           small: 5 cm, small_amount: 0.05},
  coat:   {color: rgb(0.2, 0.3, 0.1), cover: 0.3, facing: "up"},
  wear:   {color: rgb(0.55, 0.38, 0.22), roughness: 0.35, amount: 0.8, ragged: 0.5},
  dirt:   {color: rgb(0.10, 0.07, 0.05), roughness: 0.85, amount: 0.6, ragged: 0.6},
  detail: {pattern: "pores", size: 0.4 mm, stretch: 6, bump: 0.03 mm, tile: 8 cm})
box size: vec(1, 1, 1), material: oak
)";

TEST(words_surface_oak_lands_where_section_12_says) {
  auto r = runScene(kOak);
  CHECK(r->ok);
  CHECK(r->problems().empty());
  const auto& sc = r->stage.scene;
  CHECK(sc.surfaces.size() == 1 && sc.instances.size() == 1);
  if (sc.surfaces.size() != 1 || sc.instances.size() != 1) return;
  const loop::SurfaceRecipe& s = sc.surfaces[0];
  CHECK(s.name == "oak");
  CHECK(s.seed == 3);
  CHECK_NEAR(s.tile, 2, 1e-6);
  CHECK(s.project == loop::SurfaceRecipe::Project::kBox);
  CHECK(!s.worldSpace);
  CHECK(s.along == -1);
  CHECK(s.base.on);
  CHECK_NEAR(s.base.color.x, loop::srgbToLinear(0.42f), 1e-6);  // rgb() is written as seen, kept linear
  CHECK_NEAR(s.base.color2.z, loop::srgbToLinear(0.08f), 1e-6);
  CHECK_NEAR(s.base.roughness, 0.55, 1e-6);
  CHECK_NEAR(s.base.roughness2, 0.7, 1e-6);
  CHECK(s.base.metal == 0);
  CHECK(s.base.pattern.kind == loop::SurfacePattern::Kind::kGrain);
  CHECK_NEAR(s.base.pattern.size, 0.006, 1e-7);
  CHECK_NEAR(s.base.pattern.stretch, 40, 1e-6);
  CHECK_NEAR(s.base.bump, 0.00015, 1e-8);
  CHECK_NEAR(s.varyLarge, 1.2, 1e-6);
  CHECK_NEAR(s.varyAmount, 0.12, 1e-6);
  CHECK_NEAR(s.varyTint.x, loop::srgbToLinear(0.5f), 1e-6);
  CHECK_NEAR(s.varyRoughness, 0.08, 1e-6);
  CHECK_NEAR(s.varySmall, 0.05, 1e-6);
  CHECK_NEAR(s.varySmallAmount, 0.05, 1e-6);
  CHECK(s.coat[0].on && !s.coat[1].on);
  CHECK(s.coat[0].facing == loop::SurfaceLayer::Facing::kUp);
  CHECK(s.coat[0].pattern.kind == loop::SurfacePattern::Kind::kNoise);
  CHECK_NEAR(s.coat[0].cover, 0.3, 1e-6);
  CHECK(s.wear.on && s.dirt.on);
  CHECK_NEAR(s.wear.amount, 0.8, 1e-6);
  CHECK_NEAR(s.wear.roughness, 0.35, 1e-6);
  CHECK_NEAR(s.dirt.ragged, 0.6, 1e-6);
  CHECK(s.detail.kind == loop::SurfacePattern::Kind::kPores);
  CHECK_NEAR(s.detail.size, 0.0004, 1e-8);
  CHECK_NEAR(s.detail.stretch, 6, 1e-6);
  CHECK_NEAR(s.detailBump, 0.00003, 1e-9);
  CHECK_NEAR(s.detailTile, 0.08, 1e-6);
  CHECK_NEAR(s.detailFade, 2, 1e-6);
  // the material: the base's colour, roughness and metal; the settings beside the layers
  const loop::Material& m = sc.materials[sc.instances[0].material];
  CHECK(m.surface == 0);
  CHECK_NEAR(m.baseColor.y, loop::srgbToLinear(0.27f), 1e-6);
  CHECK_NEAR(m.roughness, 0.55, 1e-6);
  CHECK_NEAR(m.sheen, 0.2, 1e-6);
  // every number counts
  loop::SurfaceRecipe t = s;
  CHECK(t.fingerprint() == s.fingerprint());
  t.dirt.ragged = 0.61f;
  CHECK(t.fingerprint() != s.fingerprint());
  t = s;
  t.detailFade = 3;
  CHECK(t.fingerprint() != s.fingerprint());
}

TEST(words_surface_refuses_by_name) {
  auto r = runScene("surface(\"a\", base: {color: rgb(0.5, 0.5, 0.5)}, wera: {amount: 0.5})\n");
  CHECK(r->hasProblem("did you mean `wear`"));
  r = runScene("surface(\"a\", base: {color: rgb(0.5, 0.5, 0.5)}, wear: {amout: 0.5})\n");
  CHECK(r->hasProblem("did you mean `amount`"));
  r = runScene("surface(\"a\", base: {color: vec(0.95, 0.95, 0.95)})\n");
  CHECK(r->hasProblem("too bright"));
  r = runScene("surface(\"a\", base: {color: vec(0.01, 0.01, 0.01)})\n");
  CHECK(r->hasProblem("too dark"));
  r = runScene("surface(\"a\", base: {color: vec(0.95, 0.95, 0.95), metal: 1})\n");
  CHECK(r->problems().empty());  // a metal may be bright
  r = runScene("surface(\"a\", base: {color: rgb(0.5, 0.5, 0.5), metal: 0.5})\n");
  CHECK(r->hasProblem("a metal or it is not"));
  r = runScene("surface(\"a\", base: {color: rgb(0.5, 0.5, 0.5), pattern: \"bricks\"})\n");
  CHECK(r->hasProblem("no `size`"));
  r = runScene("surface(\"a\", base: {color: rgb(0.5, 0.5, 0.5), pattern: \"brikcs\", size: 5 cm})\n");
  CHECK(r->hasProblem("did you mean `bricks`"));
  r = runScene("surface(\"a\", base: {color: rgb(0.5, 0.5, 0.5)}, coat: {facing: \"up\"}, coat2: {facing: \"side\"})\n");
  CHECK(r->hasProblem("only one coat with a `facing`"));
  r = runScene("surface(\"a\", base: {color: rgb(0.5, 0.5, 0.5)}, coat: {facing: \"up\"}, coat2: {facing: \"any\"})\n");
  CHECK(r->problems().empty());
  r = runScene("surface(\"a\", tile: 2 m)\n");
  CHECK(r->hasProblem("needs a `base`"));
  r = runScene("surface(\"a\", base: {color: rgb(0.5, 0.5, 0.5)}, project: \"tpo\")\n");
  CHECK(r->hasProblem("did you mean \"top\""));
}

TEST(words_surface_is_found_by_name_in_this_scene_and_the_library) {
  namespace fs = std::filesystem;
  fs::path root = fs::temp_directory_path() / "loop_m3a_library";
  fs::remove_all(root);
  fs::create_directories(root / "library" / "surfaces");
  {
    std::ofstream f(root / "library" / "surfaces" / "zz_test.loop");
    f << "to zz_test()\n  return surface(\"zz_test\", tile: 1 m, base: {color: rgb(0.2, 0.4, 0.6), roughness: 0.3})\n";
  }
  looptest::SceneRun r;
  r.stage.folder = (root / "scene").string() + "/";
  loop::script::installStandardLibrary(r.vm);
  loop::script::installSceneApi(r.vm, r.stage);
  r.ok = r.vm.load("let names = materials()\nbox size: 1, material: \"zz_test\"\nbox size: 1, material: \"brass\"\nlet again = material(\"zz_test\")\nbox size: 1, material: again\n", "t.loop");
  CHECK(r.ok);
  CHECK(r.problems().empty());
  const auto& sc = r.stage.scene;
  CHECK(sc.surfaces.size() == 1 && sc.instances.size() == 3);  // made once, kept for the scene
  if (sc.surfaces.size() != 1 || sc.instances.size() != 3) return;
  CHECK(sc.surfaces[0].name == "zz_test");
  CHECK(sc.materials[sc.instances[0].material].surface == 0);
  CHECK_NEAR(sc.materials[sc.instances[0].material].baseColor.z, loop::srgbToLinear(0.6f), 1e-6);
  CHECK(sc.materials[sc.instances[1].material].name == "brass");
  CHECK(sc.materials[sc.instances[1].material].surface == -1);
  CHECK(sc.materials[sc.instances[2].material].surface == 0);
  // materials() lists the library's, then the built-in ones
  const loop::script::List* l = r.vm.global("names").as<loop::script::List>();
  CHECK(l != nullptr);
  bool lib = false, builtin = false;
  if (l)
    for (const auto& v : l->items) {
      lib = lib || (v.isString() && v.str() == "zz_test");
      builtin = builtin || (v.isString() && v.str() == "oak");
    }
  CHECK(lib && builtin);
  fs::remove_all(root);
  // a surface made here wins over a built-in name
  auto h = runScene("surface(\"oak\", base: {color: rgb(0.5, 0.4, 0.3)})\nbox size: 1, material: \"oak\"\n");
  CHECK(h->stage.scene.materials[h->stage.scene.instances[0].material].surface == 0);
  // and with no library entry the built-in list still answers
  auto b = runScene("box size: 1, material: \"oak\"\n");
  CHECK(b->problems().empty() && b->stage.scene.materials[b->stage.scene.instances[0].material].surface == -1);
}

TEST(words_surface_is_a_listed_word) {
  auto r = runScene("let w = 1\n");
  bool found = false;
  for (auto& [name, help] : r->vm.words()) found = found || name == "surface";
  CHECK(found);
}
