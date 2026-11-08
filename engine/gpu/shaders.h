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
  sunColor : vec4f,      // illuminance (lux) as rgb, w: sky light strength (render.skyLight)
  sh : array<vec4f, 9>,  // sky light, spherical harmonics; sh[0].w shadow softness, sh[1].w pattern fade
  fog : vec4f,           // density, top, anisotropy, on
  fogColor : vec4f,      // rgb, w: sun shadow depth bias
  params : vec4f,        // light count, lamps with shadow rays, shadow map size, auto exposure
  screen : vec4f,        // width, height, 1/width, 1/height
  fixedExposure : vec4f, // x: exposure when not auto, y: rays per lamp, z: debug view, w: occlusion strength
  exposure : vec4f,      // the eyes' last exposure (copied in on the GPU each frame: one storage buffer fewer)
  grid0 : vec4f,         // the light grid: low corner xyz, cell size
  grid1 : vec4f,         // cells x, y, z; w: on
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
  tex : vec4f,       // model picture layers: colour, rough/metal, normal, glow (-1 none)
  tex2 : vec4f,      // alpha cutoff, least roughness
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
// the copies a pass draws this frame (the ones its view can see), as indices
// into `instances`: a draw's instance_index runs through its stretch of it
@group(0) @binding(5) var<storage, read> visibleIds : array<u32>;

// Light is stored already exposed (like a camera's sensor), so even the sun's
// disk fits the half-float picture: last frame's exposure, or a first guess.
fn preExposure() -> f32 {
  let e = frame.exposure.x;
  if (frame.params.w > 0.5 && e > 0.0) { return e; }
  return frame.fixedExposure.x;
}
fn exposed(c : vec3f) -> vec3f { return min(c * preExposure(), vec3f(60000.0)); }

const PI = 3.14159265;

// A value that went bad (not a number, or infinite) is replaced, so one
// unlucky pixel can never spread through blur and bloom.
fn finite3(v : vec3f) -> vec3f {
  let b = (bitcast<vec3u>(v) & vec3u(0x7fffffffu)) >= vec3u(0x7f800000u);
  return select(v, vec3f(0.0), b);
}

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
// Screen slopes: how a surface's place and picture coordinates change from
// one pixel to the next. Patterns fade the detail a pixel can't hold, pictures
// pick their size, bumps tilt the surface, all from these. A pass that draws
// triangles takes them from the GPU (takeSlopes); the cluster drawing works
// them out exactly from each triangle (rebuild step 4).
var<private> slopeWorldX : vec3f;
var<private> slopeWorldY : vec3f;
var<private> slopeUvX : vec2f;
var<private> slopeUvY : vec2f;
fn planar(p : vec3f, n : vec3f) -> vec2f {
  let a = abs(n);
  if (a.y >= a.x && a.y >= a.z) { return p.xz; }
  if (a.x >= a.z) { return p.zy; }
  return p.xy;
}
fn cellHash(v : vec2f) -> f32 { return hash3(vec3f(v, 7.0)); }

// The grid pattern live: 1 on its lines (a pixel and a half wide at any
// distance), 0 between them, fading where the squares get no bigger than the
// lines. `scale` is squares per metre.
fn gridLine(world : vec3f, n : vec3f, scale : f32) -> f32 {
  let g = planar(world * scale, n);
  let w = max(abs(planar(slopeWorldX * scale, n)) + abs(planar(slopeWorldY * scale, n)), vec2f(1e-5)) * 1.5;
  let d = abs(fract(g - 0.5) - 0.5) / w;
  let line = 1.0 - clamp(min(d.x, d.y), 0.0, 1.0);
  let far = clamp(max(w.x, w.y) * 3.0 - 0.5, 0.0, 1.0);
  return mix(line, 0.25, far);
}

// The pattern with detail finer than `lod` (metres per pixel) faded to its
// average: no shimmer when a pixel covers many bricks or grains.
fn patternLod(m : Material, p : vec3f, n : vec3f, lod : f32) -> vec3f {
  var pv = pattern(m, p, n);
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
    case 10: { feature = 0.02 / sc; avg = 0.1; }
    default: {}
  }
  let fade = smoothstep(0.4, 1.5, lod / feature * frame.sh[1].w);
  return vec3f(mix(pv.x, avg, fade), mix(pv.y, 0.5, fade), mix(pv.z, 1.0, fade * 0.5));
}

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
    case 10: { // grid: even squares with thin lines
      let g = abs(fract(pl - 0.5) - 0.5);
      return vec3f(1.0 - smoothstep(0.008, 0.016, min(g.x, g.y)), 0.5, 1.0);
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
@group(1) @binding(4) var modelTex : texture_2d_array<f32>;
@group(1) @binding(5) var modelSampler : sampler;
// the light grid (engine/render/light_grid.h): per cell, where its entries
// start in the list (20 bits) and how many (12 bits); the list (lamps, and
// groups of far lamps sent as one lamp each, after them)
@group(1) @binding(6) var gridCells : texture_3d<u32>;
@group(1) @binding(7) var gridList : texture_2d<u32>;

fn srgbToLinear(c : vec3f) -> vec3f {
  return select(pow((c + 0.055) / 1.055, vec3f(2.4)), c / 12.92, c <= vec3f(0.04045));
}

struct VsIn {
  @location(0) pos : vec3f,
  @location(1) nrm : vec3f,
  @location(2) uv : vec2f,
};
struct VsOut {
  // invariant: the depth pre-pass and the lit pass must land on exactly the
  // same depth (their test is "equal"), on every GPU
  @builtin(position) @invariant clip : vec4f,
  @location(0) world : vec3f,
  @location(1) nrm : vec3f,
  @location(2) uv : vec2f,
  @location(3) @interpolate(flat) mat : u32,
};

@vertex fn vs(v : VsIn, @builtin(instance_index) ii : u32) -> VsOut {
  let inst = instances[visibleIds[ii]];
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
    s += textureSampleCompareLevel(shadowMap, shadowSampler, uv + taps[i] * texel * frame.sh[0].w, p.z - frame.fogColor.w);
  }
  return s / 12.0;
}

struct Shaded {
  color : vec3f,
  alpha : f32,
};

// How much of a lamp a point sees, 0..1: one ray to a small bulb; for a
// big light (a fire, a window) a few rays spread over it, turned per pixel,
// so shadows get soft edges where they should.
fn lampVisibility(p : vec3f, L : Light, pix : vec2f) -> f32 {
  let r = L.pos.w;
  let toL = L.pos.xyz - p;
  let dist = length(toL);
  if (dist <= r) { return 1.0; }
  let dir = toL / dist;
  let rays = max(u32(frame.fixedExposure.y), 1u);
  if (r < 0.04 || rays == 1u) {
    return select(0.0, 1.0, traceRay(p, dir, 1e-4, dist - r, true).tri == 0xffffffffu);
  }
  // interleaved gradient noise: a stable, even pattern from pixel to pixel
  let ign = fract(52.9829189 * fract(dot(pix, vec2f(0.06711056, 0.00583715))));
  let a = abs(dir.y) < 0.99;
  let t = normalize(cross(select(vec3f(1.0, 0.0, 0.0), vec3f(0.0, 1.0, 0.0), a), dir));
  let b = cross(dir, t);
  var seen = 0.0;
  let share = 1.0 / f32(rays);
  for (var k = 0u; k < rays; k++) {
    let ang = (f32(k) + ign) * 6.2831853 * share;
    let rad = r * sqrt((f32(k) + 0.5) * share);
    let q = L.pos.xyz + (t * cos(ang) + b * sin(ang)) * rad;
    let d = q - p;
    let len = length(d);
    if (traceRay(p, d / len, 1e-4, len - 0.01, true).tri == 0xffffffffu) { seen += share; }
  }
  return seen;
}

// Lamp shadows are compiled out when a quality casts none: a shader keeps
// GPU registers for its heaviest path even when that path never runs, so the
// ray code alone would slow every pixel.
override kLampShadows : bool = true;

fn shade(i : VsOut, front : bool) -> Shaded {
  let m = materials[i.mat];
  var n = normalize(i.nrm);
  if (!front) { n = -n; }
  let ng = n;  // the surface's own direction, before bumps (for shadow rays)
  let v = normalize(frame.camPos.xyz - i.world);
  var base = m.base.rgb;
  var rough = m.base.w;
  var metalScale = 1.0;
  // a model's own pictures: colour (sRGB) with its alpha, roughness (G) and metal (B)
  if (m.tex.x >= 0.0) {
    let c = textureSampleGrad(modelTex, modelSampler, i.uv, i32(m.tex.x), slopeUvX, slopeUvY);
    base *= srgbToLinear(c.rgb);
    if (m.tex2.x > 0.0 && c.a < m.tex2.x) { discard; }
  }
  if (m.tex.y >= 0.0) {
    let c = textureSampleGrad(modelTex, modelSampler, i.uv, i32(m.tex.y), slopeUvX, slopeUvY);
    rough = clamp(rough * c.g, 0.02, 1.0);
    metalScale = c.b;
  }
  rough = max(rough, m.tex2.y);
  // leaves: cut to a leaf outline from the card's coordinates
  if (m.extra.z > 0.5) {
    let y = 1.0 - i.uv.y;
    let x = (i.uv.x - 0.5) * 2.0;
    let width = sin(PI * pow(clamp(y, 0.0, 1.0), 0.75)) * 0.95;
    if (abs(x) > width) { discard; }
  }
  var glowScale = 1.0;  // a see-through grid's lines fade at their edges and far away
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
    let lod = length(abs(slopeWorldX) + abs(slopeWorldY));
    let fade = smoothstep(0.4, 1.5, lod / feature * frame.sh[1].w);
    pv = vec3f(mix(pv.x, avg, fade), mix(pv.y, 0.5, fade), mix(pv.z, 1.0, fade * 0.5));
    if (kind == 10) {
      pv = vec3f(gridLine(i.world, n, sc), 0.5, 1.0);
      // a see-through grid (opacity under a half): only its lines are drawn
      if (m.emission.w < 0.5) {
        if (pv.x < 0.5) { discard; }
        glowScale = smoothstep(0.5, 1.0, pv.x);
      }
    }
    base = mix(m.base.rgb, m.colorB.rgb, clamp(pv.x, 0.0, 1.0)) * pv.z;
    rough = clamp(rough + (pv.x - 0.5) * m.colorB.w, 0.02, 1.0);
    if (m.pattern.z > 0.0) {
      // bumps from the pattern's height, by screen-space differences
      let h = pv.y * m.pattern.z * 0.001;
      let dhx = dpdx(h);
      let dhy = dpdy(h);
      let dpx = slopeWorldX;
      let dpy = slopeWorldY;
      let r1 = cross(dpy, n);
      let r2 = cross(n, dpx);
      let det = dot(dpx, r1);
      if (abs(det) > 1e-12) {
        let g = (dhx * r1 + dhy * r2) / det;
        n = normalize(n - g);
      }
    }
  }
  let metal = m.props.x * metalScale;
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
  // lamps (inverse-square, as bulbs with a size). With the light grid, the
  // ones listed in this point's cell: the lamps near it, and far groups of
  // lamps each as one (a point outside the grid takes the nearest cell)
  var first = 0u;
  var last = u32(frame.params.x);
  let gridOn = frame.grid1.w > 0.5;
  if (gridOn) {
    let dims = vec3i(frame.grid1.xyz);
    let c = clamp(vec3i(floor((i.world - frame.grid0.xyz) / frame.grid0.w)), vec3i(0), dims - 1);
    let packed = textureLoad(gridCells, c, 0).x;
    first = packed & 0xFFFFFu;
    last = first + (packed >> 20u);
  }
  var shadowed = 0u;
  for (var j = first; j < last; j++) {
    var k = j;
    if (gridOn) { k = textureLoad(gridList, vec2i(i32(j & 1023u), i32(j >> 10u)), 0).x; }
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
    // a real shadow: can this point see the lamp? (rays through the scene)
    // panels (windows, softboxes) are left unshadowed: their real shadows are
    // so soft they barely show, and one ray to their middle would cost the
    // most of any lamp (long rays across a whole room) for a wrong, hard edge
    if (kLampShadows && L.dir.w < 1.5 && shadowed < u32(frame.params.y) && dot(I, vec3f(0.3333)) * preExposure() > 2e-4) {
      shadowed += 1u;
      I *= lampVisibility(i.world + ng * 0.004, L, i.clip.xy);
      if (dot(I, I) == 0.0) { continue; }
    }
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
  // a little less sky low down (render.occlusion), all of it times render.skyLight
  let occl = mix(1.0, clamp(0.4 + 0.6 * (n.y * 0.5 + 0.5), 0.0, 1.0), frame.fixedExposure.w);
  lo += (diffuseColor * shIrradiance(n) / PI + envSpec) * occl * frame.sunColor.w;
  if (m.props.z > 0.0) {  // clear coat: a second, sharp reflection
    let Fc = 0.04 + 0.96 * pow(1.0 - nv, 5.0);
    lo = lo * (1.0 - Fc * m.props.z) + skyRadiance(r, 0.5) * Fc * m.props.z;
  }
  if (m.props.w > 0.0) {  // sheen: a soft rim
    lo += base * m.props.w * pow(1.0 - nv, 4.0) * shIrradiance(n) / PI;
  }
  lo += m.emission.rgb * glowScale;
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

// the screen slopes of a drawn triangle, from the GPU
fn takeSlopes(i : VsOut) {
  slopeWorldX = dpdx(i.world);
  slopeWorldY = dpdy(i.world);
  slopeUvX = dpdx(i.uv);
  slopeUvY = dpdy(i.uv);
}
@fragment fn fsOpaque(i : VsOut, @builtin(front_facing) front : bool) -> @location(0) vec4f {
  takeSlopes(i);
  let s = shade(i, front);
  if (frame.fixedExposure.z > 3.5) {  // ?view=4: ray work, blue (little) to red (200+ steps)
    let k = clamp(f32(rayWork) / 200.0, 0.0, 1.0);
    return vec4f(k, 1.0 - abs(k * 2.0 - 1.0), 1.0 - k, 1.0) * select(1.0, 0.15, rayWork == 0u);
  }
  return vec4f(exposed(s.color), 1.0);
}
@fragment fn fsGlass(i : VsOut, @builtin(front_facing) front : bool) -> @location(0) vec4f {
  takeSlopes(i);
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
  let inst = instances[visibleIds[ii]];
  var o : ShadowOut;
  o.clip = frame.shadowViewProj * inst.model * vec4f(v.pos, 1.0);
  o.uv = v.uv;
  o.mat = inst.material.x;
  return o;
}
// The depth pre-pass: only where surfaces are (leaves cut to shape), so the
// expensive lighting runs once per pixel, not once per surface behind it.
// Where a surface has holes, the same holes as the lit pass cuts (shade):
// leaves cut to their outline, model pictures with holes, see-through grids.
fn holes(i : VsOut) -> bool {
  let m = materials[i.mat];
  if (m.extra.z > 0.5) {
    let y = 1.0 - i.uv.y;
    let x = (i.uv.x - 0.5) * 2.0;
    if (abs(x) > sin(PI * pow(clamp(y, 0.0, 1.0), 0.75)) * 0.95) { return true; }
  }
  if (m.tex.x >= 0.0 && m.tex2.x > 0.0) {
    let c = textureSampleGrad(modelTex, modelSampler, i.uv, i32(m.tex.x), slopeUvX, slopeUvY);
    if (c.a < m.tex2.x) { return true; }
  }
  return i32(m.pattern.x) == 10 && m.emission.w < 0.5 && gridLine(i.world, normalize(i.nrm), max(m.pattern.y, 0.01)) < 0.5;
}
@fragment fn fsDepth(i : VsOut) {
  takeSlopes(i);
  if (holes(i)) { discard; }
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

// ---- the cluster drawing (the rebuild's step 4): the cook's clusters, culled
// and given their level by the GPU (kClusterCullWgsl), drawn in one go into a
// visibility picture (which triangle each pixel sees), then lit once per pixel.
inline const char* kClusterDrawWgsl = R"(
// The cook's clusters in one pool (one buffer: WebGPU promises only 8 a
// stage): pool[0] where the triangles' corners start, pool[1] where the
// clusters start; vertices first, 8 numbers each (position, normal, picture
// coordinates), clusters 16 numbers each.
@group(3) @binding(0) var<storage, read> pool : array<u32>;
struct Cluster { bounds : vec4f, lod : vec4f, parent : vec4f, err : vec2f, first : u32, count : u32 };
fn poolF(i : u32) -> f32 { return bitcast<f32>(pool[i]); }
fn poolV4(i : u32) -> vec4f { return vec4f(poolF(i), poolF(i + 1u), poolF(i + 2u), poolF(i + 3u)); }
fn poolCluster(c : u32) -> Cluster {
  let b = pool[1] + c * 16u;
  return Cluster(poolV4(b), poolV4(b + 4u), poolV4(b + 8u), vec2f(poolF(b + 12u), poolF(b + 13u)), pool[b + 14u], pool[b + 15u]);
}
fn poolCorner(i : u32) -> u32 { return pool[pool[0] + i]; }
struct PoolVertex { pos : vec3f, nrm : vec3f, uv : vec2f };
fn poolVertex(v : u32) -> PoolVertex {
  let b = 2u + v * 8u;
  return PoolVertex(vec3f(poolF(b), poolF(b + 1u), poolF(b + 2u)), vec3f(poolF(b + 3u), poolF(b + 4u), poolF(b + 5u)), vec2f(poolF(b + 6u), poolF(b + 7u)));
}
@group(3) @binding(1) var<storage, read> drawn : array<vec2u>;  // this frame's clusters: (instance, cluster)

struct VisOut {
  @builtin(position) @invariant clip : vec4f,
  @location(0) world : vec3f,
  @location(1) nrm : vec3f,
  @location(2) uv : vec2f,
  @location(3) @interpolate(flat) mat : u32,
  @location(4) @interpolate(flat) id : vec2u,
};
// One draw for every cluster: each instance of the draw is one drawn cluster,
// its 384 corners are its (up to) 128 triangles; the rest fold to nothing.
@vertex fn vsVis(@builtin(vertex_index) vi : u32, @builtin(instance_index) k : u32) -> VisOut {
  var o : VisOut;
  let d = drawn[k];
  let cl = poolCluster(d.y);
  let tri = vi / 3u;
  if (tri >= cl.count) {
    o.clip = vec4f(2.0, 2.0, 2.0, 1.0);  // outside the view: nothing drawn
    return o;
  }
  let pv = poolVertex(poolCorner(cl.first + vi));
  let inst = instances[d.x];
  let w = inst.model * vec4f(pv.pos, 1.0);
  o.clip = frame.viewProj * w;
  o.world = w.xyz;
  o.nrm = mat3x3f(inst.normal0.xyz, inst.normal1.xyz, inst.normal2.xyz) * pv.nrm;
  o.uv = pv.uv;
  o.mat = inst.material.x;
  o.id = vec2u(d.x + 1u, (d.y << 7u) | tri);  // the copy (0: nothing here), its cluster and triangle
  return o;
}
@fragment fn fsVis(i : VisOut) -> @location(0) vec2u {
  var v : VsOut;
  v.clip = i.clip;
  v.world = i.world;
  v.nrm = i.nrm;
  v.uv = i.uv;
  v.mat = i.mat;
  takeSlopes(v);
  if (holes(v)) { discard; }
  return i.id;
}

// The sun's shadow from clusters: the same one draw, in the sun's view, depth
// only (leaves still cut to their outline by fsShadow).
@vertex fn vsVisShadow(@builtin(vertex_index) vi : u32, @builtin(instance_index) k : u32) -> ShadowOut {
  var o : ShadowOut;
  let d = drawn[k];
  let cl = poolCluster(d.y);
  if (vi / 3u >= cl.count) {
    o.clip = vec4f(2.0, 2.0, 2.0, 1.0);
    return o;
  }
  let pv = poolVertex(poolCorner(cl.first + vi));
  let inst = instances[d.x];
  o.clip = frame.shadowViewProj * inst.model * vec4f(pv.pos, 1.0);
  o.uv = pv.uv;
  o.mat = inst.material.x;
  return o;
}

// ---- lighting each pixel once: its triangle, rebuilt exactly
@group(3) @binding(2) var visTex : texture_2d<u32>;
struct ResolveOut { @builtin(position) clip : vec4f };
@vertex fn vsResolve(@builtin(vertex_index) vi : u32) -> ResolveOut {
  let p = vec2f(f32((vi << 1u) & 2u), f32(vi & 2u));
  return ResolveOut(vec4f(p * 2.0 - 1.0, 0.0, 1.0));
}
// the point of the triangle's plane the camera sees through screen point `px`
fn planeHit(px : vec2f, a : vec3f, n : vec3f) -> vec3f {
  let ndc = vec2f(px.x * frame.screen.z * 2.0 - 1.0, 1.0 - px.y * frame.screen.w * 2.0);
  let far = frame.invViewProj * vec4f(ndc, 1.0, 1.0);
  let o = frame.camPos.xyz;
  let d = far.xyz / far.w - o;
  let t = dot(a - o, n) / dot(d, n);
  return o + d * t;
}
// how much of each corner a point of the triangle has
fn bary(p : vec3f, a : vec3f, b : vec3f, c : vec3f) -> vec3f {
  let v0 = b - a;
  let v1 = c - a;
  let v2 = p - a;
  let d00 = dot(v0, v0);
  let d01 = dot(v0, v1);
  let d11 = dot(v1, v1);
  let d20 = dot(v2, v0);
  let d21 = dot(v2, v1);
  let den = d00 * d11 - d01 * d01;
  let y = (d11 * d20 - d01 * d21) / den;
  let z = (d00 * d21 - d01 * d20) / den;
  // kept on the triangle: seen edge-on, the pixel's point can fall just
  // outside it, and a normal stretched past its corners catches the light
  let w = max(vec3f(1.0 - y - z, y, z), vec3f(0.0));
  return w / max(w.x + w.y + w.z, 1e-12);
}
@fragment fn fsResolve(r : ResolveOut) -> @location(0) vec4f {
  let id = textureLoad(visTex, vec2i(r.clip.xy), 0).rg;
  if (id.x == 0u) { discard; }
  let inst = instances[id.x - 1u];
  let cl = poolCluster(id.y >> 7u);
  let base = cl.first + (id.y & 127u) * 3u;
  let A = poolVertex(poolCorner(base));
  let B = poolVertex(poolCorner(base + 1u));
  let C = poolVertex(poolCorner(base + 2u));
  let wa = (inst.model * vec4f(A.pos, 1.0)).xyz;
  let wb = (inst.model * vec4f(B.pos, 1.0)).xyz;
  let wc = (inst.model * vec4f(C.pos, 1.0)).xyz;
  let ng = cross(wb - wa, wc - wa);
  // this pixel's point, and the points one pixel right and one down: the slopes
  let p0 = planeHit(r.clip.xy, wa, ng);
  let px = planeHit(r.clip.xy + vec2f(1.0, 0.0), wa, ng);
  let py = planeHit(r.clip.xy + vec2f(0.0, 1.0), wa, ng);
  let b0 = bary(p0, wa, wb, wc);
  let bx = bary(px, wa, wb, wc);
  let by = bary(py, wa, wb, wc);
  var v : VsOut;
  v.clip = r.clip;
  v.world = wa * b0.x + wb * b0.y + wc * b0.z;
  v.nrm = mat3x3f(inst.normal0.xyz, inst.normal1.xyz, inst.normal2.xyz) * (A.nrm * b0.x + B.nrm * b0.y + C.nrm * b0.z);
  v.uv = A.uv * b0.x + B.uv * b0.y + C.uv * b0.z;
  v.mat = inst.material.x;
  slopeWorldX = px - v.world;
  slopeWorldY = py - v.world;
  slopeUvX = A.uv * bx.x + B.uv * bx.y + C.uv * bx.z - v.uv;
  slopeUvY = A.uv * by.x + B.uv * by.y + C.uv * by.z - v.uv;
  // which side the camera sees, as the GPU tells a drawn triangle (counter-
  // clockwise on screen is its front): its corners' turn as seen from the eye,
  // which stays right for big triangles reaching behind the camera
  let front = dot(ng, frame.camPos.xyz - wa) > 0.0;
  let s = shade(v, front);
  return vec4f(exposed(s.color), 1.0);
}
)";

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
    let b = textureSampleLevel(bloomTex, linearSampler, buv, 0.0).rgb;
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
    if (post.exposure.w > 0.0) { c = max(c + (c - avg) * post.exposure.w * 2.0, vec3f(0.0)); }
  }
  // the picture was stored at exposureBuf[1]; the eyes now want exposureBuf[0]
  let ratio = select(1.0, exposureBuf[0] / max(exposureBuf[1], 1e-12), post.exposure.z > 0.5);
  let exposure = ratio * exp2(post.exposure.y);
  c *= exposure;
  let b = textureSampleLevel(bloomTex, linearSampler, buv, 0.0).rgb * exposure;
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
  return vec4f(c, 1.0);
}
)";


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

// ---- Ultra: real path tracing on the GPU, one sample a frame, averaged
// over frames (it sharpens while the camera holds still). The cinema
// renderer's rules, lighter: diffuse, glossy, metal, glass, glow, the sun
// and lamps with shadows, the sky, many bounces.
inline const char* kUltraWgsl = R"(
struct Ultra {
  camPos : vec4f,   // xyz, lens radius
  camFwd : vec4f,   // xyz, focus distance
  camRight : vec4f, // xyz, tan(fov x / 2)
  camUp : vec4f,    // xyz, tan(fov y / 2)
  info : vec4u,     // frame index, width, height, max bounces
  extra : vec4u,    // pixel block size (1 full, 2 half), lamps, history cap | firefly clamp x100 << 16,
                    // flags (1 glossy, 2 glass, 4 depth of field, 8 sun shadows, 16 lamp shadows) | paths per pixel << 8
};
struct Px {
  color : vec4f,   // averaged light
  nd : vec4f,      // first hit: surface direction, distance
  albedo : vec4f,  // first hit: surface colour
};
@group(1) @binding(0) var<storage, read_write> accum : array<Px>;
@group(1) @binding(1) var outTex : texture_storage_2d<rgba16float, write>;
@group(1) @binding(2) var skyTex : texture_2d<f32>;
@group(1) @binding(3) var skySampler : sampler;
@group(1) @binding(4) var<uniform> U : Ultra;

var<private> seed : u32;
fn rand() -> f32 {
  seed = seed * 747796405u + 2891336453u;
  var w = ((seed >> ((seed >> 28u) + 4u)) ^ seed) * 277803737u;
  w = (w >> 22u) ^ w;
  return f32(w) / 4294967296.0;
}
fn dirToUv(d : vec3f) -> vec2f {
  let elev = asin(clamp(d.y, -1.0, 1.0));
  var az = atan2(d.z, d.x);
  if (az < 0.0) { az += 2.0 * PI; }
  let s = elev / (PI * 0.5);
  let v = 0.5 + 0.5 * sign(s) * sqrt(abs(s));
  return vec2f(az / (2.0 * PI), 1.0 - v);
}
fn sky(d : vec3f) -> vec3f { return textureSampleLevel(skyTex, skySampler, dirToUv(d), 0.0).rgb; }

fn ggxD(nh : f32, a : f32) -> f32 {
  let a2 = a * a;
  let dd = nh * nh * (a2 - 1.0) + 1.0;
  return a2 / (PI * dd * dd);
}
fn smithG(nv : f32, nl : f32, a : f32) -> f32 {
  let k = a * 0.5;
  return (nv / (nv * (1.0 - k) + k)) * (nl / (nl * (1.0 - k) + k));
}
fn basis(n : vec3f) -> mat3x3f {
  let s = select(-1.0, 1.0, n.z >= 0.0);
  let a = -1.0 / (s + n.z);
  let b = n.x * n.y * a;
  return mat3x3f(vec3f(1.0 + s * n.x * n.x * a, s * b, -s * n.x), vec3f(b, s + n.y * n.y * a, -n.y), n);
}

// light arriving from the sun and one lamp (picked at random, weighted
// back up), through a surface's BSDF, with shadow rays
fn brdf(n : vec3f, v : vec3f, l : vec3f, base : vec3f, f0 : vec3f, alpha : f32, diffuseW : f32) -> vec3f {
  let nl = max(dot(n, l), 0.0);
  let hv = normalize(l + v);
  let nv = max(dot(n, v), 1e-4);
  let F = f0 + (vec3f(1.0) - f0) * pow(1.0 - max(dot(v, hv), 0.0), 5.0);
  let spec = F * ggxD(max(dot(n, hv), 0.0), alpha) * smithG(nv, nl, alpha) / (4.0 * nv * nl + 1e-5);
  return (base * diffuseW * (vec3f(1.0) - F) / PI + spec) * nl;
}
fn direct(p : vec3f, n : vec3f, v : vec3f, base : vec3f, f0 : vec3f, alpha : f32, diffuseW : f32) -> vec3f {
  var L = vec3f(0.0);
  if (frame.sunDir.w > 0.5) {
    let b = basis(frame.sunDir.xyz);
    let r = 0.0047 * sqrt(rand());
    let ph = 2.0 * PI * rand();
    let l = normalize(frame.sunDir.xyz + b[0] * (r * cos(ph)) + b[1] * (r * sin(ph)));
    if (dot(n, l) > 0.0 && ((U.extra.w & 8u) == 0u || traceRay(p, l, 1e-4, 1e7, true).tri == 0xffffffffu)) {
      L += brdf(n, v, l, base, f0, alpha, diffuseW) * frame.sunColor.rgb;
    }
  }
  let count = u32(frame.params.x);
  if (count > 0u) {
    let k = min(u32(rand() * f32(count)), count - 1u);
    let li = lights[k];
    let rp = li.pos.xyz + normalize(vec3f(rand() - 0.5, rand() - 0.5, rand() - 0.5) + 1e-4) * li.pos.w * 0.9;
    let tl = rp - p;
    let dist = length(tl);
    let l = tl / max(dist, 1e-6);
    if (dot(n, l) > 0.0) {
      let d2 = max(dist * dist, li.pos.w * li.pos.w);
      var I = li.color.rgb / d2;
      if (li.dir.w > 0.5 && li.dir.w < 1.5) { I *= smoothstep(li.cone.y, li.cone.x, dot(-l, li.dir.xyz)); }
      else if (li.dir.w > 1.5) { I = li.color.rgb * li.cone.z * li.cone.w * max(dot(-l, li.dir.xyz), 0.0) / d2; }
      if ((U.extra.w & 16u) == 0u || traceRay(p, l, 1e-4, dist - li.pos.w, true).tri == 0xffffffffu) {
        L += brdf(n, v, l, base, f0, alpha, diffuseW) * I * f32(count);
      }
    }
  }
  return L;
}

@compute @workgroup_size(8, 8) fn main(@builtin(global_invocation_id) gid : vec3u) {
  let W = U.info.y;
  let H = U.info.z;
  let B = max(U.extra.x, 1u);          // each thread draws a B x B block
  let bw = (W + B - 1u) / B;
  let bh = (H + B - 1u) / B;
  if (gid.x >= bw || gid.y >= bh) { return; }
  let frameNo = U.info.x;
  let flags = U.extra.w & 255u;
  let spp = max(U.extra.w >> 8u, 1u);
  seed = (gid.x * 1973u + gid.y * 9277u + frameNo * 26699u) | 1u;
  _ = rand();
  var firstN = vec3f(0.0, 0.0, 1.0);
  var firstD = 1e4;
  var firstA = vec3f(1.0);
  var Lsum = vec3f(0.0);
  for (var sp = 0u; sp < spp; sp++) {
  // a camera ray with a little jitter (anti-aliasing) and lens blur
  let px = f32(gid.x * B) + rand() * f32(B);
  let py = f32(gid.y * B) + rand() * f32(B);
  let sx = (px / f32(W) * 2.0 - 1.0) * U.camRight.w;
  let sy = (1.0 - py / f32(H) * 2.0) * U.camUp.w;
  var d = normalize(U.camFwd.xyz + U.camRight.xyz * sx + U.camUp.xyz * sy);
  var o = U.camPos.xyz;
  if (U.camPos.w > 0.0 && (flags & 4u) != 0u) {
    let focus = o + d * (U.camFwd.w / dot(d, U.camFwd.xyz));
    let r = sqrt(rand()) * U.camPos.w;
    let a = 2.0 * PI * rand();
    o += U.camRight.xyz * (r * cos(a)) + U.camUp.xyz * (r * sin(a));
    d = normalize(focus - o);
  }
  var beta = vec3f(1.0);
  var L = vec3f(0.0);
  var specular = true;
  if (sp == 0u) { firstN = -d; }
  for (var bounce = 0u; bounce < U.info.w; bounce++) {
    let h = traceRay(o, d, 1e-4, 1e7, false);
    if (h.tri == 0xffffffffu) {
      var c = sky(d);
      if (specular && frame.sunDir.w > 0.5 && dot(d, frame.sunDir.xyz) > 0.99998) { c += frame.sunColor.rgb / 6.8e-5; }
      L += beta * c;
      break;
    }
    let m = materials[rInst[h.inst].info.y];
    let p = o + d * h.t;
    var n : vec3f;
    var ng : vec3f;
    hitNormals(h, &n, &ng);
    let front = dot(d, ng) < 0.0;
    if (!front) { n = -n; ng = -ng; }
    if (dot(n, -d) < 0.0) { n = ng; }
    var base = m.base.rgb;
    var rough = m.base.w;
    if (m.pattern.x > 0.5) {
      // how wide this pixel is out there (after a bounce: blurry anyway)
      let lod = select(1.0, h.t * 2.0 * U.camUp.w / f32(H) * f32(B), bounce == 0u);
      let pv = patternLod(m, p, n, lod);
      base = mix(m.base.rgb, m.colorB.rgb, clamp(pv.x, 0.0, 1.0)) * pv.z;
      rough = clamp(rough + (pv.x - 0.5) * m.colorB.w, 0.02, 1.0);
    }
    if (front) { L += beta * m.emission.rgb; }
    let metal = m.props.x;
    let glass = select(0.0, m.props.y, (flags & 2u) != 0u);
    let alpha = max(rough * rough, 0.002);
    let f0 = mix(vec3f(0.04), base, metal);
    let v = -d;
    if (bounce == 0u && sp == 0u) {
      firstN = n;
      firstD = h.t;
      firstA = select(max(base * (1.0 - metal) + f0 * metal, vec3f(0.03)), vec3f(1.0), glass > 0.5);
    }
    // glass: reflect or refract by Fresnel
    if (glass > 0.5) {
      let eta = select(m.extra.w, 1.0 / m.extra.w, front);
      let c = clamp(dot(v, n), 0.0, 1.0);
      let r0 = (1.0 - eta) / (1.0 + eta);
      let F = r0 * r0 + (1.0 - r0 * r0) * pow(1.0 - c, 5.0);
      let rd = refract(d, n, eta);
      if (rand() < F || dot(rd, rd) < 1e-6) {
        d = reflect(d, n);
        o = p + n * 1e-3;
      } else {
        d = normalize(rd);
        o = p - n * 1e-3;
        beta *= base;
      }
      specular = true;
      continue;
    }
    let diffuseW = (1.0 - metal);
    L += beta * direct(p + ng * 2e-3, n, v, base, f0, alpha, diffuseW);
    // the next bounce: glossy or diffuse, picked by how much each reflects
    let pSpec = select(0.0, clamp(0.1 + 0.9 * max(max(f0.x, f0.y), f0.z) + metal * 0.5, 0.1, 0.95), (flags & 1u) != 0u);
    let b = basis(n);
    if (rand() < pSpec) {
      // GGX normal (sampled by D), reflected
      let u1 = rand();
      let u2 = rand();
      let cosT = sqrt((1.0 - u1) / (1.0 + (alpha * alpha - 1.0) * u1));
      let sinT = sqrt(max(0.0, 1.0 - cosT * cosT));
      let ph = 2.0 * PI * u2;
      let hl = b * vec3f(sinT * cos(ph), sinT * sin(ph), cosT);
      let nd = reflect(d, hl);
      let nl = dot(n, nd);
      if (nl <= 0.0) { break; }
      let F = f0 + (vec3f(1.0) - f0) * pow(1.0 - max(dot(v, hl), 0.0), 5.0);
      let nv = max(dot(n, v), 1e-4);
      // weight = f cos / pdf, pdf = D nh / (4 vh)
      beta *= F * smithG(nv, nl, alpha) * max(dot(v, hl), 0.0) / (nv * max(dot(n, hl), 1e-4)) / pSpec;
      d = nd;
      specular = alpha < 0.02;
    } else {
      let r1 = rand();
      let ph = 2.0 * PI * rand();
      let rr = sqrt(r1);
      d = normalize(b * vec3f(rr * cos(ph), rr * sin(ph), sqrt(max(0.0, 1.0 - r1))));
      beta *= base * diffuseW / (1.0 - pSpec);
      specular = false;
    }
    o = p + ng * 1e-3;
    // the bulbs' and sun's light is counted by `direct`, so skip it when hit later
    if (bounce > 2u) {
      let q = clamp(max(max(beta.x, beta.y), beta.z), 0.05, 0.95);
      if (rand() > q) { break; }
      beta /= q;
    }
  }
  // firefly guard: one sample may be at most a few times brighter than white
  // on screen (rare lucky paths otherwise sparkle as dots)
  L = max(finite3(L), vec3f(0.0));
  let lum = dot(L, vec3f(0.2126, 0.7152, 0.0722));
  let cap = (f32(U.extra.z >> 16u) / 100.0) / max(preExposure(), 1e-12);
  if (lum > cap) { L *= cap / lum; }
  Lsum += L;
  }
  let L = Lsum / f32(spp);
  let idx = gid.y * bw + gid.x;
  let prev = select(accum[idx].color, vec4f(0.0), frameNo == 0u);
  // a history cap keeps the picture following slow changes (0 = sharpen forever)
  let hist = U.extra.z & 0xffffu;
  let n = select(frameNo, min(frameNo, hist - 1u), hist > 0u);
  let avg = prev + (vec4f(L, 1.0) - prev) / f32(n + 1u);
  accum[idx].color = avg;
  accum[idx].nd = vec4f(firstN, firstD);
  accum[idx].albedo = vec4f(firstA, 1.0);
  _ = outTex;
}
)";

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

// Auto exposure: the frame's average log brightness, eased over time like eyes.
// Frame blending (TAA): every frame the view moves a fraction of a pixel (a
// fixed pattern of 8), and each pixel is blended with where its point was in
// the frames before, so edges come out smooth and fine detail stops
// shimmering. The past is kept within the range of this pixel's neighbours
// (so moving things don't smear), and bright pixels count less (so one hot
// pixel doesn't flicker).
inline const char* kTaaWgsl = R"(
struct TaaU {
  prevViewProj : mat4x4f,  // last frame's, without its shift
  curViewProj : mat4x4f,   // this frame's, without its shift
  invViewProj : mat4x4f,   // this frame's, shifted (to find this pixel's point)
  screen : vec4f,          // drawn width, height, 1/width, 1/height
  full : vec4f,            // the targets' full width, height; this frame's weight; 1: start afresh
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
@fragment fn fsTaa(o : Out) -> @location(0) vec4f {
  let p = vec2i(o.pos.xy);
  let hi = vec2i(tu.screen.xy) - 1;
  let c = tame(textureLoad(cur, p, 0).rgb);
  var lo3 = toYCoCg(c);
  var hi3 = lo3;
  for (var y = -1; y <= 1; y++) {
    for (var x = -1; x <= 1; x++) {
      let n = toYCoCg(tame(textureLoad(cur, clamp(p + vec2i(x, y), vec2i(0), hi), 0).rgb));
      lo3 = min(lo3, n);
      hi3 = max(hi3, n);
    }
  }
  if (tu.full.w > 0.5) { return vec4f(untame(c), 1.0); }
  // where this pixel's point was last frame
  let d = textureLoad(depthTex, p, 0);
  let ndc = vec2f(o.pos.x * tu.screen.z * 2.0 - 1.0, 1.0 - o.pos.y * tu.screen.w * 2.0);
  let w4 = tu.invViewProj * vec4f(ndc, d, 1.0);
  // how far the point moved on screen, measured without the shifts: a still
  // view reads its past exactly in place (no blur that builds up)
  let wp = vec4f(w4.xyz / w4.w, 1.0);
  // a thing that moved: where this point of it was last frame (the frame
  // history's motion; still things and the sky stay where they are)
  let id = textureLoad(seen, p, 0).x;
  var was = wp;
  if (id > 0u) { was = back[id - 1u] * wp; }
  let prev = tu.prevViewProj * was;
  let now = tu.curViewProj * wp;
  let motion = vec2f(prev.x / prev.w - now.x / now.w, now.y / now.w - prev.y / prev.w) * 0.5;
  let puv = o.pos.xy * tu.screen.zw + motion;
  if (prev.w <= 0.0 || any(puv < vec2f(0.0)) || any(puv > vec2f(1.0))) { return vec4f(untame(c), 1.0); }
  let h = tame(textureSampleLevel(hist, lin, puv * tu.screen.xy / tu.full.xy, 0.0).rgb);
  let kept = fromYCoCg(clamp(toYCoCg(h), lo3, hi3));
  return vec4f(untame(mix(kept, c, tu.full.z)), 1.0);
}
)";

// The depth pyramid (phase 4): each level the farthest depth over 2x2 (with a
// third row and column, so odd sizes lose nothing) of the level below; level 0
// from the depth picture, half its size. Far beats near: a patch hides
// something only if every wall over it is nearer.
inline const char* kHzbWgsl = R"(
struct HzbU { src : vec4f, dst : vec4f };  // xy: the valid size of the level read and of the one written
@group(0) @binding(0) var<uniform> hu : HzbU;
@group(0) @binding(1) var depthIn : texture_depth_2d;
@group(0) @binding(2) var levelIn : texture_2d<f32>;
struct Out { @builtin(position) pos : vec4f };
@vertex fn vsHzb(@builtin(vertex_index) vi : u32) -> Out {
  let p = vec2f(f32((vi << 1u) & 2u), f32(vi & 2u));
  return Out(vec4f(p * 2.0 - 1.0, 0.0, 1.0));
}
@fragment fn fsFromDepth(o : Out) -> @location(0) vec4f {
  let x = vec2i(o.pos.xy) * 2;
  let lim = vec2i(hu.src.xy) - 1;
  var far = 0.0;
  for (var dy = 0; dy < 3; dy++) {
    for (var dx = 0; dx < 3; dx++) { far = max(far, textureLoad(depthIn, min(x + vec2i(dx, dy), lim), 0)); }
  }
  return vec4f(far, 0.0, 0.0, 1.0);
}
@fragment fn fsFromLevel(o : Out) -> @location(0) vec4f {
  let x = vec2i(o.pos.xy) * 2;
  let lim = vec2i(hu.src.xy) - 1;
  var far = 0.0;
  for (var dy = 0; dy < 3; dy++) {
    for (var dx = 0; dx < 3; dx++) { far = max(far, textureLoad(levelIn, min(x + vec2i(dx, dy), lim), 0).x); }
  }
  return vec4f(far, 0.0, 0.0, 1.0);
}
)";

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
