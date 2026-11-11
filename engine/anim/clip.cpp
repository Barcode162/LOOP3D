#include "engine/anim/clip.h"

#include <algorithm>
#include <cmath>
#include <cstdio>

namespace loop::anim {

// ------------------------------------------------------------- a channel

int Channel::find(double time, double snap) const {
  for (size_t i = 0; i < keys.size(); ++i)
    if (std::fabs(keys[i].time - time) <= snap) return static_cast<int>(i);
  return -1;
}

Key& Channel::set(double time, const float* v, double snap) {
  int i = find(time, snap);
  if (i < 0) {
    Key k;
    k.time = time;
    auto at = std::upper_bound(keys.begin(), keys.end(), time, [](double t, const Key& key) { return t < key.time; });
    i = static_cast<int>(at - keys.begin());  // its place, worked out before the list grows
    keys.insert(keys.begin() + i, k);
  }
  for (int c = 0; c < size; ++c) keys[i].v[c] = v[c];
  return keys[i];
}

bool Channel::remove(double time, double snap) {
  int i = find(time, snap);
  if (i < 0) return false;
  keys.erase(keys.begin() + i);
  return true;
}

namespace {

// A loop closed by hand: a key at the start and its twin at the very end.
bool closedLoop(const Channel& ch, double period) {
  const size_t n = ch.keys.size();
  return n >= 2 && period > 0 && ch.keys.front().time <= 1e-6 && ch.keys.back().time >= period - 1e-6;
}

}  // namespace

// The slope a smooth curve has at a key. Worked out from the keys on each
// side so the curve between two keys never leaves the range of their values
// (Fritsch and Carlson's monotone cubic): flat on a peak or a dip, flat at a
// clip's first and last key (so things start gently and land softly), and
// flat where the value was held or is about to be.
float Channel::slope(int i, int c, bool leaving, bool cyclic, double period) const {
  const int n = static_cast<int>(keys.size());
  const Key& k = keys[i];
  if (k.free) return leaving ? k.out[c] : k.in[c];
  if (n < 2) return 0;
  const bool closed = cyclic && closedLoop(*this, period);
  // the key before and the key after, round the loop's join when there is one
  const Key* prev = nullptr;
  const Key* next = nullptr;
  double prevShift = 0, nextShift = 0;
  Way arriving = k.way, departing = Way::kSmooth;
  if (i > 0) prev = &keys[i - 1];
  else if (cyclic) {
    prev = &keys[closed ? n - 2 : n - 1], prevShift = -period;
    if (closed) arriving = keys[n - 1].way;  // arriving at the start is arriving at its twin
  }
  if (i < n - 1) next = &keys[i + 1], departing = next->way;
  else if (cyclic) next = &keys[closed ? 1 : 0], nextShift = period, departing = next->way;
  if (arriving == Way::kHold) prev = nullptr;
  if (departing == Way::kHold) next = nullptr;
  if (!prev || !next) return 0;
  const double h0 = k.time - (prev->time + prevShift), h1 = (next->time + nextShift) - k.time;
  if (h0 <= 1e-9 || h1 <= 1e-9) return 0;
  const double d0 = (k.v[c] - prev->v[c]) / h0, d1 = (next->v[c] - k.v[c]) / h1;
  if (d0 * d1 <= 0) return 0;
  const double w1 = 2 * h1 + h0, w2 = h1 + 2 * h0;
  return static_cast<float>((w1 + w2) / (w1 / d0 + w2 / d1));
}

bool Channel::sample(double t, float* out, bool cyclic, double period) const {
  const int n = static_cast<int>(keys.size());
  if (n == 0) return false;
  auto copy = [&](const Key& k) {
    for (int c = 0; c < size; ++c) out[c] = k.v[c];
    return true;
  };
  if (n == 1) return copy(keys[0]);
  if (!cyclic || period <= 0) cyclic = false;
  // the two keys the moment lies between, and their moments (the loop's join
  // runs from the last key round to the first)
  int ia, ib;
  double ta, tb;
  if (t < keys.front().time) {
    if (!cyclic) return copy(keys.front());
    ia = n - 1, ib = 0, ta = keys[ia].time - period, tb = keys[ib].time;
  } else if (t >= keys.back().time) {
    if (!cyclic || closedLoop(*this, period)) return copy(keys.back());
    ia = n - 1, ib = 0, ta = keys[ia].time, tb = keys[ib].time + period;
  } else {
    auto after = std::upper_bound(keys.begin(), keys.end(), t, [](double x, const Key& key) { return x < key.time; });
    ib = static_cast<int>(after - keys.begin()), ia = ib - 1;
    ta = keys[ia].time, tb = keys[ib].time;
  }
  const Key& a = keys[ia];
  const Key& b = keys[ib];
  const double span = tb - ta;
  if (span <= 1e-9 || b.way == Way::kHold) return copy(a);
  const double u = std::clamp((t - ta) / span, 0.0, 1.0);
  if (b.way == Way::kLine || b.way == Way::kEase) {
    const double e = b.way == Way::kLine ? u : ease(b.ease, u);
    for (int c = 0; c < size; ++c) out[c] = static_cast<float>(a.v[c] + (b.v[c] - a.v[c]) * e);
    return true;
  }
  // smooth: a cubic through both keys with each key's slope
  const double u2 = u * u, u3 = u2 * u;
  const double h00 = 2 * u3 - 3 * u2 + 1, h10 = u3 - 2 * u2 + u, h01 = -2 * u3 + 3 * u2, h11 = u3 - u2;
  for (int c = 0; c < size; ++c) {
    const double ma = slope(ia, c, true, cyclic, period), mb = slope(ib, c, false, cyclic, period);
    out[c] = static_cast<float>(h00 * a.v[c] + h10 * span * ma + h01 * b.v[c] + h11 * span * mb);
  }
  return true;
}

// ---------------------------------------------------------------- a clip

Channel* Clip::find(const std::string& n) {
  for (Channel& c : channels)
    if (c.name == n) return &c;
  return nullptr;
}
const Channel* Clip::find(const std::string& n) const {
  for (const Channel& c : channels)
    if (c.name == n) return &c;
  return nullptr;
}

Channel& Clip::channel(const std::string& n, int size, bool angle) {
  if (Channel* c = find(n)) return *c;
  Channel c;
  c.name = n, c.size = size, c.angle = angle;
  channels.push_back(std::move(c));
  return channels.back();
}

double Clip::duration() const {
  if (length > 0) return length;
  double last = 0;
  for (const Channel& c : channels)
    if (!c.keys.empty()) last = std::max(last, c.keys.back().time);
  return last;
}

double Clip::snapped(double time, double within) const {
  if (rate <= 0) return time;
  const double frame = std::round(time * rate) / rate;
  return std::fabs(frame - time) <= within ? frame : time;
}

double Clip::wrap(double t) const {
  const double d = duration();
  if (d <= 0) return 0;
  if (repeat == Repeat::kOnce) return std::clamp(t, 0.0, d);
  if (repeat == Repeat::kLoop) return t - std::floor(t / d) * d;
  const double two = t - std::floor(t / (2 * d)) * 2 * d;  // there and back
  return two <= d ? two : 2 * d - two;
}

bool Clip::sample(const Channel& c, double t, float* out) const {
  return c.sample(wrap(t), out, repeat == Repeat::kLoop, duration());
}

Pose Clip::pose(const Skeleton& s, double t) const {
  Pose p = Pose::rest(s);
  float v[4];
  for (const Channel& c : channels) {
    if (c.size != 3 || !sample(c, t, v)) continue;
    int b = s.find(c.name);
    if (b >= 0) {
      p.local[b].rotation = s.bones[b].rest.rotation * turnFromBendSideTwist({v[0], v[1], v[2]});
      continue;
    }
    if (c.name.size() > 3 && c.name.compare(c.name.size() - 3, 3, "_at") == 0) {
      b = s.find(c.name.substr(0, c.name.size() - 3));
      if (b >= 0) p.local[b].position = s.bones[b].rest.position + Vec3(v[0], v[1], v[2]);
    }
  }
  return p;
}

// ------------------------------------------------------------ as script

std::string plainNumber(double n, int decimals) {
  if (!std::isfinite(n)) return "0";
  char buf[64];
  std::snprintf(buf, sizeof buf, "%.*f", decimals, n);
  std::string s = buf;
  if (s.find('.') != std::string::npos) {
    while (!s.empty() && s.back() == '0') s.pop_back();
    if (!s.empty() && s.back() == '.') s.pop_back();
  }
  if (s == "-0") s = "0";
  return s;
}

namespace {

const char* easeName(Ease e) {
  switch (e) {
    case Ease::kLinear: return "linear";
    case Ease::kIn: return "in";
    case Ease::kOut: return "out";
    case Ease::kInOut: return "in_out";
    case Ease::kSmoother: return "smoother";
    case Ease::kSine: return "sine";
    case Ease::kBack: return "back";
    case Ease::kElastic: return "elastic";
    case Ease::kBounce: return "bounce";
    case Ease::kSpring: return "spring";
  }
  return "in_out";
}

// One number of a channel as written: an angle in degrees, else plain.
std::string written(const Channel& c, double n) {
  if (!c.angle) return plainNumber(n);
  std::string d = plainNumber(n * 180.0 / 3.14159265358979323846);
  return d == "0" ? d : d + " deg";
}

std::string written(const Channel& c, const float* v) {
  if (c.size == 1) return written(c, v[0]);
  std::string s = c.size == 3 ? "vec(" : "[";  // three numbers are a vec; any other count is a list
  for (int i = 0; i < c.size; ++i) s += (i ? ", " : "") + written(c, v[i]);
  return s + (c.size == 3 ? ")" : "]");
}

std::string wayText(const Key& k) {
  if (k.way == Way::kLine) return ", ease: \"line\"";
  if (k.way == Way::kHold) return ", ease: \"hold\"";
  if (k.way == Way::kEase) return std::string(", ease: \"") + easeName(k.ease) + "\"";
  return "";
}

}  // namespace

std::string Clip::script(const std::string& target, const std::string& indent) const {
  std::string out = indent + "clip " + target;
  if (!skeleton.empty()) out += ", for: \"" + skeleton + "\"";
  if (length > 0) out += ", length: " + plainNumber(length) + " s";
  if (rate != 30) out += ", rate: " + plainNumber(rate);
  if (repeat == Repeat::kLoop) out += ", loop: yes";
  if (repeat == Repeat::kBackAndForth) out += ", loop: \"back and forth\"";
  out += "\n";
  // every moment that has a key, in order
  std::vector<double> moments;
  for (const Channel& c : channels)
    for (const Key& k : c.keys) moments.push_back(k.time);
  std::sort(moments.begin(), moments.end());
  moments.erase(std::unique(moments.begin(), moments.end(), [](double a, double b) { return std::fabs(a - b) <= 1e-4; }), moments.end());
  for (double m : moments) {
    // values that arrive the same way share a line; a key with its own slopes has its own
    struct Line { std::string way, text; };
    std::vector<Line> lines;
    const std::string head = indent + "  key " + plainNumber(m) + " s";
    for (const Channel& c : channels) {
      int i = c.find(m);
      if (i < 0) continue;
      const Key& k = c.keys[i];
      const bool shown = c.name == "visible";  // seen or not: yes or no, and always held
      const std::string value = ", " + c.name + ": " + (shown ? (k.v[0] > 0.5f ? "yes" : "no") : written(c, k.v));
      if (k.free) {
        lines.push_back({"", head + value + ", in: " + written(c, k.in) + ", out: " + written(c, k.out) + wayText(k)});
        lines.back().way = "\x01";  // never shared
        continue;
      }
      const std::string way = shown ? "" : wayText(k);
      auto same = std::find_if(lines.begin(), lines.end(), [&](const Line& l) { return l.way == way; });
      if (same == lines.end()) lines.push_back({way, head + value});
      else same->text += value;
    }
    for (const Line& l : lines) out += l.text + (l.way == "\x01" ? "" : l.way) + "\n";
  }
  return out;
}

// ------------------------------------------------- bend, side and twist

Quat turnFromBendSideTwist(Vec3 bst) {
  const float swing = std::sqrt(bst.x * bst.x + bst.y * bst.y);
  Quat s;
  if (swing > 1e-8f) s = Quat::axisAngle({bst.x / swing, 0, bst.y / swing}, swing);
  return (s * Quat::axisAngle({0, 1, 0}, bst.z)).normalized();
}

Vec3 bendSideTwistFromTurn(Quat q, float nearTwist) {
  q = q.normalized();
  // the twist is the part of the turn about the bone's length; what is left is the swing
  const float tl = std::sqrt(q.y * q.y + q.w * q.w);
  Quat twist;
  float twistAngle = 0;
  if (tl > 1e-7f) {
    twist = Quat{0, q.y / tl, 0, q.w / tl};
    twistAngle = 2 * std::atan2(twist.y, twist.w);
  }
  const Quat swing = q * twist.conjugate();
  const float sl = std::sqrt(swing.x * swing.x + swing.z * swing.z);
  float bend = 0, side = 0;
  if (sl > 1e-8f) {
    const float angle = 2 * std::atan2(sl, std::fabs(swing.w));
    const float sign = swing.w < 0 ? -1.0f : 1.0f;
    bend = sign * swing.x / sl * angle, side = sign * swing.z / sl * angle;
  }
  // the twist nearest the one asked for (a full turn more or less is the same turn)
  const float turn = 2 * kPi;
  twistAngle += turn * std::round((nearTwist - twistAngle) / turn);
  return {bend, side, twistAngle};
}

}  // namespace loop::anim
