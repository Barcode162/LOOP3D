#pragma once
// Part of the live renderer's shaders (engine/gpu/shaders.h).

namespace loop::gpu {

inline const char* kCommonWgsl = R"(
struct Frame {
  viewProj : mat4x4f,
  invViewProj : mat4x4f,
  shadowViewProj : mat4x4f,
  camPos : vec4f,        // xyz, time
  sunDir : vec4f,        // towards the sun, w = sun on
  sunColor : vec4f,      // illuminance (lux) as rgb, w: sky light strength (render.skyLight)
  sh : array<vec4f, 9>,  // sky light, spherical harmonics; sh[0].w shadow softness, sh[1].w pattern fade, sh[2].w haze visibility (0 off), sh[3].w haze height
  fog : vec4f,           // density, top, anisotropy, on
  fogColor : vec4f,      // rgb, w: sun shadow depth bias
  params : vec4f,        // light count, lamps with shadow rays, shadow map size, auto exposure
  screen : vec4f,        // width, height, 1/width, 1/height
  fixedExposure : vec4f, // x: exposure when not auto, y: rays per lamp, z: debug view, w: occlusion strength
  exposure : vec4f,      // the eyes' last exposure (copied in on the GPU each frame: one storage buffer fewer)
  grid0 : vec4f,         // the light grid: low corner xyz, cell size
  grid1 : vec4f,         // cells x, y, z; w: on
  lampMaps : vec4f,      // lamp shadow maps: how many lamps (the first in the list), tile size, -, -
  lampFaces : array<mat4x4f, 12>,  // each mapped lamp's six views (+x -x +y -y +z -z)
  lampValid : vec4f,     // per row: drawn for its lamp (1), or not yet (0: no shadow for a frame)
  shadowFarViewProj : mat4x4f,  // the sun's far shadow (the shadow map's second layer)
  shadowFar : vec4f,     // the near box's size over the far one's, the far map's layer (1 or 2; 0: none); frame blending on (1); w: drawn pixels per shown pixel (1, or less when frame blending upscales)
  cache0 : vec4f,        // the light cache: first probe xyz, spacing
  cache1 : vec4f,        // probes x, y, z; w: on
  ao : vec4f,            // ambient occlusion: strength (0 off), reach (m), frame number; w: screen reflections on (1)
  prevViewProj : mat4x4f,  // last frame's view, unshifted (screen reflections read last frame's picture)
  prevCam : vec4f,       // last frame's eye
  prevScreen : vec4f,    // last frame's drawn size
  shade : vec4f,         // the sun's shadow in true sizes: tan of the sun's angular radius, the near box's width (m), its depth (m); -
};
struct Light {
  pos : vec4f,     // xyz, radius
  color : vec4f,   // rgb candela (point/spot) or nits (panel)
  dir : vec4f,     // xyz, kind (0 point, 1 spot, 2 panel)
  cone : vec4f,    // spot: cos inner, cos outer; panel: 1 for a true rectangle (0: a group of far ones as one lamp), -, its width, height
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
  coat : vec4f,      // clear coat roughness, specular (0.5: 4% mirror), -, -
  sss : vec4f,       // subsurface colour, -
  // a surface (docs/SURFACES.md): its baked tile and the numbers worked out where drawn
  surf0 : vec4f,     // colour layer (< 0: not a surface), shape layer, tile (m), wander layer (-1 none)
  surf1 : vec4f,     // slow change: size (m), amount, roughness; 1 in world space (0 the thing's)
  surf2 : vec4f,     // slow change's tint, projection (0 box, 1 top, 2 uv)
  wear0 : vec4f, wear1 : vec4f,   // wear: colour, roughness; metal, amount (0 off), ragged, soft
  dirt0 : vec4f, dirt1 : vec4f,   // dirt: the same
  coat0 : vec4f, coat1 : vec4f, coat2 : vec4f,  // the coat that faces a way: as wear, cover for amount; facing (1 up, 2 down, 3 side), sits (1 high, 2 low)
  det0 : vec4f,      // the detail: its layer (-1 none), its tile (m), the distance it is gone by (m), how much it changes the shine
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
// ambient occlusion (phase 6): how much of the soft light reaches this point
// past what's near it, found by the light pass in the depth picture (1 elsewhere)
var<private> aoTerm : f32 = 1.0;
// screen reflections (phase 8, High): what a glossy surface reflects, found on
// screen by the light pass, and how sure (0: the sky's and the light cache's)
var<private> ssrColor : vec3f = vec3f(0.0);
var<private> ssrSeen : f32 = 0.0;  // how sure, before the surface's roughness counts
// the shared lamp shadow (High), kept over frames: this pixel's answer last
// frame where its point was (-1: none, start afresh), and this frame's
var<private> shadowPast : f32 = -1.0;
var<private> shadowNow : f32 = 1.0;
var<private> shadowCount : f32 = 0.0;  // frames it has been kept (to 32: blended by 1 over that)
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

// The windows pattern (kind 11) live, as film (engine/render/pattern.cpp): cells one floor high and one
// bay wide, a window in each one's middle, lit or dark by the same hash, with the same glow and colours.
// Its slots in Material (set in renderer_scene.cpp; windows has no scale, bump, contrast or roughness
// variation to send): pattern.y floor height, pattern.z bay, pattern.w window width, tex2.z window
// height, tex2.w the share lit, colorB.w the seed (a whole number). Returns how much of the pixel is
// window glass (cover) and the glow multiplier (times the material's emission). Where a window is
// smaller than a pixel it blends to the cell's average, and where a cell is, to the whole face's.
struct Windows { cover : f32, glow : vec3f };
fn hash32(x : u32) -> u32 {
  var v = x;
  v = v ^ (v >> 16u);
  v = v * 0x7feb352du;
  v = v ^ (v >> 15u);
  v = v * 0x846ca68bu;
  v = v ^ (v >> 16u);
  return v;
}
fn cellHashU(ix : i32, iy : i32, s : u32) -> f32 {
  return f32(hash32((bitcast<u32>(ix) * 0x9e3779b1u) ^ (bitcast<u32>(iy) * 0x85ebca77u) ^ s) >> 8u) * (1.0 / 16777216.0);
}
// a black-body colour at luminance 1 (the film's kelvin(): Kim's fit to CIE xy, then linear sRGB)
fn kelvinColor(k : f32) -> vec3f {
  let t = clamp(k, 1000.0, 40000.0);
  let t2 = t * t;
  let t3 = t2 * t;
  var x = -3.0258469e9 / t3 + 2.1070379e6 / t2 + 0.2226347e3 / t + 0.240390;
  if (t <= 4000.0) { x = -0.2661239e9 / t3 - 0.2343589e6 / t2 + 0.8776956e3 / t + 0.179910; }
  let x2 = x * x;
  let x3 = x2 * x;
  var y = 3.0817580 * x3 - 5.87338670 * x2 + 3.75112997 * x - 0.37001483;
  if (t <= 4000.0) { y = -0.9549476 * x3 - 1.37418593 * x2 + 2.09137015 * x - 0.16748867; }
  if (t <= 2222.0) { y = -1.1063814 * x3 - 1.34811020 * x2 + 2.18555832 * x - 0.20219683; }
  let X = x / y;
  let Z = (1.0 - x - y) / y;
  let c = max(vec3f(3.2404542 * X - 1.5371385 - 0.4985314 * Z, -0.9692660 * X + 1.8760108 + 0.0415560 * Z, 0.0556434 * X - 0.2040259 + 1.0572252 * Z), vec3f(0.0));
  let l = dot(c, vec3f(0.2126, 0.7152, 0.0722));
  return select(vec3f(1.0), c / l, l > 0.0);
}
fn windowsAt(m : Material, p : vec3f, n : vec3f, lod : f32) -> Windows {
  var o : Windows;
  o.cover = 0.0;
  o.glow = vec3f(0.0);
  if (abs(n.y) > 0.7) { return o; }  // roofs and floors have none
  let floorH = max(m.pattern.y, 0.01);
  let bay = max(m.pattern.z, 0.01);
  let win = vec2f(m.pattern.w, m.tex2.z);
  let lit = m.tex2.w;
  let w = planar(p, n);
  let c = vec2f(w.x / bay, w.y / floorH);
  let cell = floor(c);
  let d = (fract(c) - 0.5) * vec2f(bay, floorH);
  let ix = i32(cell.x);
  let iy = i32(cell.y);
  let s = hash32(bitcast<u32>(i32(m.colorB.w)) * 0x2545f491u + 1u);
  var g = vec3f(0.0);
  if (cellHashU(ix, iy, s) < lit) {
    let bright = 0.3 + 0.7 * cellHashU(ix, iy, s ^ 0x68e31da4u);
    let cool = cellHashU(ix, iy, s ^ 0xb5297a4du);
    g = kelvinColor(select(2700.0 + 1300.0 * cellHashU(ix, iy, s ^ 0x1b873593u), 6500.0, cool < 0.125)) * bright;
  }
  let area = min(1.0, win.x * win.y / (bay * floorH));
  // the window's edges, a pixel wide
  let e = clamp((win * 0.5 - abs(d)) / max(lod, 1e-4) + 0.5, vec2f(0.0), vec2f(1.0));
  let sharp = e.x * e.y;
  let lo = select(0.4, 1.2, frame.shadowFar.z > 0.5);
  let hi = select(1.5, 4.0, frame.shadowFar.z > 0.5);
  let fWin = smoothstep(lo, hi, lod / min(win.x, win.y) * frame.sh[1].w);
  let fCell = smoothstep(lo, hi, lod / min(bay, floorH) * frame.sh[1].w);
  o.cover = mix(mix(sharp, area, fWin), area, fCell);
  var avg = vec3f(0.0);
  if (fCell > 0.0) {
    // the face's average colour: seven in eight warm (the middle of 2,700 to 4,000 K), one in eight cool, 0.65 bright
    avg = (kelvinColor(3350.0) * 0.875 + kelvinColor(6500.0) * 0.125) * (lit * 0.65);
  }
  o.glow = mix(mix(sharp * g, area * g, fWin), area * avg, fCell);
  return o;
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
  // with frame blending, 16 shifted samples a pixel average fine lines the way
  // the film renderer's many samples do: detail stays until it is several
  // times smaller than a pixel (a 7 mm mortar line a few metres away: a faint
  // line, as in the film renderer, not nothing)
  let lo = select(0.4, 1.2, frame.shadowFar.z > 0.5);
  let hi = select(1.5, 4.0, frame.shadowFar.z > 0.5);
  let fade = smoothstep(lo, hi, lod / feature * frame.sh[1].w);
  // bricks and tiles: their thin mortar and grout fade early, but each brick's
  // or tile's own colour is as big as the brick or tile, and stays until that
  // is smaller than a pixel (whole bricks faded with their 7 mm mortar: gone
  // two metres away)
  var cellFade = fade;
  if (kind == 3) { cellFade = smoothstep(lo, hi, lod / (0.075 / sc) * frame.sh[1].w); }
  if (kind == 2) { cellFade = smoothstep(lo, hi, lod / (0.5 / sc) * frame.sh[1].w); }
  if (kind == 8) { cellFade = smoothstep(lo, hi, lod / (0.14 / sc) * frame.sh[1].w); }
  return vec3f(mix(pv.x, avg, fade), mix(pv.y, 0.5, fade), mix(pv.z, 1.0, cellFade * 0.5));
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
    // kind 11 (windows) is windowsAt, above: it also gives a glow
    default: { return vec3f(0.0, 0.5, 1.0); }
  }
}
)";

}  // namespace loop::gpu
