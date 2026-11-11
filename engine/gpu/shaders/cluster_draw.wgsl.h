#pragma once
// Part of the live renderer's shaders (engine/gpu/shaders.h).

namespace loop::gpu {

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

// A main lamp's shadow map, one of its six views (the view in its own small
// uniform: one draw per view, into its tile of the lamps' depth picture).
@group(1) @binding(9) var<uniform> lampFace : mat4x4f;
@vertex fn vsVisLampShadow(@builtin(vertex_index) vi : u32, @builtin(instance_index) k : u32) -> ShadowOut {
  var o : ShadowOut;
  let d = drawn[k];
  let cl = poolCluster(d.y);
  if (vi / 3u >= cl.count) {
    o.clip = vec4f(2.0, 2.0, 2.0, 1.0);
    return o;
  }
  let pv = poolVertex(poolCorner(cl.first + vi));
  let inst = instances[d.x];
  o.clip = lampFace * inst.model * vec4f(pv.pos, 1.0);
  o.uv = pv.uv;
  o.mat = inst.material.x;
  return o;
}

// ---- lighting each pixel once: its triangle, rebuilt exactly
@group(3) @binding(2) var visTex : texture_2d<u32>;
@group(3) @binding(3) var sceneDepth : texture_depth_2d;  // read only in this pass
@group(3) @binding(4) var pastPicture : texture_2d<f32>;  // last frame's, after blending (screen reflections)
@group(3) @binding(5) var shadowPastTex : texture_2d<f32>;  // each pixel's kept lamp shadow (x), eye distance (y), frames kept (z), last frame
@group(3) @binding(6) var shadowOutTex : texture_storage_2d<rgba16float, write>;  // and this frame's
// Screen reflections: the reflected ray followed across the depth picture (24
// steps, longer and longer); where it passes just behind what the eye sees,
// that's what it hits: its colour is read from last frame's picture, where
// that point was then. How sure: less near the picture's edges and on rough
// surfaces (0: the light cache's or the sky's reflection stays).
fn reflectionSeen(p : vec3f, n : vec3f, v : vec3f, rough : f32, pix : vec2f) -> vec4f {
  let r = reflect(-v, n);
  let size = vec2i(textureDimensions(sceneDepth));
  let jitter = fract(52.9829189 * fract(dot(pix, vec2f(0.06711056, 0.00583715))) + frame.ao.z * 0.618034);
  // the same reach in more, shorter steps at the higher levels (1.28^24 = 1.16^40)
  let steps = select(24, 40, kShading >= 3u);
  let grow = select(1.28, 1.16, kShading >= 3u);
  var t = 0.04 * (0.5 + jitter);
  var tBefore = 0.0;
  for (var k = 0; k < steps; k++) {
    let q = p + r * t;
    let c = frame.viewProj * vec4f(q, 1.0);
    if (c.w <= 0.0) { break; }
    let ndc = c.xyz / c.w;
    if (any(abs(ndc.xy) > vec2f(1.0))) { break; }
    let at = vec2i((ndc.xy * vec2f(0.5, -0.5) + 0.5) * frame.screen.xy);
    let d = textureLoad(sceneDepth, clamp(at, vec2i(0), size - 1), 0);
    if (d < 1.0) {
      let w = frame.invViewProj * vec4f(ndc.xy, d, 1.0);
      var hit = w.xyz / w.w;
      let gap = length(q - frame.camPos.xyz) - length(hit - frame.camPos.xyz);
      if (gap > 0.0 && gap < 0.1 + t * 0.05) {
        if (kShading >= 2u) {
          // the ray passed behind the surface somewhere in this step: halve the
          // step four times to find where (no stair-steps in the reflection)
          var lo = tBefore;
          var hi = t;
          for (var b = 0; b < 4; b++) {
            let mid = 0.5 * (lo + hi);
            let qm = p + r * mid;
            let cm = frame.viewProj * vec4f(qm, 1.0);
            let nm = cm.xyz / max(cm.w, 1e-6);
            let dm = textureLoad(sceneDepth, clamp(vec2i((nm.xy * vec2f(0.5, -0.5) + 0.5) * frame.screen.xy), vec2i(0), size - 1), 0);
            let wm = frame.invViewProj * vec4f(nm.xy, dm, 1.0);
            let hm = wm.xyz / wm.w;
            let gm = length(qm - frame.camPos.xyz) - length(hm - frame.camPos.xyz);
            if (dm < 1.0 && gm > 0.0 && gm < 0.1 + mid * 0.05) { hi = mid; hit = hm; } else { lo = mid; }
          }
        }
        // where that point was in last frame's picture
        let pc = frame.prevViewProj * vec4f(hit, 1.0);
        if (pc.w <= 0.0) { break; }
        let uv = (pc.xy / pc.w) * vec2f(0.5, -0.5) + 0.5;
        if (any(uv < vec2f(0.0)) || any(uv > vec2f(1.0))) { break; }
        let col = textureSampleLevel(pastPicture, skySampler, uv, 0.0).rgb / max(preExposure(), 1e-9);
        let edge = smoothstep(0.0, 0.08, min(min(uv.x, uv.y), min(1.0 - uv.x, 1.0 - uv.y)));
        return vec4f(col, edge);
      }
    }
    tBefore = t;
    t *= grow;
  }
  return vec4f(0.0);
}
// Ambient occlusion: 8 points spread over the half-ball above the surface
// (within the reach, more of them close), each looked up in the depth
// picture: one hidden behind something nearer the eye (but not far behind:
// that's a separate thing) blocks its share of the soft light. Turned per
// pixel and per frame, so frame blending smooths the grain.
fn ambientSeen(p : vec3f, n : vec3f, pix : vec2f) -> f32 {
  let reach = frame.ao.y;
  let size = vec2i(textureDimensions(sceneDepth));
  let turn = fract(52.9829189 * fract(dot(pix, vec2f(0.06711056, 0.00583715))) + frame.ao.z * 0.618034) * 6.2831853;
  let a = select(vec3f(1.0, 0.0, 0.0), vec3f(0.0, 1.0, 0.0), abs(n.x) > 0.9);
  let t = normalize(cross(n, a));
  let b = cross(n, t);
  var hidden = 0.0;
  let points = select(select(8, 12, kShading >= 2u), 16, kShading >= 3u);
  for (var k = 0; k < points; k++) {
    let u = (f32(k) + 0.5) / f32(points);
    let phi = f32(k) * 2.39996323 + turn;
    let r = sqrt(u);                          // cosine-weighted over the half-ball
    let dir = t * (r * cos(phi)) + b * (r * sin(phi)) + n * sqrt(1.0 - u);
    let q = p + dir * (reach * (0.15 + 0.85 * u * u));
    let c = frame.viewProj * vec4f(q, 1.0);
    if (c.w <= 0.0) { continue; }
    let ndc = c.xyz / c.w;
    let at = vec2i((ndc.xy * vec2f(0.5, -0.5) + 0.5) * frame.screen.xy);
    if (any(at < vec2i(0)) || any(at >= size)) { continue; }
    let d = textureLoad(sceneDepth, at, 0);
    if (d >= 1.0) { continue; }
    let w = frame.invViewProj * vec4f(ndc.xy, d, 1.0);
    let seen = length(w.xyz / w.w - frame.camPos.xyz);
    let gap = length(q - frame.camPos.xyz) - seen;
    // only what's near counts: something far in front is a separate thing (no dark rims round it)
    if (gap > 0.02) { hidden += 1.0 - smoothstep(reach * 0.3, reach, gap); }
  }
  return pow(1.0 - hidden / f32(points), frame.ao.x);
}
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
  // what each pixel shows was settled by the visibility picture (its holes cut
  // there): working the point out again here can land a hair outside a leaf seen
  // edge-on, and cutting it then left a black hole (your phone, the world's trees)
  resolving = true;
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
  // the slopes per pixel of the full picture, not of the smaller drawing: frame
  // blending fills in the detail between, so patterns and pictures keep the
  // detail the screen shows (they faded out early: your bricks)
  let perShown = frame.shadowFar.w;
  slopeWorldX = (px - v.world) * perShown;
  slopeWorldY = (py - v.world) * perShown;
  slopeUvX = (A.uv * bx.x + B.uv * bx.y + C.uv * bx.z - v.uv) * perShown;
  slopeUvY = (A.uv * by.x + B.uv * by.y + C.uv * by.z - v.uv) * perShown;
  let n0 = normalize(A.nrm * b0.x + B.nrm * b0.y + C.nrm * b0.z);
  slopeNrmX = (normalize(A.nrm * bx.x + B.nrm * bx.y + C.nrm * bx.z) - n0) * perShown;
  slopeNrmY = (normalize(A.nrm * by.x + B.nrm * by.y + C.nrm * by.z) - n0) * perShown;
  // which side the camera sees, as the GPU tells a drawn triangle (counter-
  // clockwise on screen is its front): its corners' turn as seen from the eye,
  // which stays right for big triangles reaching behind the camera
  let front = dot(ng, frame.camPos.xyz - wa) > 0.0;
  if (frame.ao.w > 0.5) {
    let m0 = materials[v.mat];
    let rough0 = select(m0.base.w, min(m0.base.w, m0.coat.x), m0.props.z > 0.0);  // a coat reflects too
    if (rough0 < 0.5) {
      let nf = normalize(ng) * select(-1.0, 1.0, front);
      let found = reflectionSeen(v.world + nf * 0.01, nf, normalize(frame.camPos.xyz - v.world), rough0, r.clip.xy);
      ssrColor = found.rgb;
      ssrSeen = found.a;
    }
  }
  // the kept lamp shadow (High): where this point was last frame, if the
  // same surface was there (its distance from last frame's eye agrees)
  let eyeDist = length(v.world - frame.camPos.xyz);
  if (frame.lampMaps.z > 0.5) {
    let pc = frame.prevViewProj * vec4f(v.world, 1.0);
    if (pc.w > 0.0) {
      let uv = (pc.xy / pc.w) * vec2f(0.5, -0.5) + 0.5;
      if (all(uv >= vec2f(0.0)) && all(uv < vec2f(1.0))) {
        let was = textureLoad(shadowPastTex, vec2i(uv * frame.prevScreen.xy), 0);
        let then = length(v.world - frame.prevCam.xyz);
        if (abs(was.y - then) < 0.03 * then + 0.02) { shadowPast = was.x; shadowCount = was.z; }
      }
    }
  }
  if (frame.ao.x > 0.0) { aoTerm = ambientSeen(v.world + normalize(ng) * select(-0.01, 0.01, front), normalize(ng) * select(-1.0, 1.0, front), r.clip.xy); }
  let s = shade(v, front);
  textureStore(shadowOutTex, vec2i(r.clip.xy), vec4f(shadowNow, eyeDist, shadowCount, 0.0));
  if (frame.fixedExposure.z > 4.5 && frame.fixedExposure.z < 5.5) { return vec4f(vec3f(aoTerm), 1.0); }  // ?view=5: the ambient occlusion alone
  if (frame.fixedExposure.z > 5.5) { return select(vec4f(1.0, 0.0, 0.0, 1.0), vec4f(vec3f(shadowNow), 1.0), shadowPast >= 0.0); }  // ?view=6: the kept lamp shadow (red: started afresh)
  return vec4f(exposed(s.color), 1.0);
}
)";

}  // namespace loop::gpu
