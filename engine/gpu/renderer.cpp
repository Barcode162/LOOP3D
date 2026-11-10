#include "engine/gpu/renderer_internal.h"
#include "engine/gpu/renderer.h"

#include "engine/core/settings.h"

#include <emscripten/emscripten.h>
#include <emscripten/html5.h>
#include <webgpu/webgpu_cpp.h>

#include <algorithm>
#include <atomic>
#include <thread>
#include <cstddef>
#include <cstring>
#include <memory>
#include <string>
#include <unordered_map>
#include <vector>

#include "engine/geometry/cook.h"
#include "engine/gpu/shaders.h"
#include "engine/render/frame_cost.h"
#include "engine/render/change_tracker.h"
#include "engine/render/frame_graph.h"
#include "engine/render/light_cache.h"
#include "engine/render/light_grid.h"
#include "engine/render/resolution.h"
#include "engine/render/pathtracer.h"
#include "engine/geometry/bvh.h"
#include "engine/render/sky.h"


// Reads a texture back with the browser's own WebGPU calls (more dependable
// than going through the C++ layer) and hands the pixels to the page.
EM_JS(void, jsCapture, (WGPUDevice devPtr, WGPUTexture texPtr, int w, int h, int bgra), {
  const dev = WebGPU.getJsObject(devPtr);
  const tex = WebGPU.getJsObject(texPtr);
  const row = Math.ceil(w * 4 / 256) * 256;
  const buf = dev.createBuffer({size: row * h, usage: GPUBufferUsage.COPY_DST | GPUBufferUsage.MAP_READ});
  const enc = dev.createCommandEncoder();
  enc.copyTextureToBuffer({texture: tex}, {buffer: buf, bytesPerRow: row}, [w, h]);
  dev.queue.submit([enc.finish()]);
  buf.mapAsync(GPUMapMode.READ).then(() => {
    const src = new Uint8Array(buf.getMappedRange());
    const px = new Uint8ClampedArray(w * h * 4);
    for (let y = 0; y < h; y++) {
      for (let x = 0; x < w; x++) {
        const i = y * row + x * 4, o = (y * w + x) * 4;
        px[o] = src[i + (bgra ? 2 : 0)];
        px[o + 1] = src[i + 1];
        px[o + 2] = src[i + (bgra ? 0 : 2)];
        px[o + 3] = 255;
      }
    }
    buf.unmap();
    if (window.loopCapturedPixels) window.loopCapturedPixels(w, h, px);
  });
});

namespace loop::gpu {

int g_skipPasses = 0;

namespace impl {
void capturePixels(WGPUDevice dev, WGPUTexture tex, int w, int h, int bgra) { jsCapture(dev, tex, w, h, bgra); }

struct Pending {
  std::string canvas;
  wgpu::Instance instance;
  wgpu::Surface surface;
  wgpu::Adapter adapter;
  std::function<void(std::unique_ptr<Renderer>)> ready;
  std::shared_ptr<GpuState> state;
};

}  // namespace impl
using namespace impl;


void Renderer::create(const char* canvas, std::function<void(std::unique_ptr<Renderer>)> ready) {
  g_gpuErrors = 0;
  auto* p = new Pending{canvas, wgpu::CreateInstance(nullptr), {}, {}, std::move(ready), nullptr};
  if (!p->instance) {
    auto r = std::move(p->ready);
    delete p;
    r(nullptr);
    return;
  }
  wgpu::EmscriptenSurfaceSourceCanvasHTMLSelector cs;
  cs.selector = p->canvas.c_str();
  wgpu::SurfaceDescriptor sd;
  sd.nextInChain = &cs;
  p->surface = p->instance.CreateSurface(&sd);
  wgpu::RequestAdapterOptions o;
  o.powerPreference = wgpu::PowerPreference::HighPerformance;
  p->instance.RequestAdapter(&o, wgpu::CallbackMode::AllowSpontaneous, [p](wgpu::RequestAdapterStatus s, wgpu::Adapter a, wgpu::StringView) {
    if (s != wgpu::RequestAdapterStatus::Success) {
      auto r = std::move(p->ready);
      delete p;
      r(nullptr);
      return;
    }
    p->adapter = a;
    wgpu::DeviceDescriptor dd;
    dd.label = "loop3d";
    // ask for as much buffer room as this GPU has (the defaults, 128 and
    // 256 MB, are less than a big screen's Ultra averages or a big world's rays)
    wgpu::Limits have, want;
    if (a.GetLimits(&have)) {
      want.maxStorageBufferBindingSize = std::min<uint64_t>(have.maxStorageBufferBindingSize, 1ull << 30);
      want.maxBufferSize = std::min<uint64_t>(have.maxBufferSize, 1ull << 30);
      want.maxTextureArrayLayers = have.maxTextureArrayLayers;  // room for every model picture
      dd.requiredLimits = &want;
    }
    // the chip's own timers, when it has them: each step of a frame timed
    static const wgpu::FeatureName kTimers = wgpu::FeatureName::TimestampQuery;
    if (a.HasFeature(kTimers)) dd.requiredFeatureCount = 1, dd.requiredFeatures = &kTimers;
    dd.SetUncapturedErrorCallback([](const wgpu::Device&, wgpu::ErrorType, wgpu::StringView m) { reportGpuError(str(m)); });
    auto state = std::make_shared<GpuState>();
    p->state = state;
    dd.SetDeviceLostCallback(wgpu::CallbackMode::AllowSpontaneous, [state](const wgpu::Device&, wgpu::DeviceLostReason why, wgpu::StringView m) {
      state->lost = true;
      if (why != wgpu::DeviceLostReason::Destroyed && why != wgpu::DeviceLostReason::CallbackCancelled)
        emscripten_log(EM_LOG_WARN, "WebGPU device lost: %s", str(m).c_str());
    });
    a.RequestDevice(&dd, wgpu::CallbackMode::AllowSpontaneous, [p](wgpu::RequestDeviceStatus s2, wgpu::Device d, wgpu::StringView msg) {
      auto r = std::move(p->ready);
      std::unique_ptr<Renderer> out;
      if (s2 == wgpu::RequestDeviceStatus::Success) out = std::make_unique<GpuRenderer>(p->canvas, p->instance, p->surface, p->adapter, d, p->state);
      else emscripten_log(EM_LOG_ERROR, "WebGPU device: %s", str(msg).c_str());
      delete p;
      r(std::move(out));
    });
  });
}

}  // namespace loop::gpu
