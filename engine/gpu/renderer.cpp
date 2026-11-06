#include "engine/gpu/renderer.h"

#include "engine/core/settings.h"

#include <emscripten/emscripten.h>
#include <emscripten/html5.h>
#include <webgpu/webgpu_cpp.h>

#include <algorithm>
#include <cstring>
#include <string>
#include <unordered_map>
#include <vector>

#include "engine/gpu/shaders.h"
#include "engine/render/frame_cost.h"
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

namespace {

// What the GPU's callbacks report. Shared (not a renderer member) so a "done"
// or "lost" signal that arrives after its renderer is gone touches nothing freed.
struct GpuState {
  bool lost = false;
  int inFlight = 0;
  uint32_t gate = 0;          // bumps when the watchdog gives up on old frames
  double lastDone = 0, lastProgress = 0;
  float gpuMs = 0, lastGpuMs = 0;
  double gpuSum = 0;  // GPU ms of the frames finished since the resolution was last judged
  int gpuCount = 0;
};

// The first few WebGPU errors go to the page (a black screen should say why).
int g_gpuErrors = 0;
void reportGpuError(const std::string& m) {
  if (++g_gpuErrors > 3) return;
  emscripten_log(EM_LOG_ERROR, "WebGPU: %s", m.c_str());
  EM_ASM({ if (window.loopGpuProblem) window.loopGpuProblem(UTF8ToString($0)); }, m.c_str());
}

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
  Vec4 tex;   // picture layers: colour, roughness/metal, normal, glow (-1 none)
  Vec4 tex2;  // alpha cutoff, -, -, -
};
struct InstanceU {
  Mat4 model;
  Vec4 n0, n1, n2;
  uint32_t material[4];
};
struct PostU {
  Vec4 exposure, look0, wb, lift, gamma, gain, shadowTint, highlightTint, fx, fx2, screen;
};
// Per post pass: where each of its two pictures holds this frame. The scene
// is drawn into the top-left part of full-size targets (automatic resolution
// changes cost nothing); uv * scale reaches it, clamped to its last texel.
struct PassU {
  Vec4 a, b;  // xy uv scale, zw largest uv
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
constexpr int kFinalPass = 2 * kBloomLevels - 1;  // pass uniforms: bloom down, bloom up, then the film
int g_samples = 4;  // multisampling of the scene pass (1 = off)

struct GpuMesh {
  wgpu::Buffer vb, ib;
  uint32_t indexCount = 0;
  AABB box;  // in the mesh's own space
};
struct Batch {
  int mesh;
  uint32_t first, count;
  bool glass;
};

class GpuRenderer final : public Renderer {
 public:
  GpuRenderer(std::string canvas, wgpu::Instance inst, wgpu::Surface surface, wgpu::Adapter adapter, wgpu::Device device, std::shared_ptr<GpuState> state)
      : canvas_(std::move(canvas)), instance_(inst), surface_(surface), adapter_(adapter), device_(device), queue_(device.GetQueue()), gs_(std::move(state)) {
    wgpu::Limits lim;
    if (device_.GetLimits(&lim)) {
      maxBinding_ = lim.maxStorageBufferBindingSize;
      maxBufferSize_ = lim.maxBufferSize;
      maxTexture_ = lim.maxTextureDimension2D;
      maxLayers_ = lim.maxTextureArrayLayers;
    }
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
    ensureStorage(visibleBuf_, visibleCap_, sizeof(uint32_t), 64);
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
    wgpu::SamplerDescriptor rs;
    rs.magFilter = rs.minFilter = wgpu::FilterMode::Linear;
    rs.mipmapFilter = wgpu::MipmapFilterMode::Linear;
    rs.addressModeU = rs.addressModeV = rs.addressModeW = wgpu::AddressMode::Repeat;
    rs.maxAnisotropy = 4;
    repeat_ = device_.CreateSampler(&rs);
    g_samples = config().render.msaa > 1 ? 4 : 1;
    for (auto& u : passUbo_) u = buffer(sizeof(PassU), wgpu::BufferUsage::Uniform | wgpu::BufferUsage::CopyDst);
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

  float pixelRatio() const override { return ratio_ * renderScale_; }
  size_t drawCalls() const override { return drawCalls_; }
  float currentExposure() const override { return lastExposure_; }
  void capture(std::function<void(int, int, const uint8_t*)> done) override { captureDone_ = std::move(done); }

  void setQuality(Quality q) override {
    if (q == quality_ && outW_) {  // picked again: only the settings may have changed
      applySettings();
      return;
    }
    quality_ = q;
    ultraFrame_ = 0;
    // start a little below full size and climb when calm (a step per 3 calm
    // seconds): a first frame too heavy for the GPU can then never run so long
    // that the browser takes the GPU away. Ultra starts lower still.
    step_ = q == Quality::kUltra ? 3 : 1, ceilStep_ = 0, calmSeconds_ = 0, sinceScale_ = 0, longCalm_ = 0;
    judgedAt_ = 0, perfSkipped_ = perfDrawn_ = 0;
    fresh_ = true;
    renderScale_ = q == Quality::kUltra ? 0.65f : 0.87f;
    lightsDirty_ = true;
    applySettings();
    outW_ = 0;  // re-make targets for this quality
  }

  void applySettings() override {
    const Config::Render& r = config().render;
    Quality q = quality_;
    // pixels per screen point: past ~1.5 the eye barely sees more, but the GPU
    // pays for every one (2x is 4 times the pixels of 1x). Ultra traces rays
    // for every pixel, so it draws at 1x and the screen scales it up.
    // (render.ratioPhone / ratioHigh / ratioUltra)
    maxRatio_ = q == Quality::kHigh ? r.ratioHigh : q == Quality::kUltra ? r.ratioUltra : r.ratioPhone;
    // lamp shadows are rays through the scene from every pixel: each one costs
    // about as much as all the rest of the lighting. Phone: none (lamps light
    // without shadows, like before they were added); High: the two lamps that
    // matter most near the camera, one ray each (crisp shadows).
    // render.lamps / render.rays try more on a strong GPU.
    shadowLamps_ = q == Quality::kPhone ? 0 : 2;
    shadowRays_ = 1;
    if (r.lamps >= 0) shadowLamps_ = r.lamps;
    if (r.rays > 0) shadowRays_ = r.rays;
    if (!r.lampShadows) shadowLamps_ = 0;
    // Ultra traces the sun's shadows; its shadow map is never read
    int size = q == Quality::kUltra ? 16 : q == Quality::kPhone ? r.sunMapPhone : r.sunMapHigh;
    size = std::clamp(size, 16, static_cast<int>(std::min<uint32_t>(maxTexture_, 8192)));
    if (size != shadowSize_ || !shadowTex_) {
      shadowSize_ = size;
      makeShadowMap();
    }
    // multisampling: every scene pipeline and target is made for it
    int samples = r.msaa > 1 ? 4 : 1;
    if (samples != g_samples) {
      g_samples = samples;
      buildPipelines();
      outW_ = 0;
      bindGroupsDirty_ = true;
    }
    lightsDirty_ = true;
    if (lastScene_ && texSize_ != std::clamp(config().render.textureSize, 16, 2048)) uploadModelTextures(*lastScene_);
    ultraFrame_ = 0;  // Ultra starts its picture again with the new settings
    if (lastScene_ && needRays() && raysBuiltFor_ != static_cast<int>(quality_)) {
      buildRayMeshes(*lastScene_);
      buildRayInstances(*lastScene_);
      raysBuiltFor_ = static_cast<int>(quality_);
    }
  }

  bool lost() const override { return gs_->lost; }
  int framesInFlight() const override { return gs_->inFlight; }

  void sync(const Scene& scene, bool geometryChanged, bool skyChanged) override {
    if (scene.textures.size() != texUploaded_ || texSize_ != std::clamp(config().render.textureSize, 16, 2048)) uploadModelTextures(scene);
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
      auto layer = [&](int t) { return t >= 0 && t < static_cast<int>(texLayer_.size()) ? static_cast<float>(texLayer_[t]) : -1.0f; };
      u.tex = {layer(m.baseTexture), layer(m.metalRoughTexture), layer(m.normalTexture), layer(m.emissiveTexture)};
      u.tex2 = {m.alphaCutoff, m.roughnessMin, 0, 0};
    }
    ensureStorage(materialsBuf_, materialsCap_, sizeof(MaterialU), mats.size());
    queue_.WriteBuffer(materialsBuf_, 0, mats.data(), mats.size() * sizeof(MaterialU));
    readLights(scene);
    fog_ = scene.fog;
    if (skyChanged || !skyBuilt_) {
      SkyModel m;
      // a sun given a size (sun(size:) or sun.size) shows it; otherwise our sun's 0.53 deg
      m.build(scene.sky, sunOn_ ? sunDir_ : Vec3(0, 1, 0), sunOn_ ? sunLux_ : 0, sunRadius_);
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
    // free the old scene's GPU memory now, not whenever the browser collects it
    for (GpuMesh& m : meshes_) {
      if (m.vb) m.vb.Destroy();
      if (m.ib) m.ib.Destroy();
    }
    meshes_.clear();
    meshRoot_.clear();
    meshBox_.clear();
    rayTrisCpu_.clear();
    rayBlasCpu_.clear();
    if (rayTris_) rayTris_.Destroy();
    rayTris_ = nullptr;
    bindGroupsDirty_ = true;
    batches_.clear();
    lastScene_ = nullptr;
    skyBuilt_ = false;
    lights_.clear();
    lightsDirty_ = true;
    lastPlaces_.clear();
    raysBuiltFor_ = -1;
    ultraFrame_ = 0;
    fresh_ = true;
  }

  // Rays through the scene are only needed for lamp shadows (High) and Ultra:
  // Phone builds no ray trees at all (less work when a scene loads, and every
  // frame something moves).
  bool needRays() const { return quality_ == Quality::kUltra || shadowLamps_ > 0; }

  void syncTransforms(const Scene& scene) override {
    lastScene_ = &scene;
    meshLods_ = scene.lods;
    if (needRays()) {
      buildRayMeshes(scene);
      buildRayInstances(scene);
      raysBuiltFor_ = static_cast<int>(quality_);
    } else {
      raysBuiltFor_ = -1;  // not kept up to date now: rebuild if a quality needs it
    }
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
    instBox_.clear(), instHidden_.clear();
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
      // where it is, for skipping what a pass can't see; scaled to nothing = hidden
      instBox_.push_back(meshes_[in.mesh].box.transformed(in.transform));
      instHidden_.push_back(std::fabs(in.transform.linear().determinant()) < 1e-12f ? 1 : 0);
    }
    if (ensureStorage(instancesBuf_, instancesCap_, sizeof(InstanceU), std::max<size_t>(1, data.size()))) bindGroupsDirty_ = true;
    if (!data.empty()) queue_.WriteBuffer(instancesBuf_, 0, data.data(), data.size() * sizeof(InstanceU));
  }

  // The passes the settings turn off, as the old skip bits (plus the tests' own).
  int skipBits() const {
    const Config::Render& r = config().render;
    return g_skipPasses | (r.sunShadows ? 0 : 1) | (r.passScene ? 0 : 2) | (r.passExposure ? 0 : 4) | (r.passBloom ? 0 : 8) | (r.passFilm ? 0 : 16) |
           (r.passShadow ? 0 : 32) | (r.passDraw ? 0 : 64) | (r.lampShadows ? 0 : 512);
  }

  bool draw(const Camera& cam, const Look& look, float dt, float time, float fade) override {
    const int skip = skipBits();
    const Config& C = config();
    // at most two frames waiting on the GPU: a slow GPU drops frames instead
    // of falling further and further behind
    if (gs_->lost) return false;
    double now = emscripten_get_now();
    if (fresh_) {
      // the first frame after a new scene or quality: the main thread has just
      // been busy building, so older frames' "done" signals arrive late and
      // would read as a slow GPU. Forget them and measure from here.
      fresh_ = false;
      ++gs_->gate;
      gs_->inFlight = 0;
      gs_->lastProgress = now;
      gs_->gpuSum = 0, gs_->gpuCount = 0;
      judgedAt_ = now, perfSkipped_ = perfDrawn_ = 0;
    }
    adaptResolution(now);
    if (gs_->inFlight >= std::clamp(C.render.framesInFlight, 1, 3)) {
      ++perfSkipped_;
      // a GPU that hasn't said "done" for 2 seconds lost the signal: stop
      // waiting, and ignore those frames' signals if they turn up later
      // (counting them would let more and more frames pile up on the GPU)
      if (now - gs_->lastProgress > 2000) {
        ++gs_->gate;
        gs_->inFlight = 0;
      } else {
        return false;
      }
    }
    if (gs_->inFlight == 0) gs_->lastProgress = now;
    ++perfDrawn_;
    resize();
    wgpu::SurfaceTexture st;
    if (!(skip & 256)) {  // 256: off-screen only (tests in browsers that can't show WebGPU)
      surface_.GetCurrentTexture(&st);
      if (st.status != wgpu::SurfaceGetCurrentTextureStatus::SuccessOptimal && st.status != wgpu::SurfaceGetCurrentTextureStatus::SuccessSuboptimal) {
        outW_ = 0;  // the surface needs configuring again
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
    Mat4 proj = Mat4::perspective(fovY, aspect, 0.05f, std::max(C.world.drawDistance, 1.0f));
    f.viewProj = proj * view;
    f.invViewProj = f.viewProj.inverse();
    // the sun's shadow: a box round what the camera sees nearby, snapped to texels
    float R = C.render.shadowDistance > 0 ? C.render.shadowDistance : quality_ == Quality::kPhone ? 25.0f : 40.0f;
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
    f.sunColor = {sunIllum_, C.render.skyLight};
    for (int i = 0; i < 9; ++i) f.sh[i] = {sh_[i], 0};
    f.sh[0].w = C.render.shadowSoftness, f.sh[1].w = C.world.patternFade;
    f.fog = {fog_.density, fog_.top, fog_.anisotropy, fog_.density > 0 ? 1.0f : 0.0f};
    f.fogColor = {fog_.albedo, C.render.shadowBias};
    f.params = {static_cast<float>(lightCount()), (skip & 512) ? 0.0f : static_cast<float>(shadowLamps_), static_cast<float>(shadowSize_), cam.autoExposure ? 1.0f : 0.0f};
    f.screen = {static_cast<float>(width_), static_cast<float>(height_), 1.0f / width_, 1.0f / height_};
    // a first guess for the eyes: the camera's own settings, or a sunny / indoor guess
    float guess = cam.autoExposure ? (sunOn_ && sunLux_ > 1000 ? 1.0f / 6000 : 1.0f / 30) : exposureFromEv100(cam.ev100());
    // lamp shadows: rays for the big soft lights (one for a small bulb)
    f.fixedExposure = {guess * std::exp2(0.0f), static_cast<float>(shadowRays_), static_cast<float>(C.render.view), C.render.occlusion};
    queue_.WriteBuffer(frameUbo_, 0, &f, sizeof f);

    PostU p{};
    float fixedExp = exposureFromEv100(cam.ev100());
    p.exposure = {fixedExp, cam.exposureCompensation, cam.autoExposure ? 1.0f : 0.0f, look.sharpen};
    p.look0 = {look.contrast, look.saturation, static_cast<float>(static_cast<int>(look.tonemap)), look.monochrome ? 1.0f : 0.0f};
    Vec3 wb = whiteBalanceGains(look.temperature, look.tint);
    p.wb = {wb, look.distortion};
    p.lift = {look.lift, look.grainSize}, p.gamma = {look.gamma, look.bloomRadius / 0.02f}, p.gain = {look.gain, C.render.fxaa ? 1.0f : 0.0f};
    p.shadowTint = {look.shadowTint, 0}, p.highlightTint = {look.highlightTint, 0};
    // film grain only when asked for live: re-rolled every frame on a small phone
    // canvas it reads as fizz, not film (stills always keep it)
    p.fx = {look.bloom, look.vignette, C.render.filmGrain ? look.grain : 0.0f, look.letterbox};
    p.fx2 = {look.blackLevel, time, look.halation, look.chromaticAberration};
    p.screen = {static_cast<float>(outW_), static_cast<float>(outH_), fade, static_cast<float>(C.render.view)};
    queue_.WriteBuffer(postUbo_, 0, &p, sizeof p);
    // eyes adapt between the scene brightnesses cam.minEv .. cam.maxEv (sunlight
    // down to a candle-lit room), at cam.adaptSpeed
    ExposureU e{{static_cast<float>(width_), static_cast<float>(height_), dt, f.fixedExposure.x},
                {exposureFromEv100(C.camera.maxEv), exposureFromEv100(C.camera.minEv), 3.0f * C.camera.adaptSpeed, 1.2f * C.camera.adaptSpeed}};
    queue_.WriteBuffer(expUbo_, 0, &e, sizeof e);
    lastExposure_ = fixedExp;

    wgpu::CommandEncoder enc = device_.CreateCommandEncoder();
    bool raster = quality_ != Quality::kUltra;
    bool ultra = !raster && rayTris_ && accumBuf_ && ultraGroup_;
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
      const Config::Ultra& U = C.ultra;
      u.info[0] = ultraFrame_, u.info[1] = width_, u.info[2] = height_, u.info[3] = static_cast<uint32_t>(std::clamp(U.bounces, 1, 16));
      u.extra[0] = ultraScale_, u.extra[1] = static_cast<uint32_t>(lightCount());
      u.extra[2] = static_cast<uint32_t>(std::clamp(U.history, 0, 65535)) | (static_cast<uint32_t>(std::clamp(U.clamp, 0.5f, 600.0f) * 100) << 16);
      u.extra[3] = (U.glossy ? 1u : 0u) | (U.glass ? 2u : 0u) | (U.dof ? 4u : 0u) | (U.sunShadows ? 8u : 0u) | (U.lampShadows ? 16u : 0u) |
                   (static_cast<uint32_t>(std::clamp(U.spp, 1, 16)) << 8);
      queue_.WriteBuffer(ultraUbo_, 0, &u, sizeof u);
      ++ultraFrame_;
    }
    // ---- what each pass draws: the copies its view can see
    if (raster) {
      buildDrawLists(Frustum::fromViewProj(f.viewProj), Frustum::fromViewProj(f.shadowViewProj), sunOn_ && !(skip & 1), cam.position,
                     static_cast<float>(height_) / (2 * std::tan(cam.fovY() * 0.5f)));
      if (bindGroupsDirty_) makeBindGroups();
    }
    // ---- 1. sun shadow
    if (raster && !(skip & 32)) {
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
      if (sunOn_ && !(skip & 1)) {
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
      // ultra.denoise off: one pass that only writes the picture
      const int passes = C.ultra.denoise ? std::clamp(C.ultra.denoisePasses, 1, 4) : 1;
      for (int k = 0; k < passes; ++k) {
        uint32_t prm[12] = {bw, bh, k == 0 ? 0u : (1u << (k - 1)), k == passes - 1 ? 1u : 0u, width_, height_, ultraScale_, ultraFrame_, 0, 0, 0, 0};
        float fe[2] = {f.fixedExposure.x, cam.autoExposure ? 1.0f : 0.0f};
        std::memcpy(&prm[8], fe, 8);
        queue_.WriteBuffer(denoiseUbo_[k], 0, prm, sizeof prm);
        cp.SetBindGroup(0, denoiseGroup_[k]);
        cp.DispatchWorkgroups((bw + 7) / 8, (bh + 7) / 8);
      }
      cp.End();
    }
    // ---- 2. the scene, in light (HDR), 4x multisampled
    if (raster && !(skip & 64)) {
      wgpu::RenderPassColorAttachment ca;
      ca.view = g_samples > 1 ? msaaView_ : hdrView_;
      if (g_samples > 1) ca.resolveTarget = hdrView_;
      ca.loadOp = wgpu::LoadOp::Clear;
      // the 4 samples per pixel are only needed until they're averaged into
      // the picture: keeping them would write 4 pictures' worth to memory
      ca.storeOp = g_samples > 1 ? wgpu::StoreOp::Discard : wgpu::StoreOp::Store;
      ca.clearValue = {0, 0, 0, 1};
      wgpu::RenderPassDepthStencilAttachment da;
      da.view = depthView_;
      da.depthLoadOp = wgpu::LoadOp::Clear;
      da.depthStoreOp = wgpu::StoreOp::Discard;
      da.depthClearValue = 1;
      wgpu::RenderPassDescriptor rp;
      rp.colorAttachmentCount = 1;
      rp.colorAttachments = &ca;
      if (!(skip & 128)) rp.depthStencilAttachment = &da;
      wgpu::RenderPassEncoder pass = enc.BeginRenderPass(&rp);
      pass.SetViewport(0, 0, static_cast<float>(width_), static_cast<float>(height_), 0, 1);
      pass.SetBindGroup(0, sceneGroup0_);
      pass.SetBindGroup(1, sceneGroup1_);
      pass.SetBindGroup(2, rayGroup_);
      if (!(skip & 2)) {
        int rays = shadowLamps_ > 0 && !(skip & 512) ? 1 : 0;
        // the depth pre-pass (render.prepass; automatic: when many lamps light
        // each pixel, see prepassOn): every pixel is lit once, not once per
        // surface drawn over it. Old note, before @invariant positions:
        // no shadow rays, but it needs both passes to land on exactly the same
        // depth, which Apple GPUs don't promise, and it measured no faster
        if (prepassOn(C.render.prepass, static_cast<int>(lightCount()))) {
          pass.SetPipeline(depthPipe_);
          drawBatches(pass, false, false);
          pass.SetPipeline(opaqueAfterDepthPipe_[rays]);
        } else {
          pass.SetPipeline(opaquePipe_[rays]);
        }
        drawBatches(pass, false, false);
        pass.SetPipeline(skyPipe_);
        pass.Draw(3);
        pass.SetPipeline(glassPipe_[rays]);
        drawBatches(pass, true, false);
      }
      pass.End();
    }
    // ---- 3. how bright it is: the eyes adjust
    if (!(skip & 4)) {
      wgpu::ComputePassEncoder cp = enc.BeginComputePass();
      cp.SetPipeline(exposurePipe_);
      cp.SetBindGroup(0, exposureGroup_);
      cp.DispatchWorkgroups(1);
      cp.End();
    }
    // ---- 4. bloom: down the chain, then back up, adding
    if (passUniformsDirty_) writePassUniforms();
    if (!(skip & 8)) {
      for (int i = 0; i < kBloomLevels; ++i) postPass(enc, bloomViews_[i], downPipe_, downGroups_[i], false, levelW(i), levelH(i));
      for (int i = kBloomLevels - 2; i >= 0; --i) postPass(enc, bloomViews_[i], upPipe_, upGroups_[i], true, levelW(i), levelH(i));
    }
    // ---- 5. the film, onto the screen
    if (!(skip & (16 | 256))) postPass(enc, st.texture.CreateView(), finalPipe_, finalGroup_, false);
    // a capture: the same film pass into a texture the page reads back
    wgpu::Texture captureTex;
    if (captureDone_) {
      wgpu::TextureDescriptor td;
      td.size = {outW_, outH_, 1};
      td.format = format_;
      td.usage = wgpu::TextureUsage::RenderAttachment | wgpu::TextureUsage::CopySrc;
      captureTex = device_.CreateTexture(&td);
      postPass(enc, captureTex.CreateView(), finalPipe_, finalGroup_, false);
    }
    wgpu::CommandBuffer cb = enc.Finish();
    queue_.Submit(1, &cb);
    ++gs_->inFlight;
    // GPU time per frame, estimated from when the GPU says it's done: from
    // submit (or from the end of the frame before, if it was still busy)
    double submitted = emscripten_get_now();
    queue_.OnSubmittedWorkDone(wgpu::CallbackMode::AllowSpontaneous,
                               [gs = gs_, gate = gs_->gate, submitted](wgpu::QueueWorkDoneStatus status, wgpu::StringView) {
                                 if (gate != gs->gate || status != wgpu::QueueWorkDoneStatus::Success) return;
                                 --gs->inFlight;
                                 double done = emscripten_get_now();
                                 double ms = done - std::max(submitted, gs->lastDone);
                                 gs->lastDone = gs->lastProgress = done;
                                 gs->lastGpuMs = static_cast<float>(ms);
                                 gs->gpuMs += (gs->lastGpuMs - gs->gpuMs) * 0.1f;
                                 gs->gpuSum += ms, ++gs->gpuCount;
                               });
    if (captureTex) {
      // the page copies and reads it with the browser's own WebGPU calls
      captureDone_ = nullptr;
      jsCapture(device_.Get(), captureTex.Get(), static_cast<int>(outW_), static_cast<int>(outH_), format_ == wgpu::TextureFormat::BGRA8Unorm ? 1 : 0);
    }
    ++presented_;
    return true;
  }

  uint64_t presentedFrames() const override { return presented_; }
  float gpuMs() const override { return gs_->gpuMs; }
  float lastGpuMs() const override { return gs_->lastGpuMs; }
  size_t gpuBytes() const override {
    auto buf = [](const wgpu::Buffer& b) -> size_t { return b ? static_cast<size_t>(b.GetSize()) : 0; };
    auto tex = [](const wgpu::Texture& t) -> size_t {
      if (!t) return 0;
      size_t texel = 4;
      switch (t.GetFormat()) {
        case wgpu::TextureFormat::RGBA16Float: texel = 8; break;
        case wgpu::TextureFormat::R8Unorm: texel = 1; break;
        default: break;
      }
      size_t bytes = 0, w = t.GetWidth(), h = t.GetHeight();
      for (uint32_t m = 0; m < t.GetMipLevelCount(); ++m, w = std::max<size_t>(1, w / 2), h = std::max<size_t>(1, h / 2)) bytes += w * h * texel;
      return bytes * t.GetDepthOrArrayLayers() * t.GetSampleCount();
    };
    size_t n = 0;
    for (const GpuMesh& m : meshes_) n += buf(m.vb) + buf(m.ib);
    for (const wgpu::Buffer* b : {&frameUbo_, &postUbo_, &expUbo_, &lightsBuf_, &materialsBuf_, &instancesBuf_, &exposureBuf_, &visibleBuf_, &ultraUbo_, &accumBuf_,
                                  &denoiseA_, &denoiseB_, &rayTris_, &rayBlas_, &rayInst_, &rayTlas_})
      n += buf(*b);
    for (const wgpu::Buffer& b : passUbo_) n += buf(b);
    for (const wgpu::Buffer& b : denoiseUbo_) n += buf(b);
    for (const wgpu::Texture* t : {&modelTex_, &shadowTex_, &shadowDummyTex_, &skyTex_, &msaaTex_, &depthTex_, &hdrTex_}) n += tex(*t);
    for (const wgpu::Texture& t : bloomTex_) n += tex(t);
    return n;
  }
  float renderScale() const override { return renderScale_; }

  // Automatic resolution, judged about once a second of real time.
  //
  // It must not depend on frames arriving: when the GPU is overloaded the
  // browser stops asking for frames at all, so "frames we had to skip" never
  // shows up and a clock that adds up frame times barely moves. (That was the
  // bug that froze every quality: the one control meant to rescue a slow GPU
  // waited for a signal that can't come while it's needed.) So it keeps real
  // time and also reads how long the GPU actually took per frame.
  //
  // Too slow (the GPU needs more than ~18 ms a frame, or fewer than 88% of
  // the frames we aim for, up to 60 a second, reached the screen): jump down
  // to the size that should fit, at once. Calm for 3 s, and the next size up
  // should still fit: a step up, but not straight back to a size that was too
  // slow; after 20 calm seconds that limit lifts a step. A change costs
  // nothing: the targets stay, only the part of them drawn changes.
  void adaptResolution(double now) {
    static constexpr float kSteps[] = {1.0f, 0.87f, 0.75f, 0.65f, 0.56f, 0.5f, 0.42f, 0.35f};
    const Config::Render& cr = config().render;
    if (cr.res > 0) {
      renderScale_ = std::clamp(cr.res, 0.2f, 1.0f);
      return;
    }
    if (judgedAt_ == 0) judgedAt_ = now;
    double secs = (now - judgedAt_) / 1000.0;
    // render.floor / floorUltra: the smallest size it may drop to
    float floorScale = quality_ == Quality::kUltra ? cr.floorUltra : cr.floor;
    int lowest = 0;
    while (lowest < 7 && kSteps[lowest + 1] >= floorScale - 1e-3f) ++lowest;
    // a GPU that hasn't finished a single frame for 2 s: smallest size now
    bool stuck = gs_->gpuCount == 0 && gs_->inFlight > 0 && now - gs_->lastProgress > 2000;
    if (secs < 1.0 && !stuck) return;
    int asked = perfSkipped_ + perfDrawn_;
    float fps = static_cast<float>(perfDrawn_ / std::max(secs, 1e-3));
    // the screen's own rate, from the calmest moments (the browser asks for
    // fewer frames when the GPU is swamped, so this second's ask can't be the aim)
    displayHz_ = std::max(displayHz_, std::min(240.0f, static_cast<float>(asked / std::max(secs, 1e-3))));
    float aim = std::min(displayHz_, static_cast<float>(cr.targetFps));
    float gpu = gs_->gpuCount ? static_cast<float>(gs_->gpuSum / gs_->gpuCount) : (gs_->inFlight > 0 ? static_cast<float>(now - gs_->lastProgress) : 0.0f);
    // the GPU is what holds frames back when its time per frame fills most of
    // the gap between frames (not a busy script, not a phone saving power)
    float interval = static_cast<float>(secs * 1000 / std::max(perfDrawn_, 1));
    bool gpuBound = gs_->gpuCount ? gpu >= interval * 0.75f : gs_->inFlight > 0;
    perfSkipped_ = perfDrawn_ = 0;
    gs_->gpuSum = 0, gs_->gpuCount = 0;
    judgedAt_ = now;
    sinceScale_ += static_cast<float>(secs);
    const float budget = 1000.0f / std::max(cr.targetFps, 1);
    auto stepFor = [&](float scale) {  // the first step at or below this scale
      int k = 0;
      while (k < lowest && kSteps[k] > scale + 1e-3f) ++k;
      return k;
    };
    bool slowGpu = gpu > budget * cr.slowGpu / 100;
    if (stuck || (gpuBound && fps < aim * 0.88f)) {
      if (step_ < lowest && (sinceScale_ > 1.2f || stuck || gpu > budget * 2)) {
        // cost goes with the number of pixels, the square of the scale
        float fit = slowGpu ? kSteps[step_] * std::sqrt(budget * 0.85f / gpu) : kSteps[step_] * 0.9f;
        int to = stuck ? lowest : std::max(step_ + 1, stepFor(fit));
        ceilStep_ = std::max(ceilStep_, step_ + 1);  // this size was too much: don't come straight back
        step_ = std::min(lowest, to);
        calmSeconds_ = longCalm_ = 0, sinceScale_ = 0;
      }
    } else if (fps >= aim * 0.97f) {
      ++calmSeconds_, ++longCalm_;
      float up = step_ > 0 ? kSteps[step_ - 1] / kSteps[step_] : 1.0f;
      bool fits = gpu * up * up < budget * cr.climbUnder / 100;
      if (calmSeconds_ >= cr.calm && step_ > ceilStep_ && fits) --step_, calmSeconds_ = 0, sinceScale_ = 0;
      // Ultra restarts its picture on every change: it doesn't go looking for more
      if (longCalm_ >= 20 && ceilStep_ > 0 && quality_ != Quality::kUltra) --ceilStep_, longCalm_ = 0;
    } else {
      calmSeconds_ = 0;
    }
    step_ = std::min(step_, lowest);
    renderScale_ = kSteps[step_];
  }

  // render.maxLights: the brightest (they go first, see uploadLights) are kept
  size_t lightCount() const { return std::min(lights_.size(), static_cast<size_t>(std::clamp(config().render.maxLights, 1, kMaxLights))); }

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
        sunRadius_ = std::fabs(l.radius - 0.02f) < 1e-6f ? 0.00465f : std::clamp(l.radius, 0.001f, 0.2f);  // 0.02: the default, never set
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
      // panels go last: they cast no ray shadows, so the shader's few shadow
      // rays go to the bulbs, lanterns and fires
      if (L.dir.w > 1.5f) {
        rank[i] = {-1.0f / (1.0f + dot(d, d)), static_cast<uint32_t>(i)};
        continue;
      }
      rank[i] = {luminance(L.color.xyz()) / std::max(dot(d, d), 0.25f), static_cast<uint32_t>(i)};
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
    if (b) b.Destroy();
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
    g.box = m.bounds();
    return g;
  }

  // What each pass draws this frame: only the copies its view can see
  // (render.cull), in the batches' order; `first` of a draw is where its
  // stretch starts in visibleIds. The sun's pass never draws glass.
  // A mesh with simpler versions (Scene::lods) is drawn as the version its
  // size on screen asks for (lodLevel, shared with `loop budget`); the sun's
  // pass draws one version simpler (shadowLodLevel).
  int lodOf(int mesh, int level) const {
    if (level <= 0 || mesh < 0 || mesh >= static_cast<int>(meshLods_.size())) return mesh;
    const auto& l = meshLods_[mesh];
    int m = level >= 2 && l[1] >= 0 ? l[1] : l[0];
    return m >= 0 && m < static_cast<int>(meshes_.size()) && meshes_[m].indexCount ? m : mesh;
  }

  void buildDrawLists(const Frustum& eye, const Frustum& sun, bool sunOn, Vec3 eyePos, float pixelsPerRadian) {
    const bool cull = config().render.cull;
    const float bias = config().world.lodBias;
    visible_.clear(), sceneDraws_.clear(), shadowDraws_.clear();
    auto list = [&](std::vector<Batch>& out, const Frustum& view, bool shadow) {
      for (const Batch& b : batches_) {
        if (shadow && b.glass) continue;
        if (b.mesh < 0 || b.mesh >= static_cast<int>(meshes_.size()) || !meshes_[b.mesh].indexCount) continue;
        auto sees = [&](uint32_t i) { return !cull || (i < instHidden_.size() && !instHidden_[i] && view.visible(instBox_[i])); };
        if (b.mesh >= static_cast<int>(meshLods_.size()) || meshLods_[b.mesh][0] < 0) {
          uint32_t start = static_cast<uint32_t>(visible_.size());
          for (uint32_t i = b.first; i < b.first + b.count; ++i)
            if (sees(i)) visible_.push_back(i);
          uint32_t n = static_cast<uint32_t>(visible_.size()) - start;
          if (n) out.push_back({b.mesh, start, n, b.glass});
          continue;
        }
        for (auto& v : lodScratch_) v.clear();
        for (uint32_t i = b.first; i < b.first + b.count; ++i)
          if (sees(i) && i < instBox_.size()) {
            int level = lodLevel(radiusPixels(instBox_[i], eyePos, pixelsPerRadian), bias);
            lodScratch_[shadow ? shadowLodLevel(level) : level].push_back(i);
          }
        for (int level = 0; level < 3; ++level) {
          if (lodScratch_[level].empty()) continue;
          uint32_t start = static_cast<uint32_t>(visible_.size());
          visible_.insert(visible_.end(), lodScratch_[level].begin(), lodScratch_[level].end());
          out.push_back({lodOf(b.mesh, level), start, static_cast<uint32_t>(lodScratch_[level].size()), b.glass});
        }
      }
    };
    list(sceneDraws_, eye, false);
    if (sunOn) list(shadowDraws_, sun, true);
    if (visible_.empty()) visible_.push_back(0);
    ensureStorage(visibleBuf_, visibleCap_, sizeof(uint32_t), visible_.size());
    queue_.WriteBuffer(visibleBuf_, 0, visible_.data(), visible_.size() * sizeof(uint32_t));
  }

  void drawBatches(wgpu::RenderPassEncoder& pass, bool glass, bool shadow) {
    for (const Batch& b : shadow ? shadowDraws_ : sceneDraws_) {
      if (b.glass != glass && !shadow) continue;
      const GpuMesh& m = meshes_[b.mesh];
      if (!m.indexCount) continue;
      pass.SetVertexBuffer(0, m.vb);
      pass.SetIndexBuffer(m.ib, wgpu::IndexFormat::Uint32);
      pass.DrawIndexed(m.indexCount, b.count, 0, 0, b.first);
      ++drawCalls_;
    }
  }

  void postPass(wgpu::CommandEncoder& enc, wgpu::TextureView target, wgpu::RenderPipeline& pipe, wgpu::BindGroup& group, bool load, uint32_t vw = 0,
                uint32_t vh = 0) {
    wgpu::RenderPassColorAttachment ca;
    ca.view = target;
    ca.loadOp = load ? wgpu::LoadOp::Load : wgpu::LoadOp::Clear;
    ca.storeOp = wgpu::StoreOp::Store;
    ca.clearValue = {0, 0, 0, 1};
    wgpu::RenderPassDescriptor rp;
    rp.colorAttachmentCount = 1;
    rp.colorAttachments = &ca;
    wgpu::RenderPassEncoder pass = enc.BeginRenderPass(&rp);
    if (vw && vh) pass.SetViewport(0, 0, static_cast<float>(vw), static_cast<float>(vh), 0, 1);
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
    scene0Layout_ = layout({ubo(0, VFC), ssbo(1, VFC), ssbo(2, VFC), ssbo(3, VF), ssbo(4, F | wgpu::ShaderStage::Compute), ssbo(5, wgpu::ShaderStage::Vertex)});
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
    wgpu::BindGroupLayoutEntry arr = tex(4, F);
    arr.texture.viewDimension = wgpu::TextureViewDimension::e2DArray;
    scene1Layout_ = layout({tex(0, F, true), samp(1, F, true), tex(2, F), samp(3, F), arr, samp(5, F)});
    postLayout_ = layout({ubo(0, F), tex(1, F), tex(2, F), samp(3, F), ssbo(4, F), ubo(5, F)});
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

    auto scenePipe = [&](const char* fs, bool blend, bool writeDepth, bool sky, wgpu::CompareFunction cmp = wgpu::CompareFunction::Less, bool color = true,
                         int lampShadows = -1) {
      wgpu::RenderPipelineDescriptor d;
      d.layout = sceneL;
      d.vertex.module = sm;
      d.vertex.entryPoint = sky ? "vsSky" : "vs";
      if (!sky) d.vertex.bufferCount = 1, d.vertex.buffers = &vbl;
      wgpu::ColorTargetState ct;
      ct.format = wgpu::TextureFormat::RGBA16Float;
      if (!color) ct.writeMask = wgpu::ColorWriteMask::None;
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
      wgpu::ConstantEntry ce;
      if (lampShadows >= 0) {
        ce.key = "kLampShadows";
        ce.value = lampShadows;
        fst.constantCount = 1;
        fst.constants = &ce;
      }
      d.fragment = &fst;
      d.primitive.cullMode = wgpu::CullMode::None;
      wgpu::DepthStencilState ds;
      ds.format = wgpu::TextureFormat::Depth32Float;
      ds.depthWriteEnabled = writeDepth;
      ds.depthCompare = sky ? wgpu::CompareFunction::LessEqual : cmp;
      d.depthStencil = &ds;
      d.multisample.count = static_cast<uint32_t>(g_samples);
      return device_.CreateRenderPipeline(&d);
    };
    const auto L = wgpu::CompareFunction::Less, E = wgpu::CompareFunction::Equal;
    // with the depth pre-pass: depth first, then light only the nearest surface;
    // [1] with lamp shadows, [0] without (the ray code compiled out)
    depthPipe_ = scenePipe("fsDepth", false, true, false, L, false);
    for (int r = 0; r < 2; ++r) {
      opaquePipe_[r] = scenePipe("fsOpaque", false, true, false, L, true, r);
      opaqueAfterDepthPipe_[r] = scenePipe("fsOpaque", false, false, false, E, true, r);
      glassPipe_[r] = scenePipe("fsGlass", true, false, false, L, true, r);
    }
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
    if (shadowTex_) shadowTex_.Destroy();
    if (shadowDummyTex_) shadowDummyTex_.Destroy();
    wgpu::TextureDescriptor td;
    td.size = {static_cast<uint32_t>(shadowSize_), static_cast<uint32_t>(shadowSize_), 1};
    td.format = wgpu::TextureFormat::Depth24Plus;
    td.usage = wgpu::TextureUsage::RenderAttachment | wgpu::TextureUsage::TextureBinding;
    shadowTex_ = device_.CreateTexture(&td);
    shadowView_ = shadowTex_.CreateView();
    td.format = wgpu::TextureFormat::R8Unorm;
    td.usage = wgpu::TextureUsage::RenderAttachment;
    shadowDummyTex_ = device_.CreateTexture(&td);
    shadowDummy_ = shadowDummyTex_.CreateView();
    bindGroupsDirty_ = true;
  }

  // Every model picture onto its own layer of one texture stack, resized to
  // render.textureSize square (a square holds any picture: models read it by
  // 0..1 coordinates), with each smaller copy averaged from the one above.
  void uploadModelTextures(const Scene& scene) {
    int S = std::clamp(config().render.textureSize, 16, 2048);
    S = std::min<int>(S, static_cast<int>(maxTexture_));
    uint32_t n = static_cast<uint32_t>(std::min<size_t>(scene.textures.size(), maxLayers_));
    if (scene.textures.size() > n) reportGpuError("more model pictures than this GPU can hold; the rest show plain colours");
    texLayer_.assign(scene.textures.size(), -1);
    int levels = 1;
    while ((S >> levels) >= 1 && levels < 12) ++levels;
    wgpu::TextureDescriptor td;
    td.size = {static_cast<uint32_t>(S), static_cast<uint32_t>(S), std::max(n, 1u)};
    td.format = wgpu::TextureFormat::RGBA8Unorm;
    td.usage = wgpu::TextureUsage::TextureBinding | wgpu::TextureUsage::CopyDst;
    td.mipLevelCount = static_cast<uint32_t>(levels);
    if (modelTex_) modelTex_.Destroy();
    modelTex_ = device_.CreateTexture(&td);
    std::vector<uint8_t> level(static_cast<size_t>(S) * S * 4), next;
    for (uint32_t layer = 0; layer < n; ++layer) {
      const Texture& t = scene.textures[layer];
      texLayer_[layer] = static_cast<int>(layer);
      for (int y = 0; y < S; ++y)
        for (int x = 0; x < S; ++x) {
          uint8_t* d = &level[(static_cast<size_t>(y) * S + x) * 4];
          if (t.empty()) { d[0] = d[1] = d[2] = d[3] = 255; continue; }
          // the average of the source pixels under this one (box filter)
          int x0 = x * t.width / S, x1 = std::max(x0 + 1, (x + 1) * t.width / S);
          int y0 = y * t.height / S, y1 = std::max(y0 + 1, (y + 1) * t.height / S);
          uint32_t acc[4] = {0, 0, 0, 0}, cnt = 0;
          for (int sy = y0; sy < y1; ++sy)
            for (int sx = x0; sx < x1; ++sx) {
              const uint8_t* s = &t.rgba[(static_cast<size_t>(sy) * t.width + sx) * 4];
              for (int c = 0; c < 4; ++c) acc[c] += s[c];
              ++cnt;
            }
          for (int c = 0; c < 4; ++c) d[c] = static_cast<uint8_t>(acc[c] / cnt);
        }
      int w = S;
      std::vector<uint8_t>* cur = &level;
      std::vector<uint8_t> work = level;
      for (int l = 0; l < levels; ++l) {
        wgpu::TexelCopyTextureInfo dst;
        dst.texture = modelTex_;
        dst.mipLevel = static_cast<uint32_t>(l);
        dst.origin = {0, 0, layer};
        wgpu::TexelCopyBufferLayout lay;
        lay.bytesPerRow = static_cast<uint32_t>(w * 4);
        lay.rowsPerImage = static_cast<uint32_t>(w);
        wgpu::Extent3D ext{static_cast<uint32_t>(w), static_cast<uint32_t>(w), 1};
        queue_.WriteTexture(&dst, cur->data(), static_cast<size_t>(w) * w * 4, &lay, &ext);
        if (w == 1) break;
        int h = w / 2;
        next.assign(static_cast<size_t>(h) * h * 4, 0);
        for (int y = 0; y < h; ++y)
          for (int x = 0; x < h; ++x)
            for (int c = 0; c < 4; ++c) {
              auto at = [&](int xx, int yy) { return static_cast<int>((*cur)[(static_cast<size_t>(yy) * w + xx) * 4 + c]); };
              next[(static_cast<size_t>(y) * h + x) * 4 + c] = static_cast<uint8_t>((at(2 * x, 2 * y) + at(2 * x + 1, 2 * y) + at(2 * x, 2 * y + 1) + at(2 * x + 1, 2 * y + 1) + 2) / 4);
            }
        work.swap(next);
        cur = &work;
        w = h;
      }
    }
    wgpu::TextureViewDescriptor vd;
    vd.dimension = wgpu::TextureViewDimension::e2DArray;
    modelView_ = modelTex_.CreateView(&vd);
    texUploaded_ = scene.textures.size();
    texSize_ = std::clamp(config().render.textureSize, 16, 2048);
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
    if (skyTex_) skyTex_.Destroy();
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

  // The canvas and the full-size targets follow the window (re-made only when
  // it changes size, or the quality changes); the part of them drawn each
  // frame follows the automatic resolution, which costs nothing to change.
  void resize() {
    double cw = 0, ch = 0;
    emscripten_get_element_css_size(canvas_.c_str(), &cw, &ch);
    ratio_ = std::min(maxRatio_, static_cast<float>(emscripten_get_device_pixel_ratio()));
    uint32_t ow = std::min(maxTexture_, static_cast<uint32_t>(std::max(1.0, cw * ratio_)));
    uint32_t oh = std::min(maxTexture_, static_cast<uint32_t>(std::max(1.0, ch * ratio_)));
    if (ow != outW_ || oh != outH_) makeTargets(ow, oh);
    uint32_t w = std::clamp<uint32_t>(static_cast<uint32_t>(std::lround(ow * renderScale_)), 1, ow);
    uint32_t h = std::clamp<uint32_t>(static_cast<uint32_t>(std::lround(oh * renderScale_)), 1, oh);
    if (w != width_ || h != height_) {
      width_ = w, height_ = h;
      ultraFrame_ = 0;  // Ultra's averages are per pixel: start afresh
      passUniformsDirty_ = true;
    }
  }

  static size_t blocks(uint32_t w, uint32_t h, uint32_t b) { return static_cast<size_t>((w + b - 1) / b) * ((h + b - 1) / b); }

  void makeTargets(uint32_t ow, uint32_t oh) {
    outW_ = ow, outH_ = oh;
    emscripten_set_canvas_element_size(canvas_.c_str(), static_cast<int>(ow), static_cast<int>(oh));
    wgpu::SurfaceConfiguration c;
    c.device = device_;
    c.format = format_;
    c.width = ow, c.height = oh;
    c.alphaMode = wgpu::CompositeAlphaMode::Opaque;
    c.presentMode = wgpu::PresentMode::Fifo;
    if (!(g_skipPasses & 256)) surface_.Configure(&c);
    // the old targets' memory goes back now (the browser would otherwise keep
    // it until it next collects garbage, and big screens use hundreds of MB)
    auto drop = [](wgpu::Texture& t) {
      if (t) t.Destroy();
      t = nullptr;
    };
    auto dropBuf = [](wgpu::Buffer& b) {
      if (b) b.Destroy();
      b = nullptr;
    };
    drop(msaaTex_), drop(depthTex_), drop(hdrTex_);
    for (auto& t : bloomTex_) drop(t);
    dropBuf(accumBuf_), dropBuf(denoiseA_), dropBuf(denoiseB_);
    auto target = [&](uint32_t tw, uint32_t th, wgpu::TextureFormat f, uint32_t samples, wgpu::TextureUsage u, wgpu::Texture& keep) {
      wgpu::TextureDescriptor td;
      td.size = {std::max(1u, tw), std::max(1u, th), 1};
      td.format = f;
      td.sampleCount = samples;
      td.usage = u;
      keep = device_.CreateTexture(&td);
      return keep.CreateView();
    };
    const auto attach = wgpu::TextureUsage::RenderAttachment;
    if (quality_ != Quality::kUltra) {  // Ultra draws with rays: no triangles, no depth
      if (g_samples > 1) msaaView_ = target(ow, oh, wgpu::TextureFormat::RGBA16Float, static_cast<uint32_t>(g_samples), attach, msaaTex_);
      depthView_ = target(ow, oh, wgpu::TextureFormat::Depth32Float, static_cast<uint32_t>(g_samples), attach, depthTex_);
    }
    hdrView_ = target(ow, oh, wgpu::TextureFormat::RGBA16Float, 1, attach | wgpu::TextureUsage::TextureBinding | wgpu::TextureUsage::StorageBinding, hdrTex_);
    if (quality_ == Quality::kUltra) {
      // Ultra's running averages: one path per pixel, or per 2x2 block when
      // the screen is too big for what this GPU lets one buffer hold
      uint64_t limit = std::min(maxBinding_, maxBufferSize_);
      ultraScale_ = 1;
      while (ultraScale_ < 4 && blocks(ow, oh, ultraScale_) * 48 > limit) ++ultraScale_;
      size_t n = blocks(ow, oh, ultraScale_);
      accumBuf_ = buffer(n * 48, wgpu::BufferUsage::Storage);
      denoiseA_ = buffer(n * 16, wgpu::BufferUsage::Storage);
      denoiseB_ = buffer(n * 16, wgpu::BufferUsage::Storage);
    }
    for (int i = 0; i < kBloomLevels; ++i)
      bloomViews_[i] = target(ow >> (i + 1), oh >> (i + 1), wgpu::TextureFormat::RGBA16Float, 1, attach | wgpu::TextureUsage::TextureBinding, bloomTex_[i]);
    width_ = height_ = 0;  // the drawn part is worked out again
    ultraFrame_ = 0;
    bindGroupsDirty_ = true;
    passUniformsDirty_ = true;
  }

  // Bloom level i: its full size, and the part of it in use this frame.
  uint32_t levelW(int i) const { return std::max(1u, width_ >> (i + 1)); }
  uint32_t levelH(int i) const { return std::max(1u, height_ >> (i + 1)); }

  // Where each post pass finds its two pictures this frame.
  void writePassUniforms() {
    auto part = [](uint32_t vw, uint32_t vh, uint32_t aw, uint32_t ah) {
      aw = std::max(1u, aw), ah = std::max(1u, ah);
      return Vec4(static_cast<float>(vw) / aw, static_cast<float>(vh) / ah, (vw - 0.5f) / aw, (vh - 0.5f) / ah);
    };
    Vec4 hdr = part(width_, height_, outW_, outH_);
    Vec4 lv[kBloomLevels];
    for (int i = 0; i < kBloomLevels; ++i) lv[i] = part(levelW(i), levelH(i), outW_ >> (i + 1), outH_ >> (i + 1));
    Vec4 whole{1, 1, 1, 1};
    PassU u;
    for (int i = 0; i < kBloomLevels; ++i) {  // down: hdr (or the level above) into level i
      u.a = i == 0 ? hdr : lv[i - 1], u.b = whole;
      queue_.WriteBuffer(passUbo_[i], 0, &u, sizeof u);
    }
    for (int i = 0; i + 1 < kBloomLevels; ++i) {  // up: level i+1 added into level i
      u.a = lv[i + 1], u.b = hdr;
      queue_.WriteBuffer(passUbo_[kBloomLevels + i], 0, &u, sizeof u);
    }
    u.a = hdr, u.b = lv[0];  // the film: the picture and the bloom, onto the screen
    queue_.WriteBuffer(passUbo_[kFinalPass], 0, &u, sizeof u);
    passUniformsDirty_ = false;
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
    sceneGroup0_ = group(scene0Layout_, {bufE(0, frameUbo_), bufE(1, lightsBuf_), bufE(2, materialsBuf_), bufE(3, instancesBuf_), bufE(4, exposureBuf_), bufE(5, visibleBuf_)});
    if (!modelView_) uploadModelTextures(Scene());
    sceneGroup1_ = group(scene1Layout_, {texE(0, shadowView_), sampE(1, shadowSampler_), texE(2, skyView_), sampE(3, linear_), texE(4, modelView_), sampE(5, repeat_)});
    // each pass reads one texture and writes another; the unused second slot
    // gets a texture that isn't the one being written
    for (int i = 0; i < kBloomLevels - 1; ++i)
      upGroups_[i] = group(postLayout_, {bufE(0, postUbo_), texE(1, bloomViews_[i + 1]), texE(2, hdrView_), sampE(3, linear_), bufE(4, exposureBuf_),
                                         bufE(5, passUbo_[kBloomLevels + i])});
    for (int i = 0; i < kBloomLevels; ++i) {
      wgpu::TextureView src = i == 0 ? hdrView_ : bloomViews_[i - 1];
      downGroups_[i] = group(postLayout_, {bufE(0, postUbo_), texE(1, src), texE(2, skyView_), sampE(3, linear_), bufE(4, exposureBuf_), bufE(5, passUbo_[i])});
    }
    finalGroup_ = group(postLayout_, {bufE(0, postUbo_), texE(1, hdrView_), texE(2, bloomViews_[0]), sampE(3, linear_), bufE(4, exposureBuf_), bufE(5, passUbo_[kFinalPass])});
    exposureGroup_ = group(expLayout_, {bufE(0, expUbo_), texE(1, hdrView_), bufE(2, exposureBuf_)});
    if (!rayTris_) emptyRayScene();
    rayGroup_ = group(rayLayout_, {bufE(0, rayTris_), bufE(1, rayBlas_), bufE(2, rayInst_), bufE(3, rayTlas_)});
    if (!ultraUbo_) ultraUbo_ = buffer(sizeof(UltraU), wgpu::BufferUsage::Uniform | wgpu::BufferUsage::CopyDst);
    ultraGroup_ = nullptr;
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
  int shadowRays_ = 1, shadowLamps_ = 2;
  uint32_t width_ = 0, height_ = 0;   // the part drawn this frame
  uint32_t outW_ = 0, outH_ = 0;      // the canvas, and every full-size target
  uint64_t maxBinding_ = 128ull << 20, maxBufferSize_ = 256ull << 20;
  uint32_t maxTexture_ = 8192;
  float ratio_ = 1, maxRatio_ = 2;
  float sunRadius_ = 0.00465f;
  int shadowSize_ = 2048;
  Quality quality_ = Quality::kHigh;

  wgpu::Buffer frameUbo_, postUbo_, expUbo_, lightsBuf_, materialsBuf_, instancesBuf_, exposureBuf_;
  size_t materialsCap_ = 0, instancesCap_ = 0;
  wgpu::Sampler linear_, shadowSampler_, repeat_;
  // model pictures: one stack of square layers (render.textureSize), each
  // scene picture resized onto its layer, with smaller copies for distance
  wgpu::Texture modelTex_;
  wgpu::TextureView modelView_;
  std::vector<int> texLayer_;  // scene texture -> layer (-1: no room)
  size_t texUploaded_ = 0;
  int texSize_ = 0;
  uint32_t maxLayers_ = 256;
  wgpu::Texture shadowTex_, shadowDummyTex_, skyTex_, msaaTex_, depthTex_, hdrTex_, bloomTex_[kBloomLevels];
  wgpu::Buffer passUbo_[2 * kBloomLevels];
  bool passUniformsDirty_ = true;
  wgpu::TextureView shadowDummy_, shadowView_, skyView_, msaaView_, depthView_, hdrView_, bloomViews_[kBloomLevels];
  wgpu::BindGroupLayout scene0Layout_, scene1Layout_, postLayout_, expLayout_;
  wgpu::RenderPipeline opaquePipe_[2], depthPipe_, opaqueAfterDepthPipe_[2], glassPipe_[2], skyPipe_, shadowPipe_, downPipe_, upPipe_, finalPipe_;
  wgpu::ComputePipeline exposurePipe_;
  wgpu::BindGroup sceneGroup0_, sceneGroup1_, downGroups_[kBloomLevels], upGroups_[kBloomLevels], finalGroup_, exposureGroup_;
  bool bindGroupsDirty_ = true, skyBuilt_ = false;

  std::vector<GpuMesh> meshes_;
  std::vector<Batch> batches_;
  std::vector<LightU> lights_, sortedLights_;
  std::vector<AABB> instBox_;          // each copy's box, in the instance buffer's order
  std::vector<uint8_t> instHidden_;    // scaled to nothing
  std::vector<uint32_t> visible_;      // this frame's draw lists (indices into the instance buffer)
  std::vector<Batch> sceneDraws_, shadowDraws_;
  std::vector<std::array<int, 2>> meshLods_;   // Scene::lods
  std::vector<uint32_t> lodScratch_[3];
  wgpu::Buffer visibleBuf_;
  size_t visibleCap_ = 0;
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
    if (!b) cap = 0;  // a fresh buffer (after a new scene) starts from what's needed, not 1.5x the last scene's
    if (!b || bytes > cap) {
      cap = std::max<size_t>(std::max<size_t>(bytes, 256), cap * 3 / 2);
      if (b) b.Destroy();
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
      // only shadows use the rays outside Ultra, and glowing things and glass
      // cast none: leave them out, so rays don't wade through rain and neon
      if ((flags & 3) && quality_ != Quality::kUltra) continue;
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
  std::shared_ptr<GpuState> gs_;
  int raysBuiltFor_ = -1;  // the quality the top ray tree was built for
  std::vector<Mat4> lastPlaces_;
  // automatic resolution: fewer pixels when the GPU falls behind
  float renderScale_ = 1, sinceScale_ = 0;
  double judgedAt_ = 0;
  float displayHz_ = 60;
  bool fresh_ = true;  // next frame starts a new measurement
  int perfSkipped_ = 0, perfDrawn_ = 0, calmSeconds_ = 0, longCalm_ = 0, step_ = 0, ceilStep_ = 0;
  uint64_t presented_ = 0;
  std::function<void(int, int, const uint8_t*)> captureDone_;
};

struct Pending {
  std::string canvas;
  wgpu::Instance instance;
  wgpu::Surface surface;
  wgpu::Adapter adapter;
  std::function<void(std::unique_ptr<Renderer>)> ready;
  std::shared_ptr<GpuState> state;
};

}  // namespace

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
