#include "engine/project/project.h"

#include <cstdlib>
#include <sys/stat.h>

#include "engine/core/file.h"
#include "engine/core/settings.h"
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

}  // namespace loop
