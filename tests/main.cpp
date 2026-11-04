// Runs every TEST in the tests/ files linked in. `loop_tests <filter>` runs
// only the tests whose name contains the filter.
#include <cstring>

#include "tests/test.h"

int main(int argc, char** argv) {
  const char* filter = argc > 1 ? argv[1] : nullptr;
  int ran = 0;
  for (auto& c : looptest::cases()) {
    if (filter && !std::strstr(c.name, filter)) continue;
    int before = looptest::failures();
    c.fn();
    ++ran;
    std::printf("%s %s\n", looptest::failures() == before ? "ok  " : "FAIL", c.name);
  }
  std::printf("\n%d tests, %d failed checks\n", ran, looptest::failures());
  return looptest::failures() ? 1 : 0;
}
