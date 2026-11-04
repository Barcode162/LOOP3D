#pragma once
// JSON values: parse and write (for glTF, settings, tool output).
#include <map>
#include <memory>
#include <string>
#include <string_view>
#include <vector>

namespace loop {

class Json {
 public:
  enum class Type { kNull, kBool, kNumber, kString, kArray, kObject };
  Json() = default;
  Json(bool b) : type_(Type::kBool), b_(b) {}
  Json(double n) : type_(Type::kNumber), n_(n) {}
  Json(int n) : type_(Type::kNumber), n_(n) {}
  Json(const char* s) : type_(Type::kString), s_(s) {}
  Json(std::string s) : type_(Type::kString), s_(std::move(s)) {}
  static Json array() { Json j; j.type_ = Type::kArray; return j; }
  static Json object() { Json j; j.type_ = Type::kObject; return j; }

  Type type() const { return type_; }
  bool isNull() const { return type_ == Type::kNull; }
  bool isNumber() const { return type_ == Type::kNumber; }
  bool isString() const { return type_ == Type::kString; }
  bool isArray() const { return type_ == Type::kArray; }
  bool isObject() const { return type_ == Type::kObject; }
  double number(double fallback = 0) const { return type_ == Type::kNumber ? n_ : fallback; }
  bool boolean(bool fallback = false) const { return type_ == Type::kBool ? b_ : fallback; }
  const std::string& string() const { return s_; }
  size_t size() const { return type_ == Type::kArray ? a_.size() : type_ == Type::kObject ? o_.size() : 0; }
  const Json& operator[](size_t i) const;
  const Json& operator[](const std::string& key) const;
  bool has(const std::string& key) const { return o_.count(key) != 0; }
  void push(Json v) { type_ = Type::kArray; a_.push_back(std::move(v)); }
  Json& set(const std::string& key, Json v) { type_ = Type::kObject; return o_[key] = std::move(v); }
  const std::vector<Json>& items() const { return a_; }
  const std::map<std::string, Json>& members() const { return o_; }

  static bool parse(std::string_view text, Json& out, std::string* error = nullptr);
  std::string dump(int indent = -1) const;

 private:
  void dumpTo(std::string& s, int indent, int depth) const;
  Type type_ = Type::kNull;
  bool b_ = false;
  double n_ = 0;
  std::string s_;
  std::vector<Json> a_;
  std::map<std::string, Json> o_;
};

}  // namespace loop
