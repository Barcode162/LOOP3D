#pragma once
// Part of the live renderer's shaders (engine/gpu/shaders.h).

namespace loop::gpu {

inline const char* kExposureWgsl = R"(
struct Params {
  size : vec4f,  // w, h, dt, first-guess exposure
  limits : vec4f,  // min exposure, max exposure, speed up, speed down
};
@group(0) @binding(0) var<uniform> P : Params;
@group(0) @binding(1) var hdr : texture_2d<f32>;
@group(0) @binding(2) var<storage, read_write> exposure : array<f32>;
var<workgroup> partial : array<f32, 256>;
var<workgroup> counts : array<f32, 256>;
@compute @workgroup_size(16, 16) fn main(@builtin(local_invocation_index) li : u32, @builtin(local_invocation_id) lid : vec3u) {
  // one workgroup walks the whole (downsampled) frame on a grid
  let W = u32(P.size.x);
  let H = u32(P.size.y);
  var s = 0.0;
  var n = 0.0;
  let stepX = max(1u, W / 64u);
  let stepY = max(1u, H / 64u);
  for (var y = lid.y * stepY; y < H; y += 16u * stepY) {
    for (var x = lid.x * stepX; x < W; x += 16u * stepX) {
      let c = textureLoad(hdr, vec2u(x, y), 0).rgb;
      let l = dot(c, vec3f(0.2126, 0.7152, 0.0722));
      // centre-weighted metering
      let u = vec2f(f32(x) / f32(W), f32(y) / f32(H)) - 0.5;
      let w = 1.0 - min(1.0, dot(u, u) * 2.0) * 0.6;
      s += log(max(l, 1e-4)) * w;
      n += w;
    }
  }
  partial[li] = s;
  counts[li] = n;
  workgroupBarrier();
  for (var k = 128u; k > 0u; k >>= 1u) {
    if (li < k) {
      partial[li] += partial[li + k];
      counts[li] += counts[li + k];
    }
    workgroupBarrier();
  }
  if (li == 0u) {
    // the picture was stored pre-exposed by `used`: undo it to meter true light
    let current = exposure[0];
    let used = select(P.size.w, current, current > 0.0);
    exposure[1] = used;
    let avg = exp(partial[0] / max(counts[0], 1.0)) / used;
    let goal = clamp(0.18 / max(avg, 1e-9), P.limits.x, P.limits.y);
    if (current <= 0.0) { exposure[0] = goal; return; }
    // eyes open to the dark slowly, close to the bright quickly
    let speed = select(P.limits.w, P.limits.z, goal < current);
    let k = 1.0 - exp(-P.size.z * speed);
    exposure[0] = exp(mix(log(current), log(goal), k));
  }
}
)";

}  // namespace loop::gpu
