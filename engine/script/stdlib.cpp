// Looped Script's own words: maths, text, lists, tables, randomness, noise,
// easing (for smooth motion), time and `say`.
#include <algorithm>
#include <cmath>

#include "engine/core/random.h"
#include "engine/geometry/noise.h"
#include "engine/render/scene.h"
#include "engine/script/vm.h"

namespace loop::script {

namespace {

Rng& rng() {
  static Rng r(12345);
  return r;
}

Value num(double d) { return Value::number(d); }
double arg(Args& a, int i, const char* name) { return a.needNum(i, name); }
List* selfList(Args& a) {
  List* l = a.self.as<List>();
  if (!l) a.vm.fail("this works on a list");
  return l;
}
Table* selfTable(Args& a) {
  Table* t = a.self.as<Table>();
  if (!t) a.vm.fail("this works on a table");
  return t;
}
const std::string& selfText(Args& a) {
  if (!a.self.isString()) a.vm.fail("this works on text");
  return a.self.str();
}
Value callFn(Args& a, const Value& fn, std::vector<Value> args) { return a.vm.call(fn, args); }

// ---- easing: t in 0..1 → 0..1 (Penner's curves and a critically damped spring)
double easeValue(const std::string& name, double t, Vm& vm) {
  t = std::clamp(t, 0.0, 1.0);
  const double c1 = 1.70158, c3 = c1 + 1;
  if (name == "linear") return t;
  if (name == "in" || name == "ease_in") return t * t * t;
  if (name == "out" || name == "ease_out") return 1 - std::pow(1 - t, 3);
  if (name == "in_out" || name == "ease_in_out" || name == "smooth") return t < 0.5 ? 4 * t * t * t : 1 - std::pow(-2 * t + 2, 3) / 2;
  if (name == "smoother") return t * t * t * (t * (t * 6 - 15) + 10);
  if (name == "sine") return -(std::cos(kPi * t) - 1) / 2;
  if (name == "back") return 1 + c3 * std::pow(t - 1, 3) + c1 * std::pow(t - 1, 2);
  if (name == "elastic") return t == 0 || t == 1 ? t : std::pow(2, -10 * t) * std::sin((t * 10 - 0.75) * (2 * kPi / 3)) + 1;
  if (name == "bounce") {
    const double n1 = 7.5625, d1 = 2.75;
    if (t < 1 / d1) return n1 * t * t;
    if (t < 2 / d1) return t -= 1.5 / d1, n1 * t * t + 0.75;
    if (t < 2.5 / d1) return t -= 2.25 / d1, n1 * t * t + 0.9375;
    return t -= 2.625 / d1, n1 * t * t + 0.984375;
  }
  if (name == "spring") return 1 - (1 + 8 * t) * std::exp(-8 * t);  // critically damped: no overshoot
  vm.fail("there's no easing called `" + name + "`", "try linear, in, out, in_out, smoother, sine, back, elastic, bounce or spring");
}

}  // namespace

void installStandardLibrary(Vm& vm) {
  vm.define("pi", num(kPi));
  vm.define("tau", num(kTwoPi));
  vm.define("infinity", num(INFINITY));

  vm.native("say", [](Args& a) {
    std::string s;
    for (int i = 0; i < a.count; ++i) s += (i ? " " : "") + a.vm.show(a.pos[i]);
    if (a.vm.onSay) a.vm.onSay(s);
    return Value();
  }, "say(text...)");
  vm.native("print", [](Args& a) {
    std::string s;
    for (int i = 0; i < a.count; ++i) s += (i ? " " : "") + a.vm.show(a.pos[i]);
    if (a.vm.onSay) a.vm.onSay(s);
    return Value();
  });
  vm.native("time", [](Args& a) { return num(a.vm.time()); }, "time(): seconds since the start");

  // ---- vectors and colours
  vm.native("vec", [](Args& a) {
    if (a.count == 1 && a.pos[0].isNumber()) return Value::vec(Vec3(static_cast<float>(a.pos[0].n)));
    return Value::vec({static_cast<float>(a.num(0, "x", 0)), static_cast<float>(a.num(1, "y", 0)), static_cast<float>(a.num(2, "z", 0))});
  }, "vec(x, y, z)");
  vm.native("rgb", [](Args& a) {
    auto c = [&](int i, const char* n) {
      double v = a.num(i, n, 0);
      return static_cast<float>(v > 1 ? v / 255 : v);  // 0..1 or 0..255
    };
    // colours are written as seen (sRGB); the engine works in linear light
    Vec3 s{c(0, "r"), c(1, "g"), c(2, "b")};
    return Value::vec({srgbToLinear(s.x), srgbToLinear(s.y), srgbToLinear(s.z)});
  }, "rgb(r, g, b): 0..1 or 0..255");
  vm.native("hex", [](Args& a) {
    std::string h = a.text(0, "code", "#ffffff");
    if (!h.empty() && h[0] == '#') h = h.substr(1);
    if (h.size() != 6) a.vm.fail("a hex colour has six digits, like hex(\"#ff8800\")");
    unsigned v = static_cast<unsigned>(std::stoul(h, nullptr, 16));
    return Value::vec({srgbToLinear(((v >> 16) & 255) / 255.0f), srgbToLinear(((v >> 8) & 255) / 255.0f), srgbToLinear((v & 255) / 255.0f)});
  }, "hex(\"#rrggbb\")");
  vm.native("kelvin", [](Args& a) { return Value::vec(kelvin(static_cast<float>(arg(a, 0, "temperature")))); }, "kelvin(2700 K): the colour of a warm bulb");
  vm.native("grey", [](Args& a) { return Value::vec(Vec3(srgbToLinear(static_cast<float>(a.num(0, "level", 0.5))))); });
  vm.native("dot", [](Args& a) { return num(dot(a.vec(0, "a", {}), a.vec(1, "b", {}))); });
  vm.native("cross", [](Args& a) { return Value::vec(cross(a.vec(0, "a", {}), a.vec(1, "b", {}))); });
  vm.native("distance", [](Args& a) { return num(length(a.vec(0, "a", {}) - a.vec(1, "b", {}))); });
  vm.native("normalize", [](Args& a) { return Value::vec(normalize(a.vec(0, "v", {}))); });
  vm.native("length", [](Args& a) {
    if (a.count && a.pos[0].isVec()) return num(length(a.pos[0].asVec()));
    if (a.count && a.pos[0].isList()) return num(static_cast<double>(a.pos[0].as<List>()->items.size()));
    if (a.count && a.pos[0].isString()) return num(static_cast<double>(a.pos[0].str().size()));
    if (a.count && a.pos[0].isTable()) return num(static_cast<double>(a.pos[0].as<Table>()->entries.size()));
    a.vm.fail("length() measures a vector, list, text or table");
  });

  // ---- maths
#define MATH1(name, expr) vm.native(name, [](Args& a) { double x = arg(a, 0, "x"); return num(expr); })
  MATH1("sqrt", std::sqrt(std::max(0.0, x)));
  MATH1("sin", std::sin(x));
  MATH1("cos", std::cos(x));
  MATH1("tan", std::tan(x));
  MATH1("asin", std::asin(std::clamp(x, -1.0, 1.0)));
  MATH1("acos", std::acos(std::clamp(x, -1.0, 1.0)));
  MATH1("floor", std::floor(x));
  MATH1("ceil", std::ceil(x));
  MATH1("round", std::round(x));
  MATH1("exp", std::exp(x));
  MATH1("log", std::log(x));
  MATH1("sign", x > 0 ? 1.0 : x < 0 ? -1.0 : 0.0);
  MATH1("degrees", x * 180 / kPi);
  MATH1("radians", x * kPi / 180);
#undef MATH1
  vm.native("abs", [](Args& a) {
    if (a.count && a.pos[0].isVec()) return Value::vec(vabs(a.pos[0].asVec()));
    return num(std::fabs(arg(a, 0, "x")));
  });
  vm.native("atan", [](Args& a) { return a.count > 1 ? num(std::atan2(arg(a, 0, "y"), arg(a, 1, "x"))) : num(std::atan(arg(a, 0, "x"))); });
  vm.native("min", [](Args& a) {
    if (a.count == 1 && a.pos[0].isList()) {
      auto& it = a.pos[0].as<List>()->items;
      if (it.empty()) return Value();
      double m = INFINITY;
      for (auto& v : it) m = std::min(m, v.n);
      return num(m);
    }
    double m = INFINITY;
    for (int i = 0; i < a.count; ++i) m = std::min(m, arg(a, i, nullptr));
    return num(m);
  });
  vm.native("max", [](Args& a) {
    if (a.count == 1 && a.pos[0].isList()) {
      auto& it = a.pos[0].as<List>()->items;
      if (it.empty()) return Value();
      double m = -INFINITY;
      for (auto& v : it) m = std::max(m, v.n);
      return num(m);
    }
    double m = -INFINITY;
    for (int i = 0; i < a.count; ++i) m = std::max(m, arg(a, i, nullptr));
    return num(m);
  });
  vm.native("clamp", [](Args& a) { return num(std::clamp(arg(a, 0, "x"), a.num(1, "low", 0), a.num(2, "high", 1))); }, "clamp(x, low, high)");
  vm.native("lerp", [](Args& a) {
    double t = arg(a, 2, "t");
    const Value* x = a.get(0, "from");
    const Value* y = a.get(1, "to");
    if (x && y && x->isVec() && y->isVec()) return Value::vec(vlerp(x->asVec(), y->asVec(), static_cast<float>(t)));
    double p = arg(a, 0, "from"), q = arg(a, 1, "to");
    return num(p + (q - p) * t);
  }, "lerp(from, to, t)");
  vm.native("smoothstep", [](Args& a) { return num(smoothstep(static_cast<float>(arg(a, 0, "edge0")), static_cast<float>(arg(a, 1, "edge1")), static_cast<float>(arg(a, 2, "x")))); });
  vm.native("ease", [](Args& a) { return num(easeValue(a.text(1, "curve", "smooth"), arg(a, 0, "t"), a.vm)); },
            "ease(t, curve): linear, in, out, in_out, smoother, sine, back, elastic, bounce, spring");
  // a value that follows another smoothly: spring(current, target, speed, dt)
  vm.native("approach", [](Args& a) {
    double k = 1 - std::exp(-a.num(2, "speed", 8) * a.num(3, "dt", 1.0 / 60));
    const Value* c = a.get(0, "current");
    const Value* t = a.get(1, "target");
    if (c && t && c->isVec() && t->isVec()) return Value::vec(vlerp(c->asVec(), t->asVec(), static_cast<float>(k)));
    double x = arg(a, 0, "current");
    return num(x + (arg(a, 1, "target") - x) * k);
  }, "approach(current, target, speed, dt): eases toward a target, never overshooting");

  // ---- randomness and noise
  vm.native("seed", [](Args& a) { rng() = Rng(static_cast<uint64_t>(arg(a, 0, "seed"))); return Value(); });
  vm.native("random", [](Args& a) {
    if (a.count == 0) return num(rng().uniform());
    if (a.count == 1) return num(rng().uniform() * arg(a, 0, "high"));
    return num(a.num(0, "low", 0) + rng().uniform() * (a.num(1, "high", 1) - a.num(0, "low", 0)));
  }, "random(), random(high), random(low, high)");
  vm.native("random_int", [](Args& a) {
    int lo = static_cast<int>(a.num(0, "low", 0)), hi = static_cast<int>(a.num(1, "high", 1));
    return num(lo + rng().below(hi - lo + 1));
  }, "random_int(low, high): both ends included");
  vm.native("chance", [](Args& a) { return Value::boolean(rng().uniform() < arg(a, 0, "probability")); }, "chance(30%): true 30% of the time");
  vm.native("noise", [](Args& a) {
    Vec3 p{static_cast<float>(a.num(0, "x", 0)), static_cast<float>(a.num(1, "y", 0)), static_cast<float>(a.num(2, "z", 0))};
    if (a.count == 1 && a.pos[0].isVec()) p = a.pos[0].asVec();
    return num(noise::perlin(p * 1.0f + Vec3(0.5f, 0.31f, 0.77f)));
  }, "noise(x, y, z): smooth randomness, -1..1");
  vm.native("wave", [](Args& a) {
    double period = a.num(1, "period", 1);
    return num(std::sin(kTwoPi * a.num(0, "t", a.vm.time()) / std::max(period, 1e-6)));
  }, "wave(t, period): a smooth -1..1 swing");
  vm.native("flicker", [](Args& a) {
    double t = a.num(0, "t", a.vm.time()), speed = a.num(1, "speed", 8);
    double s = a.num(2, "seed", 0);
    return num(0.6 * noise::perlin(Vec3(static_cast<float>(t * speed), static_cast<float>(s), 0.3f)) +
               0.4 * noise::perlin(Vec3(static_cast<float>(t * speed * 2.7), static_cast<float>(s) + 9, 1.1f)));
  }, "flicker(t, speed, seed): a flame's unsteady -1..1");

  // ---- conversions
  vm.native("text", [](Args& a) { return a.vm.text(a.count ? a.vm.show(a.pos[0]) : ""); });
  vm.native("number", [](Args& a) {
    if (a.count && a.pos[0].isNumber()) return a.pos[0];
    std::string s = a.text(0, "text", "");
    char* end = nullptr;
    double d = std::strtod(s.c_str(), &end);
    return end && end != s.c_str() ? num(d) : Value();
  });
  vm.native("type", [](Args& a) { return a.vm.text(a.count ? a.vm.typeName(a.pos[0]) : "nothing"); });
  vm.native("range", [](Args& a) {
    double lo = 0, hi, step = 1;
    if (a.count <= 1) hi = arg(a, 0, "to");
    else lo = arg(a, 0, "from"), hi = arg(a, 1, "to"), step = a.num(2, "by", hi >= lo ? 1 : -1);
    if (step == 0) a.vm.fail("range with a step of 0 never ends");
    List* l = a.vm.newList();
    for (double x = lo; step > 0 ? x < hi : x > hi; x += step) {
      l->items.push_back(num(x));
      if (l->items.size() > 10000000) a.vm.fail("that range is too long");
    }
    return Value::object(l);
  }, "range(n) or range(from, to, by): to is not included");
  vm.native("list", [](Args& a) {
    List* l = a.vm.newList();
    for (int i = 0; i < a.count; ++i) l->items.push_back(a.pos[i]);
    return Value::object(l);
  });
  vm.native("keys", [](Args& a) {
    Table* t = a.count ? a.pos[0].as<Table>() : nullptr;
    if (!t) a.vm.fail("keys() needs a table");
    List* l = a.vm.newList();
    for (auto& [k, v] : t->entries) l->items.push_back(a.vm.text(k));
    return Value::object(l);
  });

  // ---- list methods
  vm.method("list", "push", [](Args& a) {
    for (int i = 0; i < a.count; ++i) selfList(a)->items.push_back(a.pos[i]);
    return a.self;
  });
  vm.method("list", "add", [](Args& a) {
    for (int i = 0; i < a.count; ++i) selfList(a)->items.push_back(a.pos[i]);
    return a.self;
  });
  vm.method("list", "pop", [](Args& a) {
    List* l = selfList(a);
    if (l->items.empty()) return Value();
    Value v = l->items.back();
    l->items.pop_back();
    return v;
  });
  vm.method("list", "insert", [](Args& a) {
    List* l = selfList(a);
    long i = static_cast<long>(arg(a, 0, "at"));
    if (i < 0) i += static_cast<long>(l->items.size()) + 1;
    i = std::clamp<long>(i, 0, static_cast<long>(l->items.size()));
    l->items.insert(l->items.begin() + i, a.count > 1 ? a.pos[1] : Value());
    return a.self;
  });
  vm.method("list", "remove", [](Args& a) {
    List* l = selfList(a);
    for (size_t i = 0; i < l->items.size(); ++i) {
      bool same = a.count && ((l->items[i].isString() && a.pos[0].isString() && l->items[i].str() == a.pos[0].str()) ||
                              (l->items[i].isNumber() && a.pos[0].isNumber() && l->items[i].n == a.pos[0].n) ||
                              (l->items[i].isObj() && l->items[i].o == a.pos[0].o));
      if (same) {
        l->items.erase(l->items.begin() + static_cast<long>(i));
        return Value::boolean(true);
      }
    }
    return Value::boolean(false);
  });
  vm.method("list", "remove_at", [](Args& a) {
    List* l = selfList(a);
    long i = static_cast<long>(arg(a, 0, "index"));
    if (i < 0) i += static_cast<long>(l->items.size());
    if (i < 0 || i >= static_cast<long>(l->items.size())) a.vm.fail("no item there to remove");
    Value v = l->items[i];
    l->items.erase(l->items.begin() + i);
    return v;
  });
  vm.method("list", "contains", [](Args& a) {
    for (auto& v : selfList(a)->items)
      if (a.count && a.vm.show(v) == a.vm.show(a.pos[0]) && a.vm.typeName(v) == a.vm.typeName(a.pos[0])) return Value::boolean(true);
    return Value::boolean(false);
  });
  vm.method("list", "index_of", [](Args& a) {
    auto& it = selfList(a)->items;
    for (size_t i = 0; i < it.size(); ++i)
      if (a.count && a.vm.show(it[i]) == a.vm.show(a.pos[0])) return num(static_cast<double>(i));
    return num(-1);
  });
  vm.method("list", "sort", [](Args& a) {
    List* l = selfList(a);
    Value key = a.count ? a.pos[0] : Value();
    std::vector<Value> items = l->items;
    std::vector<Value> keys = items;
    if (key.isCallable())
      for (auto& k : keys) k = callFn(a, key, {k});
    std::vector<size_t> idx(items.size());
    for (size_t i = 0; i < idx.size(); ++i) idx[i] = i;
    std::stable_sort(idx.begin(), idx.end(), [&](size_t x, size_t y) {
      const Value &p = keys[x], &q = keys[y];
      if (p.isNumber() && q.isNumber()) return p.n < q.n;
      return a.vm.show(p) < a.vm.show(q);
    });
    for (size_t i = 0; i < idx.size(); ++i) l->items[i] = items[idx[i]];
    return a.self;
  }, "list.sort() or list.sort(fn(x): x.distance)");
  vm.method("list", "reverse", [](Args& a) {
    std::reverse(selfList(a)->items.begin(), selfList(a)->items.end());
    return a.self;
  });
  vm.method("list", "map", [](Args& a) {
    List* out = a.vm.newList();
    Value fn = a.count ? a.pos[0] : Value();
    for (auto v : std::vector<Value>(selfList(a)->items)) out->items.push_back(callFn(a, fn, {v}));
    return Value::object(out);
  });
  vm.method("list", "filter", [](Args& a) {
    List* out = a.vm.newList();
    Value fn = a.count ? a.pos[0] : Value();
    for (auto v : std::vector<Value>(selfList(a)->items))
      if (callFn(a, fn, {v}).truthy()) out->items.push_back(v);
    return Value::object(out);
  });
  vm.method("list", "each", [](Args& a) {
    Value fn = a.count ? a.pos[0] : Value();
    for (auto v : std::vector<Value>(selfList(a)->items)) callFn(a, fn, {v});
    return a.self;
  });
  vm.method("list", "find", [](Args& a) {
    Value fn = a.count ? a.pos[0] : Value();
    for (auto v : std::vector<Value>(selfList(a)->items))
      if (callFn(a, fn, {v}).truthy()) return v;
    return Value();
  });
  vm.method("list", "sum", [](Args& a) {
    double s = 0;
    for (auto& v : selfList(a)->items) s += v.isNumber() ? v.n : 0;
    return num(s);
  });
  vm.method("list", "join", [](Args& a) {
    std::string sep = a.text(0, "with", ", "), s;
    auto& it = selfList(a)->items;
    for (size_t i = 0; i < it.size(); ++i) s += (i ? sep : "") + a.vm.show(it[i]);
    return a.vm.text(s);
  });
  vm.method("list", "pick", [](Args& a) {
    auto& it = selfList(a)->items;
    return it.empty() ? Value() : it[rng().below(static_cast<int>(it.size()))];
  }, "list.pick(): one item at random");
  vm.method("list", "shuffle", [](Args& a) {
    auto& it = selfList(a)->items;
    for (size_t i = it.size(); i > 1; --i) std::swap(it[i - 1], it[rng().below(static_cast<int>(i))]);
    return a.self;
  });
  vm.method("list", "slice", [](Args& a) {
    auto& it = selfList(a)->items;
    long n = static_cast<long>(it.size());
    long from = static_cast<long>(a.num(0, "from", 0)), to = static_cast<long>(a.num(1, "to", static_cast<double>(n)));
    if (from < 0) from += n;
    if (to < 0) to += n;
    from = std::clamp(from, 0L, n), to = std::clamp(to, from, n);
    List* out = a.vm.newList();
    out->items.assign(it.begin() + from, it.begin() + to);
    return Value::object(out);
  });
  vm.method("list", "copy", [](Args& a) {
    List* out = a.vm.newList();
    out->items = selfList(a)->items;
    return Value::object(out);
  });
  vm.method("list", "clear", [](Args& a) {
    selfList(a)->items.clear();
    return a.self;
  });

  // ---- text methods
  vm.method("text", "upper", [](Args& a) {
    std::string s = selfText(a);
    for (auto& c : s) c = static_cast<char>(std::toupper(static_cast<unsigned char>(c)));
    return a.vm.text(s);
  });
  vm.method("text", "lower", [](Args& a) {
    std::string s = selfText(a);
    for (auto& c : s) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    return a.vm.text(s);
  });
  vm.method("text", "contains", [](Args& a) { return Value::boolean(selfText(a).find(a.text(0, "part", "")) != std::string::npos); });
  vm.method("text", "starts_with", [](Args& a) { return Value::boolean(selfText(a).rfind(a.text(0, "part", ""), 0) == 0); });
  vm.method("text", "ends_with", [](Args& a) {
    const std::string& s = selfText(a);
    std::string p = a.text(0, "part", "");
    return Value::boolean(s.size() >= p.size() && s.compare(s.size() - p.size(), p.size(), p) == 0);
  });
  vm.method("text", "replace", [](Args& a) {
    std::string s = selfText(a), from = a.text(0, "from", ""), to = a.text(1, "to", "");
    if (from.empty()) return a.self;
    size_t p = 0;
    while ((p = s.find(from, p)) != std::string::npos) s.replace(p, from.size(), to), p += to.size();
    return a.vm.text(s);
  });
  vm.method("text", "split", [](Args& a) {
    std::string s = selfText(a), sep = a.text(0, "by", " ");
    List* l = a.vm.newList();
    if (sep.empty()) {
      for (char c : s) l->items.push_back(a.vm.text(std::string(1, c)));
      return Value::object(l);
    }
    size_t p = 0, q;
    while ((q = s.find(sep, p)) != std::string::npos) {
      if (q > p || sep != " ") l->items.push_back(a.vm.text(s.substr(p, q - p)));
      p = q + sep.size();
    }
    if (p < s.size() || sep != " ") l->items.push_back(a.vm.text(s.substr(p)));
    return Value::object(l);
  });
  vm.method("text", "trim", [](Args& a) {
    std::string s = selfText(a);
    size_t b = s.find_first_not_of(" \t\n"), e = s.find_last_not_of(" \t\n");
    return a.vm.text(b == std::string::npos ? "" : s.substr(b, e - b + 1));
  });
  vm.method("text", "repeat", [](Args& a) {
    std::string s, p = selfText(a);
    for (int i = 0; i < static_cast<int>(arg(a, 0, "times")); ++i) s += p;
    return a.vm.text(s);
  });

  // ---- table methods
  vm.method("table", "keys", [](Args& a) {
    List* l = a.vm.newList();
    for (auto& [k, v] : selfTable(a)->entries) l->items.push_back(a.vm.text(k));
    return Value::object(l);
  });
  vm.method("table", "values", [](Args& a) {
    List* l = a.vm.newList();
    for (auto& [k, v] : selfTable(a)->entries) l->items.push_back(v);
    return Value::object(l);
  });
  vm.method("table", "has", [](Args& a) { return Value::boolean(selfTable(a)->find(a.text(0, "key", "")) != nullptr); });
  vm.method("table", "remove", [](Args& a) {
    Table* t = selfTable(a);
    std::string k = a.text(0, "key", "");
    auto it = t->index.find(k);
    if (it == t->index.end()) return Value::boolean(false);
    t->entries.erase(t->entries.begin() + static_cast<long>(it->second));
    t->index.clear();
    for (size_t i = 0; i < t->entries.size(); ++i) t->index[t->entries[i].first] = i;
    return Value::boolean(true);
  });
  vm.method("table", "size", [](Args& a) { return num(static_cast<double>(selfTable(a)->entries.size())); });

  // ---- vector methods
  vm.method("vec", "dot", [](Args& a) { return num(dot(a.self.asVec(), a.vec(0, "other", {}))); });
  vm.method("vec", "cross", [](Args& a) { return Value::vec(cross(a.self.asVec(), a.vec(0, "other", {}))); });
  vm.method("vec", "distance", [](Args& a) { return num(length(a.self.asVec() - a.vec(0, "other", {}))); });
  vm.method("vec", "lerp", [](Args& a) { return Value::vec(vlerp(a.self.asVec(), a.vec(0, "to", {}), static_cast<float>(arg(a, 1, "t")))); });
}

}  // namespace loop::script
