#include "engine/script/vm.h"
#include "tests/test.h"

using namespace loop::script;

namespace {
struct Run {
  Vm vm;
  std::vector<std::string> out;
  bool ok;
  explicit Run(const std::string& src, double runFor = 0) {
    installStandardLibrary(vm);
    vm.onSay = [this](const std::string& s) { out.push_back(s); };
    ok = vm.load(src, "test.loop");
    for (double t = 0; t < runFor; t += 0.05) vm.update(0.05);
  }
  std::string joined() const {
    std::string s;
    for (auto& o : out) s += (s.empty() ? "" : "|") + o;
    return s;
  }
  std::string problem() const { return vm.problems().empty() ? "" : vm.problems()[0].text(); }
};
}  // namespace

TEST(script_basics_and_units) {
  Run r(R"(
let x = 2 + 3 * 4
say x
say 30 cm + 1 m
say 50%
say 90 degrees * 2 / pi
say "a" + 1
say 7 % 3, 2 ^ 10, -x
say 1 < 2 and 3 > 4, not false, 5 is 5, 5 is not 6
)");
  CHECK(r.ok);
  CHECK(r.joined() == "14|1.3|0.5|1|a1|1 1024 -14|false true true true");
}

TEST(script_text_interpolation) {
  Run r(R"(
let name = "Mira"
let embers = 3
say "Hello {name}, you have {embers * 2} embers {{not this}}"
)");
  CHECK(r.joined() == "Hello Mira, you have 6 embers {not this}");
}

TEST(script_functions_defaults_named_and_closures) {
  Run r(R"(
to greet(who, loud: false, mark: "!")
  if loud
    return "HELLO " + who + mark
  return "hello " + who + mark
say greet("you")
say greet("you", loud: true)
say greet(who: "Mira", mark: "?")
to counter()
  let n = 0
  return fn(): n += 1
let a = counter()
let b = counter()
a()
a()
say a(), b()
to fib(n)
  return n if n < 2 else fib(n - 1) + fib(n - 2)
say fib(20)
let double = fn(x): x * 2
say [1, 2, 3].map(double)
)");
  CHECK(r.ok);
  CHECK(r.joined() == "hello you!|HELLO you!|hello Mira?|3 1|6765|[2, 4, 6]");
  if (!r.ok) std::printf("    %s\n", r.problem().c_str());
}

TEST(script_command_calls) {
  Run r(R"(
say "one"
say "two", 3
to place(thing, at: 0)
  say thing + " at " + text(at)
place "lamp", at: 2
)");
  CHECK(r.joined() == "one|two 3|lamp at 2");
}

TEST(script_loops) {
  Run r(R"(
let s = 0
for i from 1 to 10
  s += i
say s
let t = []
for i from 10 to 0 by -5
  t.push(i)
say t
let c = 0
repeat 4 times
  c += 1
say c
let words = []
for each w in ["a", "b", "c", "d"]
  if w == "b"
    continue
  if w == "d"
    break
  words.push(w)
say words.join("-")
let k = 0
while k < 5
  k += 2
say k
let keys = []
for each key in {x: 1, y: 2}
  keys.push(key)
say keys
)");
  CHECK(r.ok);
  CHECK(r.joined() == "55|[10, 5, 0]|4|a-c|6|[\"x\", \"y\"]");
  if (!r.ok) std::printf("    %s\n", r.problem().c_str());
}

TEST(script_lists_tables_vectors) {
  Run r(R"(
let l = [3, 1, 2]
l.sort()
say l, l.length, l[0], l[-1], 2 in l
let m = {name: "Mira", age: 9}
m.age += 1
m["town"] = "Emberhollow"
say m.name, m.age, m.town, m.missing
say m.keys()
let v = vec(1, 2, 3) * 2 + vec(1)
say v, v.x, v.length > 7
let words = "the last ember".split(" ")
say words.length, "ember".upper(), "a-b".replace("-", "+")
say [1, 2, 3, 4].filter(fn(x): x % 2 == 0).sum()
)");
  CHECK(r.ok);
  CHECK(r.joined() == "[1, 2, 3] 3 1 3 true|Mira 10 Emberhollow nothing|[\"name\", \"age\", \"town\"]|(3, 5, 7) 3 true|3 EMBER a+b|6");
  if (!r.ok) std::printf("    %s\n", r.problem().c_str());
}

TEST(script_wait_every_after_when) {
  Run r(R"(
let ticks = 0
every 0.5 s
  ticks += 1
after 1 s
  say "after one second"
when "door opened"
  say "creak " + text(event)
task
  say "task starts"
  wait 2 s
  say "task ends"
say "loaded"
)");
  CHECK(r.ok);
  CHECK(r.joined() == "task starts|loaded");
  for (int i = 0; i < 20; ++i) r.vm.update(0.05);  // 1 s
  r.vm.emit("door opened", Value::number(7));
  for (int i = 0; i < 25; ++i) r.vm.update(0.05);  // 2.25 s
  CHECK(r.joined() == "task starts|loaded|after one second|creak 7|task ends");
  double ticks = r.vm.global("ticks").n;
  CHECK(ticks >= 3 && ticks <= 5);
}

TEST(script_top_level_can_wait) {
  Run r(R"(
say "a"
wait 1 s
say "b"
)");
  CHECK(r.joined() == "a");
  for (int i = 0; i < 30; ++i) r.vm.update(0.05);
  CHECK(r.joined() == "a|b");
}

TEST(script_errors_are_plain_and_helpful) {
  {
    Run r("let height = 2\nsay heigth\n");
    CHECK(!r.ok);
    CHECK(r.problem().find("line 2") != std::string::npos);
    CHECK(r.problem().find("did you mean `height`") != std::string::npos);
  }
  {
    Run r("to f(size)\n  return size\nsay f(sise: 3)\n");
    CHECK(r.problem().find("did you mean `size`") != std::string::npos);
  }
  {
    Run r("if true\nsay 1\n");
    CHECK(r.problem().find("indented block") != std::string::npos);
  }
  {
    Run r("let x = \"open\n");
    CHECK(r.problem().find("closing") != std::string::npos);
  }
  {
    Run r("let l = [1]\nsay l[5]\n");
    CHECK(r.problem().find("past the end") != std::string::npos);
  }
  {
    Run r("let n = nothing\nsay n.color\n");
    CHECK(r.problem().find("of nothing") != std::string::npos);
  }
  {
    Run r("while true\n  let a = 1\n");
    CHECK(r.problem().find("without waiting") != std::string::npos);
  }
  {
    Run r("say [1, 2].lenght\n");
    CHECK(r.problem().find("did you mean `length`") != std::string::npos);
  }
  {
    std::vector<Problem> p;
    CHECK(!Vm::check("break\n", "x.loop", p));
    CHECK(!p.empty() && p[0].what.find("inside a loop") != std::string::npos);
  }
}

TEST(script_garbage_is_collected) {
  Run r(R"(
let keep = []
every 0.01 s
  let junk = []
  repeat 200 times
    junk.push({a: [1, 2, 3]})
  keep = [junk.length]
)");
  CHECK(r.ok);
  for (int i = 0; i < 200; ++i) r.vm.update(0.01);
  r.vm.collectGarbage();
  CHECK(r.vm.liveObjects() < 5000);
  CHECK(r.vm.global("keep").as<List>()->items[0].n == 200);
}

TEST(script_easing_and_smooth_motion) {
  Run r(R"(
say ease(0, "smooth"), ease(1, "smooth"), ease(0.5, "smooth")
let x = 0
repeat 200 times
  x = approach(x, 10, 6, 1 / 60)
say round(x * 100) / 100
)");
  CHECK(r.joined() == "0 1 0.5|10");
}

// Two events in one step: each handler sees its own `event` (they used to
// share one global, so the second overwrote the first before either ran).
TEST(script_each_handler_keeps_its_event) {
  Vm vm;
  installStandardLibrary(vm);
  CHECK(vm.load(R"(
let got = -1
let used = 0
when "choice"
  wait 0.01 s
  got = event
when "use"
  used += 1
)", "t.loop"));
  vm.emit("choice", Value::number(2));
  vm.emit("use");
  for (int i = 0; i < 5; ++i) vm.update(0.01);
  CHECK(vm.global("got").isNumber() && vm.global("got").n == 2);
  CHECK(vm.global("used").n == 1);
}
