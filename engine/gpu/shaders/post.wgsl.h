#pragma once
// Part of the live renderer's shaders (engine/gpu/shaders.h).

namespace loop::gpu {

inline const char* kPostWgsl = R"(
struct Post {
  exposure : vec4f,     // x fixed exposure, y compensation (stops), z auto, w sharpen
  look0 : vec4f,        // contrast, saturation, tonemap (0 agx 1 aces 2 filmic 3 linear), mono
  wb : vec4f,           // white balance gains, w: lens distortion
  lift : vec4f,         // w: grain size
  gamma : vec4f,        // w: bloom size (1 = normal)
  gain : vec4f,         // w: edge smoothing (FXAA) on
  shadowTint : vec4f, highlightTint : vec4f,
  fx : vec4f,           // bloom, vignette, grain, letterbox aspect
  fx2 : vec4f,          // black level, time, halation, chromatic aberration
  screen : vec4f,       // w, h, fade, unused
};
@group(0) @binding(0) var<uniform> post : Post;
@group(0) @binding(1) var hdr : texture_2d<f32>;
@group(0) @binding(2) var bloomTex : texture_2d<f32>;
@group(0) @binding(3) var linearSampler : sampler;
@group(0) @binding(4) var<storage, read> exposureBuf : array<f32>;
// where this pass's two pictures are: the frame is drawn into the top-left
// part of full-size targets (automatic resolution), so uv * scale reaches it,
// clamped to its edge texels (nothing from outside it, nothing wrapped round)
struct PassU { a : vec4f, b : vec4f };  // per picture: xy uv scale, zw largest uv
@group(0) @binding(5) var<uniform> pu : PassU;
fn inA(uv : vec2f) -> vec2f { return clamp(uv, 0.5 / vec2f(textureDimensions(hdr)), pu.a.zw); }
fn inB(uv : vec2f) -> vec2f { return clamp(uv, 0.5 / vec2f(textureDimensions(bloomTex)), pu.b.zw); }

struct Out {
  @builtin(position) clip : vec4f,
  @location(0) uv : vec2f,
};
@vertex fn vsFull(@builtin(vertex_index) vi : u32) -> Out {
  let p = vec2f(f32((vi << 1u) & 2u), f32(vi & 2u));
  var o : Out;
  o.clip = vec4f(p * 2.0 - 1.0, 0.0, 1.0);
  o.uv = vec2f(p.x, 1.0 - p.y);
  return o;
}
fn lum(c : vec3f) -> f32 { return dot(c, vec3f(0.2126, 0.7152, 0.0722)); }
// The glow's six levels are summed on the way up, each wider one counting
// three quarters of the one before (film's weights, 1 / (1 + level / 2), are
// close to that); their sum, to make the total an average. Summed unweighted
// and never divided, the glow was six times film's: a veil over the picture.
const kGlowUp = 0.75;
const kGlowSum = 3.288;  // 1 + 0.75 + 0.75^2 + ... + 0.75^5
fn agx(c0 : vec3f) -> vec3f {
  let inM = mat3x3f(vec3f(0.842479062253094, 0.0423282422610123, 0.0423756549057051),
                    vec3f(0.0784335999999992, 0.878468636469772, 0.0784336),
                    vec3f(0.0792237451477643, 0.0791661274605434, 0.879142973793104));
  let outM = mat3x3f(vec3f(1.19687900512017, -0.0528968517574562, -0.0529716355144438),
                     vec3f(-0.0980208811401368, 1.15190312990417, -0.0980434501171241),
                     vec3f(-0.0990297440797205, -0.0989611768448433, 1.15107367264116));
  var c = inM * max(c0, vec3f(1e-10));
  c = clamp((log2(c) + 12.47393) / (12.47393 + 4.026069), vec3f(0.0), vec3f(1.0));
  let x2 = c * c;
  let x4 = x2 * x2;
  c = 15.5 * x4 * x2 - 40.14 * x4 * c + 31.96 * x4 - 6.868 * x2 * c + 0.4298 * x2 + 0.1191 * c - 0.00232;
  return pow(clamp(outM * c, vec3f(0.0), vec3f(1.0)), vec3f(2.2));
}
fn aces(c : vec3f) -> vec3f {
  let inM = mat3x3f(vec3f(0.59719, 0.07600, 0.02840), vec3f(0.35458, 0.90834, 0.13383), vec3f(0.04823, 0.01566, 0.83777));
  let outM = mat3x3f(vec3f(1.60475, -0.10208, -0.00327), vec3f(-0.53108, 1.10813, -0.07276), vec3f(-0.07367, -0.00605, 1.07602));
  let v = inM * c;
  let a = v * (v + 0.0245786) - 0.000090537;
  let b = v * (0.983729 * v + 0.4329510) + 0.238081;
  return clamp(outM * (a / b), vec3f(0.0), vec3f(1.0));
}
fn hable(x : vec3f) -> vec3f {
  let A = 0.22; let B = 0.30; let C = 0.10; let D = 0.20; let E = 0.01; let F = 0.30;
  return ((x * (A * x + C * B) + D * E) / (x * (A * x + B) + D * F)) - E / F;
}
fn toSrgb(c : vec3f) -> vec3f {
  let lo = c * 12.92;
  let hi = 1.055 * pow(c, vec3f(1.0 / 2.4)) - 0.055;
  return select(hi, lo, c <= vec3f(0.0031308));
}
fn grain(p : vec2f, t : f32) -> f32 {
  let q = fract(vec3f(p, t) * vec3f(0.1031, 0.1030, 0.0973));
  let r = q + dot(q, q.yzx + 33.33);
  return fract((r.x + r.y) * r.z) * 2.0 - 1.0;
}

@fragment fn fsPost(i : Out) -> @location(0) vec4f {
  let W = post.screen.x;
  let H = post.screen.y;
  // cinema bars
  if (post.fx.w > 0.0 && W / H < post.fx.w + 2.0) {
    let barH = (H - W / post.fx.w) * 0.5 / H;
    if (barH > 0.0 && (i.uv.y < barH || i.uv.y > 1.0 - barH)) { return vec4f(0.0, 0.0, 0.0, 1.0); }
  }
  // lens distortion: + barrel, - pincushion
  var uv = i.uv;
  if (post.wb.w != 0.0) {
    let q = uv - 0.5;
    uv = 0.5 + q * (1.0 - post.wb.w * dot(q, q) * 2.0);
    if (uv.x < 0.0 || uv.y < 0.0 || uv.x > 1.0 || uv.y > 1.0) { return vec4f(0.0, 0.0, 0.0, 1.0); }
  }
  let huv = uv * pu.a.xy;  // in the picture
  let buv = inB(uv * pu.b.xy);  // in the bloom
  if (post.screen.w > 0.5) {  // debug views
    let raw = textureSampleLevel(hdr, linearSampler, inA(huv), 0.0).rgb;
    if (post.screen.w < 1.5 || post.screen.w > 3.5) { return vec4f(clamp(raw, vec3f(0.0), vec3f(1.0)), 1.0); }
    if (post.screen.w < 2.5) { return vec4f(exposureBuf[0] * 1000.0, exposureBuf[1] * 1000.0, 0.0, 1.0); }
    let b = textureSampleLevel(bloomTex, linearSampler, buv, 0.0).rgb / kGlowSum;
    return vec4f(clamp(b, vec3f(0.0), vec3f(1.0)), 1.0);
  }
  let cuv = uv - 0.5;
  let r2 = dot(cuv, cuv) * 2.0;
  // lens fringes
  let ca = cuv * (post.fx2.w / W) * pu.a.xy;
  var c = vec3f(textureSampleLevel(hdr, linearSampler, inA(huv + ca), 0.0).r,
                textureSampleLevel(hdr, linearSampler, inA(huv), 0.0).g,
                textureSampleLevel(hdr, linearSampler, inA(huv - ca), 0.0).b);
  // sharpening and edge smoothing look at the four neighbours
  if (post.exposure.w > 0.0 || post.gain.w > 0.5) {
    let tp = 1.0 / vec2f(textureDimensions(hdr));
    let n0 = textureSampleLevel(hdr, linearSampler, inA(huv + vec2f(tp.x, 0.0)), 0.0).rgb;
    let n1 = textureSampleLevel(hdr, linearSampler, inA(huv - vec2f(tp.x, 0.0)), 0.0).rgb;
    let n2 = textureSampleLevel(hdr, linearSampler, inA(huv + vec2f(0.0, tp.y)), 0.0).rgb;
    let n3 = textureSampleLevel(hdr, linearSampler, inA(huv - vec2f(0.0, tp.y)), 0.0).rgb;
    let avg = (n0 + n1 + n2 + n3) * 0.25;
    if (post.gain.w > 0.5) {
      // edge smoothing: where brightness jumps (in log terms), blend toward the neighbours
      let lc = log2(lum(c) + 1e-4);
      let lmin = min(min(log2(lum(n0) + 1e-4), log2(lum(n1) + 1e-4)), min(log2(lum(n2) + 1e-4), log2(lum(n3) + 1e-4)));
      let lmax = max(max(log2(lum(n0) + 1e-4), log2(lum(n1) + 1e-4)), max(log2(lum(n2) + 1e-4), log2(lum(n3) + 1e-4)));
      let edge = smoothstep(0.5, 2.0, max(lmax, lc) - min(lmin, lc));
      c = mix(c, (c + avg * 2.0) / 3.0, edge);
    }
    // sharpened, but never past its neighbours' own brightness (unbounded, the sky
    // beside a dark branch overshot to a white rim: your phone, the world's trees)
    if (post.exposure.w > 0.0) {
      let lo = min(min(min(n0, n1), min(n2, n3)), c);
      let hi = max(max(max(n0, n1), max(n2, n3)), c);
      c = clamp(c + (c - avg) * post.exposure.w * 2.0, lo, hi);
    }
  }
  // the picture was stored at exposureBuf[1]; the eyes now want exposureBuf[0]
  let ratio = select(1.0, exposureBuf[0] / max(exposureBuf[1], 1e-12), post.exposure.z > 0.5);
  let exposure = ratio * exp2(post.exposure.y);
  c *= exposure;
  // the glow: its levels' weighted average (kGlowSum: what fsUp's weights add up to), as film's
  let b = textureSampleLevel(bloomTex, linearSampler, buv, 0.0).rgb * (exposure / kGlowSum);
  c = c + (b - min(c, vec3f(8.0))) * post.fx.x;
  // halation: film's red glow, round what is bright only (the glow of everything gave a red veil
  // over the whole picture, where film's is the glow of the brights alone)
  c += b * saturate((lum(b) - 0.9) / 2.0) * vec3f(1.0, 0.25, 0.05) * post.fx2.z * 2.0;
  c *= post.wb.rgb;
  let cos4 = 1.0 / ((1.0 + r2 * 0.25) * (1.0 + r2 * 0.25));
  c *= mix(1.0, cos4 * (1.0 - 0.4 * r2), post.fx.y);
  // grade: contrast in log around middle grey, saturation, tone curve
  c = 0.18 * pow(max(c, vec3f(1e-7)) / 0.18, vec3f(post.look0.x));
  let y = lum(c);
  c = max(vec3f(0.0), vec3f(y) + (c - vec3f(y)) * post.look0.y);
  var t : vec3f;
  let tm = i32(post.look0.z);
  if (tm == 0) { t = agx(c); }
  else if (tm == 1) { t = aces(c); }
  else if (tm == 2) { t = clamp(hable(c * 2.0) / hable(vec3f(11.2)), vec3f(0.0), vec3f(1.0)); }
  else { t = min(c, vec3f(1.0)); }
  let ty = lum(t);
  t += post.shadowTint.rgb * (1.0 - smoothstep(0.0, 0.5, ty)) + post.highlightTint.rgb * smoothstep(0.4, 1.0, ty);
  t = clamp(t, vec3f(0.0), vec3f(1.0));
  t = t * post.gain.rgb + post.lift.rgb * (1.0 - t);
  t = pow(max(t, vec3f(0.0)), 1.0 / max(post.gamma.rgb, vec3f(0.01)));
  t = post.fx2.x + t * (1.0 - post.fx2.x);
  if (post.look0.w > 0.5) { t = vec3f(dot(t, vec3f(0.25, 0.65, 0.1))); }
  if (post.fx.z > 0.0) {
    let n = grain(floor(i.uv * vec2f(W, H) / max(post.lift.w, 0.5)), post.fx2.y * 60.0);
    let g = lum(t);
    t = max(vec3f(0.0), t + vec3f(n * post.fx.z * (0.4 + 2.4 * g * (1.0 - g))));
  }
  var outc = toSrgb(clamp(t, vec3f(0.0), vec3f(1.0)));
  // a still, invisible dither against banding (never re-rolled: nothing fizzes)
  outc += vec3f(grain(i.uv * vec2f(W, H) + 17.0, 0.0) / 255.0);
  return vec4f(outc * post.screen.z, 1.0);
}

// ---- bloom: a chain of half-size copies, then summed back up
@fragment fn fsDown(i : Out) -> @location(0) vec4f {
  let d = textureDimensions(hdr);
  let px = 1.0 / vec2f(d);
  // 13-tap filter (Jimenez 2014), capped so the sun doesn't flood the frame
  var c = vec3f(0.0);
  let o = array<vec2f, 13>(vec2f(0, 0), vec2f(-1, -1), vec2f(1, -1), vec2f(-1, 1), vec2f(1, 1), vec2f(-2, -2), vec2f(0, -2), vec2f(2, -2),
                            vec2f(-2, 0), vec2f(2, 0), vec2f(-2, 2), vec2f(0, 2), vec2f(2, 2));
  let w = array<f32, 13>(0.125, 0.125, 0.125, 0.125, 0.125, 0.03125, 0.0625, 0.03125, 0.0625, 0.0625, 0.03125, 0.0625, 0.03125);
  let suv = i.uv * pu.a.xy;
  for (var k = 0; k < 13; k++) {
    var s = textureSampleLevel(hdr, linearSampler, inA(suv + o[k] * px), 0.0).rgb;
    let bad = (bitcast<vec3u>(s) & vec3u(0x7fffffffu)) >= vec3u(0x7f800000u);
    s = select(s, vec3f(0.0), bad);
    let l = lum(s);
    let e = select(1.0, exposureBuf[0] / max(exposureBuf[1], 1e-12), post.exposure.z > 0.5);
    let cap = 8.0 / max(e, 1e-9);
    if (l > cap) { s *= cap / l; }
    c += s * w[k];
  }
  return vec4f(c, 1.0);
}
@fragment fn fsUp(i : Out) -> @location(0) vec4f {
  let d = textureDimensions(hdr);
  let px = 1.0 / vec2f(d);
  var c = vec3f(0.0);
  for (var y = -1; y <= 1; y++) {
    for (var x = -1; x <= 1; x++) {
      let w = select(select(0.0625, 0.125, x == 0 || y == 0), 0.25, x == 0 && y == 0);
      c += textureSampleLevel(hdr, linearSampler, inA(i.uv * pu.a.xy + vec2f(f32(x), f32(y)) * px * 1.5 * post.gamma.w), 0.0).rgb * w;
    }
  }
  return vec4f(c * kGlowUp, 1.0);
}
)";

}  // namespace loop::gpu
