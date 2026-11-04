#include "engine/core/json.h"

#include <cmath>
#include <cstdio>
#include <cctype>
#include <cstdlib>

namespace loop {

static const Json kNull;

const Json& Json::operator[](size_t i) const { return type_ == Type::kArray && i < a_.size() ? a_[i] : kNull; }
const Json& Json::operator[](const std::string& key) const {
  if (type_ != Type::kObject) return kNull;
  auto it = o_.find(key);
  return it == o_.end() ? kNull : it->second;
}

namespace {
struct Parser {
  std::string_view t;
  size_t i = 0;
  std::string err;
  void ws() { while (i < t.size() && (t[i] == ' ' || t[i] == '\n' || t[i] == '\r' || t[i] == '\t')) ++i; }
  bool fail(const char* m) { if (err.empty()) err = std::string(m) + " at " + std::to_string(i); return false; }
  static void utf8(std::string& s, unsigned cp) {
    if (cp < 0x80) s += static_cast<char>(cp);
    else if (cp < 0x800) s += static_cast<char>(0xc0 | (cp >> 6)), s += static_cast<char>(0x80 | (cp & 0x3f));
    else if (cp < 0x10000) s += static_cast<char>(0xe0 | (cp >> 12)), s += static_cast<char>(0x80 | ((cp >> 6) & 0x3f)), s += static_cast<char>(0x80 | (cp & 0x3f));
    else s += static_cast<char>(0xf0 | (cp >> 18)), s += static_cast<char>(0x80 | ((cp >> 12) & 0x3f)), s += static_cast<char>(0x80 | ((cp >> 6) & 0x3f)), s += static_cast<char>(0x80 | (cp & 0x3f));
  }
  bool str(std::string& out) {
    if (t[i] != '"') return fail("expected string");
    ++i;
    while (i < t.size() && t[i] != '"') {
      char c = t[i++];
      if (c != '\\') { out += c; continue; }
      if (i >= t.size()) return fail("bad escape");
      char e = t[i++];
      switch (e) {
        case 'n': out += '\n'; break;
        case 't': out += '\t'; break;
        case 'r': out += '\r'; break;
        case 'b': out += '\b'; break;
        case 'f': out += '\f'; break;
        case 'u': {
          if (i + 4 > t.size()) return fail("bad \\u");
          unsigned cp = static_cast<unsigned>(std::strtoul(std::string(t.substr(i, 4)).c_str(), nullptr, 16));
          i += 4;
          if (cp >= 0xd800 && cp < 0xdc00 && i + 6 <= t.size() && t[i] == '\\' && t[i + 1] == 'u') {
            unsigned lo = static_cast<unsigned>(std::strtoul(std::string(t.substr(i + 2, 4)).c_str(), nullptr, 16));
            i += 6;
            cp = 0x10000 + ((cp - 0xd800) << 10) + (lo - 0xdc00);
          }
          utf8(out, cp);
          break;
        }
        default: out += e;
      }
    }
    if (i >= t.size()) return fail("unterminated string");
    ++i;
    return true;
  }
  bool value(Json& out, int depth) {
    if (depth > 256) return fail("too deep");
    ws();
    if (i >= t.size()) return fail("unexpected end");
    char c = t[i];
    if (c == '{') {
      ++i;
      out = Json::object();
      ws();
      if (i < t.size() && t[i] == '}') { ++i; return true; }
      for (;;) {
        ws();
        std::string k;
        if (i >= t.size() || !str(k)) return fail("expected key");
        ws();
        if (i >= t.size() || t[i] != ':') return fail("expected :");
        ++i;
        Json v;
        if (!value(v, depth + 1)) return false;
        out.set(k, std::move(v));
        ws();
        if (i < t.size() && t[i] == ',') { ++i; continue; }
        if (i < t.size() && t[i] == '}') { ++i; return true; }
        return fail("expected , or }");
      }
    }
    if (c == '[') {
      ++i;
      out = Json::array();
      ws();
      if (i < t.size() && t[i] == ']') { ++i; return true; }
      for (;;) {
        Json v;
        if (!value(v, depth + 1)) return false;
        out.push(std::move(v));
        ws();
        if (i < t.size() && t[i] == ',') { ++i; continue; }
        if (i < t.size() && t[i] == ']') { ++i; return true; }
        return fail("expected , or ]");
      }
    }
    if (c == '"') {
      std::string s;
      if (!str(s)) return false;
      out = Json(std::move(s));
      return true;
    }
    if (t.substr(i, 4) == "true") { i += 4; out = Json(true); return true; }
    if (t.substr(i, 5) == "false") { i += 5; out = Json(false); return true; }
    if (t.substr(i, 4) == "null") { i += 4; out = Json(); return true; }
    size_t start = i;
    while (i < t.size() && (std::isdigit(static_cast<unsigned char>(t[i])) || t[i] == '-' || t[i] == '+' || t[i] == '.' || t[i] == 'e' || t[i] == 'E')) ++i;
    if (start == i) return fail("unexpected character");
    out = Json(std::strtod(std::string(t.substr(start, i - start)).c_str(), nullptr));
    return true;
  }
};
}  // namespace

bool Json::parse(std::string_view text, Json& out, std::string* error) {
  Parser p{text, 0, {}};
  bool ok = p.value(out, 0);
  if (ok) {
    p.ws();
    if (p.i != text.size()) ok = p.fail("trailing characters");
  }
  if (!ok && error) *error = p.err;
  return ok;
}

static void escape(std::string& s, const std::string& v) {
  s += '"';
  for (char c : v) {
    switch (c) {
      case '"': s += "\\\""; break;
      case '\\': s += "\\\\"; break;
      case '\n': s += "\\n"; break;
      case '\t': s += "\\t"; break;
      case '\r': s += "\\r"; break;
      default:
        if (static_cast<unsigned char>(c) < 0x20) {
          char b[8];
          std::snprintf(b, sizeof b, "\\u%04x", c);
          s += b;
        } else s += c;
    }
  }
  s += '"';
}

void Json::dumpTo(std::string& s, int indent, int depth) const {
  auto nl = [&](int d) {
    if (indent < 0) return;
    s += '\n';
    s.append(static_cast<size_t>(indent * d), ' ');
  };
  switch (type_) {
    case Type::kNull: s += "null"; break;
    case Type::kBool: s += b_ ? "true" : "false"; break;
    case Type::kNumber: {
      char b[32];
      if (std::isfinite(n_) && n_ == std::floor(n_) && std::fabs(n_) < 1e15) std::snprintf(b, sizeof b, "%.0f", n_);
      else if (std::isfinite(n_)) std::snprintf(b, sizeof b, "%.9g", n_);
      else std::snprintf(b, sizeof b, "null");
      s += b;
      break;
    }
    case Type::kString: escape(s, s_); break;
    case Type::kArray: {
      s += '[';
      for (size_t k = 0; k < a_.size(); ++k) {
        if (k) s += ',';
        nl(depth + 1);
        a_[k].dumpTo(s, indent, depth + 1);
      }
      if (!a_.empty()) nl(depth);
      s += ']';
      break;
    }
    case Type::kObject: {
      s += '{';
      bool first = true;
      for (auto& [k, v] : o_) {
        if (!first) s += ',';
        first = false;
        nl(depth + 1);
        escape(s, k);
        s += indent < 0 ? ":" : ": ";
        v.dumpTo(s, indent, depth + 1);
      }
      if (!o_.empty()) nl(depth);
      s += '}';
      break;
    }
  }
}

std::string Json::dump(int indent) const {
  std::string s;
  dumpTo(s, indent, 0);
  return s;
}

}  // namespace loop
