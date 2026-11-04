#include "engine/core/log.h"

#include <cstdio>
#include <mutex>

namespace loop {
static LogLevel g_level = LogLevel::kInfo;
static std::mutex g_mutex;
void setLogLevel(LogLevel l) { g_level = l; }
void logf(LogLevel l, const char* fmt, ...) {
  if (l < g_level) return;
  static const char* names[] = {"debug", "info", "warn", "error"};
  std::lock_guard<std::mutex> lock(g_mutex);
  FILE* out = l >= LogLevel::kWarn ? stderr : stdout;
  std::fprintf(out, "[%s] ", names[static_cast<int>(l)]);
  va_list ap;
  va_start(ap, fmt);
  std::vfprintf(out, fmt, ap);
  va_end(ap);
  std::fputc('\n', out);
}
}  // namespace loop
