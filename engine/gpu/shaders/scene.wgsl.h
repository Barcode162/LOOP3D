#pragma once
// Part of the live renderer's shaders (engine/gpu/shaders.h).

namespace loop::gpu {

inline const char* kSceneWgsl = R"(
@group(1) @binding(0) var shadowMap : texture_depth_2d_array;  // the sun's: near (0), far (1)
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
// the main lamps' shadow maps (Phone, where no rays are traced): six views in
// a row, a layer per lamp
@group(1) @binding(8) var lampMap : texture_depth_2d_array;
// the light cache (phase 6): per probe, the light falling on a face turned
// +x, -x, +y, -y, +z, -z (a picture each)
@group(1) @binding(10) var cachePX : texture_3d<f32>;
@group(1) @binding(11) var cacheNX : texture_3d<f32>;
@group(1) @binding(12) var cachePY : texture_3d<f32>;
@group(1) @binding(13) var cacheNY : texture_3d<f32>;
@group(1) @binding(14) var cachePZ : texture_3d<f32>;
@group(1) @binding(15) var cacheNZ : texture_3d<f32>;
// The light falling on a surface at p facing n that isn't straight from a
// lamp or the sun, blended between the probes round it and between the faces;
// and (a) how much of what such a surface looks at is open sky.
fn cacheLight4(p : vec3f, n : vec3f) -> vec4f {
  let dims = frame.cache1.xyz;
  let half = 0.5 / dims;
  let uvw = clamp(((p - frame.cache0.xyz) / frame.cache0.w + 0.5) / dims, half, 1.0 - half);
  let fx = select(textureSampleLevel(cacheNX, skySampler, uvw, 0.0), textureSampleLevel(cachePX, skySampler, uvw, 0.0), n.x > 0.0);
  let fy = select(textureSampleLevel(cacheNY, skySampler, uvw, 0.0), textureSampleLevel(cachePY, skySampler, uvw, 0.0), n.y > 0.0);
  let fz = select(textureSampleLevel(cacheNZ, skySampler, uvw, 0.0), textureSampleLevel(cachePZ, skySampler, uvw, 0.0), n.z > 0.0);
  return fx * n.x * n.x + fy * n.y * n.y + fz * n.z * n.z;
}
fn cacheLight(p : vec3f, n : vec3f) -> vec3f { return cacheLight4(p, n).rgb; }

// How much of lamp k (one of the first, mapped) a point sees: its depth in the
// lamp's view that faces it, against the map (the chip blends 2x2 of them).
fn lampMapSeen(p : vec3f, L : Light, k : u32, pix : vec2f) -> f32 {
  let v = p - L.pos.xyz;
  let a = abs(v);
  var face = 0u;
  if (a.x >= a.y && a.x >= a.z) { face = select(1u, 0u, v.x > 0.0); }
  else if (a.y >= a.z) { face = select(3u, 2u, v.y > 0.0); }
  else { face = select(5u, 4u, v.z > 0.0); }
  let c = frame.lampFaces[k * 6u + face] * vec4f(p, 1.0);
  let ndc = c.xyz / c.w;
  if (ndc.z >= 1.0) { return 1.0; }  // beyond the map's reach
  let tile = frame.lampMaps.y;
  let inset = 1.0 / tile;
  let uv = clamp(ndc.xy * vec2f(0.5, -0.5) + 0.5, vec2f(inset), vec2f(1.0 - inset));
  if (kShading >= 2u) {
    // a softer edge: four taps a texel and a half round the point (each the
    // chip's own blend of 2x2), turned per pixel, kept inside this view's tile
    let ang = pixelTurn(pix) * 6.2831853;
    let rot = vec2f(cos(ang), sin(ang));
    var seen = 0.0;
    for (var t = 0; t < 4; t++) {
      let q = clamp(uv + spiral(t, 4, rot) * (1.5 * inset), vec2f(inset), vec2f(1.0 - inset));
      seen += textureSampleCompareLevel(lampMap, shadowSampler, vec2f((f32(face) + q.x) / 6.0, q.y), i32(k), ndc.z);
    }
    return seen * 0.25;
  }
  let at = vec2f((f32(face) + uv.x) / 6.0, uv.y);
  return textureSampleCompareLevel(lampMap, shadowSampler, at, i32(k), ndc.z);
}

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
// Haze (the same lines as `hazeAt` in engine/render/sky.cpp): what the eye sees
// of a point after the distant air, which thins with height. Off at visibility 0.
fn hazeAt(colour : vec3f, eye : vec3f, point : vec3f) -> vec3f {
  let visibility = frame.sh[2].w;
  let to = point - eye;
  let d = length(to);
  if (visibility <= 0.0 || d < 0.0001) { return colour; }
  let h = max(frame.sh[3].w, 1.0);
  let dy = point.y - eye.y;
  var air = d * exp(-eye.y / h);
  if (abs(dy) >= 0.0001 * h) { air = d * h * (exp(-eye.y / h) - exp(-point.y / h)) / dy; }
  let k = 3.912 / visibility * air;
  let lost = vec3f(exp(-0.75 * k), exp(-k), exp(-1.5 * k));
  let dir = to / d;
  let side = sqrt(dir.x * dir.x + dir.z * dir.z);
  var flat = vec3f(1.0, 0.0, 0.0);
  if (side > 0.0001) { flat = vec3f(dir.x / side, 0.0, dir.z / side); }
  let toSun = max(dot(dir, frame.sunDir.xyz), 0.0);
  let t2 = toSun * toSun;
  let glow = 1.0 + 0.5 * t2 * t2 * t2 * t2;
  let horizon = skyRadiance(flat, 0.0) * glow;
  return colour * lost + horizon * (vec3f(1.0) - lost);
}
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
// split-sum environment BRDF (Karis' analytic fit): the mirror colour's share (x) and the rest (y)
fn envAB(rough : f32, nv : f32) -> vec2f {
  let c0 = vec4f(-1.0, -0.0275, -0.572, 0.022);
  let c1 = vec4f(1.0, 0.0425, 1.04, -0.04);
  let r = rough * c0 + c1;
  let a004 = min(r.x * r.x, exp2(-9.28 * nv)) * r.x + r.y;
  return vec2f(-1.04, 1.04) * a004 + r.zw;
}
fn envBrdf(f0 : vec3f, rough : f32, nv : f32) -> vec3f {
  let ab = envAB(rough, nv);
  return f0 * ab.x + ab.y;
}

// How much the shader does (render.shading), compiled in: a level's extra
// work isn't there at all in the levels below it (a shader keeps GPU registers
// for its heaviest path even when that path never runs).
//   0 Lowest: every material's colour, shine and glow, the lobes that cost per lamp left out
//   1 Standard: every lobe the film renderer has, from every light; shadows turned per pixel
//   2 High: sun shadows that sharpen where things touch and soften with distance; softer lamp-map shadows; finer creases
//   3 Highest: more of each (shadow taps, crease points, reflection steps)
override kShading : u32 = 1u;

// A number that differs from pixel to pixel (and, with frame blending, from
// frame to frame, so the blend smooths it): turns sample patterns.
fn pixelTurn(pix : vec2f) -> f32 {
  let still = fract(52.9829189 * fract(dot(pix, vec2f(0.06711056, 0.00583715))));
  return select(still, fract(still + frame.ao.z * 0.618034), frame.shadowFar.z > 0.5);
}
// 16 directions a golden angle apart: with radius sqrt((k + 0.5) / n), the
// first n cover a disk evenly for any n (Vogel's spiral)
const kSpiral = array<vec2f, 16>(vec2f(1.00000, 0.00000), vec2f(-0.73737, 0.67549), vec2f(0.08743, -0.99617), vec2f(0.60844, 0.79360), vec2f(-0.98471, -0.17418), vec2f(0.84376, -0.53673), vec2f(-0.25960, 0.96572), vec2f(-0.46091, -0.88745), vec2f(0.93932, 0.34304), vec2f(-0.92435, 0.38156), vec2f(0.42385, -0.90573), vec2f(0.29928, 0.95416), vec2f(-0.86521, -0.50141), vec2f(0.97668, -0.21472), vec2f(-0.57513, 0.81806), vec2f(-0.12851, -0.99171));
fn spiral(k : i32, n : i32, rot : vec2f) -> vec2f {
  let d = kSpiral[k] * sqrt((f32(k) + 0.5) / f32(n));
  return vec2f(d.x * rot.x - d.y * rot.y, d.x * rot.y + d.y * rot.x);
}

// One layer of the sun's shadow: taps over a disk `spread` uv wide (soft
// edges), turned by `rot`; 1 outside the layer.
fn sunShadowIn(layer : i32, p : vec4f, spread : f32, rot : vec2f) -> f32 {
  let uv = vec2f(p.x * 0.5 + 0.5, 0.5 - p.y * 0.5);
  if (uv.x < 0.0 || uv.x > 1.0 || uv.y < 0.0 || uv.y > 1.0 || p.z > 1.0) { return 1.0; }
  let n = select(select(12, 16, kShading >= 3u), 4, kShading == 0u);
  var wide = spread;
  if (kShading >= 2u) {
    // how far above this point what shades it is (the nearer depths round it,
    // on average) says how wide the sun's disk smears the shadow's edge here:
    // sharp where a thing stands on the ground, soft far from what casts it
    let size = vec2i(textureDimensions(shadowMap));
    let scale = select(1.0, 1.0 / frame.shadowFar.x, layer > 0);  // the far layer's box is that much bigger
    let search = spread * 3.0;
    var sum = 0.0;
    var count = 0.0;
    for (var k = 0; k < 8; k++) {
      let at = uv + spiral(k, 8, rot) * search;
      let d = textureLoad(shadowMap, clamp(vec2i(at * vec2f(size)), vec2i(0), size - 1), layer, 0);
      if (d < p.z - frame.fogColor.w) { sum += d; count += 1.0; }
    }
    if (count == 0.0) { return 1.0; }  // nothing near shades it
    let above = (p.z - sum / count) * frame.shade.z * scale;       // metres
    let smear = above * frame.shade.x / (frame.shade.y * scale);   // the half-shadow's half width, in uv
    wide = clamp(smear, spread * 0.5, spread * 3.0);
  }
  var s = 0.0;
  for (var i = 0; i < n; i++) {
    s += textureSampleCompareLevel(shadowMap, shadowSampler, uv + spiral(i, n, rot) * wide, layer, p.z - frame.fogColor.w);
  }
  return s / f32(n);
}
// How much of the fog along the view to a point at `dist` the sun reaches:
// 8 to 24 points along the way, by the shading level (nearer ones counting more, as their light is lost
// less on its way to the eye), each one tap of the near shadow map; turned per
// pixel and per frame, so frame blending smooths it into shafts.
fn fogSunlit(dist : f32, toPoint : vec3f, density : f32, pix : vec2f) -> f32 {
  let start = fract(52.9829189 * fract(dot(pix, vec2f(0.06711056, 0.00583715))) + frame.ao.z * 0.618034);
  var lit = 0.0;
  var total = 0.0;
  let n = select(select(select(8, 12, kShading >= 1u), 16, kShading >= 2u), 24, kShading >= 3u);
  for (var k = 0; k < n; k++) {
    let t = dist * (f32(k) + start) / f32(n);
    let w = exp(-density * t);
    let q = frame.camPos.xyz + toPoint * t;
    let p = frame.shadowViewProj * vec4f(q, 1.0);
    let uv = vec2f(p.x * 0.5 + 0.5, 0.5 - p.y * 0.5);
    var seen = 1.0;  // beyond the shadow maps: sunlit
    if (all(uv >= vec2f(0.0)) && all(uv <= vec2f(1.0)) && p.z <= 1.0) {
      seen = textureSampleCompareLevel(shadowMap, shadowSampler, uv, 0, p.z - frame.fogColor.w);
    } else if (frame.shadowFar.y > 0.5) {
      // past the near map: the far one (a low sun's shadows are long: a wood's
      // fog was lit as if no tree stood beyond the near map, a veil over everything)
      let pf = frame.shadowFarViewProj * vec4f(q, 1.0);
      let uf = vec2f(pf.x * 0.5 + 0.5, 0.5 - pf.y * 0.5);
      if (all(uf >= vec2f(0.0)) && all(uf <= vec2f(1.0)) && pf.z <= 1.0) {
        seen = textureSampleCompareLevel(shadowMap, shadowSampler, uf, i32(frame.shadowFar.y + 0.5), pf.z - frame.fogColor.w);
      }
    }
    lit += seen * w;
    total += w;
  }
  return lit / max(total, 1e-6);
}

// The sun's shadow: the sharp near map, and beyond it the far one (with the
// same softness in metres), blended over the near box's outer edge. `side`:
// the way the point is moved off its surface first (towards the sun's side).
fn shadowAt(world : vec3f, side : vec3f, pix : vec2f) -> f32 {
  let texel = 1.0 / frame.params.z;
  let spread = texel * frame.sh[0].w;
  var rot = vec2f(1.0, 0.0);
  if (kShading >= 1u && frame.shadowFar.z > 0.5) {
    let a = pixelTurn(pix) * 6.2831853;
    rot = vec2f(cos(a), sin(a));
  }
  let p = frame.shadowViewProj * vec4f(world + side * 0.03, 1.0);
  let edge = max(abs(p.x), abs(p.y));  // 0 at the near box's middle, 1 at its side
  if (frame.shadowFar.y < 0.5 || (edge < 0.8 && p.z <= 1.0)) { return sunShadowIn(0, p, spread, rot); }
  let pf = frame.shadowFarViewProj * vec4f(world + side * 0.03 / frame.shadowFar.x, 1.0);
  let far = sunShadowIn(i32(frame.shadowFar.y + 0.5), pf, max(spread * frame.shadowFar.x, texel * 0.5), rot);
  if (edge >= 0.96 || p.z > 1.0) { return far; }
  return mix(sunShadowIn(0, p, spread, rot), far, (edge - 0.8) / 0.16);
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

// One surface point's scattering, the film renderer's model (engine/render/bsdf.cpp):
// diffuse, a glossy mirror lobe (GGX), a clear coat over both, a sheen, and
// light let through thin things from behind.
struct Surf {
  n : vec3f,
  v : vec3f,
  nv : f32,
  diffuse : vec3f,      // already less by what the mirror lobe takes at this view
  translucent : vec3f,  // what comes through from behind
  f0 : vec3f,
  sheen : vec3f,
  comp : vec3f,         // light a rough surface sends back after bouncing between its own bumps
  alpha : f32,
  coat : f32,
  coatAlpha : f32,
};
// What the surface sends to the eye of light arriving from `l` (times the
// cosine). `bulb`: how much a lamp's size widens the highlight (0 for the sun).
fn surfLight(s : Surf, l : vec3f, bulb : f32) -> vec3f {
  let nl = dot(s.n, l);
  if (nl <= 0.0) { return s.translucent * (-nl / PI); }
  let h = normalize(l + s.v);
  let nh = max(dot(s.n, h), 0.0);
  let fres = pow(1.0 - clamp(dot(s.v, h), 0.0, 1.0), 5.0);
  // the coat sits on top: what it reflects doesn't reach what's under it
  let coatF = s.coat * (0.04 + 0.96 * fres);
  let under = 1.0 - coatF;
  // a bulb's size softens highlights (Karis' sphere-light trick)
  let a = clamp(s.alpha + bulb, 0.0, 1.0);
  let F = s.f0 + (vec3f(1.0) - s.f0) * fres;
  var f = s.diffuse / PI + F * s.comp * (D_ggx(nh, a) * V_smith(s.nv, nl, a) * (s.alpha * s.alpha / (a * a)));
  if (kShading >= 1u) {
    f += s.sheen * (fres / PI);
    f *= under;
    if (s.coat > 0.0) {
      let ca = clamp(s.coatAlpha + bulb, 0.0, 1.0);
      f += vec3f(coatF * D_ggx(nh, ca) * V_smith(s.nv, nl, ca) * (s.coatAlpha * s.coatAlpha / (ca * ca)));
    }
  }
  return f * nl;
}

// ---- panels (windows, softboxes, strips): glowing rectangles, lit as what they are
// A panel's two sides from the way it faces: the film renderer's rule (engine/render/pathtracer.cpp).
struct Sides { u : vec3f, v : vec3f };
fn panelSides(N : vec3f) -> Sides {
  if (abs(N.y) < 0.99) {
    let t = normalize(cross(vec3f(0.0, 1.0, 0.0), N));
    return Sides(t, cross(N, t));
  }
  let sg = select(-1.0, 1.0, N.z >= 0.0);
  let a = -1.0 / (sg + N.z);
  let c = N.x * N.y * a;
  return Sides(vec3f(1.0 + sg * N.x * N.x * a, sg * c, -sg * N.x), vec3f(c, sg + N.y * N.y * a, -N.y));
}
// One edge of a glowing polygon seen from the point: its share of the light,
// as a vector (Lambert's formula; Heitz's fit of the angle over its sine)
fn edgeLight(a : vec3f, b : vec3f) -> vec3f {
  let x = dot(a, b);
  let y = abs(x);
  let p = 0.8543985 + (0.4965155 + 0.0145206 * y) * y;
  let q = 3.4175940 + (4.1616724 + y) * y;
  let w = p / q;
  return cross(a, b) * select(0.5 * inverseSqrt(max(1.0 - x * x, 1e-7)) - w, w, x > 0.0);
}
// How much of a ball of light (its size sin^2, its height over the surface's
// horizon as a cosine) falls on a surface, with the part below the horizon cut
// off: a fit (mean error 0.005). A polygon's light is close to that of the
// ball with the same vector (Heitz and Hill 2017): no cutting of corners.
fn ballOverHorizon(x : f32, y : f32) -> f32 {
  return clamp(x * (0.9245867 + y) * (0.5 + (y - 0.9245867) * (0.5359050 + x * (-1.0054222 + x * (1.8199061 - x * 1.3172082)))), 0.0, 1.0);
}
// What the surface sends to the eye of a panel's light, the panel taken as the
// rectangle it is: its diffuse light exactly (a big panel overhead no longer
// lights the floor under it as if it were one point: that was up to half too
// bright), and its shine as its own mirror image, never brighter than the panel.
fn panelLight(s : Surf, p : vec3f, L : Light) -> vec3f {
  let N = L.dir.xyz;
  let centre = L.pos.xyz - p;
  if (dot(centre, N) >= 0.0) { return vec3f(0.0); }  // behind it: panels shine one way
  let sd = panelSides(N);
  let hu = sd.u * (0.5 * L.cone.z);
  let hv = sd.v * (0.5 * L.cone.w);
  let c0 = normalize(centre - hu - hv);
  let c1 = normalize(centre + hu - hv);
  let c2 = normalize(centre + hu + hv);
  let c3 = normalize(centre - hu + hv);
  var F = edgeLight(c0, c1) + edgeLight(c1, c2) + edgeLight(c2, c3) + edgeLight(c3, c0);
  if (dot(F, centre) < 0.0) { F = -F; }
  let len = length(F);
  if (len < 1e-7) { return vec3f(0.0); }
  let towards = F / len;
  let coatNow = s.coat * (0.04 + 0.96 * pow(1.0 - s.nv, 5.0));
  let under = 1.0 - coatNow;
  var c = s.diffuse * (ballOverHorizon(len, dot(s.n, towards)) * under);
  if (dot(s.translucent, vec3f(1.0)) > 0.0) { c += s.translucent * ballOverHorizon(len, -dot(s.n, towards)); }
  // the shine: the point of the panel nearest where the mirrored view meets its plane
  let r = reflect(-s.v, s.n);
  let rn = dot(r, N);
  var q = r * length(centre);
  if (rn < -1e-4) { q = r * (dot(centre, N) / rn); }
  let rel = q - centre;
  let on = centre + sd.u * clamp(dot(rel, sd.u), -0.5 * L.cone.z, 0.5 * L.cone.z) + sd.v * clamp(dot(rel, sd.v), -0.5 * L.cone.w, 0.5 * L.cone.w);
  let d2 = max(dot(on, on), 1e-6);
  let l = on * inverseSqrt(d2);
  let nl = dot(s.n, l);
  if (nl > 0.0) {
    let h = normalize(l + s.v);
    let nh = max(dot(s.n, h), 0.0);
    let fres = pow(1.0 - clamp(dot(s.v, h), 0.0, 1.0), 5.0);
    // the panel's size as seen from here: a mirror shows the panel itself (1),
    // a rough surface spreads that much of it (less)
    let solid = min(L.cone.z * L.cone.w * max(-dot(l, N), 0.0) / d2, 6.2831853);
    let F0 = s.f0 + (vec3f(1.0) - s.f0) * fres;
    c += F0 * s.comp * (min(D_ggx(nh, s.alpha) * V_smith(s.nv, nl, s.alpha) * nl * solid, 1.0) * (1.0 - s.coat * (0.04 + 0.96 * fres)));
    if (s.coat > 0.0) { c += vec3f(s.coat * (0.04 + 0.96 * fres) * min(D_ggx(nh, s.coatAlpha) * V_smith(s.nv, nl, s.coatAlpha) * nl * solid, 1.0)); }
  }
  return c * L.color.rgb;
}

var<private> resolving : bool = false;  // shading the visibility picture: never cut holes
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
    if (m.tex2.x > 0.0 && c.a < m.tex2.x && !resolving) { discard; }
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
    if (abs(x) > width && !resolving) { discard; }
  }
  var glowTint = vec3f(1.0);  // a windows face glows only in its lit windows
  var glowScale = 1.0;  // a see-through grid's lines fade at their edges and far away
  if (m.pattern.x > 0.5) {
    // detail smaller than a pixel fades to its average (no shimmer, no moire)
    let kind = i32(m.pattern.x);
    let sc = max(m.pattern.y, 0.01);
    let lod = length(abs(slopeWorldX) + abs(slopeWorldY));
    var pv = vec3f(0.0, 0.5, 1.0);
    if (kind == 11) {
      // windows: glass where a window is (near black, in pattern_color's hue), a glow where one is lit.
      // Its slots in the material are not scale, bump or roughness variation (see windowsAt).
      let wv = windowsAt(m, i.world, n, lod);
      base = mix(m.base.rgb, m.colorB.rgb * 0.02 / max(dot(m.colorB.rgb, vec3f(0.2126, 0.7152, 0.0722)), 0.001), wv.cover);
      rough = mix(rough, 0.05, wv.cover);
      glowTint = wv.glow;
    } else {
      pv = patternLod(m, i.world, n, lod);
    }
    if (kind == 10) {
      pv = vec3f(gridLine(i.world, n, sc), 0.5, 1.0);
      // a see-through grid (opacity under a half): only its lines are drawn
      if (m.emission.w < 0.5) {
        if (pv.x < 0.5 && !resolving) { discard; }
        glowScale = smoothstep(0.5, 1.0, pv.x);
      }
    }
    if (kind != 11) {
      base = mix(m.base.rgb, m.colorB.rgb, clamp(pv.x, 0.0, 1.0)) * pv.z;
      rough = clamp(rough + (pv.x - 0.5) * m.colorB.w, 0.02, 1.0);
    }
    if (m.pattern.z > 0.0 && kind != 11) {
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
  let glass = m.props.y * (1.0 - metal);
  let alpha = max(rough * rough, 0.002);
  let nv = max(dot(n, v), 1e-4);
  var sf : Surf;
  sf.n = n;
  sf.v = v;
  sf.nv = nv;
  sf.alpha = alpha;
  // the mirror colour: a non-metal's from `specular` (0.5: 4%), a metal's its own;
  // glass does its own mirror below
  sf.f0 = mix(vec3f(0.08 * m.coat.y), base, metal) * (1.0 - glass);
  let transl = m.extra.y * (1.0 - metal) * (1.0 - glass);
  let sss = m.extra.x * (1.0 - metal) * (1.0 - glass);
  let ab = envAB(rough, nv);
  let mirrorNow = sf.f0 + (vec3f(1.0) - sf.f0) * pow(1.0 - nv, 5.0);
  sf.diffuse = base * ((1.0 - metal) * (1.0 - glass) * (1.0 - transl) * (1.0 - 0.5 * sss)) * (1.0 - dot(mirrorNow, vec3f(0.2126, 0.7152, 0.0722)));
  sf.translucent = base * transl + base * m.sss.rgb * (0.5 * sss);
  sf.sheen = vec3f(m.props.w) * mix(vec3f(1.0), base, 0.5) * (1.0 - metal);
  sf.coat = m.props.z;
  sf.coatAlpha = max(m.coat.x * m.coat.x, 1e-4);
  sf.comp = vec3f(1.0);
  if (kShading >= 1u) { sf.comp = vec3f(1.0) + sf.f0 * (1.0 / max(ab.x + ab.y, 0.05) - 1.0); }
  let lit2 = dot(sf.translucent, vec3f(1.0)) > 0.0;  // lit from behind too
  var lo = vec3f(0.0);

  // the sun, with shadows
  if (frame.sunDir.w > 0.5) {
    let l = frame.sunDir.xyz;
    let nl = dot(n, l);
    if (nl > 0.0 || lit2) {
      let c = surfLight(sf, l, 0.0);
      if (dot(c, c) > 0.0) { lo += c * frame.sunColor.rgb * shadowAt(i.world, n * sign(nl), i.clip.xy); }
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
  // High (phase 8, render.allLampShadows, ReSTIR's idea of reusing work over
  // time): the lamps with no shadow of their own add up here, and one of them,
  // picked by how much light it gives this point, is traced; the answer is
  // blended into this pixel's own over the frames (where its point was: the
  // light pass keeps them), which stands for all of them: every lamp casts
  // shadows, for one ray a pixel
  let picking = kLampShadows && frame.lampMaps.z > 0.5;
  var restLight = vec3f(0.0);
  var restW = 0.0;
  var pick = 0xffffffffu;
  var seed = u32(i.clip.x) * 1973u + u32(i.clip.y) * 9277u + u32(frame.ao.z) * 26699u;
  for (var j = first; j < last; j++) {
    var k = j;
    if (gridOn) { k = textureLoad(gridList, vec2i(i32(j & 1023u), i32(j >> 10u)), 0).x; }
    let L = lights[k];
    var tl = L.pos.xyz - i.world;
    let d2 = max(dot(tl, tl), L.pos.w * L.pos.w);
    let l = tl * inverseSqrt(max(dot(tl, tl), 1e-8));
    let nl = dot(n, l);
    // a panel near enough to show its size (within six of its diagonals; a far
    // one, or a group of them sent as one lamp, is a point to the eye)
    let diag2 = L.cone.z * L.cone.z + L.cone.w * L.cone.w;
    let wide = kShading >= 1u && L.dir.w > 1.5 && L.cone.x > 0.5 && d2 < 36.0 * diag2;
    if (nl <= 0.0 && !lit2 && !wide) { continue; }
    var I = L.color.rgb / d2;
    if (L.dir.w > 0.5 && L.dir.w < 1.5) {  // spot
      I *= smoothstep(L.cone.y, L.cone.x, dot(-l, L.dir.xyz));
    } else if (L.dir.w > 1.5) {  // panel: radiance x area x cosines
      I = L.color.rgb * L.cone.z * L.cone.w * max(dot(-l, L.dir.xyz), 0.0) / d2;
    }
    if (dot(I, I) == 0.0 && !wide) { continue; }
    let off = ng * sign(nl);  // off the surface, on the lamp's side
    // the main lamps' shadow maps (Phone): only the first few lamps have one
    var ownShadow = false;
    if (k < u32(frame.lampMaps.x) && frame.lampValid[k] > 0.5) {
      I *= lampMapSeen(i.world + off * 0.02, L, k, i.clip.xy);
      ownShadow = true;
      if (dot(I, I) == 0.0) { continue; }
    }
    // a real shadow: can this point see the lamp? (rays through the scene)
    // panels (windows, softboxes) are left unshadowed: their real shadows are
    // so soft they barely show, and one ray to their middle would cost the
    // most of any lamp (long rays across a whole room) for a wrong, hard edge
    if (kLampShadows && L.dir.w < 1.5 && shadowed < u32(frame.params.y) && dot(I, vec3f(0.3333)) * preExposure() > 2e-4) {
      shadowed += 1u;
      I *= lampVisibility(i.world + off * 0.004, L, i.clip.xy);
      ownShadow = true;
      if (dot(I, I) == 0.0) { continue; }
    }
    // a lamp with no shadow of its own: the ambient occlusion stands in for
    // the shadows it would cast in creases and under things (most of a room's
    // lamps on Phone), in part
    let picked = picking && !ownShadow && L.dir.w < 1.5;
    if (!ownShadow && !picked) { I *= mix(1.0, aoTerm, 0.7); }
    var c = surfLight(sf, l, L.pos.w / (2.0 * sqrt(d2))) * I;
    // (blended into the point's light over the last of that reach: no ring where one takes over)
    if (wide) { c = mix(panelLight(sf, i.world, L) * mix(1.0, aoTerm, 0.7), c, smoothstep(25.0, 36.0, d2 / diag2)); }
    if (picked) {
      // one kept, each with the chance of its share of the light so far
      let w = dot(c, vec3f(0.3333));
      restLight += c;
      restW += w;
      seed = seed * 747796405u + 2891336453u;
      if (f32(seed >> 8u) / 16777216.0 * restW < w) { pick = k; }
    } else {
      lo += c;
    }
  }
  if (pick != 0xffffffffu) {
    // a pixel with a settled answer traces one frame in four (each of a 2 x 2
    // square in turn): its answer is an average over many frames anyway, and
    // the ray is the light pass's dearest work (your phone, the bar on High:
    // half the pixels each frame still cost 12 ms)
    let corner = (u32(i.clip.x) & 1u) | ((u32(i.clip.y) & 1u) << 1u);
    let rest = shadowPast >= 0.0 && shadowCount >= 4.0 && corner != (u32(frame.ao.z) & 3u);
    if (rest) {
      shadowNow = shadowPast;
    } else {
      let Lp = lights[pick];
      let seenNow = lampVisibility(i.world + ng * (0.004 * sign(dot(ng, Lp.pos.xyz - i.world))), Lp, i.clip.xy);
      shadowCount = select(1.0, min(shadowCount + 1.0, 32.0), shadowPast >= 0.0);
      shadowNow = select(seenNow, mix(shadowPast, seenNow, 1.0 / shadowCount), shadowPast >= 0.0);
    }
    lo += restLight * shadowNow;
  }
  // the soft light. Diffuse: the sky's from its harmonics, or the light cache's
  // (what really reaches here: walls, floor, the sky through openings). Glossy:
  // what the screen shows that way (High), else the blurred sky, only as bright
  // as the light coming from that way says the sky is seen there.
  let r = reflect(-v, n);
  let cached = frame.cache1.w > 0.5;
  var softN = shIrradiance(n);
  var softBack = vec3f(0.0);
  var skySeen = 1.0;
  var room = vec3f(0.0);
  if (cached) {
    // the reflected way: the sky by the share of it the probes see that way
    // (none under a ceiling), and the room's own light from that way (what
    // arrives beyond the sky's part) as an even glow: a closed room's floor
    // mirrors the room, by day and by night, never a sky it can't see
    softN = cacheLight(i.world, n);
    let cr = cacheLight4(i.world, r);
    skySeen = clamp(cr.a, 0.0, 1.0);
    room = max(cr.rgb - shIrradiance(r) * skySeen, vec3f(0.0)) / PI;
  }
  if (lit2) {
    softBack = shIrradiance(-n);
    if (cached) { softBack = cacheLight(i.world, -n); }
  }
  // a little less sky low down (render.occlusion), all of it times render.skyLight
  let occl = mix(1.0, clamp(0.4 + 0.6 * (n.y * 0.5 + 0.5), 0.0, 1.0), frame.fixedExposure.w) * frame.sunColor.w;
  // creases: the soft light they lose. A pale surface loses less (light keeps
  // bouncing in a pale crease: Jimenez 2016); a mirror loses only what its
  // own narrow view of the room is hidden (Lagarde 2014)
  var aoDiffuse = vec3f(aoTerm);
  var aoMirror = aoTerm;
  if (kShading >= 1u && aoTerm < 1.0) {
    let al = clamp(sf.diffuse + sf.translucent, vec3f(0.0), vec3f(1.0));
    let ka = 2.0404 * al - 0.3324;
    let kb = -4.7951 * al + 0.6417;
    let kc = 2.7552 * al + 0.6903;
    aoDiffuse = max(vec3f(aoTerm), ((aoTerm * ka + kb) * aoTerm + kc) * aoTerm);
    aoMirror = clamp(pow(nv + aoTerm, exp2(-16.0 * rough - 1.0)) - 1.0 + aoTerm, 0.0, 1.0);
  }
  let coatNow = sf.coat * (0.04 + 0.96 * pow(1.0 - nv, 5.0));
  let under = select(1.0, 1.0 - coatNow, kShading >= 1u);
  let seenBase = ssrSeen * (1.0 - smoothstep(0.25, 0.5, rough));
  let mirror = (sf.f0 * ab.x + ab.y) * sf.comp;
  lo += (sf.diffuse * softN * under + sf.translucent * softBack) / PI * occl * aoDiffuse;
  lo += ((skyRadiance(r, rough * 6.0) * skySeen + room) * (1.0 - seenBase) * occl + ssrColor * seenBase) * mirror * under * aoMirror;
  if (sf.coat > 0.0) {  // clear coat: a second, sharp reflection over everything
    let coatRough = sqrt(sf.coatAlpha);
    let seenCoat = ssrSeen * (1.0 - smoothstep(0.25, 0.5, coatRough));
    if (kShading == 0u) { lo *= 1.0 - coatNow; }
    lo += ((skyRadiance(r, coatRough * 6.0) * skySeen + room) * (1.0 - seenCoat) * occl + ssrColor * seenCoat) * coatNow * aoMirror;
  }
  if (m.props.w > 0.0) {  // sheen: a soft rim (the lobe's share of even light: a fit)
    lo += sf.sheen * softN / PI * (0.028 * pow(1.0 - nv, 2.4)) * under * occl * aoDiffuse;
  }
  lo += m.emission.rgb * glowScale * glowTint;
  var outA = m.emission.w;
  if (glass > 0.0) {
    // glass: what's reflected (by its own bending strength), plus the tint of
    // what's behind (blended)
    let g0 = (m.extra.w - 1.0) / (m.extra.w + 1.0);
    let F = schlick(vec3f(g0 * g0), nv);
    lo += (skyRadiance(r, 0.0) * skySeen + room) * F * (occl * glass);
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
    // the sun lights only the fog it reaches: under trees most of it is in
    // shade, with shafts of light between (god rays, as the film renderer has)
    var sunlit = 1.0;
    if (frame.sunDir.w > 0.5) { sunlit = fogSunlit(dist, -v, frame.fog.x, i.clip.xy); }
    // its soft light: the light cache's halfway along the view (a closed
    // room's haze lit by the room, not the open sky), else the sky's
    var soft = shIrradiance(vec3f(0.0, 1.0, 0.0));
    if (frame.cache1.w > 0.5) { soft = cacheLight(frame.camPos.xyz - v * (dist * 0.5), vec3f(0.0, 1.0, 0.0)) * frame.sunColor.w; }
    let inscatter = frame.fogColor.rgb * (soft / (4.0 * PI) + frame.sunColor.rgb * phase * frame.sunDir.w * sunlit);
    lo = lo * t + inscatter * (1.0 - t);
  }
  // haze: far things fade to the horizon's colour, one formula with the film's
  lo = hazeAt(lo, frame.camPos.xyz, i.world);
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

}  // namespace loop::gpu
