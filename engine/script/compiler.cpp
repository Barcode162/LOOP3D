// Looped Script, compiling: a program (ast.h) → bytecode (bytecode.h).
#include "engine/script/ast.h"
#include "engine/script/bytecode.h"
#include "engine/script/compiler.h"
#include "engine/script/vm.h"

namespace loop::script {

struct Compiler {
  Vm& vm;
  std::string file;
  std::vector<Problem>& problems;

  struct Loop {
    size_t start;
    std::vector<size_t> breaks, continues;
    bool continueToStart = true;
  };
  struct Fn {
    Proto* proto;
    Fn* parent;
    int depth;
    std::vector<std::pair<Decl*, int>> captures;
    std::vector<Loop> loops;
    int line = 0;
  };
  Fn* fs = nullptr;

  // ------------------------------------------------------------ emitting
  void op(uint8_t b) {
    fs->proto->code.push_back(b);
    fs->proto->lines.push_back(fs->line);
  }
  void u16(int v) {
    op(static_cast<uint8_t>(v & 0xff));
    op(static_cast<uint8_t>((v >> 8) & 0xff));
  }
  void op16(uint8_t o, int v) { op(o), u16(v); }
  size_t jump(uint8_t o) {
    op(o);
    u16(0xffff);
    return fs->proto->code.size() - 2;
  }
  void patch(size_t at) {
    size_t off = fs->proto->code.size() - (at + 2);
    if (off > 0xffff) problem(fs->line, "this block is too long to jump over", "split it into smaller functions");
    fs->proto->code[at] = static_cast<uint8_t>(off & 0xff);
    fs->proto->code[at + 1] = static_cast<uint8_t>(off >> 8);
  }
  void loopBack(size_t start) {
    op(OP_LOOP);
    size_t off = fs->proto->code.size() + 2 - start;
    u16(static_cast<int>(off));
  }
  int constant(Value v) {
    auto& c = fs->proto->constants;
    for (size_t i = 0; i < c.size(); ++i) {
      const Value& x = c[i];
      if (v.isNumber() && x.isNumber() && v.n == x.n && std::signbit(v.n) == std::signbit(x.n)) return static_cast<int>(i);
      if (v.isString() && x.isString() && v.str() == x.str()) return static_cast<int>(i);
    }
    c.push_back(v);
    return static_cast<int>(c.size() - 1);
  }
  int name(const std::string& s) { return constant(vm.text(s)); }
  void problem(int line, std::string what, std::string hint = {}) { problems.push_back({file, line, 0, std::move(what), std::move(hint)}); }
  int newSlot() { return fs->proto->slots++; }

  // ------------------------------------------------------------ names
  int upvalue(Fn* f, Decl* d) {
    for (auto& [decl, idx] : f->captures)
      if (decl == d) return idx;
    Proto::Capture c;
    if (f->parent && d->functionDepth == f->parent->depth) c = {true, d->slot};
    else c = {false, upvalue(f->parent, d)};
    f->proto->captures.push_back(c);
    f->captures.push_back({d, static_cast<int>(f->proto->captures.size() - 1)});
    return static_cast<int>(f->proto->captures.size() - 1);
  }
  void getName(const Expr& e) {
    Decl* d = e.decl;
    if (!d) return op16(OP_GET_GLOBAL, name(e.text));
    if (d->functionDepth == fs->depth) return op16(d->captured ? OP_GET_CELL : OP_GET_LOCAL, d->slot);
    op16(OP_GET_UPVAL, upvalue(fs, d));
  }
  void setName(const Expr& e) {
    Decl* d = e.decl;
    if (!d) return op16(OP_SET_GLOBAL, name(e.text));
    if (d->functionDepth == fs->depth) return op16(d->captured ? OP_SET_CELL : OP_SET_LOCAL, d->slot);
    op16(OP_SET_UPVAL, upvalue(fs, d));
  }
  // A new local's first value is on the stack.
  void initLocal(Decl* d) {
    if (d->slot < 0) d->slot = newSlot();
    if (d->captured) op(OP_BOX);
    op16(OP_SET_LOCAL, d->slot);
  }

  // ------------------------------------------------------------ functions
  Proto* function(Function& f, Fn* parent) {
    Proto* p = vm.newProto();
    vm.protos_.push_back(p);
    p->name = f.name;
    p->file = file;
    p->params = f.params;
    p->arity = static_cast<int>(f.params.size());
    Fn st{p, parent, parent ? parent->depth + 1 : 0, {}, {}, f.line};
    Fn* saved = fs;
    fs = &st;
    p->slots = p->arity;
    for (size_t i = 0; i < f.paramDecls.size(); ++i) f.paramDecls[i]->slot = static_cast<int>(i);
    for (size_t i = 0; i < f.params.size(); ++i) {
      if (i < f.defaults.size() && f.defaults[i]) {
        size_t j = jump(OP_DEFAULT);
        // DEFAULT's operands: slot then offset; rewrite: slot first
        fs->proto->code.insert(fs->proto->code.begin() + static_cast<long>(j), {static_cast<uint8_t>(i & 0xff), static_cast<uint8_t>(i >> 8)});
        fs->proto->lines.insert(fs->proto->lines.begin() + static_cast<long>(j), {fs->line, fs->line});
        expr(*f.defaults[i]);
        op16(OP_SET_LOCAL, static_cast<int>(i));
        patch(j + 2);
      } else {
        op16(OP_REQUIRE, static_cast<int>(i));
        u16(name(f.params[i]));
      }
    }
    for (size_t i = 0; i < f.paramDecls.size(); ++i)
      if (f.paramDecls[i]->captured) op16(OP_CELLIFY, static_cast<int>(i));
    block(f.body);
    op(OP_NOTHING);
    op(OP_RETURN);
    fs = saved;
    return p;
  }
  void closure(Function& f) {
    Proto* p = function(f, fs);
    op16(OP_CLOSURE, constant(Value::object(p)));
  }

  // ------------------------------------------------------------ statements
  void block(const Block& b) {
    for (const auto& s : b) stmt(*s);
  }
  void stmt(const Stmt& s) {
    fs->line = s.line;
    switch (s.kind) {
      case Stmt::kExpr:
        expr(*s.value);
        op(OP_POP);
        break;
      case Stmt::kLet:
        expr(*s.value);
        if (s.decl) initLocal(s.decl);
        else op16(OP_SET_GLOBAL, name(s.name));
        break;
      case Stmt::kAssign: assign(s); break;
      case Stmt::kIf: {
        std::vector<size_t> ends;
        for (size_t i = 0; i < s.conditions.size(); ++i) {
          expr(*s.conditions[i]);
          size_t skip = jump(OP_JUMP_FALSE);
          block(s.blocks[i]);
          ends.push_back(jump(OP_JUMP));
          patch(skip);
        }
        if (s.blocks.size() > s.conditions.size()) block(s.blocks.back());
        for (size_t e : ends) patch(e);
        break;
      }
      case Stmt::kWhile: {
        size_t start = fs->proto->code.size();
        fs->loops.push_back({start, {}, {}});
        expr(*s.value);
        size_t exit = jump(OP_JUMP_FALSE);
        block(s.body);
        loopBack(start);
        patch(exit);
        endLoop(start);
        break;
      }
      case Stmt::kRepeat: {
        if (!s.value) {
          size_t start = fs->proto->code.size();
          fs->loops.push_back({start, {}, {}});
          block(s.body);
          loopBack(start);
          endLoop(start);
          break;
        }
        int c = newSlot();
        expr(*s.value);
        op16(OP_SET_LOCAL, c);
        size_t start = fs->proto->code.size();
        fs->loops.push_back({start, {}, {}});
        op16(OP_COUNT_CHECK, c);
        size_t exit = fs->proto->code.size();
        u16(0xffff);
        block(s.body);
        loopBack(start);
        patch(exit);
        endLoop(start);
        break;
      }
      case Stmt::kForEach: {
        int L = newSlot(), I = newSlot();
        s.decl->slot = newSlot();
        expr(*s.value);
        op(OP_ITER_PREP);
        op16(OP_SET_LOCAL, L);
        op16(OP_CONST, constant(Value::number(0)));
        op16(OP_SET_LOCAL, I);
        size_t start = fs->proto->code.size();
        fs->loops.push_back({start, {}, {}});
        op16(OP_ITER_NEXT, L);
        u16(I);
        u16(s.decl->slot);
        size_t exit = fs->proto->code.size();
        u16(0xffff);
        if (s.decl->captured) {
          op16(OP_GET_LOCAL, s.decl->slot);
          op(OP_BOX);
          op16(OP_SET_LOCAL, s.decl->slot);
        }
        block(s.body);
        loopBack(start);
        patch(exit);
        endLoop(start);
        break;
      }
      case Stmt::kForRange: {
        int C = newSlot(), E = newSlot(), S = newSlot();
        s.decl->slot = newSlot();
        expr(*s.value);
        op16(OP_SET_LOCAL, C);
        expr(*s.value2);
        op16(OP_SET_LOCAL, E);
        if (s.value3) expr(*s.value3);
        else {
          op16(OP_GET_LOCAL, C);
          op16(OP_GET_LOCAL, E);
          op(OP_RANGE_STEP);
        }
        op16(OP_SET_LOCAL, S);
        op16(OP_GET_LOCAL, C);
        op16(OP_GET_LOCAL, S);
        op(OP_SUB);
        op16(OP_SET_LOCAL, C);
        size_t start = fs->proto->code.size();
        fs->loops.push_back({start, {}, {}});
        op16(OP_RANGE_NEXT, C);
        u16(E);
        u16(S);
        size_t exit = fs->proto->code.size();
        u16(0xffff);
        op16(OP_GET_LOCAL, C);
        if (s.decl->captured) op(OP_BOX);
        op16(OP_SET_LOCAL, s.decl->slot);
        block(s.body);
        loopBack(start);
        patch(exit);
        endLoop(start);
        break;
      }
      case Stmt::kFunction: {
        if (s.decl) {
          s.decl->slot = newSlot();
          if (s.decl->captured) {
            op(OP_NOTHING);
            op(OP_BOX);
            op16(OP_SET_LOCAL, s.decl->slot);
            closure(*s.fn);
            op16(OP_SET_CELL, s.decl->slot);
          } else {
            closure(*s.fn);
            op16(OP_SET_LOCAL, s.decl->slot);
          }
        } else {
          closure(*s.fn);
          op16(OP_SET_GLOBAL, name(s.name));
        }
        break;
      }
      case Stmt::kReturn:
        if (s.value) expr(*s.value);
        else op(OP_NOTHING);
        op(OP_RETURN);
        break;
      case Stmt::kBreak:
      case Stmt::kContinue: {
        if (fs->loops.empty()) {
          problem(s.line, std::string("`") + (s.kind == Stmt::kBreak ? "break" : "continue") + "` is only for inside a loop");
          break;
        }
        if (s.kind == Stmt::kBreak) fs->loops.back().breaks.push_back(jump(OP_JUMP));
        else loopBack(fs->loops.back().start);
        break;
      }
      case Stmt::kEvery:
      case Stmt::kAfter:
        expr(*s.value);
        closure(*s.fn);
        op(OP_SCHEDULE);
        op(s.kind == Stmt::kEvery ? 0 : 1);
        break;
      case Stmt::kWhen:
        expr(*s.value);
        closure(*s.fn);
        op(OP_SCHEDULE);
        op(2);
        break;
      case Stmt::kTask:
        op(OP_NOTHING);
        closure(*s.fn);
        op(OP_SCHEDULE);
        op(3);
        break;
      case Stmt::kWait:
        expr(*s.value);
        op(OP_WAIT);
        break;
      case Stmt::kEmit:
        expr(*s.value);
        if (s.value2) expr(*s.value2);
        else op(OP_NOTHING);
        op(OP_EMIT);
        break;
    }
  }
  void endLoop(size_t) {
    Loop l = fs->loops.back();
    fs->loops.pop_back();
    for (size_t b : l.breaks) patch(b);
  }
  void assign(const Stmt& s) {
    const Expr& t = *s.target;
    uint8_t arith = s.op == "+=" ? OP_ADD : s.op == "-=" ? OP_SUB : s.op == "*=" ? OP_MUL : s.op == "/=" ? OP_DIV : 0;
    if (t.kind == Expr::kName) {
      if (arith) getName(t);
      expr(*s.value);
      if (arith) op(arith);
      setName(t);
    } else if (t.kind == Expr::kField) {
      expr(*t.items[0]);
      if (arith) {
        op(OP_DUP);
        op16(OP_GET_FIELD, name(t.text));
      }
      expr(*s.value);
      if (arith) op(arith);
      op16(OP_SET_FIELD, name(t.text));
    } else {
      int a = newSlot(), b = newSlot();
      expr(*t.items[0]);
      op16(OP_SET_LOCAL, a);
      expr(*t.items[1]);
      op16(OP_SET_LOCAL, b);
      op16(OP_GET_LOCAL, a);
      op16(OP_GET_LOCAL, b);
      if (arith) {
        op16(OP_GET_LOCAL, a);
        op16(OP_GET_LOCAL, b);
        op(OP_GET_INDEX);
      }
      expr(*s.value);
      if (arith) op(arith);
      op(OP_SET_INDEX);
    }
  }

  // ------------------------------------------------------------ expressions
  void expr(const Expr& e) {
    int saved = fs->line;
    if (e.line) fs->line = e.line;
    switch (e.kind) {
      case Expr::kNumber: op16(OP_CONST, constant(Value::number(e.number))); break;
      case Expr::kString: op16(OP_CONST, constant(vm.text(e.text))); break;
      case Expr::kInterp:
        for (const auto& p : e.items) expr(*p);
        op16(OP_INTERP, static_cast<int>(e.items.size()));
        break;
      case Expr::kBool: op(e.flag ? OP_TRUE : OP_FALSE); break;
      case Expr::kNothing: op(OP_NOTHING); break;
      case Expr::kName: getName(e); break;
      case Expr::kList:
        for (const auto& p : e.items) expr(*p);
        op16(OP_LIST, static_cast<int>(e.items.size()));
        break;
      case Expr::kTable: {
        for (const auto& p : e.items) expr(*p);
        List* keys = vm.newList();
        for (const auto& k : e.keys) keys->items.push_back(vm.text(k));
        op16(OP_TABLE, static_cast<int>(e.items.size()));
        u16(constant(Value::object(keys)));
        break;
      }
      case Expr::kUnary:
        expr(*e.items[0]);
        op(e.text == "-" ? OP_NEG : OP_NOT);
        break;
      case Expr::kBinary: {
        expr(*e.items[0]);
        expr(*e.items[1]);
        const std::string& o = e.text;
        op(o == "+"    ? OP_ADD
           : o == "-"  ? OP_SUB
           : o == "*"  ? OP_MUL
           : o == "/"  ? OP_DIV
           : o == "%"  ? OP_MOD
           : o == "^"  ? OP_POW
           : o == "==" ? OP_EQ
           : o == "!=" ? OP_NE
           : o == "<"  ? OP_LT
           : o == "<=" ? OP_LE
           : o == ">"  ? OP_GT
           : o == ">=" ? OP_GE
                       : OP_IN);
        break;
      }
      case Expr::kAnd:
      case Expr::kOr: {
        expr(*e.items[0]);
        size_t j = jump(e.kind == Expr::kAnd ? OP_AND : OP_OR);
        expr(*e.items[1]);
        patch(j);
        break;
      }
      case Expr::kIf: {
        expr(*e.items[0]);
        size_t no = jump(OP_JUMP_FALSE);
        expr(*e.items[1]);
        size_t end = jump(OP_JUMP);
        patch(no);
        expr(*e.items[2]);
        patch(end);
        break;
      }
      case Expr::kCall: {
        expr(*e.items[0]);
        int pos = 0, named = 0;
        for (size_t i = 1; i < e.items.size(); ++i)
          if (e.keys[i].empty()) expr(*e.items[i]), ++pos;
        List* names = vm.newList();
        for (size_t i = 1; i < e.items.size(); ++i)
          if (!e.keys[i].empty()) expr(*e.items[i]), ++named, names->items.push_back(vm.text(e.keys[i]));
        if (pos > 255 || named > 255) problem(e.line, "too many settings in one call");
        op(OP_CALL);
        op(static_cast<uint8_t>(pos));
        op(static_cast<uint8_t>(named));
        u16(named ? constant(Value::object(names)) : 0);
        break;
      }
      case Expr::kField:
        expr(*e.items[0]);
        op16(OP_GET_FIELD, name(e.text));
        break;
      case Expr::kIndex:
        expr(*e.items[0]);
        expr(*e.items[1]);
        op(OP_GET_INDEX);
        break;
      case Expr::kLambda: closure(*e.fn); break;
    }
    fs->line = saved;
  }
};

Proto* compileProgram(Vm& vm, Program& prog, std::vector<Problem>& problems) {
  Compiler c{vm, prog.file, problems};
  size_t before = problems.size();
  Proto* p = c.function(*prog.main, nullptr);
  return problems.size() == before ? p : nullptr;
}

}  // namespace loop::script
