#pragma once
// Loop3D math: vectors, quaternions, matrices, boxes, rays. Right-handed,
// Y up, metres. Column-major 4x4 matrices (m[col][row]) so they upload to
// the GPU as-is.
#include <algorithm>
#include <cmath>
#include <cstdint>

namespace loop {

constexpr float kPi = 3.14159265358979323846f;
constexpr float kTwoPi = 2.0f * kPi;
constexpr float kInvPi = 1.0f / kPi;
constexpr float kDegToRad = kPi / 180.0f;
constexpr float kRadToDeg = 180.0f / kPi;
constexpr float kInf = 1e30f;

template <class T> constexpr T clamp(T v, T lo, T hi) { return v < lo ? lo : (v > hi ? hi : v); }
template <class T> constexpr T lerp(T a, T b, float t) { return a + (b - a) * t; }
constexpr float saturate(float v) { return clamp(v, 0.0f, 1.0f); }
constexpr float sqr(float v) { return v * v; }
inline float smoothstep(float e0, float e1, float x) {
  float t = saturate((x - e0) / (e1 - e0));
  return t * t * (3 - 2 * t);
}
inline float radians(float deg) { return deg * kDegToRad; }
inline float degrees(float rad) { return rad * kRadToDeg; }

struct Vec2 {
  float x = 0, y = 0;
  constexpr Vec2() = default;
  constexpr Vec2(float x_, float y_) : x(x_), y(y_) {}
  constexpr explicit Vec2(float s) : x(s), y(s) {}
  constexpr Vec2 operator+(Vec2 o) const { return {x + o.x, y + o.y}; }
  constexpr Vec2 operator-(Vec2 o) const { return {x - o.x, y - o.y}; }
  constexpr Vec2 operator*(Vec2 o) const { return {x * o.x, y * o.y}; }
  constexpr Vec2 operator*(float s) const { return {x * s, y * s}; }
  constexpr Vec2 operator/(float s) const { return {x / s, y / s}; }
  constexpr Vec2 operator-() const { return {-x, -y}; }
  Vec2& operator+=(Vec2 o) { x += o.x, y += o.y; return *this; }
  Vec2& operator-=(Vec2 o) { x -= o.x, y -= o.y; return *this; }
  Vec2& operator*=(float s) { x *= s, y *= s; return *this; }
  float& operator[](int i) { return (&x)[i]; }
  float operator[](int i) const { return (&x)[i]; }
};
constexpr Vec2 operator*(float s, Vec2 v) { return v * s; }
constexpr float dot(Vec2 a, Vec2 b) { return a.x * b.x + a.y * b.y; }
inline float length(Vec2 v) { return std::sqrt(dot(v, v)); }
inline Vec2 normalize(Vec2 v) { float l = length(v); return l > 0 ? v / l : Vec2{}; }

struct Vec3 {
  float x = 0, y = 0, z = 0;
  constexpr Vec3() = default;
  constexpr Vec3(float x_, float y_, float z_) : x(x_), y(y_), z(z_) {}
  constexpr explicit Vec3(float s) : x(s), y(s), z(s) {}
  constexpr Vec3 operator+(Vec3 o) const { return {x + o.x, y + o.y, z + o.z}; }
  constexpr Vec3 operator-(Vec3 o) const { return {x - o.x, y - o.y, z - o.z}; }
  constexpr Vec3 operator*(Vec3 o) const { return {x * o.x, y * o.y, z * o.z}; }
  constexpr Vec3 operator/(Vec3 o) const { return {x / o.x, y / o.y, z / o.z}; }
  constexpr Vec3 operator*(float s) const { return {x * s, y * s, z * s}; }
  constexpr Vec3 operator/(float s) const { return {x / s, y / s, z / s}; }
  constexpr Vec3 operator-() const { return {-x, -y, -z}; }
  Vec3& operator+=(Vec3 o) { x += o.x, y += o.y, z += o.z; return *this; }
  Vec3& operator-=(Vec3 o) { x -= o.x, y -= o.y, z -= o.z; return *this; }
  Vec3& operator*=(Vec3 o) { x *= o.x, y *= o.y, z *= o.z; return *this; }
  Vec3& operator*=(float s) { x *= s, y *= s, z *= s; return *this; }
  Vec3& operator/=(float s) { x /= s, y /= s, z /= s; return *this; }
  constexpr bool operator==(const Vec3&) const = default;
  float& operator[](int i) { return (&x)[i]; }
  float operator[](int i) const { return (&x)[i]; }
};
constexpr Vec3 operator*(float s, Vec3 v) { return v * s; }
constexpr float dot(Vec3 a, Vec3 b) { return a.x * b.x + a.y * b.y + a.z * b.z; }
constexpr Vec3 cross(Vec3 a, Vec3 b) { return {a.y * b.z - a.z * b.y, a.z * b.x - a.x * b.z, a.x * b.y - a.y * b.x}; }
inline float length(Vec3 v) { return std::sqrt(dot(v, v)); }
constexpr float lengthSq(Vec3 v) { return dot(v, v); }
inline float distance(Vec3 a, Vec3 b) { return length(a - b); }
inline Vec3 normalize(Vec3 v) { float l = length(v); return l > 0 ? v / l : Vec3{}; }
inline Vec3 vmin(Vec3 a, Vec3 b) { return {std::min(a.x, b.x), std::min(a.y, b.y), std::min(a.z, b.z)}; }
inline Vec3 vmax(Vec3 a, Vec3 b) { return {std::max(a.x, b.x), std::max(a.y, b.y), std::max(a.z, b.z)}; }
inline Vec3 vabs(Vec3 a) { return {std::fabs(a.x), std::fabs(a.y), std::fabs(a.z)}; }
inline float maxComp(Vec3 a) { return std::max(a.x, std::max(a.y, a.z)); }
inline float minComp(Vec3 a) { return std::min(a.x, std::min(a.y, a.z)); }
inline int maxAxis(Vec3 a) { return a.x > a.y ? (a.x > a.z ? 0 : 2) : (a.y > a.z ? 1 : 2); }
inline Vec3 reflect(Vec3 i, Vec3 n) { return i - n * (2 * dot(i, n)); }
// Refracts `i` (pointing toward the surface) through normal `n` with eta =
// n_from / n_to; false on total internal reflection.
inline bool refract(Vec3 i, Vec3 n, float eta, Vec3& out) {
  float c = -dot(i, n);
  float k = 1 - eta * eta * (1 - c * c);
  if (k < 0) return false;
  out = i * eta + n * (eta * c - std::sqrt(k));
  return true;
}
inline Vec3 vlerp(Vec3 a, Vec3 b, float t) { return a + (b - a) * t; }
inline bool isFinite(Vec3 v) { return std::isfinite(v.x) && std::isfinite(v.y) && std::isfinite(v.z); }
// Two unit vectors perpendicular to unit `n` (Duff et al. 2017, no branches
// on the hot path, no singularity).
inline void orthonormalBasis(Vec3 n, Vec3& t, Vec3& b) {
  float s = std::copysign(1.0f, n.z);
  float a = -1.0f / (s + n.z);
  float c = n.x * n.y * a;
  t = {1 + s * n.x * n.x * a, s * c, -s * n.x};
  b = {c, s + n.y * n.y * a, -n.y};
}

struct Vec4 {
  float x = 0, y = 0, z = 0, w = 0;
  constexpr Vec4() = default;
  constexpr Vec4(float x_, float y_, float z_, float w_) : x(x_), y(y_), z(z_), w(w_) {}
  constexpr Vec4(Vec3 v, float w_) : x(v.x), y(v.y), z(v.z), w(w_) {}
  constexpr Vec3 xyz() const { return {x, y, z}; }
  constexpr Vec4 operator+(Vec4 o) const { return {x + o.x, y + o.y, z + o.z, w + o.w}; }
  constexpr Vec4 operator-(Vec4 o) const { return {x - o.x, y - o.y, z - o.z, w - o.w}; }
  constexpr Vec4 operator*(float s) const { return {x * s, y * s, z * s, w * s}; }
  float& operator[](int i) { return (&x)[i]; }
  float operator[](int i) const { return (&x)[i]; }
};
constexpr float dot(Vec4 a, Vec4 b) { return a.x * b.x + a.y * b.y + a.z * b.z + a.w * b.w; }

struct Quat {
  float x = 0, y = 0, z = 0, w = 1;
  constexpr Quat() = default;
  constexpr Quat(float x_, float y_, float z_, float w_) : x(x_), y(y_), z(z_), w(w_) {}
  static Quat axisAngle(Vec3 axis, float rad) {
    Vec3 a = normalize(axis) * std::sin(rad * 0.5f);
    return {a.x, a.y, a.z, std::cos(rad * 0.5f)};
  }
  // Yaw (about Y), pitch (about X), roll (about Z), applied roll, pitch, yaw.
  static Quat euler(float yaw, float pitch, float roll) {
    return axisAngle({0, 1, 0}, yaw) * axisAngle({1, 0, 0}, pitch) * axisAngle({0, 0, 1}, roll);
  }
  // The rotation taking unit `a` to unit `b`.
  static Quat fromTo(Vec3 a, Vec3 b) {
    float d = dot(a, b);
    if (d < -0.999999f) {
      Vec3 axis = cross({1, 0, 0}, a);
      if (lengthSq(axis) < 1e-6f) axis = cross({0, 1, 0}, a);
      return axisAngle(axis, kPi);
    }
    Vec3 c = cross(a, b);
    Quat q{c.x, c.y, c.z, 1 + d};
    return q.normalized();
  }
  constexpr Quat operator*(Quat o) const {
    return {w * o.x + x * o.w + y * o.z - z * o.y, w * o.y - x * o.z + y * o.w + z * o.x,
            w * o.z + x * o.y - y * o.x + z * o.w, w * o.w - x * o.x - y * o.y - z * o.z};
  }
  constexpr Quat conjugate() const { return {-x, -y, -z, w}; }
  Quat normalized() const {
    float l = std::sqrt(x * x + y * y + z * z + w * w);
    return l > 0 ? Quat{x / l, y / l, z / l, w / l} : Quat{};
  }
  Vec3 rotate(Vec3 v) const {
    Vec3 u{x, y, z};
    Vec3 t = cross(u, v) * 2.0f;
    return v + t * w + cross(u, t);
  }
};
inline float dot(Quat a, Quat b) { return a.x * b.x + a.y * b.y + a.z * b.z + a.w * b.w; }
inline Quat slerp(Quat a, Quat b, float t) {
  float d = dot(a, b);
  if (d < 0) b = {-b.x, -b.y, -b.z, -b.w}, d = -d;
  if (d > 0.9995f) {
    Quat r{a.x + (b.x - a.x) * t, a.y + (b.y - a.y) * t, a.z + (b.z - a.z) * t, a.w + (b.w - a.w) * t};
    return r.normalized();
  }
  float th = std::acos(d), s = std::sin(th);
  float wa = std::sin((1 - t) * th) / s, wb = std::sin(t * th) / s;
  return {a.x * wa + b.x * wb, a.y * wa + b.y * wb, a.z * wa + b.z * wb, a.w * wa + b.w * wb};
}

struct Mat3 {
  Vec3 c[3] = {{1, 0, 0}, {0, 1, 0}, {0, 0, 1}};  // columns
  static Mat3 fromQuat(Quat q) {
    Mat3 m;
    m.c[0] = q.rotate({1, 0, 0});
    m.c[1] = q.rotate({0, 1, 0});
    m.c[2] = q.rotate({0, 0, 1});
    return m;
  }
  static Mat3 diag(Vec3 d) {
    Mat3 m;
    m.c[0] = {d.x, 0, 0}, m.c[1] = {0, d.y, 0}, m.c[2] = {0, 0, d.z};
    return m;
  }
  Vec3 operator*(Vec3 v) const { return c[0] * v.x + c[1] * v.y + c[2] * v.z; }
  Mat3 operator*(const Mat3& o) const {
    Mat3 m;
    for (int i = 0; i < 3; ++i) m.c[i] = (*this) * o.c[i];
    return m;
  }
  Mat3 transposed() const {
    Mat3 m;
    m.c[0] = {c[0].x, c[1].x, c[2].x};
    m.c[1] = {c[0].y, c[1].y, c[2].y};
    m.c[2] = {c[0].z, c[1].z, c[2].z};
    return m;
  }
  float determinant() const { return dot(c[0], cross(c[1], c[2])); }
  Mat3 inverse() const {
    Vec3 r0 = cross(c[1], c[2]), r1 = cross(c[2], c[0]), r2 = cross(c[0], c[1]);
    float d = dot(c[0], r0);
    float id = std::fabs(d) > 1e-20f ? 1.0f / d : 0.0f;
    Mat3 m;
    m.c[0] = {r0.x * id, r1.x * id, r2.x * id};
    m.c[1] = {r0.y * id, r1.y * id, r2.y * id};
    m.c[2] = {r0.z * id, r1.z * id, r2.z * id};
    return m;
  }
};

struct Mat4 {
  float m[4][4] = {{1, 0, 0, 0}, {0, 1, 0, 0}, {0, 0, 1, 0}, {0, 0, 0, 1}};  // m[col][row]

  static Mat4 translation(Vec3 t) {
    Mat4 r;
    r.m[3][0] = t.x, r.m[3][1] = t.y, r.m[3][2] = t.z;
    return r;
  }
  static Mat4 scale(Vec3 s) {
    Mat4 r;
    r.m[0][0] = s.x, r.m[1][1] = s.y, r.m[2][2] = s.z;
    return r;
  }
  static Mat4 rotation(Quat q) {
    Mat3 b = Mat3::fromQuat(q);
    Mat4 r;
    for (int i = 0; i < 3; ++i) r.m[i][0] = b.c[i].x, r.m[i][1] = b.c[i].y, r.m[i][2] = b.c[i].z;
    return r;
  }
  static Mat4 trs(Vec3 t, Quat q, Vec3 s) { return translation(t) * rotation(q) * scale(s); }
  // View matrix looking from `eye` at `target`.
  static Mat4 lookAt(Vec3 eye, Vec3 target, Vec3 up) {
    Vec3 f = normalize(target - eye), s = normalize(cross(f, up)), u = cross(s, f);
    Mat4 r;
    r.m[0][0] = s.x, r.m[1][0] = s.y, r.m[2][0] = s.z;
    r.m[0][1] = u.x, r.m[1][1] = u.y, r.m[2][1] = u.z;
    r.m[0][2] = -f.x, r.m[1][2] = -f.y, r.m[2][2] = -f.z;
    r.m[3][0] = -dot(s, eye), r.m[3][1] = -dot(u, eye), r.m[3][2] = dot(f, eye);
    return r;
  }
  // Perspective with depth 0..1 (WebGPU), reversed-Z optional (near→1).
  static Mat4 perspective(float fovY, float aspect, float zNear, float zFar, bool reversedZ = false) {
    float f = 1.0f / std::tan(fovY * 0.5f);
    Mat4 r;
    r.m[0][0] = f / aspect;
    r.m[1][1] = f;
    if (!reversedZ) {
      r.m[2][2] = zFar / (zNear - zFar);
      r.m[3][2] = zNear * zFar / (zNear - zFar);
    } else {
      r.m[2][2] = zNear / (zFar - zNear);
      r.m[3][2] = zNear * zFar / (zFar - zNear);
    }
    r.m[2][3] = -1;
    r.m[3][3] = 0;
    return r;
  }
  static Mat4 ortho(float l, float r_, float b, float t, float n, float f) {
    Mat4 r;
    r.m[0][0] = 2 / (r_ - l);
    r.m[1][1] = 2 / (t - b);
    r.m[2][2] = 1 / (n - f);
    r.m[3][0] = -(r_ + l) / (r_ - l);
    r.m[3][1] = -(t + b) / (t - b);
    r.m[3][2] = n / (n - f);
    return r;
  }
  Mat4 operator*(const Mat4& o) const {
    Mat4 r;
    for (int c = 0; c < 4; ++c)
      for (int row = 0; row < 4; ++row) {
        float s = 0;
        for (int k = 0; k < 4; ++k) s += m[k][row] * o.m[c][k];
        r.m[c][row] = s;
      }
    return r;
  }
  Vec4 operator*(Vec4 v) const {
    Vec4 r;
    for (int row = 0; row < 4; ++row) r[row] = m[0][row] * v.x + m[1][row] * v.y + m[2][row] * v.z + m[3][row] * v.w;
    return r;
  }
  Vec3 point(Vec3 p) const {
    Vec4 r = (*this) * Vec4(p, 1);
    return std::fabs(r.w - 1) > 1e-7f && r.w != 0 ? r.xyz() / r.w : r.xyz();
  }
  Vec3 vector(Vec3 v) const { return ((*this) * Vec4(v, 0)).xyz(); }
  Vec3 translationPart() const { return {m[3][0], m[3][1], m[3][2]}; }
  Mat3 linear() const {
    Mat3 r;
    for (int i = 0; i < 3; ++i) r.c[i] = {m[i][0], m[i][1], m[i][2]};
    return r;
  }
  Mat4 transposed() const {
    Mat4 r;
    for (int i = 0; i < 4; ++i)
      for (int j = 0; j < 4; ++j) r.m[i][j] = m[j][i];
    return r;
  }
  Mat4 inverse() const;  // general inverse (math.cpp)
};

struct Ray {
  Vec3 o, d;  // origin, unit direction
  Vec3 at(float t) const { return o + d * t; }
};

struct AABB {
  Vec3 lo{kInf}, hi{-kInf};
  constexpr AABB() = default;
  constexpr AABB(Vec3 a, Vec3 b) : lo(a), hi(b) {}
  void grow(Vec3 p) { lo = vmin(lo, p), hi = vmax(hi, p); }
  void grow(const AABB& b) { lo = vmin(lo, b.lo), hi = vmax(hi, b.hi); }
  bool valid() const { return lo.x <= hi.x && lo.y <= hi.y && lo.z <= hi.z; }
  Vec3 center() const { return (lo + hi) * 0.5f; }
  Vec3 extent() const { return hi - lo; }
  float area() const {
    if (!valid()) return 0;
    Vec3 e = extent();
    return 2 * (e.x * e.y + e.y * e.z + e.z * e.x);
  }
  bool contains(Vec3 p) const { return p.x >= lo.x && p.y >= lo.y && p.z >= lo.z && p.x <= hi.x && p.y <= hi.y && p.z <= hi.z; }
  bool overlaps(const AABB& b) const {
    return lo.x <= b.hi.x && hi.x >= b.lo.x && lo.y <= b.hi.y && hi.y >= b.lo.y && lo.z <= b.hi.z && hi.z >= b.lo.z;
  }
  AABB transformed(const Mat4& m) const;
};
// Slab test: the entry distance in [tmin, tmax], or a value > tmax on a miss.
inline float intersect(const AABB& b, Vec3 o, Vec3 invD, float tmin, float tmax) {
  float tx1 = (b.lo.x - o.x) * invD.x, tx2 = (b.hi.x - o.x) * invD.x;
  float t0 = std::min(tx1, tx2), t1 = std::max(tx1, tx2);
  float ty1 = (b.lo.y - o.y) * invD.y, ty2 = (b.hi.y - o.y) * invD.y;
  t0 = std::max(t0, std::min(ty1, ty2)), t1 = std::min(t1, std::max(ty1, ty2));
  float tz1 = (b.lo.z - o.z) * invD.z, tz2 = (b.hi.z - o.z) * invD.z;
  t0 = std::max(t0, std::min(tz1, tz2)), t1 = std::min(t1, std::max(tz1, tz2));
  t0 = std::max(t0, tmin), t1 = std::min(t1, tmax);
  return t0 <= t1 ? t0 : kInf;
}

struct Plane {
  Vec3 n{0, 1, 0};
  float d = 0;  // dot(n, p) + d = 0
  float distance(Vec3 p) const { return dot(n, p) + d; }
};

// Six planes, normals pointing in. From a view-projection with 0..1 depth.
struct Frustum {
  Plane p[6];
  static Frustum fromViewProj(const Mat4& vp);
  bool visible(const AABB& b) const;
};

// A transform as position, rotation, scale.
struct Transform {
  Vec3 position;
  Quat rotation;
  Vec3 scale{1, 1, 1};
  Mat4 matrix() const { return Mat4::trs(position, rotation, scale); }
  Vec3 apply(Vec3 p) const { return position + rotation.rotate(p * scale); }
  Transform operator*(const Transform& c) const {
    return {apply(c.position), rotation * c.rotation, scale * c.scale};
  }
};

}  // namespace loop
