#pragma once
// Part of the live renderer's shaders (engine/gpu/shaders.h).

namespace loop::gpu {

// ---- Ultra's noise filter: an edge-aware a-trous wavelet (Dammertz et al.)
// on light divided by surface colour, guided by each pixel's surface
// direction and distance, so edges and textures stay sharp.
inline const char* kDenoiseWgsl = R"(
struct Px { color : vec4f, nd : vec4f, albedo : vec4f };
struct P {
  size : vec4u,     // blocks across, blocks down, step, last pass
  screen : vec4u,   // width, height, block size, samples so far
  exposure : vec4f, // fixed exposure, auto (1/0)
};
@group(0) @binding(0) var<uniform> prm : P;
@group(0) @binding(1) var<storage, read> px : array<Px>;
@group(0) @binding(2) var<storage, read> src : array<vec4f>;
@group(0) @binding(3) var<storage, read_write> dst : array<vec4f>;
@group(0) @binding(4) var outTex : texture_storage_2d<rgba16float, write>;
@group(0) @binding(5) var<storage, read> exposureState : array<f32>;

fn lum(c : vec3f) -> f32 { return dot(c, vec3f(0.2126, 0.7152, 0.0722)); }

@compute @workgroup_size(8, 8) fn main(@builtin(global_invocation_id) g : vec3u) {
  let bw = prm.size.x;
  let bh = prm.size.y;
  if (g.x >= bw || g.y >= bh) { return; }
  let i = g.y * bw + g.x;
  let step = i32(prm.size.z);
  let me = px[i];
  // the first pass reads the path tracer's average (as light per unit colour)
  var c0 : vec3f;
  let fin = (bitcast<vec3u>(me.color.rgb) & vec3u(0x7fffffffu)) >= vec3u(0x7f800000u);
  if (step == 0) { c0 = select(me.color.rgb, vec3f(0.0), fin) / max(me.albedo.rgb, vec3f(0.01)); }
  else { c0 = src[i].rgb; }
  var outc = c0;
  if (step > 0) {
    let k = array<f32, 3>(0.375, 0.25, 0.0625);
    let l0 = lum(c0);
    // fewer samples, noisier: let more neighbours in
    let n = f32(prm.screen.w) + 1.0;
    let sigmaL = (0.5 + 3.0 / sqrt(n)) * (l0 + 0.02 * (l0 + 1e-3) + 1e-4);
    var sum = vec3f(0.0);
    var wsum = 0.0;
    for (var y = -2; y <= 2; y++) {
      for (var x = -2; x <= 2; x++) {
        let q = vec2i(i32(g.x) + x * step, i32(g.y) + y * step);
        if (q.x < 0 || q.y < 0 || q.x >= i32(bw) || q.y >= i32(bh)) { continue; }
        let j = u32(q.y) * bw + u32(q.x);
        let o = px[j];
        let c = src[j].rgb;
        let wn = pow(max(dot(me.nd.xyz, o.nd.xyz), 0.0), 16.0);
        let wd = exp(-abs(me.nd.w - o.nd.w) / (0.02 * me.nd.w + 0.01));
        // early passes average freely within a surface; the last keeps light edges
        let wl = select(1.0, exp(-abs(lum(c) - l0) / sigmaL), step >= 4);
        let w = k[abs(x)] * k[abs(y)] * wn * wd * wl;
        sum += c * w;
        wsum += w;
      }
    }
    outc = select(c0, sum / wsum, wsum > 1e-6);
  }
  dst[i] = vec4f(outc, 1.0);
  if (prm.size.w == 1u) {
    let e = exposureState[0];
    let pre = select(prm.exposure.x, e, prm.exposure.y > 0.5 && e > 0.0);
    let color = vec4f(min(outc * me.albedo.rgb * pre, vec3f(60000.0)), 1.0);
    let B = prm.screen.z;
    for (var by = 0u; by < B; by++) {
      for (var bx = 0u; bx < B; bx++) {
        let p = vec2u(g.x * B + bx, g.y * B + by);
        if (p.x < prm.screen.x && p.y < prm.screen.y) { textureStore(outTex, p, color); }
      }
    }
  }
}
)";

}  // namespace loop::gpu
