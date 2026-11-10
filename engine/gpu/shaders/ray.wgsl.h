#pragma once
// Part of the live renderer's shaders (engine/gpu/shaders.h).

namespace loop::gpu {

// ---- Rays through the scene, for real shadows (live) and Ultra. Every
// shape has its own tree of boxes (built once); a small top tree over the
// placed copies is rebuilt as things move. Lamps' shadows in the live
// picture are found by casting a ray from each point to the lamp.
inline const char* kRayWgsl = R"(
struct RTri {
  v0 : vec4f, e1 : vec4f, e2 : vec4f,  // corner and edges (local); w: uv parts
  n0 : vec4f, n1 : vec4f, n2 : vec4f,  // corner normals (local); w: uv parts
};
struct RNode {
  lo : vec4f,  // xyz, first (child or item) as bits
  hi : vec4f,  // xyz, count (0 = inner) as bits
};
struct RInst {
  inv : mat4x4f,                       // world → local
  n0 : vec4f, n1 : vec4f, n2 : vec4f,  // local → world for normals
  info : vec4u,                        // shape's tree root, material, flags (1 glows, 2 glass, 4 leaf), 0
};
@group(2) @binding(0) var<storage, read> rTris : array<RTri>;
@group(2) @binding(1) var<storage, read> rBlas : array<RNode>;
@group(2) @binding(2) var<storage, read> rInst : array<RInst>;
@group(2) @binding(3) var<storage, read> rTlas : array<RNode>;

struct RayHit { t : f32, tri : u32, inst : u32, u : f32, v : f32 };
// how much work this pixel's rays took (boxes and triangles tested), for the
// ?view=4 heat map
var<private> rayWork : u32 = 0u;

fn rayBox(lo : vec3f, hi : vec3f, o : vec3f, inv : vec3f, tmax : f32) -> f32 {
  let t0 = (lo - o) * inv;
  let t1 = (hi - o) * inv;
  let a = min(t0, t1);
  let b = max(t0, t1);
  let tn = max(max(a.x, a.y), max(a.z, 0.0));
  let tf = min(min(b.x, b.y), min(b.z, tmax));
  return select(1e30, tn, tn <= tf);
}
fn triUv(tr : RTri, u : f32, v : f32) -> vec2f {
  let uv0 = vec2f(tr.n0.w, tr.n1.w);
  let uv1 = vec2f(tr.n2.w, tr.e1.w);
  let uv2 = vec2f(tr.e2.w, tr.v0.w);
  return uv0 * (1.0 - u - v) + uv1 * u + uv2 * v;
}
// a leaf card's outline: true where the card is cut away
fn leafCut(uv : vec2f) -> bool {
  let y = 1.0 - uv.y;
  let x = (uv.x - 0.5) * 2.0;
  return abs(x) > sin(3.14159265 * pow(clamp(y, 0.0, 1.0), 0.75)) * 0.95;
}

// a leaf card's texture position at a hit, read straight from the buffer
fn triUvAt(q : u32, u : f32, v : f32) -> vec2f {
  let uv0 = vec2f(rTris[q].n0.w, rTris[q].n1.w);
  let uv1 = vec2f(rTris[q].n2.w, rTris[q].e1.w);
  let uv2 = vec2f(rTris[q].e2.w, rTris[q].v0.w);
  return uv0 * (1.0 - u - v) + uv1 * u + uv2 * v;
}

// The nearest hit along o + d t, t in (tmin, tmax). `shadow`: stop at the
// first blocker, and let glowing things and glass through (the light comes
// from them, or passes them).
// Both trees are walked the same way: a node's two children are tested
// before either is visited, only the ones the ray meets are visited, the
// nearer first (the other waits on a small stack), and only the parts of
// each record that are needed are read.
fn traceRay(o : vec3f, d : vec3f, tmin : f32, tmax : f32, shadow : bool) -> RayHit {
  var h = RayHit(tmax, 0xffffffffu, 0xffffffffu, 0.0, 0.0);
  if (arrayLength(&rTlas) == 0u || arrayLength(&rInst) == 0u) { return h; }
  let inv = 1.0 / select(d, vec3f(1e-20), abs(d) < vec3f(1e-20));
  if (rayBox(rTlas[0].lo.xyz, rTlas[0].hi.xyz, o, inv, h.t) >= 1e29) { return h; }
  // the stacks, made once per ray (WGSL fills every new array with zeros:
  // made inside the loop, that would happen for every shape the ray meets)
  var top : array<u32, 32>;
  var st : array<u32, 48>;
  var tsp = 0;
  var node = 0u;
  loop {
    rayWork += 1u;
    let first = bitcast<u32>(rTlas[node].lo.w);
    let count = bitcast<u32>(rTlas[node].hi.w);
    if (count == 0u) {
      let ta = rayBox(rTlas[first].lo.xyz, rTlas[first].hi.xyz, o, inv, h.t);
      let tb = rayBox(rTlas[first + 1u].lo.xyz, rTlas[first + 1u].hi.xyz, o, inv, h.t);
      if (ta < 1e29 && tb < 1e29) {
        let aFirst = ta <= tb;
        node = select(first + 1u, first, aFirst);
        if (tsp < 32) { top[tsp] = select(first, first + 1u, aFirst); tsp += 1; }
        continue;
      }
      if (ta < 1e29) { node = first; continue; }
      if (tb < 1e29) { node = first + 1u; continue; }
    } else {
      for (var k = first; k < first + count; k++) {
        let info = rInst[k].info;
        if (shadow && (info.z & 3u) != 0u) { continue; }
        // the ray in the shape's own space (not renormalised: t stays the same)
        let m = rInst[k].inv;
        let lo = (m * vec4f(o, 1.0)).xyz;
        let ld = (m * vec4f(d, 0.0)).xyz;
        let linv = 1.0 / select(ld, vec3f(1e-20), abs(ld) < vec3f(1e-20));
        let leafy = (info.z & 4u) != 0u;
        var bn = info.x;
        if (rayBox(rBlas[bn].lo.xyz, rBlas[bn].hi.xyz, lo, linv, h.t) >= 1e29) { continue; }
        var sp = 0;
        loop {
          rayWork += 1u;
          let bf = bitcast<u32>(rBlas[bn].lo.w);
          let bc = bitcast<u32>(rBlas[bn].hi.w);
          if (bc == 0u) {
            let ta = rayBox(rBlas[bf].lo.xyz, rBlas[bf].hi.xyz, lo, linv, h.t);
            let tb = rayBox(rBlas[bf + 1u].lo.xyz, rBlas[bf + 1u].hi.xyz, lo, linv, h.t);
            if (ta < 1e29 && tb < 1e29) {
              let aFirst = ta <= tb;
              bn = select(bf + 1u, bf, aFirst);
              if (sp < 48) { st[sp] = select(bf, bf + 1u, aFirst); sp += 1; }
              continue;
            }
            if (ta < 1e29) { bn = bf; continue; }
            if (tb < 1e29) { bn = bf + 1u; continue; }
          } else {
            for (var q = bf; q < bf + bc; q++) {
              rayWork += 1u;
              let e1 = rTris[q].e1.xyz;
              let e2 = rTris[q].e2.xyz;
              let p = cross(ld, e2);
              let det = dot(e1, p);
              if (abs(det) < 1e-14) { continue; }
              let id = 1.0 / det;
              let sv = lo - rTris[q].v0.xyz;
              let u = dot(sv, p) * id;
              if (u < 0.0 || u > 1.0) { continue; }
              let qv = cross(sv, e1);
              let v = dot(ld, qv) * id;
              if (v < 0.0 || u + v > 1.0) { continue; }
              let t = dot(e2, qv) * id;
              if (t <= tmin || t >= h.t) { continue; }
              if (leafy && leafCut(triUvAt(q, u, v))) { continue; }
              h = RayHit(t, q, k, u, v);
              if (shadow) { return h; }
            }
          }
          if (sp == 0) { break; }
          sp -= 1;
          bn = st[sp];
        }
      }
    }
    if (tsp == 0) { break; }
    tsp -= 1;
    node = top[tsp];
  }
  return h;
}
fn hitNormals(h : RayHit, shading : ptr<function, vec3f>, geometric : ptr<function, vec3f>) {
  let tr = rTris[h.tri];
  let ins = rInst[h.inst];
  let nm = mat3x3f(ins.n0.xyz, ins.n1.xyz, ins.n2.xyz);
  let w0 = 1.0 - h.u - h.v;
  *shading = normalize(nm * (tr.n0.xyz * w0 + tr.n1.xyz * h.u + tr.n2.xyz * h.v));
  *geometric = normalize(nm * cross(tr.e1.xyz, tr.e2.xyz));
}
)";

}  // namespace loop::gpu
