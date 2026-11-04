#include "engine/script/lexer.h"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdlib>

namespace loop::script {

std::string Problem::text() const {
  std::string s = (file.empty() ? "" : file + " ") + "line " + std::to_string(line);
  if (column > 0) s += ", column " + std::to_string(column);
  s += ": " + what;
  if (!hint.empty()) s += "; " + hint;
  return s;
}

namespace {
struct Unit {
  const char* name;
  double scale;
};
const Unit kUnits[] = {
    {"m", 1},          {"metre", 1},       {"metres", 1},      {"meter", 1},      {"meters", 1},
    {"cm", 0.01},      {"mm", 0.001},      {"km", 1000},       {"s", 1},          {"sec", 1},
    {"second", 1},     {"seconds", 1},     {"ms", 0.001},      {"min", 60},       {"minute", 60},
    {"minutes", 60},   {"h", 3600},        {"hour", 3600},     {"hours", 3600},   {"deg", 0.017453292519943295},
    {"degree", 0.017453292519943295},      {"degrees", 0.017453292519943295},     {"rad", 1},
    {"radians", 1},    {"K", 1},           {"kelvin", 1},      {"lm", 1},         {"lumen", 1},
    {"lumens", 1},     {"lux", 1},         {"lx", 1},          {"nits", 1},       {"kg", 1},
    {"g", 0.001},      {"Hz", 1},          {"hz", 1},          {"dB", 1},         {"db", 1},
    {"kmh", 1 / 3.6},  {"mph", 0.44704},   {"beats", 1},       {"beat", 1},
};
}  // namespace

bool unitScale(std::string_view u, double& scale) {
  for (const Unit& x : kUnits)
    if (u == x.name) { scale = x.scale; return true; }
  return false;
}
std::vector<std::string> unitNames() {
  std::vector<std::string> v;
  for (const Unit& x : kUnits) v.push_back(x.name);
  return v;
}

std::string closest(std::string_view w, const std::vector<std::string>& options) {
  auto dist = [](std::string_view a, std::string_view b) {
    std::vector<int> prev(b.size() + 1), cur(b.size() + 1);
    for (size_t j = 0; j <= b.size(); ++j) prev[j] = static_cast<int>(j);
    for (size_t i = 1; i <= a.size(); ++i) {
      cur[0] = static_cast<int>(i);
      for (size_t j = 1; j <= b.size(); ++j) {
        int c = std::tolower(a[i - 1]) == std::tolower(b[j - 1]) ? 0 : 1;
        cur[j] = std::min({prev[j] + 1, cur[j - 1] + 1, prev[j - 1] + c});
      }
      std::swap(prev, cur);
    }
    return prev[b.size()];
  };
  std::string best;
  int bd = 1 << 30;
  for (const auto& o : options) {
    int d = dist(w, o);
    if (d < bd) bd = d, best = o;
  }
  int limit = w.size() <= 3 ? 1 : w.size() <= 6 ? 2 : 3;
  return bd <= limit && best != w ? best : std::string();
}

bool lex(std::string_view src, const std::string& file, std::vector<Token>& out, std::vector<Problem>& problems) {
  size_t i = 0;
  int line = 1, col = 1;
  std::vector<int> indents = {0};
  int depth = 0;  // inside ( [ {: newlines don't end statements
  bool atLineStart = true;
  size_t before = problems.size();
  auto problem = [&](int l, int c, std::string what, std::string hint = {}) { problems.push_back({file, l, c, std::move(what), std::move(hint)}); };
  auto push = [&](Tok k, std::string text, int l, int c, bool sp) {
    Token t;
    t.kind = k, t.text = std::move(text), t.line = l, t.column = c, t.spaceBefore = sp;
    out.push_back(std::move(t));
  };
  while (i <= src.size()) {
    if (atLineStart && depth == 0) {
      // measure indentation; skip blank and comment-only lines
      int width = 0;
      size_t j = i;
      while (j < src.size() && (src[j] == ' ' || src[j] == '\t')) width += src[j] == '\t' ? 4 : 1, ++j;
      if (j >= src.size()) { i = j; break; }
      if (src[j] == '\n' || src[j] == '\r' || src[j] == '#') {
        while (j < src.size() && src[j] != '\n') ++j;
        if (j < src.size()) ++j;
        i = j, ++line, col = 1;
        continue;
      }
      col = static_cast<int>(j - i) + 1;
      i = j;
      if (width > indents.back()) {
        indents.push_back(width);
        push(Tok::kIndent, "", line, col, false);
      } else {
        while (width < indents.back()) {
          indents.pop_back();
          push(Tok::kDedent, "", line, col, false);
        }
        if (width != indents.back()) problem(line, col, "this line's indentation doesn't match any line above it", "line it up with the block it belongs to");
      }
      atLineStart = false;
    }
    if (i >= src.size()) break;
    char c = src[i];
    bool space = false;
    while (c == ' ' || c == '\t' || c == '\r') {
      ++i, ++col, space = true;
      if (i >= src.size()) break;
      c = src[i];
    }
    if (i >= src.size()) break;
    if (c == '#') {
      while (i < src.size() && src[i] != '\n') ++i;
      continue;
    }
    if (c == '\n') {
      ++i, ++line, col = 1;
      if (depth == 0) {
        if (!out.empty() && out.back().kind != Tok::kNewline && out.back().kind != Tok::kIndent && out.back().kind != Tok::kDedent)
          push(Tok::kNewline, "", line - 1, 0, false);
        atLineStart = true;
      }
      continue;
    }
    if (c == '\\' && i + 1 < src.size() && src[i + 1] == '\n') {  // line continues
      i += 2, ++line, col = 1;
      continue;
    }
    int startCol = col, startLine = line;
    if (std::isdigit(static_cast<unsigned char>(c)) || (c == '.' && i + 1 < src.size() && std::isdigit(static_cast<unsigned char>(src[i + 1])))) {
      size_t j = i;
      while (j < src.size() && (std::isdigit(static_cast<unsigned char>(src[j])) || src[j] == '.' || src[j] == '_')) ++j;
      if (j < src.size() && (src[j] == 'e' || src[j] == 'E') && j + 1 < src.size() &&
          (std::isdigit(static_cast<unsigned char>(src[j + 1])) || ((src[j + 1] == '-' || src[j + 1] == '+') && j + 2 < src.size() && std::isdigit(static_cast<unsigned char>(src[j + 2]))))) {
        j += 2;
        while (j < src.size() && std::isdigit(static_cast<unsigned char>(src[j]))) ++j;
      }
      std::string text(src.substr(i, j - i));
      std::string clean;
      for (char ch : text)
        if (ch != '_') clean += ch;
      Token t;
      t.kind = Tok::kNumber, t.text = text, t.line = line, t.column = startCol, t.spaceBefore = space;
      t.number = std::strtod(clean.c_str(), nullptr);
      col += static_cast<int>(j - i);
      i = j;
      // a unit: % right after the digits, or a unit word after a space
      if (i < src.size() && src[i] == '%') {
        t.number /= 100, t.unit = "%";
        ++i, ++col;
      } else {
        size_t k = i;
        while (k < src.size() && src[k] == ' ') ++k;
        size_t e = k;
        while (e < src.size() && (std::isalpha(static_cast<unsigned char>(src[e])))) ++e;
        if (e > k) {
          std::string word(src.substr(k, e - k));
          bool wordEnds = e >= src.size() || !(std::isalnum(static_cast<unsigned char>(src[e])) || src[e] == '_');
          double sc;
          if (wordEnds && unitScale(word, sc)) {
            t.number *= sc, t.unit = word;
            col += static_cast<int>(e - i);
            i = e;
          }
        }
      }
      out.push_back(t);
      continue;
    }
    if (std::isalpha(static_cast<unsigned char>(c)) || c == '_' || static_cast<unsigned char>(c) >= 0x80) {
      size_t j = i;
      while (j < src.size() && (std::isalnum(static_cast<unsigned char>(src[j])) || src[j] == '_' || static_cast<unsigned char>(src[j]) >= 0x80 ||
                                (src[j] == '\'' && j + 1 < src.size() && std::isalpha(static_cast<unsigned char>(src[j + 1])))))
        ++j;
      push(Tok::kName, std::string(src.substr(i, j - i)), line, startCol, space);
      col += static_cast<int>(j - i);
      i = j;
      continue;
    }
    if (c == '"') {
      std::string s;
      size_t j = i + 1;
      bool closed = false;
      while (j < src.size()) {
        char d = src[j];
        if (d == '\n') break;
        if (d == '"') { closed = true; break; }
        if (d == '\\' && j + 1 < src.size()) {
          char e = src[j + 1];
          s += e == 'n' ? '\n' : e == 't' ? '\t' : e;
          j += 2;
          continue;
        }
        s += d;
        ++j;
      }
      if (!closed) problem(line, startCol, "this text has no closing \"", "end it with \" on the same line");
      push(Tok::kString, s, line, startCol, space);
      col += static_cast<int>(j - i + 1);
      i = closed ? j + 1 : j;
      continue;
    }
    static const char* twos[] = {"==", "!=", "<=", ">=", "+=", "-=", "*=", "/=", "->", ".."};
    std::string sym(1, c);
    if (i + 1 < src.size())
      for (const char* t : twos)
        if (src[i] == t[0] && src[i + 1] == t[1]) sym = t;
    if (std::string("()[]{},:.+-*/%^=<>!").find(c) == std::string::npos) {
      problem(line, startCol, std::string("`") + c + "` isn't something Looped Script reads");
      ++i, ++col;
      continue;
    }
    if (sym == "(" || sym == "[" || sym == "{") ++depth;
    if ((sym == ")" || sym == "]" || sym == "}") && depth > 0) --depth;
    push(Tok::kSymbol, sym, startLine, startCol, space);
    i += sym.size(), col += static_cast<int>(sym.size());
  }
  if (!out.empty() && out.back().kind != Tok::kNewline && out.back().kind != Tok::kDedent) push(Tok::kNewline, "", line, 0, false);
  while (indents.size() > 1) indents.pop_back(), push(Tok::kDedent, "", line, 0, false);
  push(Tok::kEnd, "", line, 0, false);
  return problems.size() == before;
}

}  // namespace loop::script
