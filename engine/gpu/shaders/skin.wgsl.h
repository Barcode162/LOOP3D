#pragma once
// Part of the live renderer's shaders (engine/gpu/shaders.h).

namespace loop::gpu {

// Live skinning (docs/STUDIO.md, T-S3): a skin's points bent by its bones on
// the GPU, the same sum the film's bend makes on the processor
// (engine/anim/skin.cpp): each point goes to the pull-weighted mix of its
// bones' matrices, and its normal turns by that mix's inverse transpose (the
// cross products of its columns). It writes over the point's place and
// normal in a vertex list of nine numbers a point (the mesh's own buffer, or
// its stretch of the cluster pool); its uv and marks stay as they were.
inline const char* kSkinWgsl = R"(
struct SkinU { count : u32, base : u32, pad0 : u32, pad1 : u32 };  // points, and where the first one starts (in numbers)
struct Rest {
  p : vec4f,        // where it rests (xyz)
  n : vec4f,        // its normal at rest (xyz)
  bone : vec4u,     // its eight bones, two to a number (low half first)
  pullA : vec4f,    // how hard bones 0..3 pull
  pullB : vec4f,    // and 4..7
};
@group(0) @binding(0) var<uniform> su : SkinU;
@group(0) @binding(1) var<storage, read> rest : array<Rest>;
@group(0) @binding(2) var<storage, read> bones : array<mat4x4f>;
@group(0) @binding(3) var<storage, read_write> outv : array<f32>;

fn boneOf(r : Rest, k : u32) -> u32 {
  let w = r.bone[k / 2u];
  return select(w & 0xffffu, w >> 16u, (k & 1u) == 1u);
}

@compute @workgroup_size(64)
fn bend(@builtin(global_invocation_id) id : vec3u) {
  let i = id.x;
  if (i >= su.count) { return; }
  let r = rest[i];
  let nb = arrayLength(&bones);
  var m = mat4x4f(vec4f(0.0), vec4f(0.0), vec4f(0.0), vec4f(0.0));
  for (var k = 0u; k < 8u; k++) {
    let w = select(r.pullB[k & 3u], r.pullA[k & 3u], k < 4u);
    if (w <= 0.0) { break; }
    let b = boneOf(r, k);
    if (b >= nb) { continue; }
    m += bones[b] * w;
  }
  let x = m[0].xyz;
  let y = m[1].xyz;
  let z = m[2].xyz;
  let p = x * r.p.x + y * r.p.y + z * r.p.z + m[3].xyz;
  let bent = cross(y, z) * r.n.x + cross(z, x) * r.n.y + cross(x, y) * r.n.z;
  let n = select(r.n.xyz, normalize(bent), dot(bent, bent) > 1e-20);
  let o = su.base + i * 9u;
  outv[o] = p.x;
  outv[o + 1u] = p.y;
  outv[o + 2u] = p.z;
  outv[o + 3u] = n.x;
  outv[o + 4u] = n.y;
  outv[o + 5u] = n.z;
}
)";

}  // namespace loop::gpu
