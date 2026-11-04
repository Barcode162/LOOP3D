#pragma once
// A tiny test runner: TEST(name) { CHECK(...); CHECK_NEAR(a, b, eps); }
#include <cmath>
#include <cstdio>
#include <functional>
#include <string>
#include <vector>

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
#define CHECK_NEAR(a, b, eps)                                                                                 \
  do {                                                                                                        \
    double _a = (a), _b = (b);                                                                                \
    if (!(std::fabs(_a - _b) <= (eps)))                                                                       \
      looptest::fail(__FILE__, __LINE__, std::string(#a " ~ " #b ": ") + std::to_string(_a) + " vs " + std::to_string(_b)); \
  } while (0)
