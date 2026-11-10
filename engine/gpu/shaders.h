#pragma once
// The live renderer's shaders (WGSL). They follow the cinema renderer's
// rules: the same materials and patterns, light in real units, the same
// sky, the same film steps, so the live picture aims at the same look.

// Each shader is in its own file in engine/gpu/shaders/; this page brings them in, in order.

#include "engine/gpu/shaders/common.wgsl.h"
#include "engine/gpu/shaders/scene.wgsl.h"
#include "engine/gpu/shaders/cluster_draw.wgsl.h"
#include "engine/gpu/shaders/cluster_cull.wgsl.h"
#include "engine/gpu/shaders/post.wgsl.h"
#include "engine/gpu/shaders/ray.wgsl.h"
#include "engine/gpu/shaders/ultra.wgsl.h"
#include "engine/gpu/shaders/denoise.wgsl.h"
#include "engine/gpu/shaders/taa.wgsl.h"
#include "engine/gpu/shaders/hzb.wgsl.h"
#include "engine/gpu/shaders/exposure.wgsl.h"
