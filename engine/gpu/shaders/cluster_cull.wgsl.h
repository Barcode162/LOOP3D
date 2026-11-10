#pragma once
// Part of the live renderer's shaders (engine/gpu/shaders.h).

namespace loop::gpu {

// The cull, in a fixed order (the same picture every time, no flicker where
// two surfaces touch): every copy the CPU kept (in view as a whole) looks at
// its mesh's clusters; one is chosen when its own error looks smaller than the
// limit in pixels while its parent's doesn't (all-or-nothing per group: no
// cracks), and its sphere is in view. Each copy lists its chosen clusters in
// its own stretch, in cluster order (cull); a count-up gives every copy where
// its list starts (count); the lists are then packed in copy order (pack).
inline const char* kClusterCullWgsl = R"(
struct Instance {
  model : mat4x4f,
  normal0 : vec4f, normal1 : vec4f, normal2 : vec4f,
  material : vec4u,  // x material, y mesh
};
struct MeshInfo { clusterFirst : u32, clusterCount : u32, vertexBase : u32, pad : u32 };
struct CullU {
  planes : array<vec4f, 6>,  // the view's sides (inside: dot >= 0)
  eye : vec4f,               // xyz, w: pixels per metre at 1 m
  params : vec4f,            // x: the error limit in pixels, y: how many copies
  occl : vec4f,              // what's hidden (phase 4): x 0 off, 1 not hidden in last frame's walls,
                             // 2 hidden then but not by this frame's first pass; y, zw: the old pyramid's levels, level 0's size
  occl2 : vec4f,             // the new pyramid's: x levels, yz level 0's size
  oldView : mat4x4f,         // the view last frame's depth pyramid was made in
  newView : mat4x4f,         // this frame's (its first pass's pyramid)
};
@group(0) @binding(0) var<uniform> cu : CullU;
@group(0) @binding(1) var<storage, read> instances : array<Instance>;
@group(0) @binding(2) var<storage, read> cullList : array<vec2u>;          // per copy: its instance, where its stretch starts
@group(0) @binding(3) var<storage, read> meshes : array<MeshInfo>;
@group(0) @binding(4) var<storage, read> pool : array<u32>;
@group(0) @binding(5) var<storage, read_write> drawn : array<vec2u>;       // packed: (instance, cluster)
@group(0) @binding(6) var<storage, read_write> args : array<u32, 4>;       // the draw's: corners, clusters, 0, 0
@group(0) @binding(7) var<storage, read_write> stretch : array<vec2u>;     // each copy's chosen clusters
@group(0) @binding(8) var<storage, read_write> counts : array<vec2u>;      // per copy: how many chosen, where its list starts when packed
@group(0) @binding(9) var oldWalls : texture_2d<f32>;   // the depth pyramids: the farthest depth over each patch,
@group(0) @binding(10) var newWalls : texture_2d<f32>;  // last frame's, and this frame's first pass's
fn poolF(i : u32) -> f32 { return bitcast<f32>(pool[i]); }
fn poolV4(i : u32) -> vec4f { return vec4f(poolF(i), poolF(i + 1u), poolF(i + 2u), poolF(i + 3u)); }
fn looks(err : f32, c : vec3f, r : f32) -> f32 {
  let d = length(c - cu.eye.xyz) - r;
  if (d <= 1e-4) { return select(0.0, 3.0e38, err > 0.0); }
  return err / d * cu.eye.w;
}
// Is a sphere wholly behind the walls a depth pyramid (made in view `vp`)
// remembers? Its box's corners give the patch of screen it covers and its
// nearest depth; the pyramid's level where that patch is a texel or two says
// how far the farthest wall over it is. Anything it can't judge (behind the
// eye, past the view's edge) is not hidden.
fn hiddenIn(walls : texture_2d<f32>, vp : mat4x4f, bc : vec3f, br : f32, size0 : vec2f, levels : f32) -> bool {
  var lo = vec2f(1e9);
  var hi = vec2f(-1e9);
  var near = 2.0;
  for (var i = 0u; i < 8u; i++) {
    let corner = bc + br * vec3f(select(-1.0, 1.0, (i & 1u) != 0u), select(-1.0, 1.0, (i & 2u) != 0u), select(-1.0, 1.0, (i & 4u) != 0u));
    let q = vp * vec4f(corner, 1.0);
    if (q.w <= 1e-4) { return false; }
    let n = q.xyz / q.w;
    let uv = vec2f(n.x * 0.5 + 0.5, 0.5 - n.y * 0.5);
    lo = min(lo, uv);
    hi = max(hi, uv);
    near = min(near, n.z);
  }
  if (lo.x < 0.0 || lo.y < 0.0 || hi.x > 1.0 || hi.y > 1.0 || near <= 0.0) { return false; }
  // level 0's texel j covers the depth picture's pixels 2j..2j+2, level l's
  // texel k level l-1's 2k..2k+2: a point's texel on level l is its level-0
  // texel halved l times (size0 is the depth picture's size halved, unrounded)
  let span = (hi - lo) * size0;
  let lvl = u32(clamp(ceil(log2(max(max(span.x, span.y), 1.0))), 0.0, levels - 1.0));
  let scale = exp2(f32(lvl));
  let last = vec2i(ceil(ceil(size0) / scale)) - 1;
  let a = min(vec2i(floor(lo * size0 / scale)), last);
  let b = min(vec2i(floor(hi * size0 / scale)), last);
  let far = max(max(textureLoad(walls, a, lvl).x, textureLoad(walls, vec2i(b.x, a.y), lvl).x),
                max(textureLoad(walls, vec2i(a.x, b.y), lvl).x, textureLoad(walls, b, lvl).x));
  return near > far;
}
fn chosen(m : mat4x4f, scale : f32, id : u32) -> bool {
  let b = pool[1] + id * 16u;
  let bounds = poolV4(b);
  let lod = poolV4(b + 4u);
  let parent = poolV4(b + 8u);
  let err = vec2f(poolF(b + 12u), poolF(b + 13u));
  if (looks(err.x * scale, (m * vec4f(lod.xyz, 1.0)).xyz, lod.w * scale) > cu.params.x) { return false; }
  if (err.y < 1.0e38 && looks(err.y * scale, (m * vec4f(parent.xyz, 1.0)).xyz, parent.w * scale) <= cu.params.x) { return false; }
  let bc = (m * vec4f(bounds.xyz, 1.0)).xyz;
  let br = bounds.w * scale;
  for (var p = 0; p < 6; p++) {
    if (dot(cu.planes[p].xyz, bc) + cu.planes[p].w < -br) { return false; }
  }
  // hidden behind what's nearer (phase 4): first last frame's walls, then
  // what that skipped is tried again against this frame's own first pass
  let hiddenThen = cu.occl.x > 0.5 && hiddenIn(oldWalls, cu.oldView, bc, br, cu.occl.zw, cu.occl.y);
  if (cu.occl.x > 1.5) { return hiddenThen && !hiddenIn(newWalls, cu.newView, bc, br, cu.occl2.yz, cu.occl2.x); }
  return !hiddenThen;
}
var<workgroup> found : array<u32, 64>;
@compute @workgroup_size(64) fn cull(@builtin(workgroup_id) wg : vec3u, @builtin(num_workgroups) nwg : vec3u, @builtin(local_invocation_index) li : u32) {
  let k = wg.x + wg.y * nwg.x;
  if (f32(k) >= cu.params.y) { return; }  // the same for the whole group
  let c = cullList[k];
  let inst = instances[c.x];
  let mesh = meshes[inst.material.y];
  let m = inst.model;
  let scale = max(length(m[0].xyz), max(length(m[1].xyz), length(m[2].xyz)));
  // each of the 64 takes a run of clusters, in order
  let run = (mesh.clusterCount + 63u) / 64u;
  let first = min(li * run, mesh.clusterCount);
  let last = min(first + run, mesh.clusterCount);
  var mine = 0u;
  for (var i = first; i < last; i++) {
    if (chosen(m, scale, mesh.clusterFirst + i)) { mine++; }
  }
  found[li] = mine;
  workgroupBarrier();
  var at = 0u;
  for (var j = 0u; j < li; j++) { at += found[j]; }
  for (var i = first; i < last; i++) {
    if (chosen(m, scale, mesh.clusterFirst + i)) {
      stretch[c.y + at] = vec2u(c.x, mesh.clusterFirst + i);
      at++;
    }
  }
  if (li == 63u) { counts[k] = vec2u(at, 0u); }
}
// where every copy's list starts, in copy order (one group counts them all up)
var<workgroup> sums : array<u32, 256>;
@compute @workgroup_size(256) fn count(@builtin(local_invocation_index) li : u32) {
  let n = u32(cu.params.y);
  let run = (n + 255u) / 256u;
  let first = min(li * run, n);
  let last = min(first + run, n);
  var mine = 0u;
  for (var k = first; k < last; k++) { mine += counts[k].x; }
  sums[li] = mine;
  workgroupBarrier();
  var at = 0u;
  for (var j = 0u; j < li; j++) { at += sums[j]; }
  for (var k = first; k < last; k++) {
    counts[k].y = at;
    at += counts[k].x;
  }
  if (li == 255u) {
    args[0] = 128u * 3u;
    args[1] = at;
    args[2] = 0u;
    args[3] = 0u;
  }
}
@compute @workgroup_size(64) fn pack(@builtin(workgroup_id) wg : vec3u, @builtin(num_workgroups) nwg : vec3u, @builtin(local_invocation_index) li : u32) {
  let k = wg.x + wg.y * nwg.x;
  if (f32(k) >= cu.params.y) { return; }
  let c = cullList[k];
  let n = counts[k];
  for (var i = li; i < n.x; i += 64u) { drawn[n.y + i] = stretch[c.y + i]; }
}
)";

}  // namespace loop::gpu
