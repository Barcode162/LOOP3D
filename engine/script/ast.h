#pragma once
// The shape of a script after reading: expressions and statements.
#include <memory>
#include <string>
#include <vector>

namespace loop::script {

struct Decl;  // a local variable's declaration (for closures)

struct Expr {
  enum Kind { kNumber, kString, kInterp, kBool, kNothing, kName, kList, kTable, kUnary, kBinary, kAnd, kOr, kCall, kField, kIndex, kLambda, kIf };
  Kind kind;
  int line = 0, column = 0;
  double number = 0;
  std::string text;  // string, name, field, operator
  bool flag = false;
  std::vector<std::unique_ptr<Expr>> items;              // list items, call args, interp parts, operands
  std::vector<std::string> keys;                          // table keys, call arg names ("" = positional)
  std::shared_ptr<struct Function> fn;                    // lambda
  Decl* decl = nullptr;                                   // resolved local (null: global)
  int scopeDepth = 0;                                     // function nesting where `decl` lives
  explicit Expr(Kind k) : kind(k) {}
};
using ExprP = std::unique_ptr<Expr>;

struct Stmt;
using Block = std::vector<std::unique_ptr<Stmt>>;

struct Decl {
  std::string name;
  bool captured = false;  // some inner function uses it: keep it in a cell
  int slot = -1;
  int functionDepth = 0;
};

struct Function {
  std::string name;
  std::vector<std::string> params;
  std::vector<ExprP> defaults;  // null when none
  std::vector<Decl*> paramDecls;
  Block body;
  int line = 0;
  std::vector<std::unique_ptr<Decl>> decls;  // owned
};

struct Stmt {
  enum Kind { kExpr, kLet, kAssign, kIf, kWhile, kForEach, kForRange, kRepeat, kFunction, kReturn, kBreak, kContinue, kEvery, kAfter, kWait, kWhen, kEmit, kTask };
  Kind kind;
  int line = 0, column = 0;
  ExprP target, value, value2, value3;  // assignment target / values (range from, to, by)
  std::string op;                       // "=", "+=", ...
  std::string name;
  Decl* decl = nullptr;                 // let / for variable / local function
  std::vector<ExprP> conditions;        // if / else if
  std::vector<Block> blocks;            // their bodies, then else
  Block body;
  std::shared_ptr<Function> fn;         // function statement; also every/after/when bodies
  explicit Stmt(Kind k) : kind(k) {}
};

struct Program {
  std::string file;
  std::shared_ptr<Function> main;
};

}  // namespace loop::script
