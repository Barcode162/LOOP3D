#include "engine/project/library.h"

#include <algorithm>
#include <filesystem>
#include <map>

#include "engine/core/file.h"

namespace loop {

namespace fs = std::filesystem;

std::string findLibrary(const std::string& dir) {
  std::error_code ec;
  fs::path p = fs::absolute(dir.empty() ? fs::path(".") : fs::path(dir), ec);
  for (int up = 0; up < 12 && !p.empty(); ++up) {
    if (fs::is_directory(p / "library", ec)) return (p / "library").string() + "/";
    if (!p.has_parent_path() || p.parent_path() == p) break;
    p = p.parent_path();
  }
  return "";
}

std::string libraryEntryPath(const std::string& library, const std::string& name) {
  if (library.empty() || name.empty() || name.find('/') != std::string::npos || name.find("..") != std::string::npos) return "";
  std::error_code ec;
  for (const auto& kind : fs::directory_iterator(library, ec)) {
    if (!kind.is_directory()) continue;
    fs::path f = kind.path() / (name + ".loop");
    if (fs::is_regular_file(f, ec)) return f.string();
  }
  return "";
}

namespace {
std::string trim(const std::string& s) {
  size_t a = s.find_first_not_of(" \t\r"), b = s.find_last_not_of(" \t\r");
  return a == std::string::npos ? "" : s.substr(a, b - a + 1);
}
}  // namespace

bool parseLibraryCard(const std::string& text, LibraryEntry& out, std::vector<std::string>& problems) {
  size_t before = problems.size();
  size_t i = 0;
  bool first = true;
  while (i < text.size()) {
    size_t e = text.find('\n', i);
    if (e == std::string::npos) e = text.size();
    std::string line = text.substr(i, e - i);
    i = e + 1;
    if (line.empty() || line[0] != '#') break;  // the card ends where the recipe starts
    std::string body = trim(line.substr(1));
    if (first) {
      first = false;
      // "name · what it is"
      size_t dot = body.find("\xC2\xB7");
      if (dot == std::string::npos) {
        problems.push_back("its first line should be \"# name · what it is\"");
        continue;
      }
      out.name = trim(body.substr(0, dot));
      out.what = trim(body.substr(dot + 2));
      continue;
    }
    size_t colon = body.find(':');
    std::string key = colon == std::string::npos ? "" : body.substr(0, colon);
    std::string value = colon == std::string::npos ? "" : trim(body.substr(colon + 1));
    if (key == "kind") out.kind = value;
    else if (key == "place") out.place = value;
    else if (key == "credit") out.credit = value;
    else if (key == "licence" || key == "license") out.licence = value;
    else if (key == "from") out.from = value;
    else if (!body.empty()) out.about += (out.about.empty() ? "" : " ") + body;
  }
  if (out.name.empty() && problems.size() == before) problems.push_back("it has no card (its first line should be \"# name · what it is\")");
  if (out.what.empty() && problems.size() == before) problems.push_back("its card doesn't say what it is");
  for (auto [field, name] : {std::pair{&out.kind, "kind"}, {&out.place, "place"}, {&out.credit, "credit"}, {&out.licence, "licence"}, {&out.from, "from"}})
    if (field->empty()) problems.push_back(std::string("its card has no \"# ") + name + ": …\" line");
  return problems.size() == before;
}

bool readLibrary(const std::string& library, std::vector<LibraryEntry>& out, std::vector<std::string>& problems) {
  out.clear();
  size_t before = problems.size();
  std::error_code ec;
  std::vector<fs::path> files;
  for (const auto& kind : fs::directory_iterator(library, ec))
    if (kind.is_directory())
      for (const auto& f : fs::directory_iterator(kind.path(), ec))
        if (f.is_regular_file() && f.path().extension() == ".loop") files.push_back(f.path());
  std::sort(files.begin(), files.end());
  std::map<std::string, std::string> seen;
  for (const fs::path& f : files) {
    std::string text;
    if (!readText(f.string(), text)) {
      problems.push_back(f.string() + ": can't be read");
      continue;
    }
    LibraryEntry e;
    std::vector<std::string> why;
    parseLibraryCard(text, e, why);
    e.path = f.string();
    std::string stem = f.stem().string();
    if (e.name != stem) why.push_back("its card names it \"" + e.name + "\" but its file is " + stem + ".loop");
    // each kind has its folder: a model in models/, a room in rooms/
    if (e.kind + "s" != f.parent_path().filename().string()) why.push_back("its card says kind \"" + e.kind + "\" but it's in " + f.parent_path().filename().string() + "/");
    auto [it, fresh] = seen.emplace(stem, f.string());
    if (!fresh) why.push_back("another entry is called " + stem + " (" + it->second + "): names must be unique");
    for (const std::string& w : why) problems.push_back(f.string() + ": " + w);
    out.push_back(std::move(e));
  }
  return problems.size() == before;
}

}  // namespace loop
