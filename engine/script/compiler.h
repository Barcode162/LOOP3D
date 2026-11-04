#pragma once
#include <vector>

#include "engine/script/lexer.h"
#include "engine/script/value.h"

namespace loop::script {
class Vm;
struct Program;
// Bytecode for a parsed program; null with problems on failure.
Proto* compileProgram(Vm& vm, Program& prog, std::vector<Problem>& problems);
}  // namespace loop::script
