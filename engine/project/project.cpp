#include "engine/project/project.h"

#include <algorithm>
#include <cctype>
#include <cstdlib>
#include <filesystem>
#include <sys/stat.h>

#include "engine/core/file.h"
#include "engine/core/settings.h"
#include "engine/project/library.h"
#include "engine/script/lexer.h"

namespace loop {

std::string ProjectProblem::message(const std::string& file) const {
  return line > 0 ? file + " line " + std::to_string(line) + ": " + text : file + ": " + text;
}

namespace {

// A setting's value as written: a number, yes or no, "text", or [x, y, z].
// False if it's none of those.
bool parseValue(std::string s, Json& out) {
  while (!s.empty() && (s.back() == ' ' || s.back() == '\t' || s.back() == '\r')) s.pop_back();
  size_t lead = s.find_first_not_of(" \t");
  if (lead == std::string::npos) return false;
  s = s.substr(lead);
  if (s == "yes" || s == "true" || s == "on") return out = Json(true), true;
  if (s == "no" || s == "false" || s == "off") return out = Json(false), true;
  if (s.size() >= 2 && s.front() == '"' && s.back() == '"') return out = Json(s.substr(1, s.size() - 2)), true;
  auto number = [](const std::string& t, double& v) {
    size_t a = t.find_first_not_of(" \t"), b = t.find_last_not_of(" \t");
    if (a == std::string::npos) return false;
    std::string u = t.substr(a, b - a + 1);
    char* end = nullptr;
    v = std::strtod(u.c_str(), &end);
    return end && *end == 0 && !u.empty();
  };
  if (s.front() == '[' && s.back() == ']') {
    Json arr = Json::array();
    std::string inner = s.substr(1, s.size() - 2);
    size_t i = 0;
    while (i <= inner.size()) {
      size_t e = inner.find(',', i);
      if (e == std::string::npos) e = inner.size();
      double v;
      if (!number(inner.substr(i, e - i), v)) return false;
      arr.push(Json(v));
      i = e + 1;
    }
    return out = arr, true;
  }
  double v;
  if (!number(s, v)) return false;
  return out = Json(v), true;
}

}  // namespace

bool parseProject(const std::string& text, Project& out, std::vector<ProjectProblem>& problems) {
  static const std::vector<std::string> kWords = {"project", "start", "setting"};
  size_t before = problems.size();
  bool sawProject = false, sawStart = false;
  bool triedProject = false, triedStart = false;  // the word was there, even if its value was wrong
  int lineNo = 0;
  size_t i = 0;
  while (i <= text.size()) {
    size_t e = text.find('\n', i);
    if (e == std::string::npos) e = text.size();
    std::string line = text.substr(i, e - i);
    i = e + 1;
    ++lineNo;
    size_t a = line.find_first_not_of(" \t\r");
    if (a == std::string::npos || line[a] == '#') continue;
    size_t b = line.find_first_of(" \t", a);
    std::string word = line.substr(a, b == std::string::npos ? std::string::npos : b - a);
    if (word == "setting") {
      // setting "name", value   (# a note may follow)
      size_t q1 = b == std::string::npos ? std::string::npos : line.find_first_not_of(" \t", b);
      size_t q2 = q1 == std::string::npos || line[q1] != '"' ? std::string::npos : line.find('"', q1 + 1);
      size_t comma = q2 == std::string::npos ? std::string::npos : line.find_first_not_of(" \t", q2 + 1);
      if (q2 == std::string::npos || comma == std::string::npos || line[comma] != ',') {
        problems.push_back({lineNo, "a setting is its name in double quotes, a comma, then its value, like setting \"player.walk\", 1.6"});
        continue;
      }
      std::string name = line.substr(q1 + 1, q2 - q1 - 1);
      std::string rest = line.substr(comma + 1);
      // a note after the value (a # outside quotes)
      bool inText = false;
      for (size_t k = 0; k < rest.size(); ++k) {
        if (rest[k] == '"') inText = !inText;
        if (rest[k] == '#' && !inText) {
          rest.resize(k);
          break;
        }
      }
      Json value;
      if (!parseValue(rest, value)) {
        problems.push_back({lineNo, "\"" + name + "\" needs a value: a number, yes or no, text in double quotes, or three numbers like [0, -9.81, 0]"});
        continue;
      }
      std::string why;
      if (!Settings::get().check(name, value, &why)) {
        problems.push_back({lineNo, why});
        continue;
      }
      for (const Project::Setting& s : out.settings)
        if (s.name == name) why = name + " is given twice (first on line " + std::to_string(s.line) + ")";
      if (!why.empty()) {
        problems.push_back({lineNo, why});
        continue;
      }
      out.settings.push_back({name, value, lineNo});
      continue;
    }
    if (word != "project" && word != "start") {
      std::string near = script::closest(word, kWords);
      problems.push_back({lineNo, "\"" + word + "\" isn't a word of a project file" + (near.empty() ? "" : "; did you mean \"" + near + "\"?")});
      continue;
    }
    (word == "project" ? triedProject : triedStart) = true;
    // its value: one string in double quotes, then nothing but a note
    size_t q1 = b == std::string::npos ? std::string::npos : line.find_first_not_of(" \t", b);
    size_t q2 = q1 == std::string::npos || line[q1] != '"' ? std::string::npos : line.find('"', q1 + 1);
    if (q2 == std::string::npos) {
      problems.push_back({lineNo, word + " needs its value in double quotes, like " + word + (word == "project" ? " \"The Bar\"" : " \"bar_room\"")});
      continue;
    }
    size_t rest = line.find_first_not_of(" \t\r", q2 + 1);
    if (rest != std::string::npos && line[rest] != '#') {
      problems.push_back({lineNo, "only one value on a line: put the rest on a line of its own, or after a # as a note"});
      continue;
    }
    std::string value = line.substr(q1 + 1, q2 - q1 - 1);
    if (value.empty()) {
      problems.push_back({lineNo, word + " can't be empty"});
      continue;
    }
    bool& seen = word == "project" ? sawProject : sawStart;
    if (seen) {
      problems.push_back({lineNo, word + " is given twice"});
      continue;
    }
    seen = true;
    (word == "project" ? out.name : out.start) = value;
  }
  // a word that was there with a wrong value already has its problem
  if (!triedProject) problems.push_back({0, "no name: add a line like project \"The Bar\""});
  if (!triedStart) problems.push_back({0, "no first scene: add a line like start \"bar_room\""});
  return problems.size() == before;
}

std::string Project::scenePath(const std::string& scene) const {
  struct stat sb;
  for (const std::string& p : {joinPath(folder, "places/" + scene + ".loop"), joinPath(folder, scene + ".loop")})
    if (stat(p.c_str(), &sb) == 0) return p;
  return {};
}

bool readProject(const std::string& path, Project& out, std::vector<ProjectProblem>& problems) {
  std::string text;
  if (!readText(path, text)) {
    problems.push_back({0, "can't read the file"});
    return false;
  }
  out.folder = directoryOf(path);
  return parseProject(text, out, problems);
}

std::string projectFolderName(const std::string& name) {
  std::string out;
  for (char ch : name) {
    unsigned char u = static_cast<unsigned char>(ch);
    if (std::isalnum(u)) out += static_cast<char>(std::tolower(u));
    else if (ch == ' ' || ch == '-') out += out.empty() || out.back() == '_' ? "" : "_";
    else return {};
  }
  while (!out.empty() && out.back() == '_') out.pop_back();
  if (out.empty() || !std::isalpha(static_cast<unsigned char>(out[0]))) return {};
  return out;
}

namespace {
// Each line starting with `word ` becomes `word "value"`.
std::string withValue(const std::string& text, const std::string& word, const std::string& value) {
  std::string out, line;
  size_t i = 0;
  while (i < text.size()) {
    size_t e = text.find('\n', i);
    if (e == std::string::npos) e = text.size();
    line = text.substr(i, e - i);
    if (line.compare(0, word.size() + 1, word + " ") == 0) line = word + " \"" + value + "\"";
    out += line + "\n";
    i = e + 1;
  }
  return out;
}
}  // namespace

bool makeProject(const std::string& templateDir, const std::string& projectsDir, const std::string& name, std::string& folderOut, std::string& error) {
  namespace fs = std::filesystem;
  std::string folder = projectFolderName(name);
  if (folder.empty()) {
    error = "a project's name is letters, numbers, spaces and hyphens, starting with a letter, like \"My Game\"";
    return false;
  }
  fs::path src(templateDir), dst = fs::path(projectsDir) / folder;
  std::error_code ec;
  if (fs::exists(dst, ec)) {
    error = "there's already a project folder " + dst.string();
    return false;
  }
  std::string tname = src.filename().string();
  if (tname.empty()) tname = src.parent_path().filename().string();
  Project t;
  std::vector<ProjectProblem> ps;
  if (!readProject((src / (tname + ".looped")).string(), t, ps)) {
    error = "the template doesn't read: " + (ps.empty() ? std::string("no cover page") : ps[0].message(tname + ".looped"));
    return false;
  }
  std::string firstRoom = t.scenePath(t.start);
  if (firstRoom.empty()) {
    error = "the template's first room \"" + t.start + "\" has no file";
    return false;
  }
  auto undo = [&](const std::string& why) {
    fs::remove_all(dst, ec);
    error = why;
    return false;
  };
  fs::copy(src, dst, fs::copy_options::recursive, ec);
  if (ec) return undo("couldn't copy the template: " + ec.message());
  // the cover page: renamed, naming the project and its renamed first room
  std::string room = folder + "_room", text;
  fs::path cover = dst / (folder + ".looped");
  fs::rename(dst / (tname + ".looped"), cover, ec);
  if (ec || !readText(cover.string(), text)) return undo("couldn't rename the cover page");
  size_t firstLine = text.find('\n');
  if (text.rfind("# ", 0) == 0 && firstLine != std::string::npos) text = "# " + name + ", made from the template." + text.substr(firstLine);
  text = withValue(withValue(text, "project", name), "start", room);
  if (!writeText(cover.string(), text)) return undo("couldn't write the cover page");
  // the first room: renamed, titled with the name
  fs::path oldRoom = dst / fs::relative(fs::path(firstRoom), src, ec);
  fs::path newRoom = oldRoom.parent_path() / (room + ".loop");
  fs::rename(oldRoom, newRoom, ec);
  std::string roomText;
  if (ec || !readText(newRoom.string(), roomText)) return undo("couldn't rename the first room");
  if (!writeText(newRoom.string(), withValue(roomText, "title", name))) return undo("couldn't write the first room");
  // read back
  Project made;
  ps.clear();
  if (!readProject(cover.string(), made, ps) || made.scenePath(made.start).empty())
    return undo("the new project doesn't read: " + (ps.empty() ? std::string("its first room is missing") : ps[0].message(folder + ".looped")));
  folderOut = dst.string();
  return true;
}

std::string expandUses(const std::string& src, const std::string& dir, std::vector<std::string>& seen) {
  std::string out, line;
  size_t i = 0;
  while (i <= src.size()) {
    size_t e = src.find('\n', i);
    if (e == std::string::npos) e = src.size();
    line = src.substr(i, e - i);
    size_t a = line.find_first_not_of(" \t");
    if (a != std::string::npos && line.compare(a, 5, "use \"") == 0) {
      size_t q = line.find('"', a + 5);
      std::string name = line.substr(a + 5, q - a - 5);
      // the scene's own folder first, then the library
      std::string file = joinPath(dir, name + ".loop"), text;
      std::error_code ec;
      if (!std::filesystem::is_regular_file(file, ec)) {
        std::string lib = libraryEntryPath(findLibrary(dir), name);
        if (!lib.empty()) file = lib;
      }
      if (std::find(seen.begin(), seen.end(), file) == seen.end()) {
        seen.push_back(file);
        if (readText(file, text)) out += expandUses(text, directoryOf(file), seen) + "\n";
        else out += "say \"can't find " + name + ".loop\"\n";
      }
      out += "\n";
    } else out += line + "\n";
    i = e + 1;
  }
  return out;
}

bool readSceneSource(const std::string& path, std::string& out) {
  std::string src;
  if (!readText(path, src)) return false;
  std::vector<std::string> seen{path};
  out = expandUses(src, directoryOf(path), seen);
  return true;
}

}  // namespace loop
