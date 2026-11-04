#pragma once
// Looped Script values. Numbers, true/false, nothing, vectors (x y z) and
// colours are kept inline; text, lists, tables, functions and engine things
// live on the script heap and are cleaned up by the garbage collector.
#include <cstdint>
#include <string>
#include <unordered_map>
#include <vector>

#include "engine/core/math.h"

namespace loop::script {

class Vm;
struct Obj;
struct Str;
struct List;
struct Table;
struct Closure;
struct Native;
struct Thing;

enum class Type : uint8_t { kNothing, kBool, kNumber, kVec, kObj, kUnset };

struct Value {
  Type type = Type::kNothing;
  bool b = false;
  double n = 0;
  float v[3] = {0, 0, 0};
  Obj* o = nullptr;

  Value() = default;
  static Value number(double x) { Value r; r.type = Type::kNumber; r.n = x; return r; }
  static Value boolean(bool x) { Value r; r.type = Type::kBool; r.b = x; return r; }
  static Value vec(Vec3 x) { Value r; r.type = Type::kVec; r.v[0] = x.x, r.v[1] = x.y, r.v[2] = x.z; return r; }
  static Value object(Obj* x) { Value r; r.type = Type::kObj; r.o = x; return r; }
  static Value unset() { Value r; r.type = Type::kUnset; return r; }
  bool isNothing() const { return type == Type::kNothing; }
  bool isNumber() const { return type == Type::kNumber; }
  bool isVec() const { return type == Type::kVec; }
  bool isBool() const { return type == Type::kBool; }
  bool isObj() const { return type == Type::kObj; }
  bool isUnset() const { return type == Type::kUnset; }
  Vec3 asVec() const { return {v[0], v[1], v[2]}; }
  bool truthy() const { return !(type == Type::kNothing || (type == Type::kBool && !b) || type == Type::kUnset); }
  template <class T> T* as() const;  // the heap object as T, or null
  bool isString() const;
  bool isList() const;
  bool isTable() const;
  bool isCallable() const;
  const std::string& str() const;  // only when isString()
};

enum class ObjKind : uint8_t { kString, kList, kTable, kProto, kClosure, kCell, kNative, kThing, kBound };

struct Obj {
  ObjKind kind;
  bool marked = false;
  Obj* next = nullptr;
  explicit Obj(ObjKind k) : kind(k) {}
  virtual ~Obj() = default;
};

struct Str : Obj {
  std::string s;
  explicit Str(std::string v) : Obj(ObjKind::kString), s(std::move(v)) {}
};
struct List : Obj {
  std::vector<Value> items;
  List() : Obj(ObjKind::kList) {}
};
// A table keeps its keys in the order they were added.
struct Table : Obj {
  std::vector<std::pair<std::string, Value>> entries;
  std::unordered_map<std::string, size_t> index;
  Table() : Obj(ObjKind::kTable) {}
  Value* find(const std::string& k) {
    auto it = index.find(k);
    return it == index.end() ? nullptr : &entries[it->second].second;
  }
  void set(const std::string& k, Value v) {
    if (Value* p = find(k)) { *p = v; return; }
    index[k] = entries.size();
    entries.push_back({k, v});
  }
};
struct Cell : Obj {
  Value value;
  Cell() : Obj(ObjKind::kCell) {}
};

// Compiled code of one function.
struct Proto : Obj {
  std::string name;
  std::vector<uint8_t> code;
  std::vector<int> lines;        // source line of each byte
  std::vector<Value> constants;
  std::vector<std::string> params;
  int arity = 0, slots = 0;      // parameters, local slots
  struct Capture { bool fromLocal; int index; };
  std::vector<Capture> captures;
  std::string file;
  Proto() : Obj(ObjKind::kProto) {}
};
struct Closure : Obj {
  Proto* proto;
  std::vector<Cell*> cells;
  explicit Closure(Proto* p) : Obj(ObjKind::kClosure), proto(p) {}
};

// Arguments handed to a built-in: positional, then named.
struct Args {
  Vm& vm;
  const Value* pos;
  int count;
  const std::vector<std::pair<std::string, Value>>& named;
  int line;
  Value self;  // for methods
  const Value* get(int i, const char* name) const;  // named wins over position
  double num(int i, const char* name, double fallback) const;
  double needNum(int i, const char* name) const;
  Vec3 vec(int i, const char* name, Vec3 fallback) const;
  std::string text(int i, const char* name, const std::string& fallback) const;
  bool flag(int i, const char* name, bool fallback) const;
  bool has(int i, const char* name) const { return get(i, name) != nullptr; }
};
using NativeFn = Value (*)(Args&);
struct Native : Obj {
  std::string name;
  NativeFn fn;
  std::string help;  // "box(size, at, material)": shown in errors
  Native(std::string n, NativeFn f, std::string h) : Obj(ObjKind::kNative), name(std::move(n)), fn(f), help(std::move(h)) {}
};

// Something the engine owns (a shape in the scene, a light, a sound),
// seen from scripts with fields and methods.
struct ThingClass {
  std::string name;
  Value (*get)(Vm&, Thing&, const std::string& field, bool& found) = nullptr;
  bool (*set)(Vm&, Thing&, const std::string& field, const Value& v) = nullptr;
  std::vector<std::string> fields;  // for "did you mean"
};
struct Thing : Obj {
  const ThingClass* cls;
  int id;  // index into the engine's own list
  Table* extra = nullptr;  // fields scripts add themselves
  Thing(const ThingClass* c, int i) : Obj(ObjKind::kThing), cls(c), id(i) {}
};
// A method bound to the value it was read from: `list.push`.
struct Bound : Obj {
  Value self;
  Value fn;
  Bound(Value s, Value f) : Obj(ObjKind::kBound), self(s), fn(f) {}
};

template <class T> T* Value::as() const {
  if (type != Type::kObj || !o) return nullptr;
  if constexpr (std::is_same_v<T, Str>) return o->kind == ObjKind::kString ? static_cast<T*>(o) : nullptr;
  if constexpr (std::is_same_v<T, List>) return o->kind == ObjKind::kList ? static_cast<T*>(o) : nullptr;
  if constexpr (std::is_same_v<T, Table>) return o->kind == ObjKind::kTable ? static_cast<T*>(o) : nullptr;
  if constexpr (std::is_same_v<T, Closure>) return o->kind == ObjKind::kClosure ? static_cast<T*>(o) : nullptr;
  if constexpr (std::is_same_v<T, Native>) return o->kind == ObjKind::kNative ? static_cast<T*>(o) : nullptr;
  if constexpr (std::is_same_v<T, Thing>) return o->kind == ObjKind::kThing ? static_cast<T*>(o) : nullptr;
  if constexpr (std::is_same_v<T, Bound>) return o->kind == ObjKind::kBound ? static_cast<T*>(o) : nullptr;
  if constexpr (std::is_same_v<T, Cell>) return o->kind == ObjKind::kCell ? static_cast<T*>(o) : nullptr;
  if constexpr (std::is_same_v<T, Proto>) return o->kind == ObjKind::kProto ? static_cast<T*>(o) : nullptr;
  return nullptr;
}
inline bool Value::isString() const { return as<Str>() != nullptr; }
inline bool Value::isList() const { return as<List>() != nullptr; }
inline bool Value::isTable() const { return as<Table>() != nullptr; }
inline bool Value::isCallable() const { return as<Closure>() || as<Native>() || as<Bound>(); }
inline const std::string& Value::str() const { return as<Str>()->s; }

}  // namespace loop::script
