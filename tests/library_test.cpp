// The library (library/): every entry has its whole card (what it is, its
// kind, how to place it, its credit and licence, where it came from), every
// name is unique (also against every project's scripts, since a scene finds
// both by name), and each entry's example placement runs with no problems.
#include <filesystem>
#include <set>
#include <string>

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
