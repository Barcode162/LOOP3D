#include "engine/gpu/renderer.h"

#include <emscripten/emscripten.h>
#include <emscripten/html5.h>
#include <webgpu/webgpu_cpp.h>

#include <algorithm>
#include <cstring>
#include <string>
#include <unordered_map>
#include <vector>

#include "engine/gpu/shaders.h"
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
int g_debugView = 0;

namespace {

std::string str(wgpu::StringView s) { return s.data ? std::string(std::string_view(s)) : std::string(); }

uint16_t toHalf(float f) {
  uint32_t x;
  std::memcpy(&x, &f, 4);
  uint32_t sign = (x >> 16) & 0x8000;
  int32_t e = static_cast<int32_t>((x >> 23) & 0xff) - 127 + 15;
  uint32_t m = x & 0x7fffff;
  if (e <= 0) return static_cast<uint16_t>(sign);
  if (e >= 31) return static_cast<uint16_t>(sign | 0x7bff);  // clamp to the largest half
  return static_cast<uint16_t>(sign | (e << 10) | (m >> 13));
}

// ---- GPU-side layouts (must match shaders.h)
struct FrameU {
  Mat4 viewProj, invViewProj, shadowViewProj;
  Vec4 camPos, sunDir, sunColor;
  Vec4 sh[9];
  Vec4 fog, fogColor, params, screen, fixedExposure;
};
struct LightU {
  Vec4 pos, color, dir, cone;
};
struct MaterialU {
  Vec4 base, props, emission, pattern, colorB, extra;
};
struct InstanceU {
  Mat4 model;
  Vec4 n0, n1, n2;
  uint32_t material[4];
};
struct PostU {
  Vec4 exposure, look0, wb, lift, gamma, gain, shadowTint, highlightTint, fx, fx2, screen;
};
struct UltraU {
  Vec4 camPos, camFwd, camRight, camUp;
  uint32_t info[4];
  uint32_t extra[4];
};
struct TriU {
  Vec4 v0, e1, e2, n0, n1, n2;
};
struct RayInstU {
  Mat4 inv;
  Vec4 n0, n1, n2;
  uint32_t info[4];
};
struct NodeU {
  Vec4 lo, hi;
};
struct ExposureU {
  Vec4 size, limits;
};
static_assert(sizeof(FrameU) == 464, "frame layout");
static_assert(sizeof(InstanceU) == 128, "instance layout");

constexpr int kMaxLights = 256;
constexpr int kBloomLevels = 6;
int g_samples = 4;  // multisampling of the scene pass (1 = off)

struct GpuMesh {
  wgpu::Buffer vb, ib;
  uint32_t indexCount = 0;
};
struct Batch {
  int mesh;
  uint32_t first, count;
  bool glass;
};

class GpuRenderer final : public Renderer {
 public:
  GpuRenderer(std::string canvas, wgpu::Instance inst, wgpu::Surface surface, wgpu::Adapter adapter, wgpu::Device device)
      : canvas_(std::move(canvas)), instance_(inst), surface_(surface), adapter_(adapter), device_(device), queue_(device.GetQueue()) {
    wgpu::SurfaceCapabilities caps;
    surface_.GetCapabilities(adapter_, &caps);
    format_ = caps.formatCount > 0 ? caps.formats[0] : wgpu::TextureFormat::BGRA8Unorm;
    if (g_skipPasses & 256) format_ = wgpu::TextureFormat::RGBA8Unorm;
    frameUbo_ = buffer(sizeof(FrameU), wgpu::BufferUsage::Uniform | wgpu::BufferUsage::CopyDst);
    postUbo_ = buffer(sizeof(PostU), wgpu::BufferUsage::Uniform | wgpu::BufferUsage::CopyDst);
    expUbo_ = buffer(sizeof(ExposureU), wgpu::BufferUsage::Uniform | wgpu::BufferUsage::CopyDst);
    lightsBuf_ = buffer(sizeof(LightU) * kMaxLights, wgpu::BufferUsage::Storage | wgpu::BufferUsage::CopyDst);
    exposureBuf_ = buffer(16, wgpu::BufferUsage::Storage | wgpu::BufferUsage::CopyDst);
    float zero[4] = {0, 0, 0, 0};
    queue_.WriteBuffer(exposureBuf_, 0, zero, 16);
    ensureStorage(materialsBuf_, materialsCap_, sizeof(MaterialU), 16);
    ensureStorage(instancesBuf_, instancesCap_, sizeof(InstanceU), 16);
    wgpu::SamplerDescriptor sd;
    sd.magFilter = sd.minFilter = wgpu::FilterMode::Linear;
    sd.mipmapFilter = wgpu::MipmapFilterMode::Linear;
    sd.addressModeU = wgpu::AddressMode::Repeat;
    sd.addressModeV = sd.addressModeW = wgpu::AddressMode::ClampToEdge;
    linear_ = device_.CreateSampler(&sd);
    wgpu::SamplerDescriptor cs;
    cs.magFilter = cs.minFilter = wgpu::FilterMode::Linear;
    cs.compare = wgpu::CompareFunction::LessEqual;
    shadowSampler_ = device_.CreateSampler(&cs);
    g_samples = EM_ASM_INT({ return +(new URLSearchParams(location.search).get('msaa') || 4); });
    buildPipelines();
    makeShadowMap();
    // a plain sky until the scene says otherwise
    Sky sky;
    sky.kind = Sky::Kind::kColor;
    sky.colorNits = 0;
    SkyModel m;
    m.build(sky, {0, 1, 0}, 0);
    uploadSky(m);
  }

  float pixelRatio() const override { return ratio_; }
  size_t drawCalls() const override { return drawCalls_; }
  float currentExposure() const override { return lastExposure_; }
  void capture(std::function<void(int, int, const uint8_t*)> done) override { captureDone_ = std::move(done); }

  void setQuality(Quality q) override {
    quality_ = q;
    ultraFrame_ = 0;
    ultraScale_ = 1;
    renderScale_ = scaleCeil_ = 1, calmSeconds_ = 0, sinceScale_ = 0;
    lightsDirty_ = true;
    maxRatio_ = q == Quality::kPhone ? 1.5f : 2.0f;
    shadowSize_ = q == Quality::kUltra ? 4096 : 2048;
    makeShadowMap();
    width_ = 0;  // re-make targets
  }

  void sync(const Scene& scene, bool geometryChanged, bool skyChanged) override {
    if (geometryChanged || meshes_.size() != scene.meshes.size()) {
      for (size_t i = meshes_.size(); i < scene.meshes.size(); ++i) meshes_.push_back(upload(scene.meshes[i]));
    }
    // materials
    std::vector<MaterialU> mats(std::max<size_t>(1, scene.materials.size()));
    for (size_t i = 0; i < scene.materials.size(); ++i) {
      const Material& m = scene.materials[i];
      MaterialU& u = mats[i];
      u.base = {m.baseColor, m.roughness};
      u.props = {m.metallic, m.transmission, m.clearcoat, m.sheen};
      u.emission = {m.emission, m.opacity};
      u.pattern = {static_cast<float>(static_cast<int>(m.pattern.kind)), m.pattern.scale, m.pattern.bump, m.pattern.contrast};
      if (m.pattern.kind == Pattern::Kind::kImage) u.pattern.x = 0;
      u.colorB = {m.pattern.colorB, m.pattern.roughnessVariation};
      u.extra = {m.subsurface, m.translucency + m.subsurface * 0.4f, m.leafShape ? 1.0f : 0.0f, m.ior};
    }
    ensureStorage(materialsBuf_, materialsCap_, sizeof(MaterialU), mats.size());
    queue_.WriteBuffer(materialsBuf_, 0, mats.data(), mats.size() * sizeof(MaterialU));
    readLights(scene);
    fog_ = scene.fog;
    if (skyChanged || !skyBuilt_) {
      SkyModel m;
      m.build(scene.sky, sunOn_ ? sunDir_ : Vec3(0, 1, 0), sunOn_ ? sunLux_ : 0);
      sunIllum_ = scene.sky.kind == Sky::Kind::kPhysical ? m.sunIlluminance() * sunTint_ : sunTint_ * sunLux_;
      uploadSky(m);
      skyBuilt_ = true;
    }
    syncTransforms(scene);
  }

  void syncLights(const Scene& scene) override {
    readLights(scene);
    fog_ = scene.fog;
  }

  void reset() override {
    meshes_.clear();
    meshRoot_.clear();
    meshBox_.clear();
    rayTrisCpu_.clear();
    rayBlasCpu_.clear();
    rayTris_ = nullptr;
    batches_.clear();
    lastScene_ = nullptr;
    skyBuilt_ = false;
    lights_.clear();
    lightsDirty_ = true;
    lastPlaces_.clear();
    ultraFrame_ = 0;
  }

  void syncTransforms(const Scene& scene) override {
    lastScene_ = &scene;
    buildRayMeshes(scene);
    buildRayInstances(scene);
    // something moved more than a couple of millimetres (not just breathing):
    // let the old picture fade quickly instead of smearing
    float moved = 0;
    if (lastPlaces_.size() != scene.instances.size()) moved = 1;
    else
      for (size_t i = 0; i < scene.instances.size() && moved < 0.002f; ++i) {
        const Mat4& a = scene.instances[i].transform;
        const Mat4& b = lastPlaces_[i];
        for (int c = 0; c < 4; ++c)
          for (int r = 0; r < 3; ++r) moved = std::max(moved, std::fabs(a.m[c][r] - b.m[c][r]));
      }
    if (moved >= 0.002f) {
      lastPlaces_.resize(scene.instances.size());
      for (size_t i = 0; i < scene.instances.size(); ++i) lastPlaces_[i] = scene.instances[i].transform;
      if (ultraFrame_ > 8) ultraFrame_ = 8;
    }
    // instances sorted into batches: same mesh, opaque or glass, drawn at once
    std::vector<uint32_t> order(scene.instances.size());
    for (size_t i = 0; i < order.size(); ++i) order[i] = static_cast<uint32_t>(i);
    auto glassOf = [&](const MeshInstance& in) {
      return in.material >= 0 && in.material < static_cast<int>(scene.materials.size()) && scene.materials[in.material].transmission > 0;
    };
    std::sort(order.begin(), order.end(), [&](uint32_t a, uint32_t b) {
      const MeshInstance &x = scene.instances[a], &y = scene.instances[b];
      bool gx = glassOf(x), gy = glassOf(y);
      if (gx != gy) return gy;
      return x.mesh < y.mesh;
    });
    std::vector<InstanceU> data;
    data.reserve(order.size());
    batches_.clear();
    for (uint32_t idx : order) {
      const MeshInstance& in = scene.instances[idx];
      if (in.mesh < 0 || in.mesh >= static_cast<int>(meshes_.size())) continue;
      InstanceU u;
      u.model = in.transform;
      Mat3 nm = in.transform.linear().inverse().transposed();
      u.n0 = {nm.c[0], 0}, u.n1 = {nm.c[1], 0}, u.n2 = {nm.c[2], 0};
      u.material[0] = static_cast<uint32_t>(std::max(0, in.material));
      u.material[1] = u.material[2] = u.material[3] = 0;
      bool g = glassOf(in);
      if (!batches_.empty() && batches_.back().mesh == in.mesh && batches_.back().glass == g) ++batches_.back().count;
      else batches_.push_back({in.mesh, static_cast<uint32_t>(data.size()), 1, g});
      data.push_back(u);
    }
    if (ensureStorage(instancesBuf_, instancesCap_, sizeof(InstanceU), std::max<size_t>(1, data.size()))) bindGroupsDirty_ = true;
    if (!data.empty()) queue_.WriteBuffer(instancesBuf_, 0, data.data(), data.size() * sizeof(InstanceU));
  }

  bool draw(const Camera& cam, const Look& look, float dt, float time, float fade) override {
    // at most two frames waiting on the GPU: a slow GPU drops frames instead
    // of falling further and further behind
    adaptResolution(dt);
    if (inFlight_ >= 2) {
      ++perfSkipped_;
      // a GPU that never says it's done (a lost callback, a hidden tab) must
      // not freeze the picture for good
      if (++stuckFrames_ > 90) inFlight_ = 0, stuckFrames_ = 0;
      return false;
    }
    stuckFrames_ = 0;
    ++perfDrawn_;
    resize();
    wgpu::SurfaceTexture st;
    if (!(g_skipPasses & 256)) {  // 256: off-screen only (tests in browsers that can't show WebGPU)
      surface_.GetCurrentTexture(&st);
      if (st.status != wgpu::SurfaceGetCurrentTextureStatus::SuccessOptimal && st.status != wgpu::SurfaceGetCurrentTextureStatus::SuccessSuboptimal) {
        width_ = 0;  // the surface needs configuring again
        return false;
      }
    }
    if (bindGroupsDirty_) makeBindGroups();
    uploadLights(cam.position);
    drawCalls_ = 0;

    // ---- frame constants
    FrameU f{};
    float aspect = static_cast<float>(width_) / height_;
    Mat4 view = Mat4::lookAt(cam.position, cam.target, cam.up);
    float fovY = cam.fovY();
    Mat4 proj = Mat4::perspective(fovY, aspect, 0.05f, 4000.0f);
    f.viewProj = proj * view;
    f.invViewProj = f.viewProj.inverse();
    // the sun's shadow: a box round what the camera sees nearby, snapped to texels
    float R = quality_ == Quality::kPhone ? 25.0f : 40.0f;
    Vec3 fwd = normalize(cam.target - cam.position);
    Vec3 center = cam.position + fwd * (R * 0.6f);
    Vec3 up = std::fabs(sunDir_.y) > 0.99f ? Vec3(1, 0, 0) : Vec3(0, 1, 0);
    Mat4 lv = Mat4::lookAt(Vec3(0), -sunDir_, up);
    Vec3 lc = lv.point(center);
    float texel = 2 * R / shadowSize_;
    lc.x = std::floor(lc.x / texel) * texel, lc.y = std::floor(lc.y / texel) * texel;
    Mat4 lp = Mat4::ortho(lc.x - R, lc.x + R, lc.y - R, lc.y + R, -lc.z - R * 4, -lc.z + R * 4);
    f.shadowViewProj = lp * lv;
    f.camPos = {cam.position, time};
    f.sunDir = {sunDir_, sunOn_ ? 1.0f : 0.0f};
    f.sunColor = {sunIllum_, 0};
    for (int i = 0; i < 9; ++i) f.sh[i] = {sh_[i], 0};
    f.fog = {fog_.density, fog_.top, fog_.anisotropy, fog_.density > 0 ? 1.0f : 0.0f};
    f.fogColor = {fog_.albedo, 0};
    f.params = {static_cast<float>(lights_.size()), (g_skipPasses & 512) ? 0.0f : quality_ == Quality::kPhone ? 3.0f : 6.0f, static_cast<float>(shadowSize_), cam.autoExposure ? 1.0f : 0.0f};
    f.screen = {static_cast<float>(width_), static_cast<float>(height_), 1.0f / width_, 1.0f / height_};
    // a first guess for the eyes: the camera's own settings, or a sunny / indoor guess
    float guess = cam.autoExposure ? (sunOn_ && sunLux_ > 1000 ? 1.0f / 6000 : 1.0f / 30) : exposureFromEv100(cam.ev100());
    f.fixedExposure = {guess * std::exp2(0.0f), 0, 0, 0};
    queue_.WriteBuffer(frameUbo_, 0, &f, sizeof f);

    PostU p{};
    float fixedExp = exposureFromEv100(cam.ev100());
    p.exposure = {fixedExp, cam.exposureCompensation, cam.autoExposure ? 1.0f : 0.0f, 0};
    p.look0 = {look.contrast, look.saturation, static_cast<float>(static_cast<int>(look.tonemap)), look.monochrome ? 1.0f : 0.0f};
    Vec3 wb = whiteBalanceGains(look.temperature, look.tint);
    p.wb = {wb, 0};
    p.lift = {look.lift, 0}, p.gamma = {look.gamma, 0}, p.gain = {look.gain, 0};
    p.shadowTint = {look.shadowTint, 0}, p.highlightTint = {look.highlightTint, 0};
    p.fx = {look.bloom, look.vignette, look.grain, look.letterbox};
    p.fx2 = {look.blackLevel, time, look.halation, look.chromaticAberration};
    p.screen = {static_cast<float>(width_), static_cast<float>(height_), fade, static_cast<float>(g_debugView)};
    queue_.WriteBuffer(postUbo_, 0, &p, sizeof p);
    ExposureU e{{static_cast<float>(width_), static_cast<float>(height_), dt, f.fixedExposure.x}, {1e-7f, 5000.0f, 3.0f, 1.2f}};  // eyes adapt from sunlight down to a candle-lit room
    queue_.WriteBuffer(expUbo_, 0, &e, sizeof e);
    lastExposure_ = fixedExp;

    wgpu::CommandEncoder enc = device_.CreateCommandEncoder();
    bool ultra = quality_ == Quality::kUltra && rayTris_ && accumBuf_;
    if (ultra) {
      // when the camera moves a little, the picture blends toward the new
      // view (a short trail, never a burst of noise); a cut starts afresh
      Vec3 key[3] = {cam.position, cam.target, Vec3(cam.focalLength, cam.fStop, cam.focusDistance)};
      if (std::memcmp(key, ultraCamKey_, sizeof key) != 0) {
        float moved = length(key[0] - ultraCamKey_[0]);
        float turned = 1 - dot(normalize(key[1] - key[0]), normalize(ultraCamKey_[1] - ultraCamKey_[0] + Vec3(1e-9f)));
        bool cut = moved > 0.5f || turned > 0.05f || key[2].x != ultraCamKey_[2].x;
        std::memcpy(ultraCamKey_, key, sizeof key);
        ultraFrame_ = cut ? 0 : std::min(ultraFrame_, 3u);
      }
      UltraU u{};
      Vec3 f = normalize(cam.target - cam.position), r = normalize(cross(f, cam.up)), up = cross(r, f);
      float ty = std::tan(cam.fovY() * 0.5f);
      float focus = cam.focusDistance > 0 ? cam.focusDistance : length(cam.target - cam.position);
      u.camPos = {cam.position, (cam.focalLength / 1000.0f) / (2 * std::max(cam.fStop, 0.5f))};
      u.camFwd = {f, focus};
      u.camRight = {r, ty * static_cast<float>(width_) / height_};
      u.camUp = {up, ty};
      u.info[0] = ultraFrame_, u.info[1] = width_, u.info[2] = height_, u.info[3] = 5;
      u.extra[0] = ultraScale_, u.extra[1] = static_cast<uint32_t>(lights_.size());
      queue_.WriteBuffer(ultraUbo_, 0, &u, sizeof u);
      ++ultraFrame_;
    }
    // ---- 1. sun shadow
    if (!ultra && !(g_skipPasses & 32)) {
      wgpu::RenderPassDepthStencilAttachment da;
      da.view = shadowView_;
      da.depthLoadOp = wgpu::LoadOp::Clear;
      da.depthStoreOp = wgpu::StoreOp::Store;
      da.depthClearValue = 1;
      wgpu::RenderPassColorAttachment dc;
      dc.view = shadowDummy_;
      dc.loadOp = wgpu::LoadOp::Clear;
      dc.storeOp = wgpu::StoreOp::Discard;
      wgpu::RenderPassDescriptor rp;
      rp.colorAttachmentCount = 1;
      rp.colorAttachments = &dc;
      rp.depthStencilAttachment = &da;
      wgpu::RenderPassEncoder pass = enc.BeginRenderPass(&rp);
      if (sunOn_ && !(g_skipPasses & 1)) {
        pass.SetPipeline(shadowPipe_);
        pass.SetBindGroup(0, sceneGroup0_);
        drawBatches(pass, false, true);
      }
      pass.End();
    }
    if (ultra) {
      wgpu::ComputePassEncoder cp = enc.BeginComputePass();
      cp.SetPipeline(ultraPipe_);
      cp.SetBindGroup(0, sceneGroup0_);
      cp.SetBindGroup(1, ultraGroup_);
      cp.SetBindGroup(2, rayGroup_);
      uint32_t bw = (width_ + ultraScale_ - 1) / ultraScale_, bh = (height_ + ultraScale_ - 1) / ultraScale_;
      cp.DispatchWorkgroups((bw + 7) / 8, (bh + 7) / 8);
      // clean the noise, keeping edges
      cp.SetPipeline(denoisePipe_);
      for (int k = 0; k < 4; ++k) {
        uint32_t prm[12] = {bw, bh, k == 0 ? 0u : (1u << (k - 1)), k == 3 ? 1u : 0u, width_, height_, ultraScale_, ultraFrame_, 0, 0, 0, 0};
        float fe[2] = {f.fixedExposure.x, cam.autoExposure ? 1.0f : 0.0f};
        std::memcpy(&prm[8], fe, 8);
        queue_.WriteBuffer(denoiseUbo_[k], 0, prm, sizeof prm);
        cp.SetBindGroup(0, denoiseGroup_[k]);
        cp.DispatchWorkgroups((bw + 7) / 8, (bh + 7) / 8);
      }
      cp.End();
    }
    // ---- 2. the scene, in light (HDR), 4x multisampled
    if (!ultra && !(g_skipPasses & 64)) {
      wgpu::RenderPassColorAttachment ca;
      ca.view = g_samples > 1 ? msaaView_ : hdrView_;
      if (g_samples > 1) ca.resolveTarget = hdrView_;
      ca.loadOp = wgpu::LoadOp::Clear;
      ca.storeOp = wgpu::StoreOp::Store;
      ca.clearValue = {0, 0, 0, 1};
      wgpu::RenderPassDepthStencilAttachment da;
      da.view = depthView_;
      da.depthLoadOp = wgpu::LoadOp::Clear;
      da.depthStoreOp = wgpu::StoreOp::Discard;
      da.depthClearValue = 1;
      wgpu::RenderPassDescriptor rp;
      rp.colorAttachmentCount = 1;
      rp.colorAttachments = &ca;
      if (!(g_skipPasses & 128)) rp.depthStencilAttachment = &da;
      wgpu::RenderPassEncoder pass = enc.BeginRenderPass(&rp);
      pass.SetBindGroup(0, sceneGroup0_);
      pass.SetBindGroup(1, sceneGroup1_);
      pass.SetBindGroup(2, rayGroup_);
      if (!(g_skipPasses & 2)) {
        pass.SetPipeline(opaquePipe_);
        drawBatches(pass, false, false);
        pass.SetPipeline(skyPipe_);
        pass.Draw(3);
        pass.SetPipeline(glassPipe_);
        drawBatches(pass, true, false);
      }
      pass.End();
    }
    // ---- 3. how bright it is: the eyes adjust
    if (!(g_skipPasses & 4)) {
      wgpu::ComputePassEncoder cp = enc.BeginComputePass();
      cp.SetPipeline(exposurePipe_);
      cp.SetBindGroup(0, exposureGroup_);
      cp.DispatchWorkgroups(1);
      cp.End();
    }
    // ---- 4. bloom: down the chain, then back up, adding
    if (!(g_skipPasses & 8)) {
      for (int i = 0; i < kBloomLevels; ++i) postPass(enc, bloomViews_[i], downPipe_, downGroups_[i], false);
      for (int i = kBloomLevels - 2; i >= 0; --i) postPass(enc, bloomViews_[i], upPipe_, upGroups_[i], true);
    }
    // ---- 5. the film, onto the screen
    if (!(g_skipPasses & (16 | 256))) postPass(enc, st.texture.CreateView(), finalPipe_, finalGroup_, false);
    // a capture: the same film pass into a texture the page reads back
    wgpu::Texture captureTex;
    if (captureDone_) {
      wgpu::TextureDescriptor td;
      td.size = {width_, height_, 1};
      td.format = format_;
      td.usage = wgpu::TextureUsage::RenderAttachment | wgpu::TextureUsage::CopySrc;
      captureTex = device_.CreateTexture(&td);
      postPass(enc, captureTex.CreateView(), finalPipe_, finalGroup_, false);
    }
    wgpu::CommandBuffer cb = enc.Finish();
    queue_.Submit(1, &cb);
    ++inFlight_;
    queue_.OnSubmittedWorkDone(wgpu::CallbackMode::AllowSpontaneous, [this](wgpu::QueueWorkDoneStatus, wgpu::StringView) { --inFlight_; });
    if (captureTex) {
      // the page copies and reads it with the browser's own WebGPU calls
      captureDone_ = nullptr;
      jsCapture(device_.Get(), captureTex.Get(), static_cast<int>(width_), static_cast<int>(height_), format_ == wgpu::TextureFormat::BGRA8Unorm ? 1 : 0);
    }
    ++presented_;
    return true;
  }

  uint64_t presentedFrames() const override { return presented_; }
  float renderScale() const override { return renderScale_; }

  // Once a second: if the GPU dropped frames, draw fewer pixels; after a few
  // calm seconds, try more again, but not past a size that was too slow
  // a moment ago (so it settles instead of swinging back and forth).
  void adaptResolution(float dt) {
    perfClock_ += dt;
    sinceScale_ += dt;
    if (perfClock_ < 1.0f) return;
    float skipped = static_cast<float>(perfSkipped_) / std::max(1, perfSkipped_ + perfDrawn_);
    perfSkipped_ = perfDrawn_ = 0;
    perfClock_ = 0;
    float lowest = quality_ == Quality::kUltra ? 0.35f : 0.5f;
    if (skipped > 0.12f && renderScale_ > lowest && sinceScale_ > 1.5f) {
      scaleCeil_ = renderScale_ - 0.05f;  // this size was too much
      renderScale_ = std::max(lowest, renderScale_ * (skipped > 0.4f ? 0.7f : 0.85f));
      calmSeconds_ = 0, sinceScale_ = 0;
      width_ = 0;  // re-make targets at the new size
    } else if (skipped == 0) {
      if (++calmSeconds_ >= 4 && renderScale_ < scaleCeil_ && sinceScale_ > 4) {
        renderScale_ = std::min(scaleCeil_, renderScale_ + 0.1f);
        calmSeconds_ = 0, sinceScale_ = 0;
        width_ = 0;
      }
      if (calmSeconds_ >= 30) scaleCeil_ = std::min(1.0f, scaleCeil_ + 0.1f), calmSeconds_ = 4;  // conditions change: try again now and then
    } else calmSeconds_ = 0;
  }

 private:
  void readLights(const Scene& scene) {
    lights_.clear();
    sunOn_ = false;
    for (const Light& l : scene.lights) {
      if (l.kind == Light::Kind::kSun) {
        sunOn_ = true;
        sunDir_ = normalize(-l.direction);
        sunLux_ = l.lux;
        sunTint_ = l.color;
        continue;
      }
      if (lights_.size() >= kMaxLights) continue;
      LightU u;
      float r = std::max(l.radius, 0.01f);
      u.pos = {l.position, r};
      if (l.kind == Light::Kind::kRect) {
        u.color = {l.color * (l.lumens / (kPi * std::max(l.size.x * l.size.y, 1e-4f))), 0};
        u.dir = {normalize(l.direction), 2};
        u.cone = {0, 0, l.size.x, l.size.y};
      } else if (l.kind == Light::Kind::kSpot) {
        float ci = std::cos(l.innerAngle), co = std::cos(std::max(l.outerAngle, l.innerAngle + 1e-3f));
        u.color = {l.color * (l.lumens / (kTwoPi * (1 - 0.5f * (ci + co)))), 0};
        u.dir = {normalize(l.direction), 1};
        u.cone = {ci, co, 0, 0};
      } else {
        u.color = {l.color * (l.lumens / (4 * kPi)), 0};
        u.dir = {0, -1, 0, 0};
      }
      lights_.push_back(u);
    }
    lightsDirty_ = true;
  }

  // The lamps go to the GPU brightest-near-the-camera first: the shader
  // gives its shadow rays to the first few, so they go where they show.
  void uploadLights(const Vec3& eye) {
    if (lights_.empty()) return;
    if (!lightsDirty_ && length(eye - lightsEye_) < 0.5f) return;
    lightsDirty_ = false;
    lightsEye_ = eye;
    std::vector<std::pair<float, uint32_t>> rank(lights_.size());
    for (size_t i = 0; i < lights_.size(); ++i) {
      const LightU& L = lights_[i];
      Vec3 d = L.pos.xyz() - eye;
      float power = luminance(L.color.xyz()) * (L.dir.w > 1.5f ? L.cone.z * L.cone.w : 1.0f);
      rank[i] = {power / std::max(dot(d, d), 0.25f), static_cast<uint32_t>(i)};
    }
    std::stable_sort(rank.begin(), rank.end(), [](auto& a, auto& b) { return a.first > b.first; });
    sortedLights_.resize(lights_.size());
    for (size_t i = 0; i < rank.size(); ++i) sortedLights_[i] = lights_[rank[i].second];
    queue_.WriteBuffer(lightsBuf_, 0, sortedLights_.data(), sortedLights_.size() * sizeof(LightU));
  }

  static Vec3 whiteBalanceGains(float temperature, float tint) {
    Vec3 g = kelvin(6500 - temperature * 3000) / kelvin(6500);
    if (temperature < 0) g = kelvin(6500 / (1 + temperature * 0.6f)) / kelvin(6500);
    g = g * Vec3(1 + tint * 0.08f, 1 - tint * 0.12f, 1 + tint * 0.08f);
    return g / luminance(g);
  }

  wgpu::Buffer buffer(size_t size, wgpu::BufferUsage usage) {
    wgpu::BufferDescriptor bd;
    bd.size = (size + 15) & ~size_t(15);
    bd.usage = usage;
    return device_.CreateBuffer(&bd);
  }
  // true when the buffer was replaced (bind groups need making again)
  bool ensureStorage(wgpu::Buffer& b, size_t& cap, size_t stride, size_t count) {
    if (b && count <= cap) return false;
    cap = std::max<size_t>(count, cap * 2);
    b = buffer(stride * cap, wgpu::BufferUsage::Storage | wgpu::BufferUsage::CopyDst);
    bindGroupsDirty_ = true;
    return true;
  }

  GpuMesh upload(const Mesh& m) {
    std::vector<float> v;
    v.reserve(m.positions.size() * 8);
    for (size_t i = 0; i < m.positions.size(); ++i) {
      Vec3 n = i < m.normals.size() ? m.normals[i] : Vec3{0, 1, 0};
      Vec2 uv = i < m.uvs.size() ? m.uvs[i] : Vec2{};
      v.insert(v.end(), {m.positions[i].x, m.positions[i].y, m.positions[i].z, n.x, n.y, n.z, uv.x, uv.y});
    }
    GpuMesh g;
    g.vb = buffer(std::max<size_t>(v.size() * 4, 16), wgpu::BufferUsage::Vertex | wgpu::BufferUsage::CopyDst);
    if (!v.empty()) queue_.WriteBuffer(g.vb, 0, v.data(), v.size() * 4);
    std::vector<uint32_t> idx = m.indices;
    if (idx.size() % 2) idx.push_back(idx.empty() ? 0 : idx.back());  // whole 4-byte words for the copy
    g.ib = buffer(std::max<size_t>(idx.size() * 4, 16), wgpu::BufferUsage::Index | wgpu::BufferUsage::CopyDst);
    if (!idx.empty()) queue_.WriteBuffer(g.ib, 0, idx.data(), idx.size() * 4);
    g.indexCount = static_cast<uint32_t>(m.indices.size());
    return g;
  }

  void drawBatches(wgpu::RenderPassEncoder& pass, bool glass, bool shadow) {
    for (const Batch& b : batches_) {
      if (b.glass != glass && !shadow) continue;
      if (shadow && b.glass) continue;
      const GpuMesh& m = meshes_[b.mesh];
      if (!m.indexCount) continue;
      pass.SetVertexBuffer(0, m.vb);
      pass.SetIndexBuffer(m.ib, wgpu::IndexFormat::Uint32);
      pass.DrawIndexed(m.indexCount, b.count, 0, 0, b.first);
      ++drawCalls_;
    }
  }

  void postPass(wgpu::CommandEncoder& enc, wgpu::TextureView target, wgpu::RenderPipeline& pipe, wgpu::BindGroup& group, bool load) {
    wgpu::RenderPassColorAttachment ca;
    ca.view = target;
    ca.loadOp = load ? wgpu::LoadOp::Load : wgpu::LoadOp::Clear;
    ca.storeOp = wgpu::StoreOp::Store;
    ca.clearValue = {0, 0, 0, 1};
    wgpu::RenderPassDescriptor rp;
    rp.colorAttachmentCount = 1;
    rp.colorAttachments = &ca;
    wgpu::RenderPassEncoder pass = enc.BeginRenderPass(&rp);
    pass.SetPipeline(pipe);
    pass.SetBindGroup(0, group);
    pass.Draw(3);
    pass.End();
  }

  wgpu::ShaderModule module(const std::string& code, const char* label) {
    wgpu::ShaderSourceWGSL src;
    src.code = code.c_str();
    wgpu::ShaderModuleDescriptor md;
    md.nextInChain = &src;
    md.label = label;
    return device_.CreateShaderModule(&md);
  }

  wgpu::BindGroupLayout layout(std::vector<wgpu::BindGroupLayoutEntry> e) {
    wgpu::BindGroupLayoutDescriptor d;
    d.entryCount = e.size();
    d.entries = e.data();
    return device_.CreateBindGroupLayout(&d);
  }
  static wgpu::BindGroupLayoutEntry ubo(uint32_t b, wgpu::ShaderStage s) {
    wgpu::BindGroupLayoutEntry e;
    e.binding = b, e.visibility = s, e.buffer.type = wgpu::BufferBindingType::Uniform;
    return e;
  }
  static wgpu::BindGroupLayoutEntry ssbo(uint32_t b, wgpu::ShaderStage s, bool write = false) {
    wgpu::BindGroupLayoutEntry e;
    e.binding = b, e.visibility = s, e.buffer.type = write ? wgpu::BufferBindingType::Storage : wgpu::BufferBindingType::ReadOnlyStorage;
    return e;
  }
  static wgpu::BindGroupLayoutEntry tex(uint32_t b, wgpu::ShaderStage s, bool depth = false, bool unfilterable = false) {
    wgpu::BindGroupLayoutEntry e;
    e.binding = b, e.visibility = s;
    e.texture.sampleType = depth ? wgpu::TextureSampleType::Depth : unfilterable ? wgpu::TextureSampleType::UnfilterableFloat : wgpu::TextureSampleType::Float;
    e.texture.viewDimension = wgpu::TextureViewDimension::e2D;
    return e;
  }
  static wgpu::BindGroupLayoutEntry samp(uint32_t b, wgpu::ShaderStage s, bool compare = false) {
    wgpu::BindGroupLayoutEntry e;
    e.binding = b, e.visibility = s, e.sampler.type = compare ? wgpu::SamplerBindingType::Comparison : wgpu::SamplerBindingType::Filtering;
    return e;
  }

  void buildPipelines() {
    const auto VF = wgpu::ShaderStage::Vertex | wgpu::ShaderStage::Fragment;
    const auto F = wgpu::ShaderStage::Fragment;
    const auto VFC = VF | wgpu::ShaderStage::Compute;
    scene0Layout_ = layout({ubo(0, VFC), ssbo(1, VFC), ssbo(2, VFC), ssbo(3, VF), ssbo(4, F | wgpu::ShaderStage::Compute)});
    rayLayout_ = layout({ssbo(0, F | wgpu::ShaderStage::Compute), ssbo(1, F | wgpu::ShaderStage::Compute), ssbo(2, F | wgpu::ShaderStage::Compute),
                         ssbo(3, F | wgpu::ShaderStage::Compute)});
    {
      const auto C = wgpu::ShaderStage::Compute;
      wgpu::BindGroupLayoutEntry st;
      st.binding = 1, st.visibility = C;
      st.storageTexture.access = wgpu::StorageTextureAccess::WriteOnly;
      st.storageTexture.format = wgpu::TextureFormat::RGBA16Float;
      st.storageTexture.viewDimension = wgpu::TextureViewDimension::e2D;
      ultraLayout_ = layout({ssbo(0, C, true), st, tex(2, C), samp(3, C), ubo(4, C)});
      wgpu::BindGroupLayoutEntry st2 = st;
      st2.binding = 4;
      denoiseLayout_ = layout({ubo(0, C), ssbo(1, C), ssbo(2, C), ssbo(3, C, true), st2, ssbo(5, C)});
    }
    scene1Layout_ = layout({tex(0, F, true), samp(1, F, true), tex(2, F), samp(3, F)});
    postLayout_ = layout({ubo(0, F), tex(1, F), tex(2, F), samp(3, F), ssbo(4, F)});
    expLayout_ = layout({ubo(0, wgpu::ShaderStage::Compute), tex(1, wgpu::ShaderStage::Compute, false, true), ssbo(2, wgpu::ShaderStage::Compute, true)});
    std::string sceneCode = std::string("diagnostic(off, derivative_uniformity);\n") + kCommonWgsl + kRayWgsl + kSceneWgsl;
    wgpu::ShaderModule sm = module(sceneCode, "scene");
    wgpu::ShaderModule pm = module(kPostWgsl, "post");
    wgpu::ShaderModule em = module(kExposureWgsl, "exposure");

    auto pipeLayout = [&](std::vector<wgpu::BindGroupLayout> ls) {
      wgpu::PipelineLayoutDescriptor d;
      d.bindGroupLayoutCount = ls.size();
      d.bindGroupLayouts = ls.data();
      return device_.CreatePipelineLayout(&d);
    };
    wgpu::PipelineLayout sceneL = pipeLayout({scene0Layout_, scene1Layout_, rayLayout_});
    wgpu::PipelineLayout shadowL = pipeLayout({scene0Layout_});
    wgpu::PipelineLayout postL = pipeLayout({postLayout_});

    static wgpu::VertexAttribute attrs[3];
    attrs[0].format = wgpu::VertexFormat::Float32x3, attrs[0].offset = 0, attrs[0].shaderLocation = 0;
    attrs[1].format = wgpu::VertexFormat::Float32x3, attrs[1].offset = 12, attrs[1].shaderLocation = 1;
    attrs[2].format = wgpu::VertexFormat::Float32x2, attrs[2].offset = 24, attrs[2].shaderLocation = 2;
    wgpu::VertexBufferLayout vbl;
    vbl.arrayStride = 32;
    vbl.attributeCount = 3;
    vbl.attributes = attrs;

    auto scenePipe = [&](const char* fs, bool blend, bool writeDepth, bool sky) {
      wgpu::RenderPipelineDescriptor d;
      d.layout = sceneL;
      d.vertex.module = sm;
      d.vertex.entryPoint = sky ? "vsSky" : "vs";
      if (!sky) d.vertex.bufferCount = 1, d.vertex.buffers = &vbl;
      wgpu::ColorTargetState ct;
      ct.format = wgpu::TextureFormat::RGBA16Float;
      wgpu::BlendState bs;
      if (blend) {
        bs.color = {wgpu::BlendOperation::Add, wgpu::BlendFactor::One, wgpu::BlendFactor::OneMinusSrcAlpha};
        bs.alpha = {wgpu::BlendOperation::Add, wgpu::BlendFactor::One, wgpu::BlendFactor::OneMinusSrcAlpha};
        ct.blend = &bs;
      }
      wgpu::FragmentState fst;
      fst.module = sm;
      fst.entryPoint = fs;
      fst.targetCount = 1;
      fst.targets = &ct;
      d.fragment = &fst;
      d.primitive.cullMode = wgpu::CullMode::None;
      wgpu::DepthStencilState ds;
      ds.format = wgpu::TextureFormat::Depth32Float;
      ds.depthWriteEnabled = writeDepth;
      ds.depthCompare = sky ? wgpu::CompareFunction::LessEqual : wgpu::CompareFunction::Less;
      d.depthStencil = &ds;
      d.multisample.count = static_cast<uint32_t>(g_samples);
      return device_.CreateRenderPipeline(&d);
    };
    opaquePipe_ = scenePipe("fsOpaque", false, true, false);
    glassPipe_ = scenePipe("fsGlass", true, false, false);
    skyPipe_ = scenePipe("fsSky", false, false, true);
    {
      wgpu::RenderPipelineDescriptor d;
      d.layout = shadowL;
      d.vertex.module = sm;
      d.vertex.entryPoint = "vsShadow";
      d.vertex.bufferCount = 1, d.vertex.buffers = &vbl;
      // a small colour target that's never written: some drivers stumble
      // on passes with only a depth attachment
      static wgpu::ColorTargetState dummy;
      dummy.format = wgpu::TextureFormat::R8Unorm;
      dummy.writeMask = wgpu::ColorWriteMask::None;
      wgpu::FragmentState fst;
      fst.module = sm;
      fst.entryPoint = "fsShadow";
      fst.targetCount = 1;
      fst.targets = &dummy;
      d.fragment = &fst;
      d.primitive.cullMode = wgpu::CullMode::None;
      wgpu::DepthStencilState ds;
      ds.format = wgpu::TextureFormat::Depth24Plus;
      ds.depthWriteEnabled = true;
      ds.depthCompare = wgpu::CompareFunction::Less;
      ds.depthBias = 2;
      ds.depthBiasSlopeScale = 2.0f;
      d.depthStencil = &ds;
      shadowPipe_ = device_.CreateRenderPipeline(&d);
    }
    auto postPipe = [&](const char* fs, wgpu::TextureFormat fmt, bool additive) {
      wgpu::RenderPipelineDescriptor d;
      d.layout = postL;
      d.vertex.module = pm;
      d.vertex.entryPoint = "vsFull";
      wgpu::ColorTargetState ct;
      ct.format = fmt;
      wgpu::BlendState bs;
      if (additive) {
        bs.color = {wgpu::BlendOperation::Add, wgpu::BlendFactor::One, wgpu::BlendFactor::One};
        bs.alpha = {wgpu::BlendOperation::Add, wgpu::BlendFactor::One, wgpu::BlendFactor::One};
        ct.blend = &bs;
      }
      wgpu::FragmentState fst;
      fst.module = pm;
      fst.entryPoint = fs;
      fst.targetCount = 1;
      fst.targets = &ct;
      d.fragment = &fst;
      return device_.CreateRenderPipeline(&d);
    };
    downPipe_ = postPipe("fsDown", wgpu::TextureFormat::RGBA16Float, false);
    upPipe_ = postPipe("fsUp", wgpu::TextureFormat::RGBA16Float, true);
    finalPipe_ = postPipe("fsPost", format_, false);
    {
      std::string code = std::string(kCommonWgsl) + kRayWgsl + kUltraWgsl;
      wgpu::ComputePipelineDescriptor d;
      d.layout = pipeLayout({scene0Layout_, ultraLayout_, rayLayout_});
      d.compute.module = module(code, "ultra");
      d.compute.entryPoint = "main";
      ultraPipe_ = device_.CreateComputePipeline(&d);
    }
    {
      wgpu::ComputePipelineDescriptor d;
      d.layout = pipeLayout({denoiseLayout_});
      d.compute.module = module(kDenoiseWgsl, "denoise");
      d.compute.entryPoint = "main";
      denoisePipe_ = device_.CreateComputePipeline(&d);
      for (auto& u : denoiseUbo_) u = buffer(48, wgpu::BufferUsage::Uniform | wgpu::BufferUsage::CopyDst);
    }
    {
      wgpu::ComputePipelineDescriptor d;
      d.layout = pipeLayout({expLayout_});
      d.compute.module = em;
      d.compute.entryPoint = "main";
      exposurePipe_ = device_.CreateComputePipeline(&d);
    }
  }

  void makeShadowMap() {
    wgpu::TextureDescriptor td;
    td.size = {static_cast<uint32_t>(shadowSize_), static_cast<uint32_t>(shadowSize_), 1};
    td.format = wgpu::TextureFormat::Depth24Plus;
    td.usage = wgpu::TextureUsage::RenderAttachment | wgpu::TextureUsage::TextureBinding;
    shadowTex_ = device_.CreateTexture(&td);
    shadowView_ = shadowTex_.CreateView();
    td.format = wgpu::TextureFormat::R8Unorm;
    td.usage = wgpu::TextureUsage::RenderAttachment;
    shadowDummy_ = device_.CreateTexture(&td).CreateView();
    bindGroupsDirty_ = true;
  }

  void uploadSky(const SkyModel& m) {
    // a latitude-longitude picture of the sky (more rows near the horizon), with
    // blurred smaller copies for rough reflections
    const int W = 256, H = 128;
    int levels = 1;
    while ((W >> levels) >= 4 && (H >> levels) >= 2) ++levels;
    std::vector<std::vector<Vec3>> mips(levels);
    mips[0].resize(W * H);
    for (int y = 0; y < H; ++y)
      for (int x = 0; x < W; ++x) {
        float v = 1 - (y + 0.5f) / H, s = (v - 0.5f) * 2;
        float elev = std::copysign(s * s, s) * (kPi / 2);
        float az = (x + 0.5f) / W * kTwoPi;
        Vec3 d{std::cos(elev) * std::cos(az), std::sin(elev), std::cos(elev) * std::sin(az)};
        mips[0][y * W + x] = m.radiance(d);
      }
    for (int l = 1; l < levels; ++l) {
      int w = W >> l, h = H >> l, pw = W >> (l - 1);
      mips[l].resize(w * h);
      for (int y = 0; y < h; ++y)
        for (int x = 0; x < w; ++x) {
          // a wider blur than a plain halving, so rough metal looks smooth
          Vec3 s{0};
          float n = 0;
          for (int dy = -1; dy <= 2; ++dy)
            for (int dx = -1; dx <= 2; ++dx) {
              int sx = ((x * 2 + dx) % pw + pw) % pw, sy = std::clamp(y * 2 + dy, 0, (H >> (l - 1)) - 1);
              s += mips[l - 1][sy * pw + sx];
              n += 1;
            }
          mips[l][y * w + x] = s / n;
        }
    }
    wgpu::TextureDescriptor td;
    td.size = {W, H, 1};
    td.format = wgpu::TextureFormat::RGBA16Float;
    td.usage = wgpu::TextureUsage::TextureBinding | wgpu::TextureUsage::CopyDst;
    td.mipLevelCount = static_cast<uint32_t>(levels);
    skyTex_ = device_.CreateTexture(&td);
    for (int l = 0; l < levels; ++l) {
      int w = W >> l, h = H >> l;
      std::vector<uint16_t> px(w * h * 4);
      for (int i = 0; i < w * h; ++i) {
        px[i * 4] = toHalf(mips[l][i].x), px[i * 4 + 1] = toHalf(mips[l][i].y), px[i * 4 + 2] = toHalf(mips[l][i].z), px[i * 4 + 3] = toHalf(1);
      }
      wgpu::TexelCopyTextureInfo dst;
      dst.texture = skyTex_;
      dst.mipLevel = static_cast<uint32_t>(l);
      wgpu::TexelCopyBufferLayout lay;
      lay.bytesPerRow = static_cast<uint32_t>(w * 8);
      lay.rowsPerImage = static_cast<uint32_t>(h);
      wgpu::Extent3D ext{static_cast<uint32_t>(w), static_cast<uint32_t>(h), 1};
      queue_.WriteTexture(&dst, px.data(), px.size() * 2, &lay, &ext);
    }
    skyView_ = skyTex_.CreateView();
    m.shCoefficients(sh_);
    bindGroupsDirty_ = true;
  }

  void resize() {
    double cw = 0, ch = 0;
    emscripten_get_element_css_size(canvas_.c_str(), &cw, &ch);
    ratio_ = std::min(maxRatio_, static_cast<float>(emscripten_get_device_pixel_ratio())) * renderScale_;
    uint32_t w = static_cast<uint32_t>(std::max(1.0, cw * ratio_)), h = static_cast<uint32_t>(std::max(1.0, ch * ratio_));
    if (w == width_ && h == height_) return;
    width_ = w, height_ = h;
    emscripten_set_canvas_element_size(canvas_.c_str(), static_cast<int>(w), static_cast<int>(h));
    wgpu::SurfaceConfiguration c;
    c.device = device_;
    c.format = format_;
    c.width = w, c.height = h;
    c.alphaMode = wgpu::CompositeAlphaMode::Opaque;
    c.presentMode = wgpu::PresentMode::Fifo;
    if (!(g_skipPasses & 256)) surface_.Configure(&c);
    auto target = [&](uint32_t tw, uint32_t th, wgpu::TextureFormat f, uint32_t samples, wgpu::TextureUsage u) {
      wgpu::TextureDescriptor td;
      td.size = {std::max(1u, tw), std::max(1u, th), 1};
      td.format = f;
      td.sampleCount = samples;
      td.usage = u;
      return device_.CreateTexture(&td).CreateView();
    };
    if (g_samples > 1) msaaView_ = target(w, h, wgpu::TextureFormat::RGBA16Float, static_cast<uint32_t>(g_samples), wgpu::TextureUsage::RenderAttachment);
    depthView_ = target(w, h, wgpu::TextureFormat::Depth32Float, static_cast<uint32_t>(g_samples), wgpu::TextureUsage::RenderAttachment);
    hdrView_ = target(w, h, wgpu::TextureFormat::RGBA16Float, 1, wgpu::TextureUsage::RenderAttachment | wgpu::TextureUsage::TextureBinding | wgpu::TextureUsage::StorageBinding);
    ultraFrame_ = 0;
    accumBuf_ = buffer(static_cast<size_t>(w) * h * 48, wgpu::BufferUsage::Storage);
    denoiseA_ = buffer(static_cast<size_t>(w) * h * 16, wgpu::BufferUsage::Storage);
    denoiseB_ = buffer(static_cast<size_t>(w) * h * 16, wgpu::BufferUsage::Storage);
    for (int i = 0; i < kBloomLevels; ++i)
      bloomViews_[i] = target(w >> (i + 1), h >> (i + 1), wgpu::TextureFormat::RGBA16Float, 1,
                              wgpu::TextureUsage::RenderAttachment | wgpu::TextureUsage::TextureBinding);
    bindGroupsDirty_ = true;
  }

  wgpu::BindGroup group(wgpu::BindGroupLayout l, std::vector<wgpu::BindGroupEntry> e) {
    wgpu::BindGroupDescriptor d;
    d.layout = l;
    d.entryCount = e.size();
    d.entries = e.data();
    return device_.CreateBindGroup(&d);
  }
  static wgpu::BindGroupEntry bufE(uint32_t b, wgpu::Buffer buf) {
    wgpu::BindGroupEntry e;
    e.binding = b, e.buffer = buf;
    return e;
  }
  static wgpu::BindGroupEntry texE(uint32_t b, wgpu::TextureView v) {
    wgpu::BindGroupEntry e;
    e.binding = b, e.textureView = v;
    return e;
  }
  static wgpu::BindGroupEntry sampE(uint32_t b, wgpu::Sampler s) {
    wgpu::BindGroupEntry e;
    e.binding = b, e.sampler = s;
    return e;
  }

  void makeBindGroups() {
    if (!hdrView_) return;
    sceneGroup0_ = group(scene0Layout_, {bufE(0, frameUbo_), bufE(1, lightsBuf_), bufE(2, materialsBuf_), bufE(3, instancesBuf_), bufE(4, exposureBuf_)});
    sceneGroup1_ = group(scene1Layout_, {texE(0, shadowView_), sampE(1, shadowSampler_), texE(2, skyView_), sampE(3, linear_)});
    // each pass reads one texture and writes another; the unused second slot
    // gets a texture that isn't the one being written
    for (int i = 0; i < kBloomLevels - 1; ++i)
      upGroups_[i] = group(postLayout_, {bufE(0, postUbo_), texE(1, bloomViews_[i + 1]), texE(2, hdrView_), sampE(3, linear_), bufE(4, exposureBuf_)});
    for (int i = 0; i < kBloomLevels; ++i) {
      wgpu::TextureView src = i == 0 ? hdrView_ : bloomViews_[i - 1];
      downGroups_[i] = group(postLayout_, {bufE(0, postUbo_), texE(1, src), texE(2, skyView_), sampE(3, linear_), bufE(4, exposureBuf_)});
    }
    finalGroup_ = group(postLayout_, {bufE(0, postUbo_), texE(1, hdrView_), texE(2, bloomViews_[0]), sampE(3, linear_), bufE(4, exposureBuf_)});
    exposureGroup_ = group(expLayout_, {bufE(0, expUbo_), texE(1, hdrView_), bufE(2, exposureBuf_)});
    if (!rayTris_) emptyRayScene();
    rayGroup_ = group(rayLayout_, {bufE(0, rayTris_), bufE(1, rayBlas_), bufE(2, rayInst_), bufE(3, rayTlas_)});
    if (!ultraUbo_) ultraUbo_ = buffer(sizeof(UltraU), wgpu::BufferUsage::Uniform | wgpu::BufferUsage::CopyDst);
    if (accumBuf_) {
      ultraGroup_ = group(ultraLayout_, {bufE(0, accumBuf_), texE(1, hdrView_), texE(2, skyView_), sampE(3, linear_), bufE(4, ultraUbo_)});
      // passes: average → A, A → B (step 1), B → A (step 2), A → B (step 4, onto the picture)
      wgpu::Buffer srcs[4] = {denoiseB_, denoiseA_, denoiseB_, denoiseA_}, dsts[4] = {denoiseA_, denoiseB_, denoiseA_, denoiseB_};
      for (int k = 0; k < 4; ++k)
        denoiseGroup_[k] = group(denoiseLayout_, {bufE(0, denoiseUbo_[k]), bufE(1, accumBuf_), bufE(2, srcs[k]), bufE(3, dsts[k]), texE(4, hdrView_), bufE(5, exposureBuf_)});
    }
    bindGroupsDirty_ = false;
  }

  std::string canvas_;
  wgpu::Instance instance_;
  wgpu::Surface surface_;
  wgpu::Adapter adapter_;
  wgpu::Device device_;
  wgpu::Queue queue_;
  wgpu::TextureFormat format_;
  uint32_t width_ = 0, height_ = 0;
  float ratio_ = 1, maxRatio_ = 2;
  int shadowSize_ = 2048;
  Quality quality_ = Quality::kHigh;

  wgpu::Buffer frameUbo_, postUbo_, expUbo_, lightsBuf_, materialsBuf_, instancesBuf_, exposureBuf_;
  size_t materialsCap_ = 0, instancesCap_ = 0;
  wgpu::Sampler linear_, shadowSampler_;
  wgpu::Texture shadowTex_, skyTex_;
  wgpu::TextureView shadowDummy_, shadowView_, skyView_, msaaView_, depthView_, hdrView_, bloomViews_[kBloomLevels];
  wgpu::BindGroupLayout scene0Layout_, scene1Layout_, postLayout_, expLayout_;
  wgpu::RenderPipeline opaquePipe_, glassPipe_, skyPipe_, shadowPipe_, downPipe_, upPipe_, finalPipe_;
  wgpu::ComputePipeline exposurePipe_;
  wgpu::BindGroup sceneGroup0_, sceneGroup1_, downGroups_[kBloomLevels], upGroups_[kBloomLevels], finalGroup_, exposureGroup_;
  bool bindGroupsDirty_ = true, skyBuilt_ = false;

  std::vector<GpuMesh> meshes_;
  std::vector<Batch> batches_;
  std::vector<LightU> lights_, sortedLights_;
  bool lightsDirty_ = true;
  Vec3 lightsEye_{1e9f};
  Fog fog_;
  bool sunOn_ = false;
  Vec3 sunDir_{0, 1, 0}, sunTint_{1}, sunIllum_{0};
  float sunLux_ = 0;
  Vec3 sh_[9];
  size_t drawCalls_ = 0;
  float lastExposure_ = 1;
  // Ultra
  const Scene* lastScene_ = nullptr;
  wgpu::BindGroupLayout ultraLayout_;
  wgpu::ComputePipeline ultraPipe_;
  wgpu::Buffer ultraUbo_, accumBuf_, denoiseA_, denoiseB_, denoiseUbo_[4];
  wgpu::BindGroupLayout denoiseLayout_;
  wgpu::ComputePipeline denoisePipe_;
  wgpu::BindGroup denoiseGroup_[4];
  wgpu::BindGroup ultraGroup_;
  uint32_t ultraFrame_ = 0;
  Vec3 ultraCamKey_[3];

  // ---- rays through the scene: a tree per shape (once), a tree over the copies
  wgpu::BindGroupLayout rayLayout_;
  wgpu::BindGroup rayGroup_;
  wgpu::Buffer rayTris_, rayBlas_, rayInst_, rayTlas_;
  std::vector<TriU> rayTrisCpu_;
  std::vector<NodeU> rayBlasCpu_;
  std::vector<uint32_t> meshRoot_;  // per mesh: its tree's root node (~0: none)
  std::vector<AABB> meshBox_;
  uint32_t ultraScale_ = 1;

  static NodeU packNode(const AABB& box, uint32_t first, uint32_t count) {
    NodeU u;
    float f, c;
    std::memcpy(&f, &first, 4);
    std::memcpy(&c, &count, 4);
    u.lo = {box.lo, f}, u.hi = {box.hi, c};
    return u;
  }
  std::unordered_map<const void*, size_t> storageCap_;
  // Writes into the buffer, making a bigger one only when it doesn't fit.
  void uploadStorage(wgpu::Buffer& b, const void* data, size_t bytes) {
    size_t& cap = storageCap_[&b];
    if (!b || bytes > cap) {
      cap = std::max<size_t>(std::max<size_t>(bytes, 256), cap * 3 / 2);
      b = buffer(cap, wgpu::BufferUsage::Storage | wgpu::BufferUsage::CopyDst);
      bindGroupsDirty_ = true;
    }
    if (bytes) queue_.WriteBuffer(b, 0, data, (bytes + 3) & ~size_t(3));
  }
  void emptyRayScene() {
    // a single empty box: every ray misses
    NodeU empty = packNode(AABB(Vec3(1), Vec3(-1)), 0, 1);
    RayInstU ri{};
    TriU t{};
    uploadStorage(rayTris_, &t, sizeof t);
    uploadStorage(rayBlas_, &empty, sizeof empty);
    uploadStorage(rayInst_, &ri, sizeof ri);
    uploadStorage(rayTlas_, &empty, sizeof empty);
  }

  // Each new shape's own tree, in its own space; built once.
  void buildRayMeshes(const Scene& scene) {
    if (meshRoot_.size() >= scene.meshes.size()) return;
    for (size_t mi = meshRoot_.size(); mi < scene.meshes.size(); ++mi) {
      const Mesh& m = scene.meshes[mi];
      std::vector<Triangle> geo;
      std::vector<TriU> local;
      for (size_t i = 0; i + 2 < m.indices.size(); i += 3) {
        Vec3 p[3], n[3];
        Vec2 uv[3];
        for (int k = 0; k < 3; ++k) {
          uint32_t idx = m.indices[i + k];
          p[k] = m.positions[idx];
          n[k] = idx < m.normals.size() ? m.normals[idx] : Vec3(0, 1, 0);
          uv[k] = idx < m.uvs.size() ? m.uvs[idx] : Vec2{};
        }
        if (lengthSq(cross(p[1] - p[0], p[2] - p[0])) < 1e-24f) continue;
        geo.push_back({p[0], p[1] - p[0], p[2] - p[0]});
        TriU t;
        t.v0 = {p[0], uv[2].y};
        t.e1 = {p[1] - p[0], uv[1].y}, t.e2 = {p[2] - p[0], uv[2].x};
        t.n0 = {n[0], uv[0].x}, t.n1 = {n[1], uv[0].y}, t.n2 = {n[2], uv[1].x};
        local.push_back(t);
      }
      if (geo.empty()) {
        meshRoot_.push_back(~0u);
        meshBox_.push_back(AABB());
        continue;
      }
      Bvh bvh;
      bvh.build(geo, 4);
      uint32_t nodeBase = static_cast<uint32_t>(rayBlasCpu_.size()), triBase = static_cast<uint32_t>(rayTrisCpu_.size());
      for (uint32_t k : bvh.order()) rayTrisCpu_.push_back(local[k]);
      for (const Bvh::Node& n : bvh.nodes())
        rayBlasCpu_.push_back(packNode(n.box, n.count ? n.left + triBase : n.left + nodeBase, n.count));
      meshRoot_.push_back(nodeBase);
      meshBox_.push_back(bvh.bounds());
    }
    uploadStorage(rayTris_, rayTrisCpu_.data(), rayTrisCpu_.size() * sizeof(TriU));
    uploadStorage(rayBlas_, rayBlasCpu_.data(), rayBlasCpu_.size() * sizeof(NodeU));
  }

  // The top tree over every placed copy; cheap, rebuilt as things move.
  void buildRayInstances(const Scene& scene) {
    struct Item {
      AABB box;
      RayInstU ri;
    };
    std::vector<Item> items;
    items.reserve(scene.instances.size());
    for (const MeshInstance& in : scene.instances) {
      if (in.mesh < 0 || in.mesh >= static_cast<int>(meshRoot_.size()) || meshRoot_[in.mesh] == ~0u) continue;
      if (std::fabs(in.transform.linear().determinant()) < 1e-12f) continue;  // hidden (scaled to nothing)
      Item it;
      it.box = meshBox_[in.mesh].transformed(in.transform);
      it.ri.inv = in.transform.inverse();
      Mat3 nm = in.transform.linear().inverse().transposed();
      it.ri.n0 = {nm.c[0], 0}, it.ri.n1 = {nm.c[1], 0}, it.ri.n2 = {nm.c[2], 0};
      uint32_t flags = 0;
      if (in.material >= 0 && in.material < static_cast<int>(scene.materials.size())) {
        const Material& m = scene.materials[in.material];
        if (luminance(m.emission) > 0) flags |= 1;
        if (m.transmission > 0) flags |= 2;
        if (m.leafShape) flags |= 4;
      }
      it.ri.info[0] = meshRoot_[in.mesh], it.ri.info[1] = static_cast<uint32_t>(std::max(0, in.material)), it.ri.info[2] = flags, it.ri.info[3] = 0;
      items.push_back(it);
    }
    if (items.empty()) {
      NodeU empty = packNode(AABB(Vec3(1), Vec3(-1)), 0, 1);
      RayInstU ri{};
      uploadStorage(rayInst_, &ri, sizeof ri);
      uploadStorage(rayTlas_, &empty, sizeof empty);
      return;
    }
    // median splits on the longest axis; leaves of up to 2 copies
    std::vector<NodeU> nodes;
    std::vector<RayInstU> ordered;
    ordered.reserve(items.size());
    struct Task {
      uint32_t node, first, count;
    };
    std::vector<uint32_t> idx(items.size());
    for (size_t i = 0; i < idx.size(); ++i) idx[i] = static_cast<uint32_t>(i);
    nodes.push_back({});
    std::vector<Task> stack = {{0, 0, static_cast<uint32_t>(items.size())}};
    std::vector<std::pair<uint32_t, std::pair<uint32_t, uint32_t>>> leaves;  // node, (first, count) in idx
    while (!stack.empty()) {
      Task t = stack.back();
      stack.pop_back();
      AABB box, cbox;
      for (uint32_t k = 0; k < t.count; ++k) box.grow(items[idx[t.first + k]].box), cbox.grow(items[idx[t.first + k]].box.center());
      if (t.count <= 2) {
        nodes[t.node] = packNode(box, 0, t.count);
        leaves.push_back({t.node, {t.first, t.count}});
        continue;
      }
      int axis = maxAxis(cbox.extent());
      uint32_t mid = t.first + t.count / 2;
      std::nth_element(idx.begin() + t.first, idx.begin() + mid, idx.begin() + t.first + t.count,
                       [&](uint32_t a, uint32_t b) { return items[a].box.center()[axis] < items[b].box.center()[axis]; });
      uint32_t l = static_cast<uint32_t>(nodes.size());
      nodes.push_back({});
      nodes.push_back({});
      nodes[t.node] = packNode(box, l, 0);
      stack.push_back({l, t.first, mid - t.first});
      stack.push_back({l + 1, mid, t.first + t.count - mid});
    }
    // leaves point at runs of copies in the order they're stored
    for (auto& [node, run] : leaves) {
      uint32_t first = static_cast<uint32_t>(ordered.size());
      for (uint32_t k = 0; k < run.second; ++k) ordered.push_back(items[idx[run.first + k]].ri);
      float f;
      std::memcpy(&f, &first, 4);
      nodes[node].lo.w = f;
    }
    uploadStorage(rayInst_, ordered.data(), ordered.size() * sizeof(RayInstU));
    uploadStorage(rayTlas_, nodes.data(), nodes.size() * sizeof(NodeU));
  }
  int inFlight_ = 0;
  std::vector<Mat4> lastPlaces_;
  // automatic resolution: fewer pixels when the GPU falls behind
  float renderScale_ = 1, scaleCeil_ = 1, perfClock_ = 0, sinceScale_ = 0;
  int perfSkipped_ = 0, perfDrawn_ = 0, calmSeconds_ = 0, stuckFrames_ = 0;
  uint64_t presented_ = 0;
  std::function<void(int, int, const uint8_t*)> captureDone_;
};

struct Pending {
  std::string canvas;
  wgpu::Instance instance;
  wgpu::Surface surface;
  wgpu::Adapter adapter;
  std::function<void(std::unique_ptr<Renderer>)> ready;
};

}  // namespace

void Renderer::create(const char* canvas, std::function<void(std::unique_ptr<Renderer>)> ready) {
  auto* p = new Pending{canvas, wgpu::CreateInstance(nullptr), {}, {}, std::move(ready)};
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
    dd.SetUncapturedErrorCallback([](const wgpu::Device&, wgpu::ErrorType, wgpu::StringView m) {
      emscripten_log(EM_LOG_ERROR, "WebGPU: %s", str(m).c_str());
    });
    a.RequestDevice(&dd, wgpu::CallbackMode::AllowSpontaneous, [p](wgpu::RequestDeviceStatus s2, wgpu::Device d, wgpu::StringView msg) {
      auto r = std::move(p->ready);
      std::unique_ptr<Renderer> out;
      if (s2 == wgpu::RequestDeviceStatus::Success) out = std::make_unique<GpuRenderer>(p->canvas, p->instance, p->surface, p->adapter, d);
      else emscripten_log(EM_LOG_ERROR, "WebGPU device: %s", str(msg).c_str());
      delete p;
      r(std::move(out));
    });
  });
}

}  // namespace loop::gpu
