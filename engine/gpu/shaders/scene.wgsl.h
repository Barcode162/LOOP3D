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
// lamp or the sun, blended between the probes round it and between the faces.
fn cacheLight(p : vec3f, n : vec3f) -> vec3f {
  let dims = frame.cache1.xyz;
  let half = 0.5 / dims;
  let uvw = clamp(((p - frame.cache0.xyz) / frame.cache0.w + 0.5) / dims, half, 1.0 - half);
  let fx = select(textureSampleLevel(cacheNX, skySampler, uvw, 0.0), textureSampleLevel(cachePX, skySampler, uvw, 0.0), n.x > 0.0).rgb;
  let fy = select(textureSampleLevel(cacheNY, skySampler, uvw, 0.0), textureSampleLevel(cachePY, skySampler, uvw, 0.0), n.y > 0.0).rgb;
  let fz = select(textureSampleLevel(cacheNZ, skySampler, uvw, 0.0), textureSampleLevel(cachePZ, skySampler, uvw, 0.0), n.z > 0.0).rgb;
  return fx * n.x * n.x + fy * n.y * n.y + fz * n.z * n.z;
}

// How much of lamp k (one of the first, mapped) a point sees: its depth in the
// lamp's view that faces it, against the map (the chip blends 2x2 of them).
fn lampMapSeen(p : vec3f, L : Light, k : u32) -> f32 {
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
// split-sum environment BRDF (Karis' analytic fit)
fn envBrdf(f0 : vec3f, rough : f32, nv : f32) -> vec3f {
  let c0 = vec4f(-1.0, -0.0275, -0.572, 0.022);
  let c1 = vec4f(1.0, 0.0425, 1.04, -0.04);
  let r = rough * c0 + c1;
  let a004 = min(r.x * r.x, exp2(-9.28 * nv)) * r.x + r.y;
  let ab = vec2f(-1.04, 1.04) * a004 + r.zw;
  return f0 * ab.x + ab.y;
}

// One layer of the sun's shadow: 12 taps of a Poisson disk (soft edges),
// `spread` uv wide; 1 outside it.
fn sunShadowIn(layer : i32, p : vec4f, spread : f32) -> f32 {
  let uv = vec2f(p.x * 0.5 + 0.5, 0.5 - p.y * 0.5);
  if (uv.x < 0.0 || uv.x > 1.0 || uv.y < 0.0 || uv.y > 1.0 || p.z > 1.0) { return 1.0; }
  var s = 0.0;
  let taps = array<vec2f, 12>(vec2f(-0.326, -0.406), vec2f(-0.840, -0.074), vec2f(-0.696, 0.457), vec2f(-0.203, 0.621),
                              vec2f(0.962, -0.195), vec2f(0.473, -0.480), vec2f(0.519, 0.767), vec2f(0.185, -0.893),
                              vec2f(0.507, 0.064), vec2f(0.896, 0.412), vec2f(-0.322, -0.933), vec2f(-0.792, -0.598));
  for (var i = 0; i < 12; i++) {
    s += textureSampleCompareLevel(shadowMap, shadowSampler, uv + taps[i] * spread, layer, p.z - frame.fogColor.w);
  }
  return s / 12.0;
}
// How much of the fog along the view to a point at `dist` the sun reaches:
// 8 points along the way (nearer ones counting more, as their light is lost
// less on its way to the eye), each one tap of the near shadow map; turned per
// pixel and per frame, so frame blending smooths it into shafts.
fn fogSunlit(dist : f32, toPoint : vec3f, density : f32, pix : vec2f) -> f32 {
  let start = fract(52.9829189 * fract(dot(pix, vec2f(0.06711056, 0.00583715))) + frame.ao.z * 0.618034);
  var lit = 0.0;
  var total = 0.0;
  for (var k = 0; k < 8; k++) {
    let t = dist * (f32(k) + start) / 8.0;
    let w = exp(-density * t);
    let q = frame.camPos.xyz + toPoint * t;
    let p = frame.shadowViewProj * vec4f(q, 1.0);
    let uv = vec2f(p.x * 0.5 + 0.5, 0.5 - p.y * 0.5);
    var seen = 1.0;  // beyond the shadow map: sunlit
    if (all(uv >= vec2f(0.0)) && all(uv <= vec2f(1.0)) && p.z <= 1.0) {
      seen = textureSampleCompareLevel(shadowMap, shadowSampler, uv, 0, p.z - frame.fogColor.w);
    }
    lit += seen * w;
    total += w;
  }
  return lit / max(total, 1e-6);
}

// The sun's shadow: the sharp near map, and beyond it the far one (with the
// same softness in metres), blended over the near box's outer edge.
fn shadowAt(world : vec3f, n : vec3f) -> f32 {
  let texel = 1.0 / frame.params.z;
  let spread = texel * frame.sh[0].w;
  let p = frame.shadowViewProj * vec4f(world + n * 0.03, 1.0);
  let edge = max(abs(p.x), abs(p.y));  // 0 at the near box's middle, 1 at its side
  if (frame.shadowFar.y < 0.5 || (edge < 0.8 && p.z <= 1.0)) { return sunShadowIn(0, p, spread); }
  let pf = frame.shadowFarViewProj * vec4f(world + n * 0.03 / frame.shadowFar.x, 1.0);
  let far = sunShadowIn(i32(frame.shadowFar.y + 0.5), pf, max(spread * frame.shadowFar.x, texel * 0.5));
  if (edge >= 0.96 || p.z > 1.0) { return far; }
  return mix(sunShadowIn(0, p, spread), far, (edge - 0.8) / 0.16);
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
  var glowScale = 1.0;  // a see-through grid's lines fade at their edges and far away
  if (m.pattern.x > 0.5) {
    // detail smaller than a pixel fades to its average (no shimmer, no moire)
    let kind = i32(m.pattern.x);
    let sc = max(m.pattern.y, 0.01);
    let lod = length(abs(slopeWorldX) + abs(slopeWorldY));
    var pv = patternLod(m, i.world, n, lod);
    if (kind == 10) {
      pv = vec3f(gridLine(i.world, n, sc), 0.5, 1.0);
      // a see-through grid (opacity under a half): only its lines are drawn
      if (m.emission.w < 0.5) {
        if (pv.x < 0.5 && !resolving) { discard; }
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
    if (nl <= 0.0) { continue; }
    var I = L.color.rgb / d2;
    if (L.dir.w > 0.5 && L.dir.w < 1.5) {  // spot
      I *= smoothstep(L.cone.y, L.cone.x, dot(-l, L.dir.xyz));
    } else if (L.dir.w > 1.5) {  // panel: radiance x area x cosines
      I = L.color.rgb * L.cone.z * L.cone.w * max(dot(-l, L.dir.xyz), 0.0) / d2;
    }
    // the main lamps' shadow maps (Phone): only the first few lamps have one
    var ownShadow = false;
    if (k < u32(frame.lampMaps.x) && frame.lampValid[k] > 0.5) {
      I *= lampMapSeen(i.world + ng * 0.02, L, k);
      ownShadow = true;
      if (dot(I, I) == 0.0) { continue; }
    }
    // a real shadow: can this point see the lamp? (rays through the scene)
    // panels (windows, softboxes) are left unshadowed: their real shadows are
    // so soft they barely show, and one ray to their middle would cost the
    // most of any lamp (long rays across a whole room) for a wrong, hard edge
    if (kLampShadows && L.dir.w < 1.5 && shadowed < u32(frame.params.y) && dot(I, vec3f(0.3333)) * preExposure() > 2e-4) {
      shadowed += 1u;
      I *= lampVisibility(i.world + ng * 0.004, L, i.clip.xy);
      ownShadow = true;
      if (dot(I, I) == 0.0) { continue; }
    }
    // a lamp with no shadow of its own: the ambient occlusion stands in for
    // the shadows it would cast in creases and under things (most of a room's
    // lamps on Phone), in part
    let picked = picking && !ownShadow && L.dir.w < 1.5;
    if (!ownShadow && !picked) { I *= mix(1.0, aoTerm, 0.7); }
    let h = normalize(l + v);
    let nh = max(dot(n, h), 0.0);
    // a bulb's size softens highlights (Karis' sphere-light trick)
    let a2 = clamp(alpha + L.pos.w / (2.0 * sqrt(d2)), 0.0, 1.0);
    let F = schlick(f0, max(dot(v, h), 0.0));
    let spec = D_ggx(nh, a2) * V_smith(nv, nl, a2) * F * (alpha * alpha / (a2 * a2));
    let c = (diffuseColor * (1.0 - F) / PI + spec) * nl * I;
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
      let seenNow = lampVisibility(i.world + ng * 0.004, lights[pick], i.clip.xy);
      shadowCount = select(1.0, min(shadowCount + 1.0, 32.0), shadowPast >= 0.0);
      shadowNow = select(seenNow, mix(shadowPast, seenNow, 1.0 / shadowCount), shadowPast >= 0.0);
    }
    lo += restLight * shadowNow;
  }
  // the sky's light: diffuse from its harmonics, glossy from the blurred sky
  let r = reflect(-v, n);
  let envSpec = skyRadiance(r, rough * 6.0) * envBrdf(f0, rough, nv);
  // a little less sky low down (render.occlusion), all of it times render.skyLight
  let occl = mix(1.0, clamp(0.4 + 0.6 * (n.y * 0.5 + 0.5), 0.0, 1.0), frame.fixedExposure.w);
  if (frame.cache1.w > 0.5) {
    // the light cache: the soft light that really reaches here (walls, floor,
    // the sky through openings); reflections of the sky only as bright as the
    // light coming from that way says the sky is seen there
    let seen = clamp(dot(cacheLight(i.world, r), vec3f(0.2126, 0.7152, 0.0722)) / max(dot(shIrradiance(r), vec3f(0.2126, 0.7152, 0.0722)), 1e-4), 0.0, 1.0);
    lo += (diffuseColor * cacheLight(i.world, n) / PI + envSpec * seen * (1.0 - ssrSeen)) * occl * aoTerm * frame.sunColor.w;
  } else {
    lo += (diffuseColor * shIrradiance(n) / PI + envSpec * (1.0 - ssrSeen)) * occl * aoTerm * frame.sunColor.w;
  }
  // what the screen shows in the reflected direction (the room itself, not the sky)
  lo += ssrColor * envBrdf(f0, rough, nv) * ssrSeen;
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
