// The world words: keys (registered by installSceneApi, scene_api.cpp).
//
//   let crate = box size: 1, at: vec(-2, 0.5, 0)
//   clip crate, loop: yes
//     key 0 s, at: vec(-2, 0.5, 0)
//     key 2 s, at: vec(2, 0.5, 0), turn: 90 deg
//
// A clip belongs to a thing, a lamp or a group; each `key` line under it pins
// values to a moment, and the stage fills in every moment between
// (engine/anim/clip.h). The clip is a value too: play(), reverse(), stop().
#include "engine/script/words_internal.h"

#include <cmath>

namespace loop::script {

namespace {

bool same(Vec3 a, Vec3 b) { return a.x == b.x && a.y == b.y && a.z == b.z; }

}  // namespace

bool Stage::showClip(int ci) {
  Playing& c = clips[ci];
  float v[4] = {0, 0, 0, 0};
  auto has = [&](const char* name, int size) {
    const anim::Channel* ch = c.clip.find(name);
    return ch && ch->size == size && c.clip.sample(*ch, c.moment, v);
  };
  if (c.kind == 't') {
    if (c.target < 0 || c.target >= static_cast<int>(placed.size())) return false;
    Placed& p = placed[c.target];
    Vec3 at = p.position, turn = p.rotation, scale = p.scale;
    bool visible = p.visible;
    if (has("at", 3)) at = Vec3(v[0], v[1], v[2]);
    if (has("rotation", 3)) turn = Vec3(v[0], v[1], v[2]);
    if (has("turn", 1)) turn.x = v[0];
    if (has("tilt", 1)) turn.y = v[0];
    if (has("roll", 1)) turn.z = v[0];
    if (has("scale", 3)) scale = Vec3(v[0], v[1], v[2]);
    if (has("visible", 1)) visible = v[0] > 0.5f;
    if (same(at, p.position) && same(turn, p.rotation) && same(scale, p.scale) && visible == p.visible && !p.useQuat) return false;
    p.position = at, p.rotation = turn, p.scale = scale, p.visible = visible, p.useQuat = false;
    updateTransform(c.target);
    return true;
  }
  if (c.kind == 'l') {
    if (c.target < 0 || c.target >= static_cast<int>(scene.lights.size())) return false;
    Light& l = scene.lights[c.target];
    float& bright = l.kind == Light::Kind::kSun ? l.lux : l.lumens;
    Vec3 at = l.position, color = l.color;
    float lumens = bright;
    if (has("at", 3)) at = Vec3(v[0], v[1], v[2]);
    if (has("lumens", 1)) lumens = std::max(0.0f, v[0]);
    if (has("color", 3)) color = unitLuminanceOr(Vec3(v[0], v[1], v[2]));
    if (has("kelvin", 1)) color = kelvin(v[0]);
    if (same(at, l.position) && same(color, l.color) && lumens == bright) return false;
    l.position = at, l.color = color, bright = lumens;
    // a sun changes the sky too; a lamp only itself
    if (l.kind == Light::Kind::kSun) ++version;
    else ++lightsVersion;
    return true;
  }
  if (c.kind == 'g') {
    if (c.target < 0 || c.target >= static_cast<int>(groups.size())) return false;
    Vec3 at = groups[c.target].position;
    float turn = groups[c.target].turn;
    bool visible = groups[c.target].visible;
    if (has("at", 3)) at = Vec3(v[0], v[1], v[2]);
    if (has("turn", 1)) turn = v[0];
    if (has("visible", 1)) visible = v[0] > 0.5f;
    bool changed = false;
    if (!same(at, groups[c.target].position) || turn != groups[c.target].turn) placeGroup(c.target, at, turn), changed = true;
    if (visible != groups[c.target].visible) showGroup(c.target, visible), changed = true;
    return changed;
  }
  return false;
}

void Stage::seekClip(int ci, double moment) {
  if (ci < 0 || ci >= static_cast<int>(clips.size())) return;
  clips[ci].moment = moment;
  showClip(ci);
}

void Stage::updateClips(float dt) {
  for (size_t i = 0; i < clips.size(); ++i) {
    Playing& c = clips[i];
    // a thing that has come to rest is something to bump into where it now stands
    auto rests = [&] {
      if (c.kind == 't' && c.target < static_cast<int>(placed.size()) && placed[c.target].collide) collidersDirty = true;
      if (c.kind == 'g' && c.target < static_cast<int>(groups.size()))
        for (int part : groups[c.target].parts)
          if (placed[part].collide) collidersDirty = true;
      c.moving = false;
    };
    if (!c.playing || c.kind == 'n') {
      if (c.moving) rests();
      continue;
    }
    const double d = c.clip.duration();
    c.moment += static_cast<double>(dt) * c.speed;
    if (c.clip.repeat == anim::Repeat::kOnce) {
      if (c.speed >= 0 && c.moment >= d) c.moment = d, c.playing = false;
      else if (c.speed < 0 && c.moment <= 0) c.moment = 0, c.playing = false;
    } else {
      const double period = c.clip.repeat == anim::Repeat::kLoop ? d : 2 * d;
      if (period > 0) c.moment -= std::floor(c.moment / period) * period;
    }
    const bool changed = showClip(static_cast<int>(i));
    if (c.moving && !changed) rests();
    c.moving = changed;
  }
}

namespace words {

namespace {

Stage::Playing& clipOf(Vm& vm, Thing& t) {
  Stage& s = stageOf(vm);
  if (t.id < 0 || t.id >= static_cast<int>(s.clips.size())) vm.fail("that clip is gone");
  return s.clips[t.id];
}

void setRepeat(Vm& vm, anim::Clip& clip, const Value& v) {
  if (v.isString()) {
    const std::string w = v.str();
    if (w == "back and forth" || w == "back_and_forth" || w == "there and back") clip.repeat = anim::Repeat::kBackAndForth;
    else if (w == "yes" || w == "loop") clip.repeat = anim::Repeat::kLoop;
    else if (w == "no" || w == "once") clip.repeat = anim::Repeat::kOnce;
    else vm.fail("a clip's `loop` is yes, no or \"back and forth\", not \"" + w + "\"");
  } else clip.repeat = v.truthy() ? anim::Repeat::kLoop : anim::Repeat::kOnce;
}

Value getClip(Vm& vm, Thing& t, const std::string& f, bool& found) {
  Stage::Playing& c = clipOf(vm, t);
  found = true;
  if (f == "playing") return Value::boolean(c.playing);
  if (f == "time" || f == "moment") return Value::number(c.clip.wrap(c.moment));
  if (f == "speed") return Value::number(c.speed);
  if (f == "length") return Value::number(c.clip.duration());
  if (f == "loop") return c.clip.repeat == anim::Repeat::kBackAndForth ? vm.text("back and forth") : Value::boolean(c.clip.repeat == anim::Repeat::kLoop);
  if (f == "name") return vm.text(c.clip.name);
  found = false;
  return {};
}

bool setClip(Vm& vm, Thing& t, const std::string& f, const Value& v) {
  Stage& s = stageOf(vm);
  Stage::Playing& c = clipOf(vm, t);
  auto number = [&] {
    if (!v.isNumber()) vm.fail("a clip's `" + f + "` is a number");
    return v.n;
  };
  if (f == "playing") c.playing = v.truthy();
  else if (f == "time" || f == "moment") s.seekClip(t.id, std::max(0.0, number()));
  else if (f == "speed") c.speed = number();
  else if (f == "length") c.clip.length = std::max(0.0, number());
  else if (f == "loop") setRepeat(vm, c.clip, v);
  else return false;
  return true;
}

const ThingClass kClipClass{"clip", getClip, setClip, {"playing", "time", "speed", "length", "loop", "name"}};

// The values a key may pin, by what the clip is on: the name as written, the
// name kept, how many numbers, whether they are angles.
struct Keyable {
  const char* written;
  const char* kept;
  int size;
  bool angle;
};
const std::vector<Keyable>& keyables(char kind) {
  static const std::vector<Keyable> thing = {{"at", "at", 3, false},        {"position", "at", 3, false}, {"turn", "turn", 1, true},
                                             {"tilt", "tilt", 1, true},     {"roll", "roll", 1, true},    {"rotation", "rotation", 3, true},
                                             {"scale", "scale", 3, false},  {"visible", "visible", 1, false}};
  static const std::vector<Keyable> lamp = {{"at", "at", 3, false},         {"position", "at", 3, false},    {"lumens", "lumens", 1, false},
                                            {"brightness", "lumens", 1, false}, {"color", "color", 3, false}, {"colour", "color", 3, false},
                                            {"kelvin", "kelvin", 1, false}};
  static const std::vector<Keyable> group = {{"at", "at", 3, false}, {"position", "at", 3, false}, {"turn", "turn", 1, true}, {"visible", "visible", 1, false}};
  return kind == 't' ? thing : kind == 'l' ? lamp : group;
}

const char* kindName(char kind) { return kind == 't' ? "thing" : kind == 'l' ? "lamp" : kind == 'g' ? "group" : "clip"; }

// A script value as a key's numbers. False when it is neither a number, a vec nor yes/no.
bool numbersOf(const Value& v, float* out, int& size) {
  if (v.isNumber()) return out[0] = static_cast<float>(v.n), size = 1, true;
  if (v.isBool()) return out[0] = v.b ? 1.0f : 0.0f, size = 1, true;
  if (v.isVec()) return out[0] = v.v[0], out[1] = v.v[1], out[2] = v.v[2], size = 3, true;
  return false;
}

}  // namespace

void registerAnim(Vm& vm) {
  // ---- clip: movement written as keys, on a thing, a lamp or a group
  vm.native("clip", [](Args& a) {
    Stage& s = stageOf(a.vm);
    const Value* on = a.get(0, "on");
    Stage::Playing p;
    p.line = a.line;
    Thing* t = on ? on->as<Thing>() : nullptr;
    if (t && t->cls == &kShapeClass) {
      p.kind = 't', p.target = t->id;
      if (placedOf(a.vm, *t).body >= 0) a.vm.fail("a solid thing is moved by physics, so keys can't move it too", "leave out `solid` for a thing that moves by keys");
    } else if (t && t->cls == &kLightClass) p.kind = 'l', p.target = t->id;
    else if (t && t->cls == &kGroupClass) p.kind = 'g', p.target = t->id;
    else if (on && on->isString()) p.kind = 'n', p.clip.name = on->str();
    else
      a.vm.fail("a clip needs what it moves: a thing, a lamp or a group",
                "name the thing first, like `let crate = box size: 1`, then `clip crate` with its `key` lines indented under it");
    p.clip.skeleton = a.text(-1, "for", "");
    p.clip.length = std::max(0.0, a.num(-1, "length", 0));
    p.clip.rate = a.num(-1, "rate", 30);
    if (p.clip.rate <= 0) a.vm.fail("a clip's `rate` is frames a second, above 0");
    if (const Value* loop = a.get(-1, "loop")) setRepeat(a.vm, p.clip, *loop);
    p.starts = a.flag(-1, "play", true);
    p.playing = p.starts && p.kind != 'n';
    p.speed = p.startSpeed = a.num(-1, "speed", 1);
    s.clips.push_back(std::move(p));
    const int ci = static_cast<int>(s.clips.size() - 1);
    if (const Value* block = a.get(-1, "of")) {
      // the lines under it: each `key` adds to this clip (taken off the list again even if a line fails)
      struct Reading {
        Stage& s;
        ~Reading() { s.clipsBeingRead.pop_back(); }
      } reading{s};
      s.clipsBeingRead.push_back(ci);
      a.vm.call(*block, {});
    }
    if (s.clips[ci].playing) s.showClip(ci);  // it starts on its first key
    return Value::object(a.vm.newThing(&kClipClass, ci));
  }, "clip(thing, length, rate: 30, loop: yes | no | \"back and forth\", play: yes, speed: 1) with `key` lines indented under it: movement by keys for a thing, a lamp or a group; gives back the clip (.play(), .reverse(), .stop(), .playing, .time, .speed)");

  // ---- key: values pinned to a moment
  vm.native("key", [](Args& a) {
    Stage& s = stageOf(a.vm);
    if (s.clipsBeingRead.empty()) a.vm.fail("a key belongs to a clip", "put it on an indented line under a `clip` line, like `clip crate` then `  key 1 s, at: vec(2, 0, 0)`");
    Stage::Playing& p = s.clips[s.clipsBeingRead.back()];
    const Value* tv = a.get(0, "time");
    if (!tv || !tv->isNumber()) a.vm.fail("a key starts with its moment, like `key 1.5 s, at: vec(2, 0, 0)`");
    if (tv->n < 0) a.vm.fail("a key's moment can't be before the clip starts (0 s)");
    const double time = p.clip.snapped(tv->n);
    // how the values arrive here from the key before
    anim::Way way = anim::Way::kSmooth;
    Ease ease = Ease::kInOut;
    if (const Value* e = a.get(-1, "ease")) {
      const std::string w = e->isString() ? e->str() : "";
      if (!anim::wayByName(w, way, ease)) {
        std::string c = closest(w, {"smooth", "line", "hold", "in", "out", "in_out", "smoother", "sine", "back", "elastic", "bounce", "spring"});
        a.vm.fail("a key's `ease` is how it arrives: \"smooth\", \"line\", \"hold\", or an ease like \"in\", \"out\", \"back\", \"bounce\"", c.empty() ? "" : "did you mean \"" + c + "\"?");
      }
    }
    const Value* in = a.get(-1, "in");
    const Value* out = a.get(-1, "out");
    auto setting = [](const std::string& n) { return n == "time" || n == "ease" || n == "in" || n == "out" || n == "of"; };
    int values = 0;
    for (const auto& named : a.named) values += setting(named.first) ? 0 : 1;
    if (values == 0) a.vm.fail("a key needs a value to pin, like `key 1 s, at: vec(2, 0, 0)`");
    if ((in || out) && values != 1) a.vm.fail("a key with its own `in` or `out` slopes holds one value", "write the other values on their own `key` lines at the same moment");
    for (const auto& [written, value] : a.named) {
      if (setting(written)) continue;
      float v[4] = {0, 0, 0, 0};
      int size = 0;
      if (!numbersOf(value, v, size)) a.vm.fail("a key's `" + written + "` should be a number or a vec(…), not " + a.vm.typeName(value));
      std::string kept = written;
      bool angle = false;
      if (p.kind == 'n') {
        // a clip by name is for a skeleton: a vec turns a bone (bend, side, twist), "<bone>_at" moves it
        angle = size == 3 && !(written.size() > 3 && written.compare(written.size() - 3, 3, "_at") == 0);
      } else {
        const Keyable* k = nullptr;
        std::vector<std::string> names;
        for (const Keyable& x : keyables(p.kind)) {
          names.push_back(x.written);
          if (written == x.written) k = &x;
        }
        if (!k) {
          std::string c = closest(written, names);
          a.vm.fail(std::string("a ") + kindName(p.kind) + " has no `" + written + "` to key", c.empty() ? "it has: at, turn, tilt, roll, scale, visible (a lamp: at, lumens, color, kelvin)" : "did you mean `" + c + "`?");
        }
        kept = k->kept, angle = k->angle;
        if (kept == "scale" && size == 1) v[1] = v[2] = v[0], size = 3;  // one number sizes it evenly
        if (size != k->size)
          a.vm.fail(std::string("a key's `") + written + "` should be " + (k->size == 3 ? "a vec(…) of three numbers" : "one number"));
      }
      anim::Channel& ch = p.clip.channel(kept, size, angle);
      if (ch.size != size) a.vm.fail("`" + written + "` was a " + (ch.size == 3 ? "vec" : "number") + " in this clip's earlier keys; a value keeps its shape");
      anim::Key& k = ch.set(time, v);
      k.way = kept == "visible" ? anim::Way::kHold : way, k.ease = ease;
      k.free = false;
      if (in || out) {
        // slopes given by hand, for the one value on this line
        float iv[4] = {0, 0, 0, 0}, ov[4] = {0, 0, 0, 0};
        int is = size, os = size;
        if ((in && (!numbersOf(*in, iv, is) || is != size)) || (out && (!numbersOf(*out, ov, os) || os != size)))
          a.vm.fail("a key's `in` and `out` are slopes shaped like its value (a number for a number, a vec for a vec), in units a second");
        k.free = true;
        for (int c = 0; c < size; ++c) k.in[c] = in ? iv[c] : ov[c], k.out[c] = out ? ov[c] : iv[c];
      }
    }
    return Value();
  }, "key(moment, at | turn | tilt | roll | scale | visible (a lamp: at, lumens, color, kelvin), ease: \"smooth\" | \"line\" | \"hold\" | an ease's name, in, out): under a `clip` line, pins values to a moment; the engine fills in between");

  // ---- skeleton: bones written down, one a line
  vm.native("skeleton", [](Args& a) {
    Stage& s = stageOf(a.vm);
    const std::string name = a.text(0, "name", "");
    if (name.empty()) a.vm.fail("a skeleton needs a name, like `skeleton \"human\"` with its `bone` lines indented under it");
    const Value* block = a.get(-1, "of");
    if (!block) a.vm.fail("a skeleton needs its bones: `bone` lines indented under it");
    if (s.readingBones) a.vm.fail("a skeleton can't be written inside another skeleton");
    {
      struct Reading {
        Stage& s;
        ~Reading() { s.readingBones = false; }
      } reading{s};
      s.bonesBeingRead.clear();
      s.readingBones = true;
      a.vm.call(*block, {});
    }
    std::vector<anim::BoneSpec> specs = std::move(s.bonesBeingRead);
    s.bonesBeingRead.clear();
    if (a.flag(-1, "mirror", true)) anim::addMirrors(specs);
    anim::Skeleton sk;
    std::string why;
    int line = 0;
    if (!anim::buildSkeleton(name, specs, sk, &why, &line)) a.vm.fail(why + (line > 0 ? " (the bone on line " + std::to_string(line) + ")" : ""));
    s.skeletons[name] = std::move(sk);
    return a.vm.text(name);
  }, "skeleton(name, mirror: yes) with `bone` lines indented under it: a skeleton by name; every bone called left_… gets its right_… twin");

  vm.native("bone", [](Args& a) {
    Stage& s = stageOf(a.vm);
    if (!s.readingBones) a.vm.fail("a bone belongs to a skeleton", "put it on an indented line under a `skeleton` line");
    anim::BoneSpec b;
    b.line = a.line;
    b.name = a.text(0, "name", "");
    if (b.name.empty()) a.vm.fail("a bone starts with its name, like `bone \"left_forearm\", parent: \"left_upper_arm\", from: vec(…), to: vec(…)`");
    b.parent = a.text(-1, "parent", "");
    b.from = a.vec(-1, "from", Vec3(0));
    b.to = a.vec(-1, "to", b.from + Vec3(0, 0.1f, 0));
    b.bends = a.vec(-1, "bends", Vec3(0, 0, -1));
    b.ring = static_cast<int>(a.num(-1, "ring", 1));
    if (b.ring < 1 || b.ring > 3) a.vm.fail("a bone's `ring` is 1 (the ones you move), 2 (the ones that follow) or 3 (the face)");
    b.free = a.flag(-1, "free", false);
    // how far it turns from rest, each as [least, most]; a turn not named does not happen
    const char* turns[3] = {"bend", "side", "twist"};
    for (int k = 0; k < 3; ++k) {
      const Value* v = a.get(-1, turns[k]);
      if (!v) continue;
      List* l = v->as<List>();
      if (!l || l->items.size() != 2 || !l->items[0].isNumber() || !l->items[1].isNumber())
        a.vm.fail(std::string("a bone's `") + turns[k] + "` is its least and most, like " + turns[k] + ": [-10 deg, 150 deg]");
      b.lo[k] = f32(l->items[0].n), b.hi[k] = f32(l->items[1].n);
    }
    for (const auto& named : a.named) {
      static const std::vector<std::string> known = {"name", "parent", "from", "to", "bends", "ring", "free", "bend", "side", "twist"};
      if (std::find(known.begin(), known.end(), named.first) != known.end()) continue;
      std::string c = closest(named.first, known);
      a.vm.fail("a bone has no `" + named.first + "`", c.empty() ? "it has: parent, from, to, bends, bend, side, twist, free, ring" : "did you mean `" + c + "`?");
    }
    s.bonesBeingRead.push_back(std::move(b));
    return Value();
  }, "bone(name, parent, from, to, bends, bend: [least, most], side: [..], twist: [..], free: no, ring: 1): under a `skeleton` line; where it starts and ends in the rest pose, the way a positive bend carries its tip, and how far it turns");

  // ---- bones: a skeleton shown, each bone a slim shape from its start to its tip
  vm.native("bones", [](Args& a) {
    Stage& s = stageOf(a.vm);
    const std::string name = a.text(0, "skeleton", "");
    auto found = s.skeletons.find(name);
    if (found == s.skeletons.end()) {
      std::vector<std::string> names;
      for (const auto& k : s.skeletons) names.push_back(k.first);
      std::string c = closest(name, names);
      a.vm.fail("there's no skeleton called \"" + name + "\"", c.empty() ? "bring one in first, like `use \"human\"`" : "did you mean \"" + c + "\"?");
    }
    const anim::Skeleton& sk = found->second;
    const float tall = anim::standingHeight(sk);
    const float k = a.has(-1, "height") && tall > 0 ? f32(a.num(-1, "height", tall)) / tall : 1;
    Transform root;
    root.position = a.vec(-1, "at", Vec3(0));
    root.rotation = Quat::axisAngle({0, 1, 0}, f32(a.num(-1, "turn", 0)));
    root.scale = Vec3(k);
    // one shape for every bone: a slim eight-sided stone, widest a tenth of the way along
    int mesh = cachedMesh(s, "bone shape", [] {
      Mesh m;
      const Vec3 base{0, 0, 0}, tip{0, 1, 0};
      const Vec3 ring[4] = {{1, 0.12f, 0}, {0, 0.12f, 1}, {-1, 0.12f, 0}, {0, 0.12f, -1}};
      auto tri = [&](Vec3 p, Vec3 q, Vec3 r) {
        const Vec3 n = normalize(cross(q - p, r - p));
        uint32_t i = m.addVertex(p, n, {0, 0});
        m.addVertex(q, n, {1, 0}), m.addVertex(r, n, {0, 1});
        m.addTriangle(i, i + 1, i + 2);
      };
      for (int i = 0; i < 4; ++i) tri(ring[i], tip, ring[(i + 1) % 4]), tri(base, ring[i], ring[(i + 1) % 4]);
      return m;
    });
    const Value* made = a.get(-1, "material") ? a.get(-1, "material") : a.get(-1, "made_of");
    const int mat = made ? materialFrom(a.vm, *made) : materialFrom(a.vm, a.vm.text("porcelain"));
    const std::vector<Transform> w = anim::Pose::rest(sk).world(sk, root);
    std::vector<int> parts;
    for (size_t i = 0; i < sk.bones.size(); ++i) {
      const float len = sk.bones[i].length * k;
      if (len <= 0) continue;
      const float wide = std::clamp(len * 0.09f, 0.0025f, 0.03f);
      const int pi = placePart(s, mesh, mat, w[i].position, 0, Vec3(wide, len, wide));
      Stage::Placed& p = s.placed[pi];
      p.quat = w[i].rotation, p.useQuat = true, p.name = sk.bones[i].name, p.line = a.line;
      s.updateTransform(pi);
      parts.push_back(pi);
    }
    ++s.version;
    return newGroup(a.vm, s, parts, root.position);
  }, "bones(skeleton, at, turn, height, material): shows a skeleton by name in its rest pose, each bone a slim shape from its start to its tip");

  // ---- the clip as a value
  vm.method("clip", "play", [](Args& a) {
    Thing* t = a.self.as<Thing>();
    Stage::Playing& c = clipOf(a.vm, *t);
    c.speed = std::fabs(c.speed) > 0 ? std::fabs(c.speed) : 1;
    const double d = c.clip.duration();
    if (a.has(0, "from")) c.moment = std::clamp(a.num(0, "from", 0), 0.0, d);
    else if (c.clip.repeat == anim::Repeat::kOnce && c.moment >= d) c.moment = 0;  // at its end: from the start again
    c.playing = true;
    return a.self;
  }, "clip.play(from): plays forwards from where it is (from its start when it had ended, or from the moment given)");
  vm.method("clip", "reverse", [](Args& a) {
    Thing* t = a.self.as<Thing>();
    Stage::Playing& c = clipOf(a.vm, *t);
    c.speed = -(std::fabs(c.speed) > 0 ? std::fabs(c.speed) : 1);
    if (c.clip.repeat == anim::Repeat::kOnce && c.moment <= 0) c.moment = c.clip.duration();  // at its start: from the end
    c.playing = true;
    return a.self;
  }, "clip.reverse(): plays backwards from where it is (from its end when it was at its start)");
  vm.method("clip", "stop", [](Args& a) {
    Thing* t = a.self.as<Thing>();
    clipOf(a.vm, *t).playing = false;
    return a.self;
  }, "clip.stop(): holds where it is");
}

}  // namespace words
}  // namespace loop::script
