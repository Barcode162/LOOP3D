#include "engine/project/project.h"

#include "engine/core/file.h"
#include "engine/script/lexer.h"

namespace loop {

std::string ProjectProblem::message(const std::string& file) const {
  return line > 0 ? file + " line " + std::to_string(line) + ": " + text : file + ": " + text;
}

bool parseProject(const std::string& text, Project& out, std::vector<ProjectProblem>& problems) {
  static const std::vector<std::string> kWords = {"project", "start"};
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
