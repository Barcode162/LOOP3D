#pragma once
// Part of the live renderer's shaders (engine/gpu/shaders.h).

namespace loop::gpu {

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
                    // flags (1 glossy, 2 glass, 4 depth of field, 8 sun shadows, 16 lamp shadows, 32 reuse) | paths per pixel << 8
  prevPos : vec4f,  // last frame's camera (to find where a point was)
  prevFwd : vec4f,
  prevRight : vec4f,
  prevUp : vec4f,
  reuse : vec4u,    // this frame's half of the reservoirs (0 or 1), last frame's usable (1), blocks across, blocks down
};
// ReSTIR GI (Ouyang et al. 2021): each pixel keeps one path's bounce, the
// point its first bounce reached (x2) and the light leaving it towards the
// pixel's own point (x1), chosen among all the paths it and its neighbours
// tried, each weighed by its light. Reused from last frame (where the point
// was) and from three neighbours, the soft bounced light settles in a few
// frames instead of hundreds, and follows a moving camera.
struct Res {
  pos : vec4f,  // x2, and how many paths this one stands for (M)
  nrm : vec4f,  // x2's surface direction (zero: the sky), and the weight W
  rad : vec4f,  // the light leaving x2 towards x1; w: 1 when this pixel has one
  at : vec4f,   // x1 (the pixel's own point), and its direction packed
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
@group(1) @binding(5) var<storage, read_write> res : array<Res>;

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

fn octEnc(n : vec3f) -> vec2f {
  let p = n.xy / (abs(n.x) + abs(n.y) + abs(n.z));
  if (n.z >= 0.0) { return p; }
  return (vec2f(1.0) - abs(p.yx)) * select(vec2f(-1.0), vec2f(1.0), p >= vec2f(0.0));
}
fn octDec(e : vec2f) -> vec3f {
  var n = vec3f(e, 1.0 - abs(e.x) - abs(e.y));
  if (n.z < 0.0) { n = vec3f((vec2f(1.0) - abs(n.yx)) * select(vec2f(-1.0), vec2f(1.0), n.xy >= vec2f(0.0)), n.z); }
  return normalize(n);
}
fn lumOf(c : vec3f) -> f32 { return dot(c, vec3f(0.2126, 0.7152, 0.0722)); }
// the reservoir being filled: the path kept, and the weights of all it was chosen from
var<private> rPos : vec3f;
var<private> rNrm : vec3f;
var<private> rRad : vec3f;
var<private> rSum : f32;
var<private> rM : f32;
fn rTake(pos : vec3f, nrm : vec3f, rad : vec3f, w : f32, m : f32) {
  rSum += w;
  rM += m;
  if (w > 0.0 && rand() * rSum < w) { rPos = pos; rNrm = nrm; rRad = rad; }
}
// how much bigger a reused path's x2 looks from here than from where it was
// found (the reconnection's Jacobian); 0 when it can't be reused
fn reuseJ(x1 : vec3f, n1 : vec3f, foundAt : vec3f, q : Res) -> f32 {
  let x2 = q.pos.xyz;
  if (dot(n1, x2 - x1) <= 0.0) { return 0.0; }  // behind this surface
  if (dot(q.nrm.xyz, q.nrm.xyz) < 0.5) { return 1.0; }  // the sky: the same from anywhere near
  let dNew = x1 - x2;
  let dOld = foundAt - x2;
  let cNew = abs(dot(q.nrm.xyz, normalize(dNew)));
  let cOld = abs(dot(q.nrm.xyz, normalize(dOld)));
  let j = (cNew / max(cOld, 1e-4)) * (dot(dOld, dOld) / max(dot(dNew, dNew), 1e-8));
  return select(0.0, j, j > 0.05 && j < 20.0);
}
// a reused reservoir into the one being filled
fn rReuse(x1 : vec3f, n1 : vec3f, q : Res, mCap : f32) {
  if (q.rad.w < 0.5 || q.pos.w <= 0.0) { return; }
  let m = min(q.pos.w, mCap);
  let j = reuseJ(x1, n1, q.at.xyz, q);
  rTake(q.pos.xyz, q.nrm.xyz, q.rad.rgb, lumOf(q.rad.rgb) * q.nrm.w * m * j, m);
}
// is a stored point the same surface as this pixel's (near it, facing the same way)?
fn samePlace(x1 : vec3f, n1 : vec3f, depth : f32, q : Res) -> bool {
  if (q.rad.w < 0.5) { return false; }
  let n = octDec(unpack2x16snorm(bitcast<u32>(q.at.w)));
  return dot(n, n1) > 0.9 && abs(dot(q.at.xyz - x1, n1)) < 0.02 * depth + 0.01 && length(q.at.xyz - x1) < 0.1 * depth + 0.05;
}
// where a point was on last frame's screen, in blocks (-1: not on it)
fn wasAt(p : vec3f, W : u32, H : u32, B : u32) -> vec2i {
  let v = p - U.prevPos.xyz;
  let z = dot(v, U.prevFwd.xyz);
  if (z <= 1e-4) { return vec2i(-1); }
  let sx = dot(v, U.prevRight.xyz) / z / U.prevRight.w;
  let sy = dot(v, U.prevUp.xyz) / z / U.prevUp.w;
  let px = (sx * 0.5 + 0.5) * f32(W) / f32(B);
  let py = (0.5 - sy * 0.5) * f32(H) / f32(B);
  if (px < 0.0 || py < 0.0 || px >= f32(U.reuse.z) || py >= f32(U.reuse.w)) { return vec2i(-1); }
  return vec2i(i32(px), i32(py));
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
  // ReSTIR GI: on the first path, the pixel's point and its diffuse bounce
  let reusing = sp == 0u && (flags & 32u) != 0u;
  var giAt = false;        // the pixel's point is a surface that takes reused light
  var giPath = false;      // this path's first bounce was diffuse: a new candidate
  var x1 = vec3f(0.0);
  var n1 = vec3f(0.0, 1.0, 0.0);
  var ng1 = n1;
  var diff1 = vec3f(0.0);  // its diffuse colour over pi
  var depth1 = 0.0;
  var pDiff = 1.0;
  var Lpre = vec3f(0.0);
  var x2 = vec3f(0.0);
  var n2 = vec3f(0.0);
  var got2 = false;
  for (var bounce = 0u; bounce < U.info.w; bounce++) {
    let h = traceRay(o, d, 1e-4, 1e7, false);
    if (giPath && !got2) {
      // where the first bounce went (the sky: a point far that way)
      got2 = true;
      if (h.tri == 0xffffffffu) { x2 = o + d * 1e5; n2 = vec3f(0.0); }
    }
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
    if (giPath && bounce == 1u) { x2 = p; n2 = ng; }
    var base = m.base.rgb;
    var rough = m.base.w;
    var winGlow = vec3f(1.0);  // a windows face glows only in its lit windows
    if (m.pattern.x > 0.5) {
      // how wide this pixel is out there (after a bounce: blurry anyway)
      let lod = select(1.0, h.t * 2.0 * U.camUp.w / f32(H) * f32(B), bounce == 0u);
      if (i32(m.pattern.x) == 11) {
        // windows (its slots are not scale or roughness variation: see windowsAt)
        let wv = windowsAt(m, p, n, lod);
        base = mix(m.base.rgb, m.colorB.rgb * 0.02 / max(dot(m.colorB.rgb, vec3f(0.2126, 0.7152, 0.0722)), 0.001), wv.cover);
        rough = mix(rough, 0.05, wv.cover);
        winGlow = wv.glow;
      } else {
        let pv = patternLod(m, p, n, lod);
        base = mix(m.base.rgb, m.colorB.rgb, clamp(pv.x, 0.0, 1.0)) * pv.z;
        rough = clamp(rough + (pv.x - 0.5) * m.colorB.w, 0.02, 1.0);
      }
    }
    if (front) { L += beta * m.emission.rgb * winGlow; }
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
    if (reusing && bounce == 0u && diffuseW > 0.0) {
      // the pixel's diffuse light comes from the reservoirs, not this path
      giAt = true;
      x1 = p; n1 = n; ng1 = ng; depth1 = h.t;
      diff1 = beta * base * diffuseW / PI;
      pDiff = 1.0 - pSpec;
    }
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
      if (giAt && bounce == 0u) {
        // a new candidate: the light along this bounce, counted from 1 again
        giPath = true;
        Lpre = L;
        L = vec3f(0.0);
        beta = vec3f(1.0);
      }
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
  let cap = (f32(U.extra.z >> 16u) / 100.0) / max(preExposure(), 1e-12);
  if (reusing) {
    let N = U.reuse.z * U.reuse.w;
    let here = gid.y * U.reuse.z + gid.x;
    let now = U.reuse.x * N;
    let was = (1u - U.reuse.x) * N;
    var keep = Res(vec4f(0.0), vec4f(0.0), vec4f(0.0), vec4f(0.0));
    if (giAt) {
      var Lo = vec3f(0.0);
      if (giPath) {
        Lo = L;
        if (lumOf(Lo) > cap * 4.0) { Lo *= cap * 4.0 / lumOf(Lo); }
        L = Lpre;
        if (!got2) { x2 = x1 + d * 1e5; n2 = vec3f(0.0); }
      }
      // this frame's candidate (none when the glossy lobe was picked: it still counts)
      rPos = vec3f(0.0); rNrm = vec3f(0.0); rRad = vec3f(0.0); rSum = 0.0; rM = 0.0;
      let wi = normalize(x2 - x1);
      let pdf = pDiff * max(dot(n1, wi), 1e-4) / PI;
      rTake(x2, n2, Lo, select(0.0, lumOf(Lo) / pdf, giPath), 1.0);
      // last frame's, where this point was
      let at = wasAt(x1, W, H, B);
      if (U.reuse.y == 1u && at.x >= 0) {
        let q = res[was + u32(at.y) * U.reuse.z + u32(at.x)];
        if (samePlace(x1, n1, depth1, q)) { rReuse(x1, n1, q, 20.0); }
      }
      let pt = select(lumOf(rRad), 0.0, rM <= 0.0);
      let Wt = select(0.0, rSum / (rM * pt), pt > 0.0 && rM > 0.0);
      keep = Res(vec4f(rPos, rM), vec4f(rNrm, Wt), vec4f(rRad, 1.0), vec4f(x1, bitcast<f32>(pack2x16snorm(octEnc(n1)))));
      // and three neighbours' (last frame's, round where this point was)
      if (U.reuse.y == 1u) {
        let c = select(vec2f(f32(gid.x), f32(gid.y)), vec2f(at), at.x >= 0);
        for (var k = 0u; k < 3u; k++) {
          let a = rand() * 2.0 * PI;
          let r = 2.0 + rand() * 14.0;
          let q2 = vec2i(c + vec2f(cos(a), sin(a)) * r);
          if (q2.x < 0 || q2.y < 0 || q2.x >= i32(U.reuse.z) || q2.y >= i32(U.reuse.w)) { continue; }
          let q = res[was + u32(q2.y) * U.reuse.z + u32(q2.x)];
          // a neighbour on the same surface (facing the same way, in the same plane)
          if (q.rad.w > 0.5 && dot(octDec(unpack2x16snorm(bitcast<u32>(q.at.w))), n1) > 0.9 && abs(dot(q.at.xyz - x1, n1)) < 0.05 * depth1 + 0.02) {
            rReuse(x1, n1, q, 20.0);
          }
        }
      }
      // the light it gives, if x2 is really seen from here
      let ps = lumOf(rRad);
      if (ps > 0.0 && rM > 0.0) {
        let Ws = rSum / (rM * ps);
        let sky = dot(rNrm, rNrm) < 0.5;
        let toX2 = rPos - x1;
        let dist = length(toX2);
        let wi2 = toX2 / max(dist, 1e-6);
        let cosT = dot(n1, wi2);
        if (cosT > 0.0) {
          let far = select(dist - 2e-3, 1e7, sky);
          if (traceRay(x1 + ng1 * 2e-3, wi2, 1e-4, far, true).tri == 0xffffffffu) {
            L += diff1 * rRad * cosT * Ws;
          }
        }
      }
    }
    res[now + here] = keep;
  }
  let lum = dot(L, vec3f(0.2126, 0.7152, 0.0722));
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

}  // namespace loop::gpu
