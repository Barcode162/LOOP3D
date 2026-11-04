#pragma once
// Whole-file reads and writes.
#include <cstdint>
#include <string>
#include <vector>

namespace loop {
bool readFile(const std::string& path, std::vector<uint8_t>& out);
bool readText(const std::string& path, std::string& out);
bool writeFile(const std::string& path, const void* data, size_t size);
bool writeText(const std::string& path, const std::string& text);
std::string directoryOf(const std::string& path);  // "a/b/c.txt" -> "a/b/"
std::string joinPath(const std::string& dir, const std::string& name);
}  // namespace loop
