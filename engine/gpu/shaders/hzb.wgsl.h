#pragma once
// Part of the live renderer's shaders (engine/gpu/shaders.h).

namespace loop::gpu {

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

}  // namespace loop::gpu
