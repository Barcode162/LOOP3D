#include "engine/script/vm.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>

#include "engine/script/bytecode.h"
#include "engine/script/compiler.h"
#include "engine/script/parser.h"

namespace loop::script {

namespace {
struct ScriptFail {
  std::string what, hint;
};
}  // namespace

struct Vm::Fiber {
  struct Frame {
    Closure* cl;
    size_t ip, base, callee;
  };
  std::vector<Value> stack;
  std::vector<Frame> frames;
  double wake = 0;
  bool done = false, noWait = false;
  int timer = -1;  // the timer that started it
  Value result;
  // the `event` this fiber was started for: each handler keeps its own (two
  // events in one step used to share one global, the last overwriting the first)
  Value event;
  bool hasEvent = false;
};

// ------------------------------------------------------------------ heap
template <class T, class... A>
T* Vm::alloc(A&&... a) {
  T* o = new T(std::forward<A>(a)...);
  o->next = heap_;
  heap_ = o;
  ++objects_;
  return o;
}

Vm::Vm() = default;

Vm::~Vm() {
  Obj* o = heap_;
  while (o) {
    Obj* n = o->next;
    delete o;
    o = n;
  }
}

Value Vm::text(std::string s) { return Value::object(alloc<Str>(std::move(s))); }
List* Vm::newList() { return alloc<List>(); }
Table* Vm::newTable() { return alloc<Table>(); }
Thing* Vm::newThing(const ThingClass* cls, int id) { return alloc<Thing>(cls, id); }
Value Vm::bound(Value self, Value fn) { return Value::object(alloc<Bound>(self, fn)); }
Proto* Vm::newProto() { return alloc<Proto>(); }

void Vm::native(const std::string& name, NativeFn fn, const std::string& help) {
  globals_[name] = Value::object(alloc<Native>(name, fn, help.empty() ? name : help));
}
void Vm::method(const std::string& kind, const std::string& name, NativeFn fn, const std::string& help) {
  methods_[kind][name] = Value::object(alloc<Native>(name, fn, help.empty() ? name : help));
}
std::vector<std::pair<std::string, std::string>> Vm::words() const {
  std::vector<std::pair<std::string, std::string>> out;
  for (const auto& [name, v] : globals_)
    if (const Native* n = v.as<Native>()) out.push_back({name, n->help});
  std::sort(out.begin(), out.end());
  return out;
}
Value Vm::global(const std::string& name) const {
  auto it = globals_.find(name);
  return it == globals_.end() ? Value() : it->second;
}
size_t Vm::fibers() const { return fibers_.size(); }

void Vm::fail(const std::string& what, const std::string& hint) { throw ScriptFail{what, hint}; }

void Vm::collectGarbage() {
  std::vector<Obj*> gray;
  auto mark = [&](const Value& v) {
    if (v.type == Type::kObj && v.o && !v.o->marked) v.o->marked = true, gray.push_back(v.o);
  };
  auto markObj = [&](Obj* o) {
    if (o && !o->marked) o->marked = true, gray.push_back(o);
  };
  for (auto& [k, v] : globals_) mark(v);
  for (auto& [k, m] : methods_)
    for (auto& [n, v] : m) mark(v);
  for (auto& v : pinned_) mark(v);
  for (Proto* p : protos_) markObj(p);
  for (auto& f : fibers_) {
    for (auto& v : f->stack) mark(v);
    for (auto& fr : f->frames) markObj(fr.cl);
    mark(f->result);
    mark(f->event);
  }
  for (auto& t : timers_) markObj(t.fn);
  for (auto& [e, c] : handlers_) markObj(c);
  for (auto& [e, v] : events_) mark(v);
  while (!gray.empty()) {
    Obj* o = gray.back();
    gray.pop_back();
    switch (o->kind) {
      case ObjKind::kList:
        for (auto& v : static_cast<List*>(o)->items) mark(v);
        break;
      case ObjKind::kTable:
        for (auto& [k, v] : static_cast<Table*>(o)->entries) mark(v);
        break;
      case ObjKind::kProto:
        for (auto& v : static_cast<Proto*>(o)->constants) mark(v);
        break;
      case ObjKind::kClosure: {
        auto* c = static_cast<Closure*>(o);
        markObj(c->proto);
        for (Cell* cell : c->cells) markObj(cell);
        break;
      }
      case ObjKind::kCell: mark(static_cast<Cell*>(o)->value); break;
      case ObjKind::kThing: markObj(static_cast<Thing*>(o)->extra); break;
      case ObjKind::kBound:
        mark(static_cast<Bound*>(o)->self);
        mark(static_cast<Bound*>(o)->fn);
        break;
      default: break;
    }
  }
  Obj** link = &heap_;
  size_t live = 0;
  while (*link) {
    Obj* o = *link;
    if (!o->marked) {
      *link = o->next;
      delete o;
    } else {
      o->marked = false;
      link = &o->next;
      ++live;
    }
  }
  objects_ = live;
  nextCollect_ = std::max<size_t>(20000, live * 2);
}

// ------------------------------------------------------------------ text
std::string Vm::typeName(const Value& v) const {
  switch (v.type) {
    case Type::kNothing: return "nothing";
    case Type::kUnset: return "nothing";
    case Type::kBool: return "true/false";
    case Type::kNumber: return "a number";
    case Type::kVec: return "a vector";
    case Type::kObj: break;
  }
  switch (v.o->kind) {
    case ObjKind::kString: return "text";
    case ObjKind::kList: return "a list";
    case ObjKind::kTable: return "a table";
    case ObjKind::kClosure:
    case ObjKind::kNative:
    case ObjKind::kBound: return "a function";
    case ObjKind::kThing: return "a " + static_cast<Thing*>(v.o)->cls->name;
    default: return "a value";
  }
}

static std::string num(double d) {
  if (std::isnan(d)) return "not a number";
  if (std::isinf(d)) return d > 0 ? "infinity" : "-infinity";
  if (d == std::floor(d) && std::fabs(d) < 1e15) {
    char b[32];
    std::snprintf(b, sizeof b, "%.0f", d);
    return b;
  }
  char b[32];
  std::snprintf(b, sizeof b, "%.4f", d);
  std::string s = b;
  while (!s.empty() && s.back() == '0') s.pop_back();
  if (!s.empty() && s.back() == '.') s.pop_back();
  return s;
}

std::string Vm::show(const Value& v) const {
  switch (v.type) {
    case Type::kNothing:
    case Type::kUnset: return "nothing";
    case Type::kBool: return v.b ? "true" : "false";
    case Type::kNumber: return num(v.n);
    case Type::kVec: return "(" + num(v.v[0]) + ", " + num(v.v[1]) + ", " + num(v.v[2]) + ")";
    case Type::kObj: break;
  }
  auto quoted = [&](const Value& x) { return x.isString() ? "\"" + x.str() + "\"" : show(x); };
  switch (v.o->kind) {
    case ObjKind::kString: return v.str();
    case ObjKind::kList: {
      std::string s = "[";
      auto& it = static_cast<List*>(v.o)->items;
      for (size_t i = 0; i < it.size(); ++i) s += (i ? ", " : "") + quoted(it[i]);
      return s + "]";
    }
    case ObjKind::kTable: {
      std::string s = "{";
      auto& e = static_cast<Table*>(v.o)->entries;
      for (size_t i = 0; i < e.size(); ++i) s += (i ? ", " : "") + e[i].first + ": " + quoted(e[i].second);
      return s + "}";
    }
    case ObjKind::kClosure: return "the function " + static_cast<Closure*>(v.o)->proto->name;
    case ObjKind::kNative: return "the function " + static_cast<Native*>(v.o)->name;
    case ObjKind::kThing: return static_cast<Thing*>(v.o)->cls->name + " #" + std::to_string(static_cast<Thing*>(v.o)->id);
    default: return "a value";
  }
}

// ------------------------------------------------------------------ values
bool Vm::equal(const Value& a, const Value& b) const {
  if (a.type != b.type) return (a.isNothing() || a.isUnset()) && (b.isNothing() || b.isUnset());
  switch (a.type) {
    case Type::kNothing:
    case Type::kUnset: return true;
    case Type::kBool: return a.b == b.b;
    case Type::kNumber: return a.n == b.n;
    case Type::kVec: return a.v[0] == b.v[0] && a.v[1] == b.v[1] && a.v[2] == b.v[2];
    case Type::kObj:
      if (a.isString() && b.isString()) return a.str() == b.str();
      return a.o == b.o;
  }
  return false;
}

Value Vm::arith(char op, const Value& a, const Value& b) {
  if (a.isNumber() && b.isNumber()) {
    double x = a.n, y = b.n;
    switch (op) {
      case '+': return Value::number(x + y);
      case '-': return Value::number(x - y);
      case '*': return Value::number(x * y);
      case '/':
        if (y == 0) fail("dividing by zero", "check what you divide by isn't 0");
        return Value::number(x / y);
      case '%':
        if (y == 0) fail("remainder of dividing by zero");
        return Value::number(x - y * std::floor(x / y));
      case '^': return Value::number(std::pow(x, y));
    }
  }
  if (op == '+' && (a.isString() || b.isString())) return text(show(a) + show(b));
  if (op == '+' && a.isList() && b.isList()) {
    List* l = newList();
    l->items = a.as<List>()->items;
    for (auto& v : b.as<List>()->items) l->items.push_back(v);
    return Value::object(l);
  }
  if (a.isVec() || b.isVec()) {
    if (a.isVec() && b.isVec()) {
      Vec3 x = a.asVec(), y = b.asVec();
      switch (op) {
        case '+': return Value::vec(x + y);
        case '-': return Value::vec(x - y);
        case '*': return Value::vec(x * y);
        case '/': return Value::vec(x / y);
      }
    }
    if (a.isVec() && b.isNumber()) {
      if (op == '*') return Value::vec(a.asVec() * static_cast<float>(b.n));
      if (op == '/') return Value::vec(a.asVec() / static_cast<float>(b.n));
    }
    if (a.isNumber() && b.isVec() && op == '*') return Value::vec(b.asVec() * static_cast<float>(a.n));
  }
  static const char* names[] = {"add", "subtract", "multiply", "divide", "take the remainder of", "raise"};
  const char* ops = "+-*/%^";
  const char* w = names[std::strchr(ops, op) - ops];
  fail(std::string("can't ") + w + " " + typeName(a) + " and " + typeName(b),
       a.isNothing() || b.isNothing() ? "one side is nothing: was something not set yet?" : "");
}

bool Vm::less(const Value& a, const Value& b, bool orEqual) {
  if (a.isNumber() && b.isNumber()) return orEqual ? a.n <= b.n : a.n < b.n;
  if (a.isString() && b.isString()) return orEqual ? a.str() <= b.str() : a.str() < b.str();
  fail("can't compare " + typeName(a) + " with " + typeName(b));
}

std::vector<std::string> Vm::knownNames() const {
  std::vector<std::string> n;
  for (auto& [k, v] : globals_) n.push_back(k);
  return n;
}

Value Vm::getField(const Value& obj, const std::string& name) {
  auto methodOf = [&](const std::string& kind) -> Value {
    auto it = methods_.find(kind);
    if (it == methods_.end()) return {};
    auto m = it->second.find(name);
    return m == it->second.end() ? Value() : bound(obj, m->second);
  };
  if (obj.isVec()) {
    Vec3 v = obj.asVec();
    if (name == "x" || name == "r" || name == "red") return Value::number(v.x);
    if (name == "y" || name == "g" || name == "green") return Value::number(v.y);
    if (name == "z" || name == "b" || name == "blue") return Value::number(v.z);
    if (name == "length") return Value::number(length(v));
    if (name == "normalized" || name == "direction") return Value::vec(normalize(v));
    Value m = methodOf("vec");
    if (!m.isNothing()) return m;
    fail("a vector has no `" + name + "`", "it has x, y, z, length and normalized");
  }
  if (obj.isObj()) {
    switch (obj.o->kind) {
      case ObjKind::kTable: {
        if (Value* v = obj.as<Table>()->find(name)) return *v;
        Value m = methodOf("table");
        return m;  // nothing when missing
      }
      case ObjKind::kList: {
        auto& it = obj.as<List>()->items;
        if (name == "length" || name == "count" || name == "size") return Value::number(static_cast<double>(it.size()));
        if (name == "first") return it.empty() ? Value() : it.front();
        if (name == "last") return it.empty() ? Value() : it.back();
        Value m = methodOf("list");
        if (!m.isNothing()) return m;
        std::vector<std::string> opts = {"length", "first", "last"};
        for (auto& [k, v] : methods_["list"]) opts.push_back(k);
        std::string c = closest(name, opts);
        fail("a list has no `" + name + "`", c.empty() ? "" : "did you mean `" + c + "`?");
      }
      case ObjKind::kString: {
        if (name == "length") return Value::number(static_cast<double>(obj.str().size()));
        Value m = methodOf("text");
        if (!m.isNothing()) return m;
        fail("text has no `" + name + "`");
      }
      case ObjKind::kThing: {
        Thing* t = obj.as<Thing>();
        bool found = false;
        if (t->cls->get) {
          Value v = t->cls->get(*this, *t, name, found);
          if (found) return v;
        }
        if (t->extra)
          if (Value* v = t->extra->find(name)) return *v;
        Value m = methodOf(t->cls->name);
        if (!m.isNothing()) return m;
        m = methodOf("thing");
        if (!m.isNothing()) return m;
        std::vector<std::string> opts = t->cls->fields;
        for (auto& [k, v] : methods_[t->cls->name]) opts.push_back(k);
        std::string c = closest(name, opts);
        fail("a " + t->cls->name + " has no `" + name + "`", c.empty() ? "" : "did you mean `" + c + "`?");
      }
      default: break;
    }
  }
  if (obj.isNothing() || obj.isUnset()) fail("can't read `" + name + "` of nothing", "the value before the `.` is nothing: was it set?");
  fail("can't read `" + name + "` of " + typeName(obj));
}

void Vm::setField(const Value& obj, const std::string& name, const Value& v) {
  if (Table* t = obj.as<Table>()) return t->set(name, v);
  if (Thing* t = obj.as<Thing>()) {
    if (t->cls->set && t->cls->set(*this, *t, name, v)) return;
    if (!t->extra) t->extra = newTable();
    t->extra->set(name, v);
    return;
  }
  if (obj.isNothing()) fail("can't set `" + name + "` on nothing", "the value before the `.` is nothing: was it set?");
  fail("can't set `" + name + "` on " + typeName(obj), "only tables and things have fields you can set");
}

Value Vm::index(const Value& obj, const Value& idx) {
  if (List* l = obj.as<List>()) {
    if (!idx.isNumber()) fail("a list's items are numbered", "use list[0] for the first");
    long i = static_cast<long>(std::floor(idx.n));
    long n = static_cast<long>(l->items.size());
    if (i < 0) i += n;
    if (i < 0 || i >= n) fail("item " + num(idx.n) + " is past the end of a list of " + std::to_string(n), "the first is [0], the last is [-1]");
    return l->items[i];
  }
  if (Table* t = obj.as<Table>()) {
    std::string k = idx.isString() ? idx.str() : show(idx);
    Value* v = t->find(k);
    return v ? *v : Value();
  }
  if (obj.isString()) {
    const std::string& s = obj.str();
    if (!idx.isNumber()) fail("text is indexed by number");
    long i = static_cast<long>(idx.n), n = static_cast<long>(s.size());
    if (i < 0) i += n;
    if (i < 0 || i >= n) fail("letter " + num(idx.n) + " is past the end of the text");
    return text(std::string(1, s[i]));
  }
  if (obj.isVec() && idx.isNumber() && idx.n >= 0 && idx.n < 3) return Value::number(obj.v[static_cast<int>(idx.n)]);
  fail("can't look inside " + typeName(obj) + " with [ ]");
}

void Vm::setIndex(const Value& obj, const Value& idx, const Value& v) {
  if (List* l = obj.as<List>()) {
    if (!idx.isNumber()) fail("a list's items are numbered");
    long i = static_cast<long>(idx.n), n = static_cast<long>(l->items.size());
    if (i < 0) i += n;
    if (i == n) return l->items.push_back(v);
    if (i < 0 || i > n) fail("item " + num(idx.n) + " is past the end of a list of " + std::to_string(n));
    l->items[i] = v;
    return;
  }
  if (Table* t = obj.as<Table>()) return t->set(idx.isString() ? idx.str() : show(idx), v);
  fail("can't put things into " + typeName(obj) + " with [ ]");
}

// ------------------------------------------------------------------ Args
const Value* Args::get(int i, const char* name) const {
  if (name)
    for (auto& [k, v] : named)
      if (k == name) return &v;
  if (i >= 0 && i < count && !pos[i].isUnset()) return &pos[i];
  return nullptr;
}
double Args::num(int i, const char* name, double fb) const {
  const Value* v = get(i, name);
  if (!v || v->isNothing()) return fb;
  if (!v->isNumber()) vm.fail(std::string("`") + (name ? name : "this") + "` should be a number, not " + vm.typeName(*v));
  return v->n;
}
double Args::needNum(int i, const char* name) const {
  const Value* v = get(i, name);
  if (!v) vm.fail(std::string("missing `") + (name ? name : "a number") + "`");
  return num(i, name, 0);
}
Vec3 Args::vec(int i, const char* name, Vec3 fb) const {
  const Value* v = get(i, name);
  if (!v || v->isNothing()) return fb;
  if (v->isVec()) return v->asVec();
  if (v->isNumber()) return Vec3(static_cast<float>(v->n));
  if (List* l = v->as<List>(); l && l->items.size() == 3 && l->items[0].isNumber())
    return {static_cast<float>(l->items[0].n), static_cast<float>(l->items[1].n), static_cast<float>(l->items[2].n)};
  vm.fail(std::string("`") + (name ? name : "this") + "` should be a vector like vec(1, 2, 3), not " + vm.typeName(*v));
}
std::string Args::text(int i, const char* name, const std::string& fb) const {
  const Value* v = get(i, name);
  if (!v || v->isNothing()) return fb;
  return vm.show(*v);
}
bool Args::flag(int i, const char* name, bool fb) const {
  const Value* v = get(i, name);
  return v ? v->truthy() : fb;
}

// ------------------------------------------------------------------ running
void Vm::report(Fiber* f, const std::string& what, const std::string& hint) {
  Problem p;
  p.what = what, p.hint = hint;
  if (f && !f->frames.empty()) {
    auto& fr = f->frames.back();
    Proto* pr = fr.cl->proto;
    size_t at = fr.ip > 0 ? fr.ip - 1 : 0;
    p.line = at < pr->lines.size() ? pr->lines[at] : 0;
    p.file = pr->file;
    // where it was called from, in plain words
    std::string trace;
    for (int i = static_cast<int>(f->frames.size()) - 2; i >= 0 && i >= static_cast<int>(f->frames.size()) - 4; --i) {
      auto& c = f->frames[i];
      size_t a = c.ip > 0 ? c.ip - 1 : 0;
      trace += " (called from line " + std::to_string(a < c.cl->proto->lines.size() ? c.cl->proto->lines[a] : 0) + ")";
    }
    if (pr->name != "the script") p.what = "in `" + pr->name + "`: " + p.what;
    p.what += trace;
  }
  problems_.push_back(p);
  if (onProblem) onProblem(p);
}

void Vm::spawn(Closure* c, double at, double) {
  auto f = std::make_unique<Fiber>();
  f->stack.push_back(Value::object(c));
  f->stack.resize(1 + c->proto->slots);
  for (int i = 0; i < c->proto->arity; ++i) f->stack[1 + i] = Value::unset();
  f->frames.push_back({c, 0, 1, 0});
  f->wake = at;
  fibers_.push_back(std::move(f));
}

bool Vm::load(std::string_view source, const std::string& file) {
  Program prog;
  size_t before = problems_.size();
  if (!parse(source, file, prog, problems_)) return false;
  Proto* main = compileProgram(*this, prog, problems_);
  if (!main) return false;
  Closure* c = alloc<Closure>(main);
  spawn(c, time_);
  Fiber* f = fibers_.back().get();
  runFiber(*f, time_);
  fibers_.erase(std::remove_if(fibers_.begin(), fibers_.end(), [](auto& x) { return x->done; }), fibers_.end());
  return problems_.size() == before;
}

bool Vm::check(std::string_view source, const std::string& file, std::vector<Problem>& problems) {
  Program prog;
  if (!parse(source, file, prog, problems)) return false;
  Vm scratch;
  return compileProgram(scratch, prog, problems) != nullptr;
}

void Vm::emit(const std::string& event, Value payload) { events_.push_back({event, payload}); }

void Vm::update(double dt) {
  time_ += std::max(0.0, dt);
  // events → handler fibers
  auto events = std::move(events_);
  events_.clear();
  for (auto& [name, payload] : events)
    for (auto& [ev, fn] : handlers_)
      if (ev == name) {
        globals_["event"] = payload;
        spawn(fn, time_);
        fibers_.back()->event = payload, fibers_.back()->hasEvent = true;
      }
  // timers → fibers
  for (size_t i = 0; i < timers_.size(); ++i) {
    Timer& t = timers_[i];
    if (t.next > time_) continue;
    if (t.every > 0 && t.running) {
      t.next = time_ + t.every;
      continue;
    }
    spawn(t.fn, time_);
    fibers_.back()->timer = static_cast<int>(i);
    t.running = true;
    if (t.every > 0) t.next = std::max(t.next + t.every, time_ + t.every * 0.25);
    else t.next = kInf;
  }
  for (size_t i = 0; i < fibers_.size(); ++i) {
    Fiber* f = fibers_[i].get();
    if (f->done || f->wake > time_) continue;
    runFiber(*f, time_);
  }
  for (auto& f : fibers_)
    if (f->done && f->timer >= 0) timers_[f->timer].running = false;
  fibers_.erase(std::remove_if(fibers_.begin(), fibers_.end(), [](auto& x) { return x->done; }), fibers_.end());
  // one-shot timers that fired are dropped (kept while their fiber runs)
  if (objects_ > nextCollect_) collectGarbage();
}

Value Vm::call(Value fn, const std::vector<Value>& args) {
  if (Native* n = fn.as<Native>()) {
    std::vector<std::pair<std::string, Value>> none;
    Args a{*this, args.data(), static_cast<int>(args.size()), none, 0, {}};
    return n->fn(a);
  }
  Closure* c = fn.as<Closure>();
  Bound* b = fn.as<Bound>();
  if (b) {
    std::vector<Value> all = {b->self};
    all.insert(all.end(), args.begin(), args.end());
    if (Native* n = b->fn.as<Native>()) {
      std::vector<std::pair<std::string, Value>> none;
      Args a{*this, args.data(), static_cast<int>(args.size()), none, 0, b->self};
      return n->fn(a);
    }
    return call(b->fn, all);
  }
  if (!c) fail("that isn't a function: it's " + typeName(fn));
  if (++depth_ > 200) {
    --depth_;
    fail("functions calling functions too deeply", "is something calling itself forever?");
  }
  auto f = std::make_unique<Fiber>();
  f->noWait = true;
  f->stack.push_back(fn);
  f->stack.resize(1 + c->proto->slots);
  for (int i = 0; i < c->proto->arity; ++i) f->stack[1 + i] = i < static_cast<int>(args.size()) ? args[i] : Value::unset();
  f->frames.push_back({c, 0, 1, 0});
  Fiber* raw = f.get();
  fibers_.push_back(std::move(f));  // rooted while it runs
  Fiber* saved = current_;
  bool ok = runFiber(*raw, time_);
  current_ = saved;
  --depth_;
  Value r = raw->result;
  raw->done = true;
  if (!ok) fail("a function called from here stopped with a problem");
  return r;
}

#define READ8() (code[fr->ip++])
#define READ16() (fr->ip += 2, static_cast<int>(code[fr->ip - 2] | (code[fr->ip - 1] << 8)))

bool Vm::runFiber(Fiber& f, double now) {
  current_ = &f;
  if (f.hasEvent) globals_["event"] = f.event;
  size_t steps = 0;
  try {
    while (!f.frames.empty()) {
      Fiber::Frame* fr = &f.frames.back();
      Proto* proto = fr->cl->proto;
      const uint8_t* code = proto->code.data();
      std::vector<Value>& st = f.stack;
      for (;;) {
        if (++steps > budget) {
          fail("this has run " + std::to_string(budget) + " steps without waiting", "a loop that never ends? add `wait` inside it, or check its condition");
        }
        uint8_t op = READ8();
        switch (op) {
          case OP_CONST: st.push_back(proto->constants[READ16()]); break;
          case OP_NOTHING: st.push_back({}); break;
          case OP_TRUE: st.push_back(Value::boolean(true)); break;
          case OP_FALSE: st.push_back(Value::boolean(false)); break;
          case OP_POP: st.pop_back(); break;
          case OP_DUP: st.push_back(st.back()); break;
          case OP_GET_LOCAL: st.push_back(st[fr->base + READ16()]); break;
          case OP_SET_LOCAL: st[fr->base + READ16()] = st.back(), st.pop_back(); break;
          case OP_GET_CELL: {
            Value& v = st[fr->base + READ16()];
            Cell* c = v.as<Cell>();
            st.push_back(c ? c->value : v);
            break;
          }
          case OP_SET_CELL: {
            Value& v = st[fr->base + READ16()];
            if (Cell* c = v.as<Cell>()) c->value = st.back();
            else v = st.back();
            st.pop_back();
            break;
          }
          case OP_BOX: {
            Cell* c = alloc<Cell>();
            c->value = st.back();
            st.back() = Value::object(c);
            break;
          }
          case OP_GET_UPVAL: st.push_back(fr->cl->cells[READ16()]->value); break;
          case OP_SET_UPVAL: fr->cl->cells[READ16()]->value = st.back(), st.pop_back(); break;
          case OP_GET_GLOBAL: {
            const std::string& n = proto->constants[READ16()].str();
            auto it = globals_.find(n);
            if (it == globals_.end()) {
              std::string c = closest(n, knownNames());
              fail("`" + n + "` isn't known", c.empty() ? "set it first with `let " + n + " = …`" : "did you mean `" + c + "`?");
            }
            st.push_back(it->second);
            break;
          }
          case OP_SET_GLOBAL: globals_[proto->constants[READ16()].str()] = st.back(), st.pop_back(); break;
          case OP_GET_FIELD: {
            const std::string& n = proto->constants[READ16()].str();
            Value v = getField(st.back(), n);
            st.back() = v;
            break;
          }
          case OP_SET_FIELD: {
            const std::string& n = proto->constants[READ16()].str();
            Value v = st.back();
            st.pop_back();
            Value o = st.back();
            st.pop_back();
            setField(o, n, v);
            break;
          }
          case OP_GET_INDEX: {
            Value i = st.back();
            st.pop_back();
            st.back() = index(st.back(), i);
            break;
          }
          case OP_SET_INDEX: {
            Value v = st.back();
            st.pop_back();
            Value i = st.back();
            st.pop_back();
            Value o = st.back();
            st.pop_back();
            setIndex(o, i, v);
            break;
          }
          case OP_ADD: case OP_SUB: case OP_MUL: case OP_DIV: case OP_MOD: case OP_POW: {
            Value b = st.back();
            st.pop_back();
            static const char ops[] = {'+', '-', '*', '/', '%', '^'};
            st.back() = arith(ops[op - OP_ADD], st.back(), b);
            break;
          }
          case OP_NEG: {
            Value& a = st.back();
            if (a.isNumber()) a.n = -a.n;
            else if (a.isVec()) a = Value::vec(-a.asVec());
            else fail("can't make " + typeName(a) + " negative");
            break;
          }
          case OP_NOT: st.back() = Value::boolean(!st.back().truthy()); break;
          case OP_EQ: case OP_NE: {
            Value b = st.back();
            st.pop_back();
            bool e = equal(st.back(), b);
            st.back() = Value::boolean(op == OP_EQ ? e : !e);
            break;
          }
          case OP_LT: case OP_LE: case OP_GT: case OP_GE: {
            Value b = st.back();
            st.pop_back();
            Value a = st.back();
            bool r = op == OP_LT ? less(a, b, false) : op == OP_LE ? less(a, b, true) : op == OP_GT ? less(b, a, false) : less(b, a, true);
            st.back() = Value::boolean(r);
            break;
          }
          case OP_IN: {
            Value b = st.back();
            st.pop_back();
            Value a = st.back();
            bool r = false;
            if (List* l = b.as<List>()) {
              for (auto& x : l->items)
                if (equal(x, a)) { r = true; break; }
            } else if (Table* t = b.as<Table>()) r = t->find(a.isString() ? a.str() : show(a)) != nullptr;
            else if (b.isString()) r = b.str().find(show(a)) != std::string::npos;
            else fail("`in` looks inside a list, a table or text, not " + typeName(b));
            st.back() = Value::boolean(r);
            break;
          }
          case OP_JUMP: {
            int o = READ16();
            fr->ip += o;
            break;
          }
          case OP_LOOP: {
            int o = READ16();
            fr->ip -= o;
            break;
          }
          case OP_JUMP_FALSE: {
            int o = READ16();
            if (!st.back().truthy()) fr->ip += o;
            st.pop_back();
            break;
          }
          case OP_AND: {
            int o = READ16();
            if (!st.back().truthy()) fr->ip += o;
            else st.pop_back();
            break;
          }
          case OP_OR: {
            int o = READ16();
            if (st.back().truthy()) fr->ip += o;
            else st.pop_back();
            break;
          }
          case OP_CALL: {
            int argc = READ8(), named = READ8(), namesK = READ16();
            size_t calleeAt = st.size() - argc - named - 1;
            Value callee = st[calleeAt];
            Value self;
            bool hasSelf = false;
            if (Bound* b = callee.as<Bound>()) {
              self = b->self, hasSelf = true;
              callee = b->fn;
            }
            if (Native* n = callee.as<Native>()) {
              std::vector<std::pair<std::string, Value>> nv;
              if (named) {
                List* names = proto->constants[namesK].as<List>();
                for (int i = 0; i < named; ++i) nv.push_back({names->items[i].str(), st[calleeAt + 1 + argc + i]});
              }
              Args a{*this, st.data() + calleeAt + 1, argc, nv, proto->lines[fr->ip - 1], self};
              Value r = n->fn(a);
              st.resize(calleeAt);
              st.push_back(r);
              break;
            }
            Closure* c = callee.as<Closure>();
            if (!c) {
              fail("this isn't a function, it's " + typeName(callee), callee.isNothing() ? "is the name spelled right, and set before this line?" : "");
            }
            Proto* p = c->proto;
            int extra = hasSelf ? 1 : 0;
            if (argc + extra > p->arity) {
              fail("`" + p->name + "` takes " + std::to_string(p->arity) + " setting" + (p->arity == 1 ? "" : "s") + ", but got " + std::to_string(argc + extra));
            }
            std::vector<Value> params(p->arity, Value::unset());
            if (hasSelf) params[0] = self;
            for (int i = 0; i < argc; ++i) params[i + extra] = st[calleeAt + 1 + i];
            if (named) {
              List* names = proto->constants[namesK].as<List>();
              for (int i = 0; i < named; ++i) {
                const std::string& n = names->items[i].str();
                auto it = std::find(p->params.begin(), p->params.end(), n);
                if (it == p->params.end()) {
                  std::string cl = closest(n, p->params);
                  fail("`" + p->name + "` has no setting `" + n + "`", cl.empty() ? "its settings are: " + [&] {
                    std::string s;
                    for (auto& x : p->params) s += (s.empty() ? "" : ", ") + x;
                    return s.empty() ? std::string("none") : s;
                  }() : "did you mean `" + cl + "`?");
                }
                params[it - p->params.begin()] = st[calleeAt + 1 + argc + i];
              }
            }
            st.resize(calleeAt + 1);
            st[calleeAt] = callee;
            for (auto& v : params) st.push_back(v);
            st.resize(calleeAt + 1 + p->slots);
            if (f.frames.size() > 400) fail("functions calling functions too deeply", "is something calling itself forever?");
            f.frames.push_back({c, 0, calleeAt + 1, calleeAt});
            goto reframe;
          }
          case OP_RETURN: {
            Value r = st.back();
            size_t callee = fr->callee;
            f.frames.pop_back();
            st.resize(callee);
            if (f.frames.empty()) {
              f.result = r;
              f.done = true;
              current_ = nullptr;
              return true;
            }
            st.push_back(r);
            goto reframe;
          }
          case OP_CLOSURE: {
            Proto* p = proto->constants[READ16()].as<Proto>();
            Closure* c = alloc<Closure>(p);
            for (auto& cap : p->captures) {
              if (cap.fromLocal) {
                Value& v = st[fr->base + cap.index];
                Cell* cell = v.as<Cell>();
                if (!cell) {  // not boxed yet (defensive): box it now
                  cell = alloc<Cell>();
                  cell->value = v;
                  v = Value::object(cell);
                }
                c->cells.push_back(cell);
              } else c->cells.push_back(fr->cl->cells[cap.index]);
            }
            st.push_back(Value::object(c));
            break;
          }
          case OP_LIST: {
            int n = READ16();
            List* l = alloc<List>();
            l->items.assign(st.end() - n, st.end());
            st.resize(st.size() - n);
            st.push_back(Value::object(l));
            break;
          }
          case OP_TABLE: {
            int n = READ16();
            List* keys = proto->constants[READ16()].as<List>();
            Table* t = alloc<Table>();
            for (int i = 0; i < n; ++i) t->set(keys->items[i].str(), st[st.size() - n + i]);
            st.resize(st.size() - n);
            st.push_back(Value::object(t));
            break;
          }
          case OP_INTERP: {
            int n = READ16();
            std::string s;
            for (size_t i = st.size() - n; i < st.size(); ++i) s += show(st[i]);
            st.resize(st.size() - n);
            st.push_back(text(std::move(s)));
            break;
          }
          case OP_ITER_PREP: {
            Value v = st.back();
            List* l = alloc<List>();
            if (List* src = v.as<List>()) l->items = src->items;
            else if (Table* t = v.as<Table>()) for (auto& [k, x] : t->entries) l->items.push_back(text(k));
            else if (v.isString()) for (char ch : v.str()) l->items.push_back(text(std::string(1, ch)));
            else if (v.isNumber()) for (int i = 0; i < static_cast<int>(v.n); ++i) l->items.push_back(Value::number(i));
            else fail("can't go through each of " + typeName(v), "`for each` works on lists, tables, text and numbers");
            st.back() = Value::object(l);
            break;
          }
          case OP_ITER_NEXT: {
            int L = READ16(), I = READ16(), V = READ16(), exit = READ16();
            List* l = st[fr->base + L].as<List>();
            double& i = st[fr->base + I].n;
            if (!l || i >= static_cast<double>(l->items.size())) {
              fr->ip += exit;
              break;
            }
            st[fr->base + V] = l->items[static_cast<size_t>(i)];
            i += 1;
            break;
          }
          case OP_RANGE_STEP: {
            Value to = st.back();
            st.pop_back();
            Value from = st.back();
            if (!from.isNumber() || !to.isNumber()) fail("`for … from … to …` needs numbers");
            st.back() = Value::number(to.n >= from.n ? 1 : -1);
            break;
          }
          case OP_RANGE_NEXT: {
            int C = READ16(), E = READ16(), S = READ16(), exit = READ16();
            Value& c = st[fr->base + C];
            const Value& e = st[fr->base + E];
            const Value& s = st[fr->base + S];
            if (!c.isNumber() || !e.isNumber() || !s.isNumber()) fail("`for … from … to …` needs numbers");
            if (s.n == 0) fail("a `for` step of 0 never gets anywhere");
            c.n += s.n;
            double eps = std::fabs(s.n) * 1e-9;
            if (s.n > 0 ? c.n > e.n + eps : c.n < e.n - eps) fr->ip += exit;
            break;
          }
          case OP_DEFAULT: {
            int slot = READ16(), skip = READ16();
            if (!st[fr->base + slot].isUnset()) fr->ip += skip;
            break;
          }
          case OP_REQUIRE: {
            int slot = READ16(), k = READ16();
            if (st[fr->base + slot].isUnset()) fail("`" + proto->name + "` needs its `" + proto->constants[k].str() + "` setting");
            break;
          }
          case OP_CELLIFY: {
            Value& v = st[fr->base + READ16()];
            Cell* c = alloc<Cell>();
            c->value = v;
            v = Value::object(c);
            break;
          }
          case OP_WAIT: {
            Value d = st.back();
            st.pop_back();
            if (!d.isNumber()) fail("`wait` needs a time, like `wait 2 s`");
            if (f.noWait) fail("can't wait here", "this function was called by the engine and must finish at once; start a `task` to wait");
            f.wake = now + std::max(0.0, d.n);
            current_ = nullptr;
            return true;  // not done; resumes later
          }
          case OP_SCHEDULE: {
            int kind = READ8();
            Closure* c = st.back().as<Closure>();
            st.pop_back();
            Value arg = st.back();
            st.pop_back();
            if (kind == 0 || kind == 1) {
              if (!arg.isNumber()) fail(std::string("`") + (kind ? "after" : "every") + "` needs a time, like `every 2 s`");
              if (kind == 0 && arg.n <= 0) fail("`every` needs a time above zero");
              timers_.push_back({c, time_ + arg.n, kind == 0 ? arg.n : -1});
            } else if (kind == 2) {
              handlers_.push_back({show(arg), c});
            } else {
              spawn(c, time_);
              // run the new task straight away, up to its first wait
              Fiber* nf = fibers_.back().get();
              nf->event = f.event, nf->hasEvent = f.hasEvent;  // a task started from a handler sees its event
              Fiber* saved = current_;
              size_t savedSteps = steps;
              runFiber(*nf, now);
              current_ = saved;
              if (f.hasEvent) globals_["event"] = f.event;
              steps = savedSteps;
              fr = &f.frames.back();
            }
            break;
          }
          case OP_EMIT: {
            Value payload = st.back();
            st.pop_back();
            Value name = st.back();
            st.pop_back();
            emit(show(name), payload);
            break;
          }
          case OP_COUNT_CHECK: {
            int slot = READ16(), exit = READ16();
            Value& c = st[fr->base + slot];
            if (!c.isNumber()) fail("`repeat` needs a number of times");
            if (c.n < 1) fr->ip += exit;
            else c.n -= 1;
            break;
          }
          default: fail("the machine met an instruction it doesn't know (a bug in Looped)");
        }
      }
    reframe:;
    }
  } catch (const ScriptFail& e) {
    report(&f, e.what, e.hint);
    f.done = true;
    current_ = nullptr;
    return false;
  }
  f.done = true;
  current_ = nullptr;
  return true;
}

}  // namespace loop::script
