#pragma once
// Part of the live renderer's shaders (engine/gpu/shaders.h).

namespace loop::gpu {

// Auto exposure: the frame's average log brightness, eased over time like eyes.
// Frame blending (TAA): every frame the view moves a fraction of a pixel (a
// fixed pattern of 8), and each pixel is blended with where its point was in
// the frames before, so edges come out smooth and fine detail stops
// shimmering. The past is kept within the range of this pixel's neighbours
// (so moving things don't smear), and bright pixels count less (so one hot
// pixel doesn't flicker).
inline const char* kTaaWgsl = R"(
// Frame blending that upscales (phase 7): every pixel of the full-size
// picture takes the fresh sample that landed nearest it this frame (each
// frame's view is shifted a fraction of a pixel, so over a few frames the
// samples fill in every place), trusting it more the nearer it landed, and
// keeps the rest from its past, read where that point was last frame and
// kept within the colours round the fresh sample (so nothing smears).
struct TaaU {
  prevViewProj : mat4x4f,  // last frame's, without its shift
  curViewProj : mat4x4f,   // this frame's, without its shift
  invViewProj : mat4x4f,   // this frame's, without its shift (the full-size pixels aren't shifted)
  screen : vec4f,          // drawn width, height, 1/width, 1/height
  full : vec4f,            // the full picture's width, height; the fresh sample's most weight; 1: start afresh
  jitter : vec4f,          // where this frame's samples landed (drawn pixels from their middle); the drawn picture's texture size
};
@group(0) @binding(0) var<uniform> tu : TaaU;
@group(0) @binding(1) var cur : texture_2d<f32>;
@group(0) @binding(2) var hist : texture_2d<f32>;
@group(0) @binding(3) var depthTex : texture_depth_2d;
@group(0) @binding(4) var lin : sampler;
@group(0) @binding(5) var seen : texture_2d<u32>;            // which copy each pixel shows (+1; 0: none)
@group(0) @binding(6) var<storage, read> back : array<mat4x4f>;  // each copy: now -> where it was last frame
struct Out { @builtin(position) pos : vec4f };
@vertex fn vsTaa(@builtin(vertex_index) vi : u32) -> Out {
  let p = vec2f(f32((vi << 1u) & 2u), f32(vi & 2u));
  return Out(vec4f(p * 2.0 - 1.0, 0.0, 1.0));
}
fn toYCoCg(c : vec3f) -> vec3f { return vec3f(dot(c, vec3f(0.25, 0.5, 0.25)), dot(c, vec3f(0.5, 0.0, -0.5)), dot(c, vec3f(-0.25, 0.5, -0.25))); }
fn fromYCoCg(c : vec3f) -> vec3f { return vec3f(c.x + c.y - c.z, c.x + c.z, c.x - c.y - c.z); }
fn tame(c : vec3f) -> vec3f { return c / (1.0 + max(c.r, max(c.g, c.b)) * 0.001); }       // brightness counts less (light is stored pre-exposed)
fn untame(c : vec3f) -> vec3f { return c / max(1.0 - max(c.r, max(c.g, c.b)) * 0.001, 1e-4); }
// The past read sharply: a Catmull-Rom curve through 4x4 pixels (in 5
// samples), not a 2x2 blend, which would soften it a little every frame.
fn histSharp(uv : vec2f) -> vec3f {
  let size = tu.full.xy;
  let p = uv * size - 0.5;
  let f = fract(p);
  let t = floor(p) + 0.5;
  let w0 = f * (-0.5 + f * (1.0 - 0.5 * f));
  let w1 = 1.0 + f * f * (-2.5 + 1.5 * f);
  let w2 = f * (0.5 + f * (2.0 - 1.5 * f));
  let w3 = f * f * (-0.5 + 0.5 * f);
  let w12 = w1 + w2;
  let at0 = (t - 1.0) / size;
  let at3 = (t + 2.0) / size;
  let at12 = (t + w2 / w12) / size;
  var c = vec3f(0.0);
  c += textureSampleLevel(hist, lin, vec2f(at12.x, at0.y), 0.0).rgb * w12.x * w0.y;
  c += textureSampleLevel(hist, lin, vec2f(at0.x, at12.y), 0.0).rgb * w0.x * w12.y;
  c += textureSampleLevel(hist, lin, vec2f(at12.x, at12.y), 0.0).rgb * w12.x * w12.y;
  c += textureSampleLevel(hist, lin, vec2f(at3.x, at12.y), 0.0).rgb * w3.x * w12.y;
  c += textureSampleLevel(hist, lin, vec2f(at12.x, at3.y), 0.0).rgb * w12.x * w3.y;
  let wsum = w12.x * w0.y + w0.x * w12.y + w12.x * w12.y + w3.x * w12.y + w12.x * w3.y;
  return max(c / wsum, vec3f(0.0));
}
@fragment fn fsTaa(o : Out) -> @location(0) vec4f {
  let s = tu.screen.xy / tu.full.xy;          // drawn pixels per full-size pixel
  let r = o.pos.xy * s;                        // this pixel's middle, in drawn pixels
  let hi = vec2i(tu.screen.xy) - 1;
  // the drawn pixel whose sample landed nearest (pixel k's sample is at k + 0.5 + jitter)
  let k = clamp(vec2i(floor(r - tu.jitter.xy)), vec2i(0), hi);
  let d = r - (vec2f(k) + 0.5 + tu.jitter.xy);
  let c = tame(textureLoad(cur, k, 0).rgb);
  var lo3 = toYCoCg(c);
  var hi3 = lo3;
  for (var y = -1; y <= 1; y++) {
    for (var x = -1; x <= 1; x++) {
      let n = toYCoCg(tame(textureLoad(cur, clamp(k + vec2i(x, y), vec2i(0), hi), 0).rgb));
      lo3 = min(lo3, n);
      hi3 = max(hi3, n);
    }
  }
  // starting afresh: the drawn picture, blended up
  let fresh = tame(textureSampleLevel(cur, lin, (r - tu.jitter.xy) / tu.jitter.zw, 0.0).rgb);
  if (tu.full.w > 0.5) { return vec4f(untame(fresh), 1.0); }
  // where this pixel's point was last frame
  let dep = textureLoad(depthTex, k, 0);
  let uv = o.pos.xy / tu.full.xy;
  let ndc = vec2f(uv.x * 2.0 - 1.0, 1.0 - uv.y * 2.0);
  let w4 = tu.invViewProj * vec4f(ndc, dep, 1.0);
  let wp = vec4f(w4.xyz / w4.w, 1.0);
  // a thing that moved: where this point of it was last frame (still things and the sky stay)
  let id = textureLoad(seen, k, 0).x;
  var was = wp;
  if (id > 0u) { was = back[id - 1u] * wp; }
  let prev = tu.prevViewProj * was;
  let now = tu.curViewProj * wp;
  let motion = vec2f(prev.x / prev.w - now.x / now.w, now.y / now.w - prev.y / prev.w) * 0.5;
  let puv = uv + motion;
  if (prev.w <= 0.0 || any(puv < vec2f(0.0)) || any(puv > vec2f(1.0))) { return vec4f(untame(fresh), 1.0); }
  let h = tame(histSharp(puv));
  let kept = fromYCoCg(clamp(toYCoCg(h), lo3, hi3));
  // the fresh sample counts by how near it landed (a sample half a drawn pixel away: about half)
  let weight = tu.full.z * exp(-2.8 * dot(d, d));
  return vec4f(untame(mix(kept, c, weight)), 1.0);
}
)";

}  // namespace loop::gpu
