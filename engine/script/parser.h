#pragma once
// Looped Script, understanding: tokens → a program (ast.h), with names
// resolved to the variable they mean. Problems come back in plain words.
#include <string_view>

#include "engine/script/ast.h"
#include "engine/script/lexer.h"

namespace loop::script {

bool parse(std::string_view source, const std::string& file, Program& out, std::vector<Problem>& problems);

// Every word the language itself reserves.
const std::vector<std::string>& keywords();

}  // namespace loop::script
