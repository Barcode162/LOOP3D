#pragma once
// A tiny test runner: TEST(name) { CHECK(...); CHECK_NEAR(a, b, eps); }
#include <cmath>
#include <cstdio>
#include <functional>
#include <memory>
#include <string>
#include <vector>

#include "engine/script/scene_api.h"

namespace looptest {
struct Case {
  const char* name;
  std::function<void()> fn;
};
inline std::vector<Case>& cases() {
  static std::vector<Case> c;
  return c;
}
inline int& failures() {
  static int f = 0;
  return f;
}
struct Register {
  Register(const char* n, std::function<void()> f) { cases().push_back({n, std::move(f)}); }
};
inline void fail(const char* file, int line, const std::string& what) {
  ++failures();
  std::printf("  FAIL %s:%d: %s\n", file, line, what.c_str());
}
}  // namespace looptest

#define LT_CAT2(a, b) a##b
#define LT_CAT(a, b) LT_CAT2(a, b)
#define TEST(name)                                                                   \
  static void LT_CAT(test_, name)();                                                 \
  static looptest::Register LT_CAT(reg_, name)(#name, LT_CAT(test_, name));          \
  static void LT_CAT(test_, name)()
#define CHECK(cond) \
  do { if (!(cond)) looptest::fail(__FILE__, __LINE__, #cond); } while (0)
// runScene: run a script on a fresh stage and hand the stage back, so a word's
// test is three lines:
//   auto r = runScene("box size: 1\n");
//   CHECK(r->ok && r->stage.scene.instances.size() == 1);
//   CHECK(r->hasProblem("did you mean"));
namespace looptest {
struct SceneRun {
  loop::script::Vm vm;
  loop::script::Stage stage;  // things (stage.scene.instances), lights (stage.scene.lights), ...
  bool ok = false;            // the script's top level ran without failing
  // The problems found, as text.
  std::vector<std::string> problems() const {
    std::vector<std::string> out;
    for (auto& p : vm.problems()) out.push_back(p.text());
    return out;
  }
  // True when some problem's text holds `part`.
  bool hasProblem(const std::string& part) const {
    for (auto& p : vm.problems())
      if (p.text().find(part) != std::string::npos) return true;
    return false;
  }
};
// Not movable (the stage's words point at it), so it comes back in a pointer.
inline std::unique_ptr<SceneRun> runScene(const std::string& script, const std::string& file = "t.loop") {
  auto r = std::make_unique<SceneRun>();
  loop::script::installStandardLibrary(r->vm);
  loop::script::installSceneApi(r->vm, r->stage);
  r->ok = r->vm.load(script, file);
  return r;
}
}  // namespace looptest
using looptest::runScene;

#define CHECK_NEAR(a, b, eps)                                                                               \
  do {                                                                                                        \
    double _a = (a), _b = (b);                                                                                \
    if (!(std::fabs(_a - _b) <= (eps)))                                                                       \
      looptest::fail(__FILE__, __LINE__, std::string(#a " ~ " #b ": ") + std::to_string(_a) + " vs " + std::to_string(_b)); \
  } while (0)
