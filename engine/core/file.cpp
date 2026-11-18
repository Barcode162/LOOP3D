#include "engine/core/file.h"

#include <cstdio>

namespace loop {

bool readFile(const std::string& path, std::vector<uint8_t>& out) {
  // Read in chunks, never trusting the size a seek reports: a folder opens like a file on some systems and
  // reports a huge size (asking for that much memory crashed `loop check <folder>`); reading it fails instead.
  FILE* f = std::fopen(path.c_str(), "rb");
  if (!f) return false;
  out.clear();
  uint8_t chunk[65536];
  size_t got;
  while ((got = std::fread(chunk, 1, sizeof chunk, f)) > 0) out.insert(out.end(), chunk, chunk + got);
  bool ok = !std::ferror(f);
  std::fclose(f);
  return ok;
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
