#include "engine/core/file.h"

#include <cstdio>

namespace loop {

bool readFile(const std::string& path, std::vector<uint8_t>& out) {
  FILE* f = std::fopen(path.c_str(), "rb");
  if (!f) return false;
  std::fseek(f, 0, SEEK_END);
  long n = std::ftell(f);
  std::fseek(f, 0, SEEK_SET);
  if (n < 0) { std::fclose(f); return false; }
  out.resize(static_cast<size_t>(n));
  size_t got = n ? std::fread(out.data(), 1, out.size(), f) : 0;
  std::fclose(f);
  return got == out.size();
}

bool readText(const std::string& path, std::string& out) {
  std::vector<uint8_t> b;
  if (!readFile(path, b)) return false;
  out.assign(b.begin(), b.end());
  return true;
}

bool writeFile(const std::string& path, const void* data, size_t size) {
  FILE* f = std::fopen(path.c_str(), "wb");
  if (!f) return false;
  size_t put = size ? std::fwrite(data, 1, size, f) : 0;
  return std::fclose(f) == 0 && put == size;
}

bool writeText(const std::string& path, const std::string& text) { return writeFile(path, text.data(), text.size()); }

std::string directoryOf(const std::string& path) {
  size_t s = path.find_last_of("/\\");
  return s == std::string::npos ? std::string() : path.substr(0, s + 1);
}

std::string joinPath(const std::string& dir, const std::string& name) {
  if (dir.empty() || (!name.empty() && name[0] == '/')) return name;
  return dir.back() == '/' ? dir + name : dir + "/" + name;
}

}  // namespace loop
