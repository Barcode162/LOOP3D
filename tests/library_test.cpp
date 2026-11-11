// The library (library/): every entry has its whole card (what it is, its
// kind, how to place it, its credit and licence, where it came from), every
// name is unique (also against every project's scripts, since a scene finds
// both by name), and each entry's example placement runs with no problems.
#include <filesystem>
#include <set>
#include <string>

#include "engine/core/image.h"
#include "engine/core/settings.h"
#include "engine/project/library.h"
#include "engine/project/project.h"
#include "engine/script/scene_api.h"
#include "tests/test.h"

using namespace loop;
using namespace loop::script;

TEST(library_cards_are_whole_and_names_unique) {
  std::string lib = findLibrary(".");
  CHECK(!lib.empty());
  std::vector<LibraryEntry> entries;
  std::vector<std::string> problems;
  bool ok = readLibrary(lib, entries, problems);
  for (const std::string& p : problems) std::printf("    %s\n", p.c_str());
  CHECK(ok);
  CHECK(entries.size() >= 5);
  // a project's script with an entry's name would hide it (a scene's own folder comes first)
  std::set<std::string> names;
  for (const LibraryEntry& e : entries) names.insert(e.name);
  namespace fs = std::filesystem;
  for (const auto& f : fs::recursive_directory_iterator("projects"))
    if (f.path().extension() == ".loop" && names.count(f.path().stem().string())) {
      std::printf("    %s has a library entry's name\n", f.path().string().c_str());
      CHECK(false);
    }
}

TEST(library_card_refuses_what_has_no_licence) {
  LibraryEntry e;
  std::vector<std::string> problems;
  CHECK(!parseLibraryCard("# chair · a chair\n# kind: piece\n# place: chair(AT)\n# credit: J Production\n# from: here\n\nto chair(at)\n", e, problems));
  CHECK(problems.size() == 1 && problems[0].find("licence") != std::string::npos);
  problems.clear();
  CHECK(!parseLibraryCard("to chair(at)\n", e, problems));
  CHECK(!problems.empty());
}

TEST(library_entries_place_their_example) {
  std::string lib = findLibrary(".");
  std::vector<LibraryEntry> entries;
  std::vector<std::string> problems;
  readLibrary(lib, entries, problems);
  for (const LibraryEntry& e : entries) {
    Settings::get().resetAll();
    std::string place = e.place;
    for (size_t at; (at = place.find("AT")) != std::string::npos;) place.replace(at, 2, "vec(0, 0, 0)");
    // a scene that uses the entry by name, from a project's folder
    std::vector<std::string> seen;
    std::string src = expandUses("use \"" + e.name + "\"\n" + place + "\n", "projects/showcase/", seen);
    Vm vm;
    Stage st;
    installStandardLibrary(vm);
    installSceneApi(vm, st);
    bool ok = vm.load(src, e.name + ".loop");
    for (const auto& p : vm.problems()) std::printf("    %s: %s\n", e.name.c_str(), p.text().c_str());
    CHECK(ok);
    for (int i = 0; i < 60; ++i) vm.update(1.0 / 120), st.update(1.0f / 120);
    CHECK(vm.problems().empty());
    if (st.scene.instances.empty()) std::printf("    %s placed nothing\n", e.name.c_str());
    CHECK(!st.scene.instances.empty());
  }
}

TEST(library_card_new_lines_are_read_and_checked) {
  LibraryEntry e;
  std::vector<std::string> problems;
  CHECK(parseLibraryCard("# chair · a chair\n# kind: piece\n# place: chair(AT)\n# credit: J\n# licence: ours\n# from: here\n"
                         "# tags: seat, wood\n# size: chair\n# sockets: seat, top\n# seeds: 1-9\n\nto chair(at)\n", e, problems));
  CHECK(e.tags == "seat, wood" && e.size == "chair" && e.sockets == "seat, top" && e.seeds == "1-9");
  CHECK(e.about.empty());
  // without them the card is still whole
  LibraryEntry plain;
  problems.clear();
  CHECK(parseLibraryCard("# a · b\n# kind: piece\n# place: a(AT)\n# credit: J\n# licence: ours\n# from: here\n", plain, problems));
  CHECK(plain.size.empty());
  // a size or a socket the plan doesn't name is refused
  LibraryEntry bad;
  problems.clear();
  CHECK(!parseLibraryCard("# a · b\n# kind: piece\n# place: a(AT)\n# credit: J\n# licence: ours\n# from: here\n# size: huge\n# sockets: seat, chair\n", bad, problems));
  CHECK(problems.size() == 2);
  // the picture lines: calls split by |, and a view the plan names
  LibraryEntry shot, badView;
  problems.clear();
  CHECK(parseLibraryCard("# a · b\n# kind: piece\n# place: a(AT)\n# credit: J\n# licence: ours\n# from: here\n# pictures: a(AT) | a(AT, 2)\n# view: front\n", shot, problems));
  CHECK(shot.pictures == "a(AT) | a(AT, 2)" && shot.view == "front");
  problems.clear();
  CHECK(!parseLibraryCard("# a · b\n# kind: piece\n# place: a(AT)\n# credit: J\n# licence: ours\n# from: here\n# view: sideways\n", badView, problems));
  CHECK(libraryBudget("hand") == 2000 && libraryBudget("chair") == 10000 && libraryBudget("sofa") == 30000 && libraryBudget("room") == 100000);
  CHECK(libraryBudget("land") == 0 && libraryBudget("") == 0);
}

TEST(library_entries_are_within_their_size_budget) {
  std::string lib = findLibrary(".");
  std::vector<LibraryEntry> entries;
  std::vector<std::string> problems;
  CHECK(readLibrary(lib, entries, problems));
  std::vector<size_t> counts;
  bool ok = checkLibraryBudgets(lib, entries, counts, problems);
  for (const std::string& p : problems) std::printf("    %s\n", p.c_str());
  CHECK(ok);
  CHECK(counts.size() == entries.size());
  for (size_t i = 0; i < entries.size(); ++i) std::printf("    %-14s %zu triangles\n", entries[i].name.c_str(), counts[i]);
  // an entry over its size's budget is refused (the same entry, called a hand when it is bigger)
  for (size_t i = 0; i < entries.size(); ++i)
    if (counts[i] > libraryBudget("hand")) {
      LibraryEntry big = entries[i];
      big.size = "hand";
      problems.clear();
      CHECK(!checkLibraryBudgets(lib, {big}, counts, problems));
      CHECK(problems.size() == 1 && problems[0].find("over") != std::string::npos);
      break;
    }
}

// `loop library picture` for an entry of kind surface: the test ball, near and --far
// (docs/SURFACES.md section 11). Run as the real command, from a made-up library folder.
namespace {
bool pictureIsNotFlat(const std::string& path) {
  Image img;
  if (!readPng(path, img, false) || img.width != 512 || img.height != 512) return false;
  float lo = 1e9f, hi = -1e9f;
  for (const Vec4& p : img.px) lo = std::min(lo, p.x + p.y + p.z), hi = std::max(hi, p.x + p.y + p.z);
  return hi - lo > 0.3f;
}
}  // namespace

TEST(library_picture_of_a_surface_is_the_test_ball) {
  namespace fs = std::filesystem;
  const std::string dir = "build/m8_library";
  std::error_code ec;
  fs::remove_all(dir, ec);
  fs::create_directories(dir + "/library/surfaces", ec);
  fs::create_directories(dir + "/library/models", ec);
  fs::copy_file("library/models/test_ball.loop", dir + "/library/models/test_ball.loop", ec);
  CHECK(!ec);
  FILE* f = std::fopen((dir + "/library/surfaces/test_red.loop").c_str(), "w");
  CHECK(f != nullptr);
  if (!f) return;
  std::fputs(
      "# test_red · glossy red paint, for the test\n#\n# kind: surface\n# place: test_ball(\"test_red\", AT)\n# credit: J Production\n"
      "# licence: our own work\n# from: order M8\n# size: chair\n\n"
      "to test_red(seed: 1)\n"
      "  return surface(\"test_red\", seed: seed, tile: 0.5 m, base: {color: rgb(0.55, 0.05, 0.04), roughness: 0.3}, "
      "wear: {color: rgb(0.6, 0.6, 0.62), amount: 0.9}, dirt: {color: rgb(0.06, 0.04, 0.03), amount: 0.8})\n",
      f);
  std::fclose(f);
  // small pictures: 8 samples, not the command's "good"
  const std::string loop = fs::absolute("build/loop").string();
  const std::string go = "cd " + dir + " && " + loop + " library picture ";
  CHECK(std::system((go + "test_red --samples 8 -o near.png > near.txt 2>&1").c_str()) == 0);
  CHECK(std::system((go + "test_red --samples 8 --far --seed 2 -o far.png > far.txt 2>&1").c_str()) == 0);
  CHECK(pictureIsNotFlat(dir + "/near.png"));
  CHECK(pictureIsNotFlat(dir + "/far.png"));
  // the check places a surface's example on the test ball too (it said "test_ball isn't known": order M3d)
  CHECK(std::system(("cd " + dir + " && " + loop + " library check > check.txt 2>&1").c_str()) == 0);
  // a built-in material's name is not a surface entry; and --far is only for a surface
  CHECK(std::system((go + "oak -o oak.png > oak.txt 2>&1").c_str()) != 0);
  CHECK(!fs::exists(dir + "/oak.png"));
  CHECK(std::system((go + "test_ball --far -o x.png > x.txt 2>&1").c_str()) != 0);
}
