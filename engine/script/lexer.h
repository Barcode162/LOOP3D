#pragma once
// Looped Script, reading: text → tokens, with indentation turned into
// block starts and ends, numbers with their units already in metres,
// seconds, radians..., and every problem told with its line, column, plain
// words and a hint.
#include <string>
#include <string_view>
#include <vector>

namespace loop::script {

struct Problem {
  std::string file;
  int line = 0, column = 0;
  std::string what, hint;
  std::string text() const;  // "story.loop line 4, column 9: ...; did you mean `x`?"
};

enum class Tok { kName, kNumber, kString, kSymbol, kNewline, kIndent, kDedent, kEnd };

struct Token {
  Tok kind = Tok::kEnd;
  std::string text;      // name, symbol, string contents, or the number as written
  double number = 0;     // in base units
  std::string unit;      // as written ("cm"), empty if none
  int line = 0, column = 0;
  bool spaceBefore = false;
  bool is(std::string_view s) const { return (kind == Tok::kName || kind == Tok::kSymbol) && text == s; }
};

bool lex(std::string_view source, const std::string& file, std::vector<Token>& out, std::vector<Problem>& problems);

// The units numbers can carry, and what they multiply by.
bool unitScale(std::string_view unit, double& scale);
std::vector<std::string> unitNames();

// Closest word for "did you mean …?" (empty if nothing is close).
std::string closest(std::string_view word, const std::vector<std::string>& options);

}  // namespace loop::script
