#pragma once
// A project's cover page: the .looped file in its folder, beside its scenes
// (.loop files). It names the project and the scene to open first:
//
//   # The Bar: built from empty, one piece at a time.
//   project "The Bar"
//   start "bar_room"
//
// One word and its value in quotes per line; lines starting with # are notes.
#include <string>
#include <vector>

namespace loop {

struct Project {
  std::string name;    // project "…"
  std::string start;   // start "…": the scene opened first (its .loop file beside this one)
  std::string folder;  // where the cover page is (ends with /), when read from a file
};

struct ProjectProblem {
  int line = 0;  // 0: the whole file
  std::string text;
  std::string message(const std::string& file) const;  // "bar.looped line 3: …"
};

// Reads a cover page's text. False if anything is wrong; each problem says
// its line and, for a misspelt word, the word meant.
bool parseProject(const std::string& text, Project& out, std::vector<ProjectProblem>& problems);
// The same from a file; `out.folder` is the file's folder.
bool readProject(const std::string& path, Project& out, std::vector<ProjectProblem>& problems);

}  // namespace loop
