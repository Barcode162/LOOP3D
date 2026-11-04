#pragma once
// The live renderer's shaders (WGSL). They follow the cinema renderer's
// rules: the same materials and patterns, light in real units, the same
// sky, the same film steps, so the live picture aims at the same look.

namespace loop::gpu {

inline const char* kCommonWgsl = R"(
struct Frame {
  viewProj : mat4x4f,
  invViewProj : mat4x4f,
  shadowViewProj : mat4x4f,
  camPos : vec4f,        // xyz, time
  sunDir : vec4f,        // towards the sun, w = sun on
  sunColor : vec4f,      // illuminance (lux) as rgb
  sh : array<vec4f, 9>,  // sky light, spherical harmonics
  fog : vec4f,           // density, top, anisotropy, on
  fogColor : vec4f,
  params : vec4f,        // light count, exposure compensation, shadow size, auto exposure
  screen : vec4f,        // width, height, 1/width, 1/height
  fixedExposure : vec4f, // x: exposure when not auto
};
struct Light {
  pos : vec4f,     // xyz, radius
  color : vec4f,   // rgb candela (point/spot) or nits (panel)
  dir : vec4f,     // xyz, kind (0 point, 1 spot, 2 panel)
  cone : vec4f,    // cos inner, cos outer, panel w, panel h
};
struct Material {
  base : vec4f,      // rgb, roughness
  props : vec4f,     // metal, glass, clearcoat, sheen
  emission : vec4f,  // rgb nits, opacity
  pattern : vec4f,   // kind, scale, bump (mm), contrast
  colorB : vec4f,    // rgb, roughness variation
  extra : vec4f,     // subsurface, translucency, leaf, ior
};
struct Instance {
  model : mat4x4f,
  normal0 : vec4f, normal1 : vec4f, normal2 : vec4f,
  material : vec4u,
};
@group(0) @binding(0) var<uniform> frame : Frame;
@group(0) @binding(1) var<storage, read> lights : array<Light>;
@group(0) @binding(2) var<storage, read> materials : array<Material>;
@group(0) @binding(3) var<storage, read> instances : array<Instance>;
@group(0) @binding(4) var<storage, read> exposureState : array<f32>;

// Light is stored already exposed (like a camera's sensor), so even the sun's
// disk fits the half-float picture: last frame's exposure, or a first guess.
fn preExposure() -> f32 {
  let e = exposureState[0];
  if (frame.params.w > 0.5 && e > 0.0) { return e; }
  return frame.fixedExposure.x;
}
fn exposed(c : vec3f) -> vec3f { return min(c * preExposure(), vec3f(60000.0)); }

const PI = 3.14159265;

fn hash3(p : vec3f) -> f32 {
  var q = fract(p * 0.3183099 + vec3f(0.1, 0.2, 0.3));
  q *= 17.0;
  return fract(q.x * q.y * q.z * (q.x + q.y + q.z));
}
fn vnoise(p : vec3f) -> f32 {
  let i = floor(p);
  let f = fract(p);
  let u = f * f * (3.0 - 2.0 * f);
  return mix(mix(mix(hash3(i), hash3(i + vec3f(1, 0, 0)), u.x), mix(hash3(i + vec3f(0, 1, 0)), hash3(i + vec3f(1, 1, 0)), u.x), u.y),
             mix(mix(hash3(i + vec3f(0, 0, 1)), hash3(i + vec3f(1, 0, 1)), u.x), mix(hash3(i + vec3f(0, 1, 1)), hash3(i + vec3f(1, 1, 1)), u.x), u.y), u.z) * 2.0 - 1.0;
}
fn fbm(p0 : vec3f) -> f32 {
  var p = p0;
  var s = 0.0;
  var a = 1.0;
  var n = 0.0;
  for (var i = 0; i < 5; i++) {
    s += a * vnoise(p);
    n += a;
    a *= 0.5;
    p = p * 2.03 + vec3f(1.7, 9.2, 3.1);
  }
  return s / n;
}
fn planar(p : vec3f, n : vec3f) -> vec2f {
  let a = abs(n);
  if (a.y >= a.x && a.y >= a.z) { return p.xz; }
  if (a.x >= a.z) { return p.zy; }
  return p.xy;
}
fn cellHash(v : vec2f) -> f32 { return hash3(vec3f(v, 7.0)); }

// The pattern's mix (x), height (y) and brightness tint (z), as the cinema renderer.
fn pattern(m : Material, p : vec3f, n : vec3f) -> vec3f {
  let kind = i32(m.pattern.x);
  let q = p * m.pattern.y;
  let pl = planar(q, n);
  switch (kind) {
    case 1: { // checker
      let c = i32(floor(pl.x)) + i32(floor(pl.y));
      return vec3f(f32(c & 1), 0.5, 1.0);
    }
    case 2: { // tiles
      let g = fract(pl);
      let e = min(min(g.x, 1.0 - g.x), min(g.y, 1.0 - g.y));
      let grout = 1.0 - smoothstep(0.015, 0.03, e);
      return vec3f(grout, 1.0 - grout * 0.8, 0.88 + 0.24 * cellHash(floor(pl)));
    }
    case 3: { // bricks
      var b = vec2f(pl.x / 0.225, pl.y / 0.075);
      let row = floor(b.y);
      if ((i32(row) & 1) == 1) { b.x += 0.5; }
      let g = fract(b);
      let e = min(min(g.x, 1.0 - g.x) * 0.225, min(g.y, 1.0 - g.y) * 0.075);
      let mortar = 1.0 - smoothstep(0.004, 0.007, e);
      let v = cellHash(vec2f(floor(b.x), row));
      return vec3f(mortar, 1.0 - mortar, 0.8 + 0.35 * v + fbm(q * 40.0) * 0.15);
    }
    case 4: { // wood
      let r = length(q.xz) * 6.0 + fbm(q * vec3f(1.5, 0.15, 1.5)) * 2.5;
      let ring = fract(r);
      let rings = smoothstep(0.0, 0.35, ring) * (1.0 - smoothstep(0.55, 1.0, ring));
      let grain = vnoise(q * vec3f(40.0, 1.5, 40.0)) * 0.5 + 0.5;
      let mx = clamp(rings * 0.7 + grain * 0.3, 0.0, 1.0) * m.pattern.w;
      return vec3f(mx, 1.0 - mx * 0.3, 1.0);
    }
    case 5: { // marble
      let t = fbm(q * 1.5);
      var v = sin((q.x + q.y * 0.5) * 3.0 + t * 9.0) * 0.5 + 0.5;
      v = pow(1.0 - v, 6.0);
      let t2 = fbm(q * 6.0 + vec3f(4.0));
      return vec3f(clamp(v * 1.4 + pow(abs(t2), 3.0) * 0.6, 0.0, 1.0) * m.pattern.w, 0.5, 1.0);
    }
    case 6: { // noise
      let v = fbm(q) * 0.5 + 0.5;
      return vec3f(clamp((v - 0.5) * 2.0 * m.pattern.w + 0.5, 0.0, 1.0), v, 1.0);
    }
    case 7: { // ground
      let slope = 1.0 - clamp(n.y, 0.0, 1.0);
      let patches = fbm(vec3f(p.x, 0.0, p.z) * (0.15 * m.pattern.y)) * 0.5 + 0.5;
      let fine = fbm(p * (4.0 * m.pattern.y)) * 0.5 + 0.5;
      let soil = smoothstep(0.55, 0.7, patches + fine * 0.2);
      let rock = smoothstep(0.25, 0.45, slope + fine * 0.1);
      return vec3f(max(soil * 0.6, rock), fine, 0.8 + 0.4 * fine);
    }
    case 8: { // planks
      let bx = pl.y / 0.14;
      let plank = floor(bx);
      let len = 1.2 + 1.2 * cellHash(vec2f(plank, 0.0));
      let ax = pl.x / len + cellHash(vec2f(plank, 1.0));
      let seg = floor(ax);
      let gx = fract(bx);
      let gy = fract(ax);
      let e = min(min(gx, 1.0 - gx) * 0.14, min(gy, 1.0 - gy) * len);
      let gap = 1.0 - smoothstep(0.0008, 0.002, e);
      let id = cellHash(vec2f(plank, seg));
      let r = abs(gx - 0.5) * 6.0 + fbm(vec3f(pl.x * 0.6, id * 9.0, gx * 2.0)) * 3.0;
      let ring = fract(r);
      let figure = smoothstep(0.0, 0.4, ring) * (1.0 - smoothstep(0.6, 1.0, ring));
      let grain = vnoise(vec3f(pl.x * 3.0, gx * 60.0, id * 50.0)) * 0.5 + 0.5;
      let mx = max(gap, clamp(figure * 0.55 + grain * 0.25, 0.0, 1.0) * m.pattern.w);
      return vec3f(mx, 1.0 - gap - figure * 0.05, 0.75 + 0.5 * id);
    }
    default: { return vec3f(0.0, 0.5, 1.0); }
  }
}
)";

inline const char* kSceneWgsl = R"(
@group(1) @binding(0) var shadowMap : texture_depth_2d;
@group(1) @binding(1) var shadowSampler : sampler_comparison;
@group(1) @binding(2) var skyTex : texture_2d<f32>;
@group(1) @binding(3) var skySampler : sampler;

struct VsIn {
  @location(0) pos : vec3f,
  @location(1) nrm : vec3f,
  @location(2) uv : vec2f,
};
struct VsOut {
  @builtin(position) clip : vec4f,
  @location(0) world : vec3f,
  @location(1) nrm : vec3f,
  @location(2) uv : vec2f,
  @location(3) @interpolate(flat) mat : u32,
};

@vertex fn vs(v : VsIn, @builtin(instance_index) ii : u32) -> VsOut {
  let inst = instances[ii];
  var o : VsOut;
  let w = inst.model * vec4f(v.pos, 1.0);
  o.world = w.xyz;
  o.clip = frame.viewProj * w;
  let nm = mat3x3f(inst.normal0.xyz, inst.normal1.xyz, inst.normal2.xyz);
  o.nrm = normalize(nm * v.nrm);
  o.uv = v.uv;
  o.mat = inst.material.x;
  return o;
}

fn dirToUv(d : vec3f) -> vec2f {
  let elev = asin(clamp(d.y, -1.0, 1.0));
  var az = atan2(d.z, d.x);
  if (az < 0.0) { az += 2.0 * PI; }
  let s = elev / (PI * 0.5);
  let v = 0.5 + 0.5 * sign(s) * sqrt(abs(s));
  return vec2f(az / (2.0 * PI), 1.0 - v);
}
fn skyRadiance(d : vec3f, lod : f32) -> vec3f { return textureSampleLevel(skyTex, skySampler, dirToUv(d), lod).rgb; }
fn shIrradiance(n : vec3f) -> vec3f {
  // irradiance from the sky's spherical harmonics (Ramamoorthi & Hanrahan)
  let c1 = 0.429043; let c2 = 0.511664; let c3 = 0.743125; let c4 = 0.886227; let c5 = 0.247708;
  let L = frame.sh;
  let x = n.x; let y = n.y; let z = n.z;
  return max(vec3f(0.0),
      c1 * L[8].rgb * (x * x - y * y) + c3 * L[6].rgb * z * z + c4 * L[0].rgb - c5 * L[6].rgb
    + 2.0 * c1 * (L[4].rgb * x * y + L[7].rgb * x * z + L[5].rgb * y * z)
    + 2.0 * c2 * (L[3].rgb * x + L[1].rgb * y + L[2].rgb * z));
}

fn D_ggx(nh : f32, a : f32) -> f32 {
  let a2 = a * a;
  let d = nh * nh * (a2 - 1.0) + 1.0;
  return a2 / (PI * d * d);
}
fn V_smith(nv : f32, nl : f32, a : f32) -> f32 {
  let a2 = a * a;
  let gv = nl * sqrt(nv * nv * (1.0 - a2) + a2);
  let gl = nv * sqrt(nl * nl * (1.0 - a2) + a2);
  return 0.5 / max(gv + gl, 1e-5);
}
fn schlick(f0 : vec3f, c : f32) -> vec3f { return f0 + (vec3f(1.0) - f0) * pow(1.0 - clamp(c, 0.0, 1.0), 5.0); }
// split-sum environment BRDF (Karis' analytic fit)
fn envBrdf(f0 : vec3f, rough : f32, nv : f32) -> vec3f {
  let c0 = vec4f(-1.0, -0.0275, -0.572, 0.022);
  let c1 = vec4f(1.0, 0.0425, 1.04, -0.04);
  let r = rough * c0 + c1;
  let a004 = min(r.x * r.x, exp2(-9.28 * nv)) * r.x + r.y;
  let ab = vec2f(-1.04, 1.04) * a004 + r.zw;
  return f0 * ab.x + ab.y;
}

fn shadowAt(world : vec3f, n : vec3f) -> f32 {
  let p = frame.shadowViewProj * vec4f(world + n * 0.03, 1.0);
  let uv = vec2f(p.x * 0.5 + 0.5, 0.5 - p.y * 0.5);
  if (uv.x < 0.0 || uv.x > 1.0 || uv.y < 0.0 || uv.y > 1.0 || p.z > 1.0) { return 1.0; }
  let texel = 1.0 / frame.params.z;
  var s = 0.0;
  // a 12-tap Poisson disk: soft edges
  let taps = array<vec2f, 12>(vec2f(-0.326, -0.406), vec2f(-0.840, -0.074), vec2f(-0.696, 0.457), vec2f(-0.203, 0.621),
                              vec2f(0.962, -0.195), vec2f(0.473, -0.480), vec2f(0.519, 0.767), vec2f(0.185, -0.893),
                              vec2f(0.507, 0.064), vec2f(0.896, 0.412), vec2f(-0.322, -0.933), vec2f(-0.792, -0.598));
  for (var i = 0; i < 12; i++) {
    s += textureSampleCompareLevel(shadowMap, shadowSampler, uv + taps[i] * texel * 1.8, p.z - 0.0015);
  }
  return s / 12.0;
}

struct Shaded {
  color : vec3f,
  alpha : f32,
};

fn shade(i : VsOut, front : bool) -> Shaded {
  let m = materials[i.mat];
  var n = normalize(i.nrm);
  if (!front) { n = -n; }
  let v = normalize(frame.camPos.xyz - i.world);
  var base = m.base.rgb;
  var rough = m.base.w;
  // leaves: cut to a leaf outline from the card's coordinates
  if (m.extra.z > 0.5) {
    let y = 1.0 - i.uv.y;
    let x = (i.uv.x - 0.5) * 2.0;
    let width = sin(PI * pow(clamp(y, 0.0, 1.0), 0.75)) * 0.95;
    if (abs(x) > width) { discard; }
  }
  if (m.pattern.x > 0.5) {
    var pv = pattern(m, i.world, n);
    // detail smaller than a pixel fades to its average (no shimmer, no moire)
    let kind = i32(m.pattern.x);
    let sc = max(m.pattern.y, 0.01);
    var feature = 0.5 / sc;
    var avg = 0.5;
    switch (kind) {
      case 1: { feature = 1.0 / sc; }
      case 2: { feature = 0.03 / sc; avg = 0.1; }
      case 3: { feature = 0.008 / sc; avg = 0.15; }
      case 4: { feature = 0.03 / sc; avg = 0.4; }
      case 5: { feature = 0.08 / sc; avg = 0.3; }
      case 6: { feature = 0.12 / sc; }
      case 7: { feature = 0.06 / sc; avg = 0.3; }
      case 8: { feature = 0.003 / sc; avg = 0.3; }
      default: {}
    }
    let lod = length(fwidth(i.world));
    let fade = smoothstep(0.4, 1.5, lod / feature);
    pv = vec3f(mix(pv.x, avg, fade), mix(pv.y, 0.5, fade), mix(pv.z, 1.0, fade * 0.5));
    base = mix(m.base.rgb, m.colorB.rgb, clamp(pv.x, 0.0, 1.0)) * pv.z;
    rough = clamp(rough + (pv.x - 0.5) * m.colorB.w, 0.02, 1.0);
    if (m.pattern.z > 0.0) {
      // bumps from the pattern's height, by screen-space differences
      let h = pv.y * m.pattern.z * 0.001;
      let dhx = dpdx(h);
      let dhy = dpdy(h);
      let dpx = dpdx(i.world);
      let dpy = dpdy(i.world);
      let r1 = cross(dpy, n);
      let r2 = cross(n, dpx);
      let det = dot(dpx, r1);
      if (abs(det) > 1e-12) {
        let g = (dhx * r1 + dhy * r2) / det;
        n = normalize(n - g);
      }
    }
  }
  let metal = m.props.x;
  let glass = m.props.y;
  let alpha = max(rough * rough, 0.002);
  let nv = max(dot(n, v), 1e-4);
  let f0 = mix(vec3f(0.04), base, metal);
  let diffuseColor = base * (1.0 - metal) * (1.0 - glass);
  var lo = vec3f(0.0);

  // the sun, with shadows
  if (frame.sunDir.w > 0.5) {
    let l = frame.sunDir.xyz;
    let nl = dot(n, l);
    let wrap = m.extra.y;  // translucent sheets let light through from behind
    if (nl > 0.0 || wrap > 0.0) {
      let sh = shadowAt(i.world, n);
      let h = normalize(l + v);
      let nh = max(dot(n, h), 0.0);
      let F = schlick(f0, max(dot(v, h), 0.0));
      let spec = D_ggx(nh, alpha) * V_smith(nv, max(nl, 1e-4), alpha) * F;
      let diff = diffuseColor * (1.0 - F) / PI;
      var c = (diff + spec) * max(nl, 0.0);
      c += base * wrap * max(-nl, 0.0) / PI * 0.6;
      lo += c * frame.sunColor.rgb * sh;
    }
  }
  // lamps (inverse-square, as bulbs with a size)
  let count = u32(frame.params.x);
  for (var k = 0u; k < count; k++) {
    let L = lights[k];
    var tl = L.pos.xyz - i.world;
    let d2 = max(dot(tl, tl), L.pos.w * L.pos.w);
    let l = tl * inverseSqrt(max(dot(tl, tl), 1e-8));
    let nl = dot(n, l);
    if (nl <= 0.0) { continue; }
    var I = L.color.rgb / d2;
    if (L.dir.w > 0.5 && L.dir.w < 1.5) {  // spot
      I *= smoothstep(L.cone.y, L.cone.x, dot(-l, L.dir.xyz));
    } else if (L.dir.w > 1.5) {  // panel: radiance x area x cosines
      I = L.color.rgb * L.cone.z * L.cone.w * max(dot(-l, L.dir.xyz), 0.0) / d2;
    }
    // reach: fade smoothly to nothing past 0.05 lux
    let h = normalize(l + v);
    let nh = max(dot(n, h), 0.0);
    // a bulb's size softens highlights (Karis' sphere-light trick)
    let a2 = clamp(alpha + L.pos.w / (2.0 * sqrt(d2)), 0.0, 1.0);
    let F = schlick(f0, max(dot(v, h), 0.0));
    let spec = D_ggx(nh, a2) * V_smith(nv, nl, a2) * F * (alpha * alpha / (a2 * a2));
    lo += (diffuseColor * (1.0 - F) / PI + spec) * nl * I;
  }
  // the sky's light: diffuse from its harmonics, glossy from the blurred sky
  let r = reflect(-v, n);
  let envSpec = skyRadiance(r, rough * 6.0) * envBrdf(f0, rough, nv);
  let occl = clamp(0.4 + 0.6 * (n.y * 0.5 + 0.5), 0.0, 1.0);  // a little less sky low down
  lo += (diffuseColor * shIrradiance(n) / PI + envSpec) * occl;
  if (m.props.z > 0.0) {  // clear coat: a second, sharp reflection
    let Fc = 0.04 + 0.96 * pow(1.0 - nv, 5.0);
    lo = lo * (1.0 - Fc * m.props.z) + skyRadiance(r, 0.5) * Fc * m.props.z;
  }
  if (m.props.w > 0.0) {  // sheen: a soft rim
    lo += base * m.props.w * pow(1.0 - nv, 4.0) * shIrradiance(n) / PI;
  }
  lo += m.emission.rgb;
  var outA = m.emission.w;
  if (glass > 0.0) {
    // glass: what's reflected, plus the tint of what's behind (blended)
    let F = schlick(vec3f(0.04), nv);
    lo += skyRadiance(r, 0.0) * F;
    outA = mix(1.0, max(F.x, 0.08), glass);
    lo = lo / max(outA, 0.08);
  }
  // fog: light lost and gained along the way to the eye
  if (frame.fog.w > 0.5) {
    let dist = length(frame.camPos.xyz - i.world);
    let below = clamp((frame.fog.y - min(i.world.y, frame.camPos.y)) / max(abs(i.world.y - frame.camPos.y), 0.01), 0.0, 1.0);
    let t = exp(-frame.fog.x * dist * select(1.0, below, abs(i.world.y - frame.camPos.y) > 0.01));
    let cosSun = dot(-v, frame.sunDir.xyz);
    let g = frame.fog.z;
    let phase = (1.0 - g * g) / (4.0 * PI * pow(1.0 + g * g - 2.0 * g * cosSun, 1.5));
    let inscatter = frame.fogColor.rgb * (shIrradiance(vec3f(0.0, 1.0, 0.0)) / (4.0 * PI) + frame.sunColor.rgb * phase * frame.sunDir.w);
    lo = lo * t + inscatter * (1.0 - t);
  }
  return Shaded(lo, outA);
}

@fragment fn fsOpaque(i : VsOut, @builtin(front_facing) front : bool) -> @location(0) vec4f {
  let s = shade(i, front);
  return vec4f(exposed(s.color), 1.0);
}
@fragment fn fsGlass(i : VsOut, @builtin(front_facing) front : bool) -> @location(0) vec4f {
  let s = shade(i, front);
  return vec4f(exposed(s.color) * s.alpha, s.alpha);  // premultiplied
}

// ---- shadow pass
struct ShadowOut {
  @builtin(position) clip : vec4f,
  @location(0) uv : vec2f,
  @location(1) @interpolate(flat) mat : u32,
};
@vertex fn vsShadow(v : VsIn, @builtin(instance_index) ii : u32) -> ShadowOut {
  let inst = instances[ii];
  var o : ShadowOut;
  o.clip = frame.shadowViewProj * inst.model * vec4f(v.pos, 1.0);
  o.uv = v.uv;
  o.mat = inst.material.x;
  return o;
}
@fragment fn fsShadow(i : ShadowOut) {
  let m = materials[i.mat];
  if (m.extra.z > 0.5) {
    let y = 1.0 - i.uv.y;
    let x = (i.uv.x - 0.5) * 2.0;
    if (abs(x) > sin(PI * pow(clamp(y, 0.0, 1.0), 0.75)) * 0.95) { discard; }
  }
}

// ---- the sky behind everything
struct SkyOut {
  @builtin(position) clip : vec4f,
  @location(0) ndc : vec2f,
};
@vertex fn vsSky(@builtin(vertex_index) vi : u32) -> SkyOut {
  let p = vec2f(f32((vi << 1u) & 2u), f32(vi & 2u)) * 2.0 - 1.0;
  var o : SkyOut;
  o.clip = vec4f(p, 1.0, 1.0);
  o.ndc = p;
  return o;
}
@fragment fn fsSky(i : SkyOut) -> @location(0) vec4f {
  let w = frame.invViewProj * vec4f(i.ndc, 1.0, 1.0);
  let d = normalize(w.xyz / w.w - frame.camPos.xyz);
  var c = skyRadiance(d, 0.0);
  if (frame.sunDir.w > 0.5) {
    let cs = dot(d, frame.sunDir.xyz);
    let disk = smoothstep(0.999985, 0.999995, cs);  // ~0.27 degrees
    c += frame.sunColor.rgb * disk / 6.8e-5;
  }
  return vec4f(exposed(c), 1.0);
}
)";

inline const char* kPostWgsl = R"(
struct Post {
  exposure : vec4f,     // x fixed exposure, y compensation (stops), z auto, w adapt speed
  look0 : vec4f,        // contrast, saturation, tonemap (0 agx 1 aces 2 filmic 3 linear), mono
  wb : vec4f,           // white balance gains
  lift : vec4f, gamma : vec4f, gain : vec4f,
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
  if (post.screen.w > 0.5) {  // debug views
    let raw = textureSampleLevel(hdr, linearSampler, i.uv, 0.0).rgb;
    if (post.screen.w < 1.5) { return vec4f(clamp(raw, vec3f(0.0), vec3f(1.0)), 1.0); }
    if (post.screen.w < 2.5) { return vec4f(exposureBuf[0] * 1000.0, exposureBuf[1] * 1000.0, 0.0, 1.0); }
    let b = textureSampleLevel(bloomTex, linearSampler, i.uv, 0.0).rgb;
    return vec4f(clamp(b, vec3f(0.0), vec3f(1.0)), 1.0);
  }
  let cuv = i.uv - 0.5;
  let r2 = dot(cuv, cuv) * 2.0;
  // lens fringes
  let ca = post.fx2.w / W;
  var c = vec3f(textureSampleLevel(hdr, linearSampler, i.uv + cuv * ca, 0.0).r,
                textureSampleLevel(hdr, linearSampler, i.uv, 0.0).g,
                textureSampleLevel(hdr, linearSampler, i.uv - cuv * ca, 0.0).b);
  // the picture was stored at exposureBuf[1]; the eyes now want exposureBuf[0]
  let ratio = select(1.0, exposureBuf[0] / max(exposureBuf[1], 1e-12), post.exposure.z > 0.5);
  let exposure = ratio * exp2(post.exposure.y);
  c *= exposure;
  let b = textureSampleLevel(bloomTex, linearSampler, i.uv, 0.0).rgb * exposure;
  c = c + (b - min(c, vec3f(8.0))) * post.fx.x;
  c += b * vec3f(1.0, 0.25, 0.05) * post.fx2.z * 0.5;
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
    let n = grain(i.uv * vec2f(W, H), post.fx2.y * 60.0);
    let g = lum(t);
    t = max(vec3f(0.0), t + vec3f(n * post.fx.z * (0.4 + 2.4 * g * (1.0 - g))));
  }
  var outc = toSrgb(clamp(t, vec3f(0.0), vec3f(1.0)));
  outc += vec3f(grain(i.uv * vec2f(W, H) + 17.0, post.fx2.y) / 255.0);
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
  for (var k = 0; k < 13; k++) {
    var s = textureSampleLevel(hdr, linearSampler, i.uv + o[k] * px, 0.0).rgb;
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
      c += textureSampleLevel(hdr, linearSampler, i.uv + vec2f(f32(x), f32(y)) * px * 1.5, 0.0).rgb * w;
    }
  }
  return vec4f(c, 1.0);
}
)";

// Auto exposure: the frame's average log brightness, eased over time like eyes.
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
