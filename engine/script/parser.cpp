#include "engine/script/parser.h"

#include <algorithm>

namespace loop::script {

const std::vector<std::string>& keywords() {
  static const std::vector<std::string> k = {"let", "to", "if", "else", "otherwise", "elif", "while", "for", "each", "in", "from", "by",
                                             "repeat", "times", "return", "break", "continue", "every", "after", "wait", "when", "emit",
                                             "task", "and", "or", "not", "true", "false", "yes", "no", "nothing", "nil", "fn", "is", "forever"};
  return k;
}

namespace {

bool isKeyword(const std::string& s) {
  const auto& k = keywords();
  return std::find(k.begin(), k.end(), s) != k.end();
}

struct Parser {
  std::vector<Token> toks;
  size_t pos = 0;
  std::string file;
  std::vector<Problem>& problems;
  bool failed = false;

  struct FnCtx {
    Function* fn;
    std::vector<std::vector<Decl*>> blocks;
    int depth;
  };
  std::vector<FnCtx> fns;

  Parser(std::vector<Token> t, std::string f, std::vector<Problem>& p) : toks(std::move(t)), file(std::move(f)), problems(p) {}

  // ------------------------------------------------------------ tokens
  const Token& peek(int o = 0) const { return toks[std::min(pos + o, toks.size() - 1)]; }
  const Token& next() { return toks[std::min(pos++, toks.size() - 1)]; }
  bool at(std::string_view s) const { return peek().is(s); }
  bool atKind(Tok k) const { return peek().kind == k; }
  bool accept(std::string_view s) {
    if (at(s)) { ++pos; return true; }
    return false;
  }
  void error(const Token& t, std::string what, std::string hint = {}) {
    if (failed) return;  // one problem per statement; recover at the next line
    problems.push_back({file, t.line, t.column, std::move(what), std::move(hint)});
    failed = true;
  }
  bool expect(std::string_view s, const char* why) {
    if (accept(s)) return true;
    error(peek(), std::string("expected `") + std::string(s) + "` " + why, describeFound());
    return false;
  }
  std::string describeFound() const {
    const Token& t = peek();
    switch (t.kind) {
      case Tok::kNewline: return "found the end of the line";
      case Tok::kEnd: return "found the end of the script";
      case Tok::kIndent: return "found an indented line";
      case Tok::kDedent: return "found the end of the block";
      case Tok::kString: return "found text \"" + t.text + "\"";
      default: return "found `" + t.text + "`";
    }
  }
  void skipToLineEnd() {
    int nest = 0;
    while (!atKind(Tok::kEnd)) {
      if (atKind(Tok::kIndent)) ++nest;
      if (atKind(Tok::kDedent)) {
        if (nest == 0) return;
        --nest;
      }
      if (atKind(Tok::kNewline) && nest == 0) { ++pos; return; }
      ++pos;
    }
  }

  // ------------------------------------------------------------ scopes
  Decl* declare(const std::string& name) {
    FnCtx& f = fns.back();
    if (f.depth == 0 && f.blocks.size() <= 1) return nullptr;  // top level: a global
    auto d = std::make_unique<Decl>();
    d->name = name;
    d->functionDepth = f.depth;
    Decl* raw = d.get();
    f.fn->decls.push_back(std::move(d));
    f.blocks.back().push_back(raw);
    return raw;
  }
  void resolve(Expr& e) {
    for (int fi = static_cast<int>(fns.size()) - 1; fi >= 0; --fi) {
      auto& f = fns[fi];
      for (int b = static_cast<int>(f.blocks.size()) - 1; b >= 0; --b)
        for (int k = static_cast<int>(f.blocks[b].size()) - 1; k >= 0; --k)
          if (f.blocks[b][k]->name == e.text) {
            e.decl = f.blocks[b][k];
            if (fi != static_cast<int>(fns.size()) - 1) e.decl->captured = true;
            return;
          }
    }
  }
  void pushBlock() { fns.back().blocks.emplace_back(); }
  void popBlock() { fns.back().blocks.pop_back(); }

  // ------------------------------------------------------------ blocks
  // After a header: `:` then either a statement on the same line, or a
  // new line and an indented block. The colon is optional before a block.
  void parseBody(Block& out, const Token& header) {
    bool colon = accept(":");
    if (colon && !atKind(Tok::kNewline)) {
      pushBlock();
      if (auto s = statement()) out.push_back(std::move(s));
      popBlock();
      return;
    }
    if (!accept("") && !atKind(Tok::kNewline)) {
      error(peek(), "expected the end of the line here", describeFound());
      return;
    }
    ++pos;  // newline
    if (!atKind(Tok::kIndent)) {
      error(header, "`" + header.text + "` needs an indented block under it", "indent the lines that belong to it by two spaces");
      return;
    }
    ++pos;
    pushBlock();
    while (!atKind(Tok::kDedent) && !atKind(Tok::kEnd)) {
      failed = false;
      size_t before = pos;
      if (auto s = statement()) out.push_back(std::move(s));
      if (failed) skipToLineEnd();
      if (pos == before) ++pos;
    }
    popBlock();
    accept("");
    if (atKind(Tok::kDedent)) ++pos;
  }

  std::shared_ptr<Function> beginFunction(const std::string& name, int line) {
    auto f = std::make_shared<Function>();
    f->name = name;
    f->line = line;
    fns.push_back({f.get(), {{}}, static_cast<int>(fns.size())});
    return f;
  }
  void endFunction() { fns.pop_back(); }

  bool parseParams(Function& f) {
    // ( a, b: default, c )
    if (!accept("(")) return true;
    while (!at(")")) {
      const Token& t = peek();
      if (t.kind == Tok::kName && isKeyword(t.text)) {
        error(t, "`" + t.text + "` can't name a setting", "it's one of Looped Script's own words; pick another name, like `" + t.text + "_at`");
        return false;
      }
      if (t.kind != Tok::kName) {
        error(t, "expected a name for this setting", describeFound());
        return false;
      }
      std::string name = next().text;
      while (peek().kind == Tok::kName && !isKeyword(peek().text) && !at(":")) name += "_" + next().text;
      f.params.push_back(name);
      if (accept(":") || accept("=")) f.defaults.push_back(expression());
      else f.defaults.push_back(nullptr);
      if (!accept(",")) break;
    }
    return expect(")", "to close the settings");
  }

  // ------------------------------------------------------------ statements
  std::unique_ptr<Stmt> make(Stmt::Kind k, const Token& t) {
    auto s = std::make_unique<Stmt>(k);
    s->line = t.line, s->column = t.column;
    return s;
  }
  void endOfStatement() {
    if (failed) return;
    if (atKind(Tok::kNewline)) { ++pos; return; }
    if (atKind(Tok::kDedent) || atKind(Tok::kEnd)) return;
    error(peek(), "this line goes on after the statement ended", describeFound() + "; put the rest on its own line");
  }
  // `group at: …, turn: …` with indented lines under it: the lines become a
  // function handed to the word as `of:`, which runs it at once. True when a
  // block was taken (the statement's line end is left for the caller).
  bool groupBlock(Expr& call, const Token& header) {
    if (call.kind != Expr::kCall || call.items.empty() || call.items[0]->kind != Expr::kName || call.items[0]->text != "group") return false;
    if (!atKind(Tok::kNewline) || peek(1).kind != Tok::kIndent) return false;
    auto e = node(Expr::kLambda, header);
    e->fn = beginFunction("group", header.line);
    parseBody(e->fn->body, header);
    endFunction();
    --pos;  // the caller's endOfStatement wants a line end; the block ate it
    toks[pos].kind = Tok::kNewline;
    call.items.push_back(std::move(e));
    call.keys.push_back("of");
    return true;
  }
  // A function made from a block: every / after / when / task bodies.
  std::shared_ptr<Function> blockFunction(const char* name, const Token& header) {
    auto fn = beginFunction(name, header.line);
    parseBody(fn->body, header);
    endFunction();
    return fn;
  }

  std::unique_ptr<Stmt> statement() {
    const Token& t = peek();
    if (t.kind == Tok::kNewline) { ++pos; return nullptr; }
    if (t.kind == Tok::kIndent) {
      error(t, "this line is indented but nothing above it opens a block", "remove the extra spaces");
      return nullptr;
    }
    if (t.kind == Tok::kName) {
      const std::string& w = t.text;
      if (w == "let") {
        next();
        const Token& n = peek();
        if (n.kind != Tok::kName || isKeyword(n.text)) {
          error(n, "`let` needs a name", "like `let height = 2 m`");
          return nullptr;
        }
        auto s = make(Stmt::kLet, t);
        s->name = next().text;
        if (!expect("=", "after the name")) return nullptr;
        s->value = expression();
        if (s->value) groupBlock(*s->value, t);
        s->decl = declare(s->name);  // after the value: `let x = x + 1` reads the outer x
        endOfStatement();
        return s;
      }
      if (w == "to" && peek(1).kind == Tok::kName) {
        next();
        auto s = make(Stmt::kFunction, t);
        s->name = next().text;
        while (peek().kind == Tok::kName && !isKeyword(peek().text)) s->name += "_" + next().text;
        s->decl = declare(s->name);
        s->fn = beginFunction(s->name, t.line);
        if (!parseParams(*s->fn)) { endFunction(); return nullptr; }
        for (auto& p : s->fn->params) s->fn->paramDecls.push_back(declareParam(p));
        parseBody(s->fn->body, t);
        endFunction();
        return s;
      }
      if (w == "if") {
        next();
        auto s = make(Stmt::kIf, t);
        s->conditions.push_back(expression());
        s->blocks.emplace_back();
        parseBody(s->blocks.back(), t);
        for (;;) {
          if ((at("else") || at("otherwise")) && peek(1).is("if")) {
            const Token& h = next();
            next();
            s->conditions.push_back(expression());
            s->blocks.emplace_back();
            parseBody(s->blocks.back(), h);
          } else if (at("elif")) {
            const Token& h = next();
            s->conditions.push_back(expression());
            s->blocks.emplace_back();
            parseBody(s->blocks.back(), h);
          } else if (at("else") || at("otherwise")) {
            const Token& h = next();
            s->blocks.emplace_back();
            parseBody(s->blocks.back(), h);
            break;
          } else break;
        }
        return s;
      }
      if (w == "while") {
        next();
        auto s = make(Stmt::kWhile, t);
        s->value = expression();
        parseBody(s->body, t);
        return s;
      }
      if (w == "repeat") {
        next();
        auto s = make(Stmt::kRepeat, t);
        if (accept("forever") || at(":") || atKind(Tok::kNewline)) s->value = nullptr;
        else {
          s->value = expression();
          if (!accept("times")) {
            error(peek(), "`repeat` needs `times` after the count", "like `repeat 3 times`");
            return nullptr;
          }
        }
        parseBody(s->body, t);
        return s;
      }
      if (w == "for") {
        next();
        accept("each");
        const Token& n = peek();
        if (n.kind != Tok::kName || isKeyword(n.text)) {
          error(n, "`for` needs a name for each item", "like `for each tree in trees`");
          return nullptr;
        }
        std::string var = next().text;
        if (accept("in")) {
          auto s = make(Stmt::kForEach, t);
          s->name = var;
          s->value = expression();
          pushBlock();
          s->decl = declareLocal(var);
          parseBody(s->body, t);
          popBlock();
          return s;
        }
        if (accept("from")) {
          auto s = make(Stmt::kForRange, t);
          s->name = var;
          s->value = expression();
          if (!expect("to", "after the first number")) return nullptr;
          s->value2 = expression();
          if (accept("by")) s->value3 = expression();
          pushBlock();
          s->decl = declareLocal(var);
          parseBody(s->body, t);
          popBlock();
          return s;
        }
        error(peek(), "`for " + var + "` needs `in` or `from`", "like `for each x in list` or `for i from 1 to 10`");
        return nullptr;
      }
      if (w == "return") {
        next();
        auto s = make(Stmt::kReturn, t);
        if (!atKind(Tok::kNewline) && !atKind(Tok::kDedent) && !atKind(Tok::kEnd)) s->value = expression();
        endOfStatement();
        return s;
      }
      if (w == "break" || w == "continue") {
        next();
        auto s = make(w == "break" ? Stmt::kBreak : Stmt::kContinue, t);
        endOfStatement();
        return s;
      }
      if (w == "every" || w == "after") {
        next();
        auto s = make(w == "every" ? Stmt::kEvery : Stmt::kAfter, t);
        s->value = expression();
        s->fn = blockFunction(w == "every" ? "every" : "after", t);
        return s;
      }
      if (w == "when") {
        next();
        auto s = make(Stmt::kWhen, t);
        s->value = expression();
        s->fn = blockFunction("when", t);
        return s;
      }
      if (w == "task") {
        next();
        auto s = make(Stmt::kTask, t);
        s->fn = blockFunction("task", t);
        return s;
      }
      if (w == "wait") {
        next();
        auto s = make(Stmt::kWait, t);
        s->value = expression();
        endOfStatement();
        return s;
      }
      if (w == "emit") {
        next();
        auto s = make(Stmt::kEmit, t);
        s->value = expression();
        if (accept(",")) s->value2 = expression();
        endOfStatement();
        return s;
      }
      // a command: `say "hello"`, `wait_for x`, `box size: 1 m`
      if (!isKeyword(w) && startsCommandArgs()) {
        auto s = make(Stmt::kExpr, t);
        auto call = std::make_unique<Expr>(Expr::kCall);
        call->line = t.line, call->column = t.column;
        auto callee = std::make_unique<Expr>(Expr::kName);
        callee->line = t.line, callee->column = t.column;
        callee->text = next().text;
        resolve(*callee);
        call->items.push_back(std::move(callee));
        call->keys.push_back("");
        parseArgs(*call, nullptr);
        groupBlock(*call, t);
        s->value = std::move(call);
        endOfStatement();
        return s;
      }
    }
    // an expression or an assignment
    auto e = expression();
    if (!e) return nullptr;
    for (const char* op : {"=", "+=", "-=", "*=", "/="}) {
      if (at(op)) {
        const Token& o = next();
        if (e->kind != Expr::kName && e->kind != Expr::kField && e->kind != Expr::kIndex) {
          error(o, "the left of `" + std::string(op) + "` must be a name, a field or an item", "like `x = 1`, `lamp.brightness = 2` or `list[0] = 5`");
          return nullptr;
        }
        auto s = make(Stmt::kAssign, t);
        s->op = op;
        s->target = std::move(e);
        s->value = expression();
        endOfStatement();
        return s;
      }
    }
    auto s = make(Stmt::kExpr, t);
    groupBlock(*e, t);
    s->value = std::move(e);
    endOfStatement();
    return s;
  }

  Decl* declareParam(const std::string& name) {
    FnCtx& f = fns.back();
    auto d = std::make_unique<Decl>();
    d->name = name;
    d->functionDepth = f.depth;
    Decl* raw = d.get();
    f.fn->decls.push_back(std::move(d));
    f.blocks.back().push_back(raw);
    return raw;
  }
  // Always a local (loop variables even at the top level).
  Decl* declareLocal(const std::string& name) { return declareParam(name); }

  bool startsCommandArgs() const {
    const Token& n = peek(1);
    if (n.line != peek().line) return false;
    switch (n.kind) {
      case Tok::kNumber:
      case Tok::kString: return true;
      case Tok::kName: {
        static const std::vector<std::string> ops = {"and", "or", "is", "in", "if", "else", "otherwise", "times", "to", "from", "by"};
        return std::find(ops.begin(), ops.end(), n.text) == ops.end();
      }
      case Tok::kSymbol: return (n.text == "[" || n.text == "{") && n.spaceBefore;
      default: return false;
    }
  }

  // ------------------------------------------------------------ expressions
  ExprP expression() {
    if (at("fn")) return lambda();
    auto e = orExpr();
    // `a if cond else b`
    if (e && at("if") && peek().line == toks[pos - 1].line) {
      const Token& t = next();
      auto c = orExpr();
      if (!accept("else") && !accept("otherwise")) {
        error(peek(), "`… if …` needs `else` and a value for when it's not true", "like `\"day\" if light else \"night\"`");
        return e;
      }
      auto f = expression();
      auto r = node(Expr::kIf, t);
      r->items.push_back(std::move(c));
      r->items.push_back(std::move(e));
      r->items.push_back(std::move(f));
      return r;
    }
    return e;
  }
  ExprP node(Expr::Kind k, const Token& t) {
    auto e = std::make_unique<Expr>(k);
    e->line = t.line, e->column = t.column;
    return e;
  }
  ExprP lambda() {
    const Token& t = next();
    auto e = node(Expr::kLambda, t);
    e->fn = beginFunction("fn", t.line);
    if (!parseParams(*e->fn)) { endFunction(); return e; }
    for (auto& p : e->fn->params) e->fn->paramDecls.push_back(declareParam(p));
    if (!expect(":", "after the settings of `fn`")) { endFunction(); return e; }
    if (atKind(Tok::kNewline)) {
      // a block body: the lambda ends the line it's on
      --pos;  // let parseBody see the colon
      parseBody(e->fn->body, t);
      --pos;  // the caller's endOfStatement wants a line end; the block ate it
      toks[pos].kind = Tok::kNewline;
    } else {
      // one line: a value to give back, or an assignment (`fn(): n += 1`
      // changes n and gives back its new value)
      auto v = expression();
      const char* ops[] = {"=", "+=", "-=", "*=", "/="};
      for (const char* o : ops) {
        if (v && at(o) && v->kind == Expr::kName) {
          next();
          auto as = std::make_unique<Stmt>(Stmt::kAssign);
          as->line = t.line;
          as->op = o;
          auto again = std::make_unique<Expr>(Expr::kName);
          again->text = v->text, again->decl = v->decl, again->line = v->line;
          as->target = std::move(v);
          as->value = expression();
          e->fn->body.push_back(std::move(as));
          v = std::move(again);
          break;
        }
      }
      auto r = std::make_unique<Stmt>(Stmt::kReturn);
      r->line = t.line;
      r->value = std::move(v);
      e->fn->body.push_back(std::move(r));
    }
    endFunction();
    return e;
  }
  ExprP binary(ExprP l, const Token& op, ExprP r, const std::string& name) {
    auto e = node(Expr::kBinary, op);
    e->text = name;
    e->items.push_back(std::move(l));
    e->items.push_back(std::move(r));
    return e;
  }
  ExprP orExpr() {
    auto l = andExpr();
    while (at("or")) {
      const Token& t = next();
      auto e = node(Expr::kOr, t);
      e->items.push_back(std::move(l));
      e->items.push_back(andExpr());
      l = std::move(e);
    }
    return l;
  }
  ExprP andExpr() {
    auto l = notExpr();
    while (at("and")) {
      const Token& t = next();
      auto e = node(Expr::kAnd, t);
      e->items.push_back(std::move(l));
      e->items.push_back(notExpr());
      l = std::move(e);
    }
    return l;
  }
  ExprP notExpr() {
    if (at("not")) {
      const Token& t = next();
      auto e = node(Expr::kUnary, t);
      e->text = "not";
      e->items.push_back(notExpr());
      return e;
    }
    return comparison();
  }
  ExprP comparison() {
    auto l = additive();
    for (;;) {
      if (at("==") || at("!=") || at("<") || at("<=") || at(">") || at(">=")) {
        const Token& t = next();
        l = binary(std::move(l), t, additive(), t.text);
      } else if (at("is")) {
        const Token& t = next();
        bool neg = accept("not");
        l = binary(std::move(l), t, additive(), neg ? "!=" : "==");
      } else if (at("in") && !(peek(1).kind == Tok::kNewline)) {
        const Token& t = next();
        l = binary(std::move(l), t, additive(), "in");
      } else break;
    }
    return l;
  }
  ExprP additive() {
    auto l = multiplicative();
    while (at("+") || at("-")) {
      const Token& t = next();
      l = binary(std::move(l), t, multiplicative(), t.text);
    }
    return l;
  }
  ExprP multiplicative() {
    auto l = unary();
    while (at("*") || at("/") || at("%")) {
      const Token& t = next();
      l = binary(std::move(l), t, unary(), t.text);
    }
    return l;
  }
  ExprP unary() {
    if (at("-")) {
      const Token& t = next();
      auto e = node(Expr::kUnary, t);
      e->text = "-";
      e->items.push_back(unary());
      return e;
    }
    return power();
  }
  ExprP power() {
    auto l = postfix();
    if (at("^")) {
      const Token& t = next();
      return binary(std::move(l), t, unary(), "^");
    }
    return l;
  }
  void parseArgs(Expr& call, const char* close) {
    // positional values and `name words: value` settings, separated by commas
    auto ends = [&] {
      if (close) return at(close);
      return atKind(Tok::kNewline) || atKind(Tok::kDedent) || atKind(Tok::kEnd);
    };
    while (!ends() && !atKind(Tok::kEnd)) {
      std::string key;
      size_t save = pos;
      if (peek().kind == Tok::kName) {
        std::string k = peek().text;
        size_t j = pos + 1;
        while (toks[j].kind == Tok::kName && !toks[j].is(":")) k += "_" + toks[j].text, ++j;
        if (toks[j].is(":") && (toks[j + 1].kind != Tok::kNewline)) {
          key = k;
          pos = j + 1;
        }
      }
      if (key.empty()) pos = save;
      auto v = expression();
      if (!v) return;
      call.items.push_back(std::move(v));
      call.keys.push_back(key);
      if (!accept(",")) break;
      while (close && atKind(Tok::kNewline)) ++pos;
    }
    if (close) expect(close, "to close the call");
  }
  ExprP postfix() {
    auto e = primary();
    if (!e) return e;
    for (;;) {
      if (at("(") && !peek().spaceBefore) {
        const Token& t = next();
        auto c = node(Expr::kCall, t);
        c->items.push_back(std::move(e));
        c->keys.push_back("");
        parseArgs(*c, ")");
        e = std::move(c);
      } else if (at(".")) {
        const Token& t = next();
        const Token& n = peek();
        if (n.kind != Tok::kName) {
          error(n, "expected a field name after `.`", describeFound());
          return e;
        }
        auto f = node(Expr::kField, t);
        f->text = next().text;
        f->items.push_back(std::move(e));
        e = std::move(f);
      } else if (at("[") && !peek().spaceBefore) {
        const Token& t = next();
        auto ix = node(Expr::kIndex, t);
        ix->items.push_back(std::move(e));
        ix->items.push_back(expression());
        expect("]", "to close the index");
        e = std::move(ix);
      } else break;
    }
    return e;
  }
  ExprP interpolate(const Token& t) {
    // "Hello {name}, you have {count} embers" → parts joined at run time
    const std::string& s = t.text;
    if (s.find('{') == std::string::npos) {
      auto e = node(Expr::kString, t);
      e->text = s;
      return e;
    }
    auto e = node(Expr::kInterp, t);
    std::string lit;
    for (size_t i = 0; i < s.size(); ++i) {
      if (s[i] == '{' && i + 1 < s.size() && s[i + 1] == '{') { lit += '{', ++i; continue; }
      if (s[i] == '}' && i + 1 < s.size() && s[i + 1] == '}') { lit += '}', ++i; continue; }
      if (s[i] != '{') { lit += s[i]; continue; }
      size_t close = s.find('}', i);
      if (close == std::string::npos) {
        error(t, "a `{` in this text isn't closed with `}`", "write {{ for a plain {");
        return e;
      }
      auto part = node(Expr::kString, t);
      part->text = lit;
      e->items.push_back(std::move(part));
      lit.clear();
      std::vector<Token> sub;
      std::vector<Problem> p;
      lex(std::string_view(s).substr(i + 1, close - i - 1), file, sub, p);
      for (auto& tk : sub) tk.line = t.line, tk.column += t.column + static_cast<int>(i) + 1;
      std::swap(sub, toks);
      size_t savePos = pos;
      pos = 0;
      auto inner = expression();
      std::swap(sub, toks);
      pos = savePos;
      if (inner) e->items.push_back(std::move(inner));
      i = close;
    }
    auto part = node(Expr::kString, t);
    part->text = lit;
    e->items.push_back(std::move(part));
    return e;
  }
  ExprP primary() {
    const Token& t = peek();
    switch (t.kind) {
      case Tok::kNumber: {
        next();
        auto e = node(Expr::kNumber, t);
        e->number = t.number;
        e->text = t.unit;
        return e;
      }
      case Tok::kString: next(); return interpolate(t);
      case Tok::kName: {
        if (t.text == "true" || t.text == "yes" || t.text == "false" || t.text == "no") {
          next();
          auto e = node(Expr::kBool, t);
          e->flag = t.text == "true" || t.text == "yes";
          return e;
        }
        if (t.text == "nothing" || t.text == "nil") {
          next();
          return node(Expr::kNothing, t);
        }
        if (isKeyword(t.text)) {
          error(t, "`" + t.text + "` can't be used here", "it's one of Looped Script's own words");
          return nullptr;
        }
        next();
        auto e = node(Expr::kName, t);
        e->text = t.text;
        resolve(*e);
        return e;
      }
      case Tok::kSymbol: {
        if (t.text == "(") {
          next();
          auto e = expression();
          expect(")", "to close the bracket");
          return e;
        }
        if (t.text == "[") {
          next();
          auto e = node(Expr::kList, t);
          while (atKind(Tok::kNewline)) ++pos;
          while (!at("]") && !atKind(Tok::kEnd)) {
            auto v = expression();
            if (!v) return e;
            e->items.push_back(std::move(v));
            if (!accept(",")) break;
            while (atKind(Tok::kNewline)) ++pos;
          }
          expect("]", "to close the list");
          return e;
        }
        if (t.text == "{") {
          next();
          auto e = node(Expr::kTable, t);
          while (!at("}") && !atKind(Tok::kEnd)) {
            std::string key;
            if (peek().kind == Tok::kString) key = next().text;
            else if (peek().kind == Tok::kName) {
              key = next().text;
              while (peek().kind == Tok::kName) key += "_" + next().text;
            } else {
              error(peek(), "a table's keys are names or text", "like {name: \"Mira\", age: 9}");
              return e;
            }
            if (!expect(":", "after the key")) return e;
            auto v = expression();
            if (!v) return e;
            e->keys.push_back(key);
            e->items.push_back(std::move(v));
            if (!accept(",")) break;
          }
          expect("}", "to close the table");
          return e;
        }
        break;
      }
      default: break;
    }
    error(t, "expected a value here", describeFound());
    return nullptr;
  }
};

}  // namespace

bool parse(std::string_view source, const std::string& file, Program& out, std::vector<Problem>& problems) {
  std::vector<Token> toks;
  size_t before = problems.size();
  lex(source, file, toks, problems);
  if (problems.size() != before) return false;
  Parser p(std::move(toks), file, problems);
  out.file = file;
  out.main = p.beginFunction("the script", 1);
  while (!p.atKind(Tok::kEnd)) {
    p.failed = false;
    size_t at = p.pos;
    if (p.atKind(Tok::kDedent)) { ++p.pos; continue; }
    if (auto s = p.statement()) out.main->body.push_back(std::move(s));
    if (p.failed) p.skipToLineEnd();
    if (p.pos == at) ++p.pos;
  }
  p.endFunction();
  return problems.size() == before;
}

}  // namespace loop::script
