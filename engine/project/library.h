#pragma once
// The library: what every project can use, and what grows with every piece of
// work. One folder, library/<kinds>/<name>.loop beside projects/ (a model in
// library/models/, a room in library/rooms/), each entry a
// text recipe (Looped Script) that starts with its card:
//
//   # lantern · a stone post with an iron cage of frosted glass, and a flame you light
//   # lantern(name, at) places one, dark; light_lantern(l) lights it. ...
//   #
//   # kind: model
//   # place: lantern("a lantern", AT)
//   # credit: J Production, made in Loop3D
//   # licence: our own work, free for every J Production project
//   # from: The Lantern Isle (2026-11-06)
//
// The first line is its name and what it is; the lines after, how to use it;
// then kind (model, room, piece, ...), place (one line that places an example,
// AT standing for where: the editor puts the view's spot there), credit,
// licence and where it came from. Four lines are optional (docs/LIBRARY_PLAN.md,
// section 2): `tags:` (words to find it by), `size:` (hand, chair, sofa, room
// or land: its triangle budget), `sockets:` (what it offers: seat, hold, top,
// hang, open, light, lie) and `seeds:` (the range of seeds that look right,
// for a recipe that takes one). An entry without a credit or a licence is
// refused (CLAUDE.md: no asset without a licence that allows it).
//
// A scene brings an entry in by name, as any shared script: `use "lantern"`
// (its own folder first, then the library).
#include <string>
#include <vector>

namespace loop {

struct LibraryEntry {
  std::string name, what, about;  // about: the lines saying how to use it
  std::string kind, place, credit, licence, from;
  std::string tags, size, sockets, seeds;  // optional card lines ("" when absent)
  std::string pictures, view;  // optional: up to three calls split by "|" for `loop library picture --case n`; where the camera stands
  std::string path;               // the entry's file
};

// The library folder for a scene in `dir`: the nearest library/ in it or a
// folder above it (the repo's own, beside projects/); "" if there's none.
std::string findLibrary(const std::string& dir);
// An entry's file by name (library/<kinds>/<name>.loop); "" if there's none.
std::string libraryEntryPath(const std::string& library, const std::string& name);
// Reads one entry's card from its text. Problems name what's missing.
bool parseLibraryCard(const std::string& text, LibraryEntry& out, std::vector<std::string>& problems);
// Every entry of the library, by kind then name. False if any is wrong
// (a missing card line, two entries with one name); each problem says which.
bool readLibrary(const std::string& library, std::vector<LibraryEntry>& out, std::vector<std::string>& problems);

// The most triangles an entry of that size may have (hand, chair, sofa, room);
// 0 for a size with no budget (land: the plan states none) or no size.
size_t libraryBudget(const std::string& size);
// Places the entry's example and counts the triangles it makes (every copy
// counted). False, with why, if it won't load.
bool countLibraryTriangles(const std::string& library, const LibraryEntry& e, size_t& triangles, std::string& why);
// The check: counts every entry's triangles into `counts` (same order as
// `entries`) and adds a problem for each one over its size's budget.
// False if any is.
bool checkLibraryBudgets(const std::string& library, const std::vector<LibraryEntry>& entries, std::vector<size_t>& counts, std::vector<std::string>& problems);

}  // namespace loop
