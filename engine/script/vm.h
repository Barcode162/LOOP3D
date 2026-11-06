#pragma once
// Looped Script, running. A script is compiled to bytecode and run by this
// machine. Many things can run at once (every `every`, `after`, `when` and
// `task` is its own fiber) and any of them can `wait`: the machine keeps
// the time and wakes them. A runaway loop that never waits is stopped
// after a step budget with its line, instead of freezing the game.
//
//   Vm vm;
//   installStandardLibrary(vm);       // maths, text, lists, tables, time
//   vm.load(source, "story.loop");    // problems in vm.problems()
//   each frame: vm.update(dt);
#include <functional>
#include <memory>
#include <string>
#include <unordered_map>
#include <vector>

#include "engine/script/lexer.h"
#include "engine/script/value.h"
#include "engine/core/random.h"

namespace loop::script {

struct Program;

class Vm {
 public:
  Vm();
  ~Vm();
  Vm(const Vm&) = delete;
  Vm& operator=(const Vm&) = delete;

  // Compiles and runs a script's top level. False if it didn't compile or
  // its top level failed; see problems(). Top-level code that waits keeps
  // running in later updates.
  bool load(std::string_view source, const std::string& file);
  // Only checks a script (nothing runs).
  static bool check(std::string_view source, const std::string& file, std::vector<Problem>& problems);

  // Moves time on by dt seconds and runs whatever is due.
  void update(double dt);
  double time() const { return time_; }
  // An event for `when` handlers: `when "door opened"`.
  void emit(const std::string& event, Value payload = {});

  // Built-ins and globals.
  void native(const std::string& name, NativeFn fn, const std::string& help = {});
  void define(const std::string& name, Value v) { globals_[name] = v; }
  Value global(const std::string& name) const;
  bool hasGlobal(const std::string& name) const { return globals_.count(name) != 0; }
  // Every built-in word and its help ("box(size, at, material)"), by name.
  std::vector<std::pair<std::string, std::string>> words() const;
  // Calls a script function (or built-in) from C++. Can't wait inside.
  Value call(Value fn, const std::vector<Value>& args);

  // Heap values.
  Value text(std::string s);
  List* newList();
  Table* newTable();
  Thing* newThing(const ThingClass* cls, int id);
  Value bound(Value self, Value fn);
  Proto* newProto();
  // Methods for a kind of value ("list", "text", "vec", "table") or a
  // ThingClass name: found by `value.method`.
  void method(const std::string& kind, const std::string& name, NativeFn fn, const std::string& help = {});

  // Raise a script error from a built-in (shown with the script's line).
  [[noreturn]] void fail(const std::string& what, const std::string& hint = {});

  const std::vector<Problem>& problems() const { return problems_; }
  void clearProblems() { problems_.clear(); }
  std::function<void(const std::string&)> onSay;    // `say` output
  std::function<void(const Problem&)> onProblem;    // run-time problems as they happen

  // This script's own random numbers (`random`, `chance`, `seed`): every
  // script starts from the same seed, so a scene is the same each time it
  // runs, whatever ran before it.
  Rng& random() { return random_; }

  // Settings and statistics.
  size_t budget = 2000000;  // steps one fiber may take per update before it's stopped
  size_t steps() const { return steps_; }
  size_t liveObjects() const { return objects_; }
  size_t fibers() const;
  void collectGarbage();
  // Values C++ holds on to (so the collector keeps them).
  void keep(Value v) { pinned_.push_back(v); }

  // Turning values into text the way scripts see them.
  std::string show(const Value& v) const;
  std::string typeName(const Value& v) const;

  struct Fiber;
  struct Impl;

 private:
  friend struct Compiler;
  friend struct Args;
  template <class T, class... A> T* alloc(A&&... a);
  bool runFiber(Fiber& f, double now);
  void report(Fiber* f, const std::string& what, const std::string& hint);
  Value getField(const Value& obj, const std::string& name);
  void setField(const Value& obj, const std::string& name, const Value& v);
  Value index(const Value& obj, const Value& idx);
  void setIndex(const Value& obj, const Value& idx, const Value& v);
  Value arith(char op, const Value& a, const Value& b);
  bool less(const Value& a, const Value& b, bool orEqual);
  bool equal(const Value& a, const Value& b) const;
  void spawn(Closure* c, double at, double every = -1);
  std::vector<std::string> knownNames() const;

  std::unordered_map<std::string, Value> globals_;
  std::unordered_map<std::string, std::unordered_map<std::string, Value>> methods_;
  std::vector<Problem> problems_;
  std::vector<std::unique_ptr<Fiber>> fibers_;
  struct Timer {
    Closure* fn;
    double next, every;  // every < 0: once
    bool running = false;
  };
  std::vector<Timer> timers_;
  std::vector<std::pair<std::string, Closure*>> handlers_;
  std::vector<std::pair<std::string, Value>> events_;
  std::vector<Value> pinned_;
  std::vector<Proto*> protos_;
  Obj* heap_ = nullptr;
  size_t objects_ = 0, nextCollect_ = 20000, steps_ = 0;
  double time_ = 0;
  Rng random_{12345};
  Fiber* current_ = nullptr;
  int depth_ = 0;  // nested C++ → script calls
};

// Maths, text, lists, tables, randomness, time, `say`.
void installStandardLibrary(Vm& vm);

}  // namespace loop::script
