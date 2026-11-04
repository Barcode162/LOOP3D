#pragma once
// Logging with levels; printf-style.
#include <cstdarg>

namespace loop {
enum class LogLevel { kDebug, kInfo, kWarn, kError };
void setLogLevel(LogLevel l);
void logf(LogLevel l, const char* fmt, ...) __attribute__((format(printf, 2, 3)));
#define LOOP_INFO(...) ::loop::logf(::loop::LogLevel::kInfo, __VA_ARGS__)
#define LOOP_WARN(...) ::loop::logf(::loop::LogLevel::kWarn, __VA_ARGS__)
#define LOOP_ERROR(...) ::loop::logf(::loop::LogLevel::kError, __VA_ARGS__)
#define LOOP_DEBUG(...) ::loop::logf(::loop::LogLevel::kDebug, __VA_ARGS__)
}  // namespace loop
