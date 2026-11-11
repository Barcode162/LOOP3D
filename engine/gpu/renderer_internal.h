#pragma once
// The live renderer (WebGPU), shared by its parts: the class and the GPU-side layouts. Each
// part of the work is in its own file beside it (renderer_setup, _scene, _draw, _light, _clusters,
// _shadows, _rays); renderer.cpp starts it.
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

namespace loop::gpu {

extern int g_skipPasses;

namespace impl {

// Reads a texture back and hands its pixels to the page (renderer.cpp).
void capturePixels(WGPUDevice dev, WGPUTexture tex, int w, int h, int bgra);


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
  // each step's time from the chip's timers (frame_graph.h), and the places a
  // frame's stamps are read back from (one per frame still on its way)
  FrameGraph steps;
  struct StampSlot {
    wgpu::Buffer buf;
    std::vector<std::string> steps;  // the step each pair of stamps belongs to
    uint64_t frame = 0;              // which frame (numbered from 1)
    bool busy = false;
  };
  StampSlot slots[6];
  // the pieces the view drew (piecesDrawn), copied out of the draw lists
  wgpu::Buffer piecesBuf;
  bool piecesBusy = false;
  uint32_t pieces[3] = {0, 0, 0};  // the view's two passes, the sun's shadow
};

// The first few WebGPU errors go to the page (a black screen should say why).
inline int g_gpuErrors = 0;
inline void reportGpuError(const std::string& m) {
  if (++g_gpuErrors > 3) return;
  emscripten_log(EM_LOG_ERROR, "WebGPU: %s", m.c_str());
  EM_ASM({ if (window.loopGpuProblem) window.loopGpuProblem(UTF8ToString($0)); }, m.c_str());
}

inline std::string str(wgpu::StringView s) { return s.data ? std::string(std::string_view(s)) : std::string(); }

inline uint16_t toHalf(float f) {
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
  Vec4 exposure;  // filled on the GPU from exposureBuf_ (the scene's passes may read only 8 storage buffers on an iPhone)
  Vec4 grid0, grid1;  // the light grid: low corner, cell size; cells x y z, on
  Vec4 lampMaps;      // lamp shadow maps: lamps, tile size, atlas width, height
  Mat4 lampFaces[12]; // each mapped lamp's six views
  Vec4 lampValid;     // per row: its map is drawn for its lamp (1) or not yet (0)
  Mat4 shadowFarViewProj;  // the sun's far shadow (a second, coarser map)
  Vec4 shadowFar;          // the near box's size over the far one's, the far map's layer (1 or 2; 0: none)
  Vec4 cache0, cache1;     // the light cache: first probe xyz, spacing; probes x y z, on
  Vec4 ao;                 // ambient occlusion: strength, reach (m), frame number; screen reflections on
  Mat4 prevViewProj;       // last frame's view, unshifted
  Vec4 prevCam;            // last frame's eye; w: its drawn width
  Vec4 prevScreen;         // x, y: last frame's drawn size
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
  Vec4 prevPos, prevFwd, prevRight, prevUp;  // last frame's camera (ReSTIR GI's reuse)
  uint32_t reuse[4];                         // this frame's half, last frame's usable, blocks across, down
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
static_assert(sizeof(FrameU) == 1536, "frame layout");
static_assert(sizeof(InstanceU) == 128, "instance layout");

constexpr int kMaxLights = 256;
constexpr int kBloomLevels = 6;
constexpr int kFinalPass = 2 * kBloomLevels - 1;  // pass uniforms: bloom down, bloom up, then the film
inline int g_samples = 4;  // multisampling of the scene pass (1 = off)

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
  GpuRenderer(std::string canvas, wgpu::Instance inst, wgpu::Surface surface, wgpu::Adapter adapter, wgpu::Device device, std::shared_ptr<GpuState> state);

  float pixelRatio() const override { return ratio_ * renderScale_; }
  size_t drawCalls() const override { return drawCalls_; }
  float currentExposure() const override { return lastExposure_; }
  void capture(std::function<void(int, int, const uint8_t*)> done) override { captureDone_ = std::move(done); }

  void setQuality(Quality q) override;

  void applySettings() override;

  bool lost() const override { return gs_->lost; }
  int framesInFlight() const override { return gs_->inFlight; }

  void sync(const Scene& scene, bool geometryChanged, bool skyChanged) override;

  void syncLights(const Scene& scene) override;

  void reset() override;

  // Rays through the scene are only needed for lamp shadows (High) and Ultra:
  // Phone builds no ray trees at all (less work when a scene loads, and every
  // frame something moves).
  bool needRays() const { return quality_ == Quality::kUltra || shadowLamps_ > 0; }
  // High: the lamps with no shadow of their own share one ray a pixel (phase 8)
  bool sharedLampShadows() const;
  // the main lamps' shadow maps: Phone (no rays), and High with the shared ray
  bool lampMapsWanted() const;

  void syncTransforms(const Scene& scene) override;

  // ---- each copy's move since the last frame drawn (the frame history's
  // motion, phase 3): the matrix that takes where a copy's surface is now to
  // where it was then; frame blending follows moving things with it instead
  // of smearing them. Still copies keep the identity; only the ones that
  // moved are written, and set still again once they stop.
  wgpu::Buffer backBuf_;
  size_t backCap_ = 0;
  std::vector<Mat4> drawnModel_;                  // each slot's place when last drawn
  std::vector<uint32_t> movedNow_, movedBefore_;  // slots moved since the last frame drawn, and in it
  void stillAgain(size_t slots);
  void writeMotion();

  // Only some copies moved: each new transform goes into its own place in the
  // instance buffer (the order and the batches stay as they are), and only
  // those places are sent, in a few runs.
  void moveInstances(const Scene& scene, const std::vector<int>& ids, const std::vector<Mat4>& xfs) override;

  // The passes the settings turn off, as the old skip bits (plus the tests' own).
  int skipBits() const;

  bool draw(const Camera& cam, const Look& look, float dt, float time, float fade) override;

  uint64_t presentedFrames() const override { return presented_; }
  uint64_t shadowDraws() const override { return shadowRedraws_; }
  uint64_t lampShadowDraws() const override { return lampMapDraws_; }
  bool lampListsReady() const;
  bool timers() const override { return static_cast<bool>(stampSet_); }
  std::vector<std::pair<std::string, float>> stepMs() const override;
  std::vector<std::pair<std::string, float>> takeStepAverages() override { return gs_->steps.takeAverages(); }
  std::array<uint32_t, 3> piecesDrawn() const override;
  float gpuMs() const override { return gs_->gpuMs; }
  float lastGpuMs() const override { return gs_->lastGpuMs; }
  size_t gpuBytes() const override;
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
  void adaptResolution(double now);

  // render.maxLights: the brightest (they go first, see uploadLights) are kept
  size_t lightCount() const { return std::min(lights_.size(), static_cast<size_t>(std::clamp(config().render.maxLights, 1, kMaxLights))); }

 private:
  void readLights(const Scene& scene);

  // ---- the light cache (engine/render/light_cache.h, phase 6): probes of the
  // light that isn't straight from a lamp or the sun, baked by the film
  // renderer's path tracer a few milliseconds a frame (nearest the eye first)
  // and sent every few frames. A probe not baked yet holds the sky's light
  // (as before the cache). Baked again when shapes, placements or lamps move;
  // the path tracer (tens of MB) lives only while baking.
  LightCache cache_;
  std::unique_ptr<PathTracer> tracer_;
  std::vector<int> cacheOrder_;
  size_t cacheNext_ = 0;
  int cacheUnsent_ = 0, cacheFrames_ = 0;
  ChangeTracker::Made cacheMade_;
  wgpu::Texture cacheTex_[6];
  wgpu::TextureView cacheView_[6];
  uint64_t cacheBaked_ = 0;  // probes baked since the start
  uint64_t cacheRelit_ = 0;  // probes marked to bake again near a change
  std::vector<AABB> relight_;  // where something changed since the last relight
  std::vector<Vec3> relightFrom_;  // per box: the lamp that changed (its light spreads from there through the rooms), or NaN for a thing that moved
  // each lamp's light, smoothed over about a second (a flickering flame keeps
  // a steady average; a lamp switched on or off doesn't), and as last baked
  std::vector<float> lampAvg_, lampBaked_;
  double lampWatchedAt_ = 0;
  void watchLampsForCache();
  std::vector<AABB> moverFrom_;  // per place: its box when it started moving
  double cacheStarted_ = -1e9;  // when the bake last started (ms)
  void makeCacheTextures();
  // The sky's light on a surface facing n, from its nine numbers (as
  // shIrradiance in the shader works it out); a probe not baked yet holds it.
  Vec3 skyIrradiance(Vec3 n) const;
  void skyProbe(int i);
  void sendCache();
  void keepLightCache(Vec3 eye);
  int farShadowWaiting() const override { return farBuilding_ ? farParts_ - farPart_ : 0; }
  int lightCacheWaiting() const override { return tracer_ ? static_cast<int>(cacheOrder_.size() - cacheNext_) : 0; }
  uint64_t lightCacheRelit() const override { return cacheRelit_; }
  int lightCacheProbes() const override { return cache_.on ? cache_.probes() : 0; }

  // ---- the light grid (engine/render/light_grid.h): the lamps in a tree of
  // groups, each group sent as one more lamp (its members' light added up,
  // at their middle); each cell lists the lamps near it and the groups far
  // enough away. Off or few lamps: every lamp everywhere, as before.
  LightGrid grid_;
  std::vector<GridLamp> gridLampsCpu_;
  float gridKey_ = -1;     // the settings the grid was laid out for
  wgpu::Texture gridCellsTex_, gridListTex_;
  wgpu::TextureView gridCellsView_, gridListView_;
  static constexpr int kGridCells = 4096;
  wgpu::Texture gridTexture(wgpu::TextureDimension dim, uint32_t x, uint32_t y, uint32_t z);
  void writeGridTexture(const wgpu::Texture& t, const void* data, uint32_t x, uint32_t y, uint32_t z);
  static float gridSetting();
  // Lays the grid out for the lamps and settings now: the tree, the cells.
  void layOutGrid();
  // Each group as one lamp: its members' light added up, at their middle,
  // as big as the ball holding them (which also softens its shine).
  LightU groupLamp(const LightGrid::Group& grp) const;
  // Each frame: laid out again when its setting changed.
  void keepGrid();

  // The lamps go to the GPU brightest-near-the-camera first: the shader
  // gives its shadow rays to the first few, so they go where they show.
  void uploadLights(const Vec3& eye);

  static Vec3 whiteBalanceGains(float temperature, float tint);

  wgpu::Buffer buffer(size_t size, wgpu::BufferUsage usage);
  // true when the buffer was replaced (bind groups need making again)
  bool ensureStorage(wgpu::Buffer& b, size_t& cap, size_t stride, size_t count);

  // ---- clusters (the rebuild's step 4): every mesh's cooked tree (the cook,
  // engine/geometry/cluster.h) in four pools the GPU culls and draws from in
  // one go: the vertices, the clusters' triangles (as vertex numbers in the
  // pool), the clusters, and per mesh where its clusters are.
  // One view's clusters: the copies to look at, the chosen clusters, the draw.
  // The eye has one, the sun's shadow another.
  struct ClusterList {
    wgpu::Buffer ubo, cullList, drawn, args, stretch, counts;
    size_t cullListCap = 0, drawnCap = 0, stretchCap = 0, countsCap = 0;
    uint32_t count = 0;
    std::vector<uint32_t> cpu;  // per copy its instance and where its stretch starts
    wgpu::BindGroup cullGroup, drawGroup;
    wgpu::BindGroup cullGroupB;  // the same with the two depth pyramids the other way round
  };
  struct ClusterU {
    Vec4 bounds, lod, parent;            // spheres (centre, radius) in the mesh's space
    float error, parentError;            // the parent's is a huge number when nothing replaces it
    uint32_t first, count;               // its triangles' corners in the index pool, and how many triangles
  };
  struct MeshInfoU {
    uint32_t clusterFirst, clusterCount, vertexBase, pad;
  };
  wgpu::Buffer poolVerts_, poolIdx_, poolClusters_, poolMeshes_;
  size_t pooledMeshes_ = 0, poolClusterCount_ = 0;
  // Cooks every mesh (the cook keeps each tree, so only new shapes cost) and
  // sends the pools again: shapes change only when a scene is built.
  void uploadClusters(const Scene& scene);

  // ---- the background cook: a new scene is drawn at once the old way while
  // one core cooks its clusters from copies of its shapes; the drawing switches
  // over the frame after (the cook keeps the trees, so the switch is quick).
  // A page on one core cooks at once instead.
  struct CookJob {
    std::vector<Mesh> meshes;  // positions and triangles: all the cook reads
    std::atomic<bool> done{false};
  };
  std::shared_ptr<CookJob> cookJob_;
  void wantClusters(const Scene& scene);
  bool clustered() const override { return clustersReady(); }
  bool clustersReady() const;
  // The GPU's cull starts from the copies the CPU found in a view (opaque
  // ones: glass is still drawn the old way), with room for every cluster they
  // have. Detail is chosen from where the eye is, whatever the view (the sun's
  // shadow keeps the detail you see).
  // movers: 0 every copy, 1 only what stands still, 2 only what moved lately
  void prepareList(ClusterList& L, const std::vector<Batch>& draws, const Mat4& viewProj, Vec3 eye, float perPixel, float occlusion = 0,
                   int movers = 0);
  // The same for a list of copies (places in the instance buffer).
  void prepareSlots(ClusterList& L, const std::vector<uint32_t>& slots, const Mat4& viewProj, Vec3 eye, float perPixel);
  void finishList(ClusterList& L, size_t room, const Mat4& viewProj, Vec3 eye, float perPixel, float occlusion);
  // choose (each copy its own list, in order), count up where each list
  // starts, pack: the same order every frame
  // A pass of the step now running gets the chip's two stamps (its start and
  // end), when the chip has timers and a place to read them back is free;
  // nothing outside a step (a capture) is stamped.
  // a pass timed under its own name inside a step (the meter, the recordings)
  const char* stampAs_ = nullptr;
  const wgpu::PassTimestampWrites* stamp(wgpu::PassTimestampWrites& w);

  void runCull(wgpu::CommandEncoder& enc, const ClusterList& L);

  GpuMesh upload(const Mesh& m);

  // What each pass draws this frame: only the copies its view can see
  // (render.cull), in the batches' order; `first` of a draw is where its
  // stretch starts in visibleIds. The sun's pass never draws glass.
  // A mesh with simpler versions (Scene::lods) is drawn as the version its
  // size on screen asks for (lodLevel, shared with `loop budget`); the sun's
  // pass draws one version simpler (shadowLodLevel).
  int lodOf(int mesh, int level) const;

  void buildDrawLists(const Frustum& eye, const Frustum& sun, bool sunOn, Vec3 eyePos, float pixelsPerRadian);

  void drawBatches(wgpu::RenderPassEncoder& pass, bool glass, bool shadow);

  void postPass(wgpu::CommandEncoder& enc, wgpu::TextureView target, wgpu::RenderPipeline& pipe, wgpu::BindGroup& group, bool load, uint32_t vw = 0,
                uint32_t vh = 0);

  wgpu::ShaderModule module(const std::string& code, const char* label);

  wgpu::BindGroupLayout layout(std::vector<wgpu::BindGroupLayoutEntry> e);
  std::vector<std::pair<wgpu::BindGroupLayout, FrameGraph::StageBuffers>> stageBuffers_;
  static wgpu::BindGroupLayoutEntry ubo(uint32_t b, wgpu::ShaderStage s);
  static wgpu::BindGroupLayoutEntry ssbo(uint32_t b, wgpu::ShaderStage s, bool write = false);
  static wgpu::BindGroupLayoutEntry tex(uint32_t b, wgpu::ShaderStage s, bool depth = false, bool unfilterable = false);
  static wgpu::BindGroupLayoutEntry samp(uint32_t b, wgpu::ShaderStage s, bool compare = false);

  void buildPipelines();

  void makeShadowMap();

  // Every model picture onto its own layer of one texture stack, resized to
  // render.textureSize square (a square holds any picture: models read it by
  // 0..1 coordinates), with each smaller copy averaged from the one above.
  void uploadModelTextures(const Scene& scene);

  void uploadSky(const SkyModel& m);

  // The canvas and the full-size targets follow the window (re-made only when
  // it changes size, or the quality changes); the part of them drawn each
  // frame follows the automatic resolution, which costs nothing to change.
  void resize();

  static size_t blocks(uint32_t w, uint32_t h, uint32_t b) { return static_cast<size_t>((w + b - 1) / b) * ((h + b - 1) / b); }

  void makeTargets(uint32_t ow, uint32_t oh);

  // Bloom level i: its full size, and the part of it in use this frame.
  // bloom's levels: halves of the picture; at 2 pixels a point and more, from a
  // quarter (the same sizes on screen as at 1 a point, for the same cost)
  int bloomShift() const { return upscaling_ && ratio_ >= 1.75f ? 2 : 1; }
  uint32_t levelW(int i) const { return std::max(1u, postW_ >> (i + bloomShift())); }
  uint32_t levelH(int i) const { return std::max(1u, postH_ >> (i + bloomShift())); }

  // Where each post pass finds its two pictures this frame.
  void writePassUniforms();

  wgpu::BindGroup group(wgpu::BindGroupLayout l, std::vector<wgpu::BindGroupEntry> e);
  static wgpu::BindGroupEntry bufE(uint32_t b, wgpu::Buffer buf);
  static wgpu::BindGroupEntry texE(uint32_t b, wgpu::TextureView v);
  static wgpu::BindGroupEntry sampE(uint32_t b, wgpu::Sampler s);

  void makeBindGroups();

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
  wgpu::TextureView shadowLayerView_[3];
  wgpu::TextureView shadowDummy_, shadowView_, skyView_, msaaView_, depthView_, hdrView_, bloomViews_[kBloomLevels];
  wgpu::BindGroupLayout scene0Layout_, scene1Layout_, postLayout_, expLayout_;
  wgpu::RenderPipeline opaquePipe_[2], depthPipe_, opaqueAfterDepthPipe_[2], glassPipe_[2], skyPipe_, shadowPipe_, downPipe_, upPipe_, finalPipe_;
  wgpu::ComputePipeline exposurePipe_;
  wgpu::BindGroup sceneGroup0_, sceneGroup1_, downGroups_[kBloomLevels], upGroups_[kBloomLevels], finalGroup_, exposureGroup_;
  // the cluster drawing (render.clusters): the visibility picture, its passes and inputs
  wgpu::Texture visTex_;
  wgpu::TextureView visView_;
  wgpu::BindGroupLayout clusterDrawLayout_, clusterResolveLayout_, cullLayout_;
  wgpu::RenderPipeline visPipe_, resolvePipe_[2], shadowVisPipe_;
  wgpu::BindGroupLayout emptyLayout_;  // groups a pipeline has room for but doesn't use
  wgpu::BindGroup emptyGroup_;
  wgpu::ComputePipeline cullPipe_, countPipe_, packPipe_;
  wgpu::BindGroup clusterResolveGroup_[4];
  int shadowIndex_ = 0;  // which kept-shadow picture is last frame's
  wgpu::Texture pastDummyTex_;
  ClusterList eyeList_, sunList_;
  // ---- what's hidden (phase 4): two depth pyramids, last frame's and this
  // frame's first pass's, each level the farthest depth over a patch
  ClusterList sunMovers_;  // the sun's shadow of what moved lately (drawn every frame on the kept rest)
  // the sun's far shadow (a second layer, five times the near box, moving things left out)
  static constexpr float kFarShadow = 5.0f;
  ClusterList sunFarList_;
  wgpu::Buffer sunFarUbo_;
  wgpu::BindGroup sunFarGroup_;
  Mat4 sunFarViewProj_;
  Vec3 sunFarEye_;
  Vec3 shadowLook_{0, 0, 0};  // the way the sun's shadow boxes lean (the look, within 30 degrees)
  // The far map is drawn again a part a frame (your phone's recording: a
  // whole redraw cost the Glade 16-22 ms and the Woods 50 at once), into the
  // shadow picture's other far layer, while the one drawn before is still
  // shown; when every part is in, the new one is shown.
  ChangeTracker::Made sunFarMade_;    // what the far map shown was made from
  ChangeTracker::Made farBuildMade_;  // what the one being drawn is made from
  ChangeTracker::Made farShownHard_, farBuildHard_;  // a new scene or settings: the old one is no use at all
  bool sunFarValid_ = false;
  int farShown_ = 0;       // the layer shown (1 or 2), 0 for none
  Mat4 farShownViewProj_;
  bool farBuilding_ = false;
  int farTarget_ = 1, farPart_ = 0, farParts_ = 1;
  Mat4 farBuildViewProj_;
  Vec3 farBuildEye_;
  std::vector<uint32_t> sunFarSlots_, farPartSlots_;
  float farMsPerSlot_ = 0;  // what drawing one thing into it has cost lately (the step timers)
  size_t farTimedSlots_ = 0;
  static constexpr float kFarPartMs = 2.5f;  // a part's share of a frame
  void startSunFar();
  void prepareSunFar(float perPixel);
  // ---- the main lamps' shadow maps (Phone, where no rays are traced): the
  // first lamps in the list (the brightest near the eye, panels last) get six
  // views each, in one depth picture; drawn again only when the lamps chosen,
  // the placements, the shapes or the settings change (moving things are left
  // out of them: they would draw all twelve views every frame)
  static constexpr int kLampMapsMost = 2;
  ClusterList lampLists_[kLampMapsMost * 6];
  wgpu::Buffer lampFaceUbo_[kLampMapsMost * 6];
  wgpu::BindGroup lampFaceGroup_[kLampMapsMost * 6];
  wgpu::BindGroupLayout lampFaceLayout_;
  wgpu::RenderPipeline lampShadowPipe_;
  wgpu::Texture lampMapTex_, lampDummyTex_;
  wgpu::TextureView lampMapView_, lampDummyView_, lampLayerView_[kLampMapsMost];
  int lampMapSize_ = 0;   // the views' size the picture was made for (0: a 1x1 stand-in)
  int lampMapCount_ = 0;  // lamps mapped this frame
  Mat4 lampFaceMat_[kLampMapsMost * 6];
  uint64_t lampMapDraws_ = 0;
  std::vector<uint32_t> lampSlots_;
  // The depth picture: six views across, a row per lamp (a stand-in when unused).
  void makeLampMap(int size);
  // Phone's lamp maps keep their rows: a mapped lamp still among the first
  // stays in its row (only a newcomer's row is drawn again), the others move up.
  uint32_t lampRowLamp_[kLampMapsMost] = {~0u, ~0u};
  void keepLampRows(std::vector<std::pair<float, uint32_t>>& rank);
  // How far a lamp's map reaches: where its light falls below a quarter of a
  // lux (a shadow farther out is too faint to see), 2 to 15 m.
  static float lampMapReach(const LightU& L);
  ChangeTracker::Made lampRowMade_[kLampMapsMost];
  int lampRowDrawing_ = -1;  // the row drawn this frame (one at most: the work spread over frames)
  float lampRowValid_[kLampMapsMost] = {0, 0};
  Vec3 lampRowPos_[kLampMapsMost];
  float lampRowReach_[kLampMapsMost] = {0, 0};
  // Which lamps get maps this frame, and their views (into the frame's constants).
  void chooseLampMaps(FrameU& f);
  // The row drawn this frame: its six lists, the still copies within its reach.
  void prepareLampMaps();
  ClusterList eyeList2_;  // the second pass: what the first skipped, tried again
  wgpu::Texture hzbTex_[2];
  wgpu::TextureView hzbAll_[2];
  std::vector<wgpu::TextureView> hzbMip_[2];
  std::vector<wgpu::Buffer> hzbUbo_;
  std::vector<wgpu::BindGroup> hzbGroup_[2];
  wgpu::BindGroupLayout hzbDepthLayout_, hzbLevelLayout_;
  wgpu::RenderPipeline hzbDepthPipe_, hzbLevelPipe_;
  Mat4 hzbView_[2];
  struct HzbSize {
    uint32_t x = 1, y = 1, z = 1;  // level 0's valid size, and how many levels
    float w0 = 1, h0 = 1;          // the depth picture's size halved, unrounded (where a point lands)
  } hzbSize_[2];
  uint32_t hzbLevels_ = 0;  // levels each pyramid holds
  int hzbIndex_ = 0;        // the pyramid this frame makes (the other is last frame's)
  bool hzbValid_ = false;   // last frame made one
  Vec3 sunEye_;                       // where the shadow's detail is judged from (the eye, in steps)
  uint64_t shadowRedraws_ = 0;
  wgpu::Texture stillTex_;
  wgpu::TextureView stillView_;
  bool stillDrawn_ = false;            // stillTex_ holds the still things' shadow
  std::vector<uint64_t> lastMoved_;    // per place: the frame it last moved in (0: never)
  std::vector<uint32_t> movers_;       // the places that moved in the last two seconds
  std::vector<uint8_t> isMover_;
  static constexpr uint64_t kMoverFrames = 120;
  // A copy that moved joins the movers; one still for two seconds leaves them.
  // Either way the kept shadow of the rest is drawn again ("movers").
  void updateMovers();
  // times the sun's shadow was drawn
  bool secondDrawn_ = false;          // this frame's second pass ran
  mutable bool piecesWanted_ = false;  // someone asked how many pieces were drawn
  HzbSize hzbSizeNow() const;
  // this frame's pyramid from its depth picture (after the first pass)
  void buildHzb(wgpu::CommandEncoder& enc);
  // frame blending (render.taa, with clusters): two pictures of the past, swapped each frame
  wgpu::Texture histTex_[2];
  wgpu::TextureView histView_[2];
  wgpu::BindGroupLayout taaLayout_;
  wgpu::RenderPipeline taaPipe_;
  wgpu::BindGroup taaGroup_[2];
  wgpu::Buffer taaUbo_;
  int histIndex_ = 0;
  bool histValid_ = false;
  Mat4 prevViewProj_;
  Vec3 prevCam_;
  uint32_t prevScreen_[2] = {1, 1};
  // each pixel's shadow answer for the lamps sharing a ray (High), kept and
  // blended over frames, where that point was: two pictures, swapped each frame
  wgpu::Texture shadowHistTex_[2];
  wgpu::TextureView shadowHistView_[2];
  uint32_t jitterFrame_ = 0;
  Vec2 jitterPx_{0, 0};
  // The frame blending upscales: it writes the whole full-size picture from
  // the smaller one drawn, so every step after it (exposure, bloom, film)
  // works on the full size. postW_ x postH_: the picture those steps read.
  bool upscaling_ = false;
  uint32_t postW_ = 0, postH_ = 0;
  bool blending() const { return clustersReady() && config().render.taa && histView_[0]; }
  std::vector<uint32_t> meshClusterCount_;
  bool bindGroupsDirty_ = true, skyBuilt_ = false;

  std::vector<GpuMesh> meshes_;
  std::vector<Batch> batches_;
  std::vector<LightU> lights_, sortedLights_;
  std::vector<uint8_t> lampNoShadow_;  // parallel to lights_: 1 when the lamp was set `shadows: no`
  std::vector<AABB> instBox_;          // each copy's box, in the instance buffer's order
  std::vector<InstanceU> instData_;    // the instance buffer's contents, kept to send only what moved
  std::vector<int> slotOf_;            // scene instance -> its place in the instance buffer (-1: not drawn)
  std::vector<int> touched_;
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
  wgpu::Buffer resBuf_;  // Ultra's ReSTIR GI reservoirs
  size_t resBlocks_ = 0;
  uint32_t resDims_[2] = {0, 0}, resHalf_ = 0;
  bool resValid_ = false;
  Vec4 resPrev_[4];
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

  static NodeU packNode(const AABB& box, uint32_t first, uint32_t count);
  std::unordered_map<const void*, size_t> storageCap_;
  // Writes into the buffer, making a bigger one only when it doesn't fit.
  void uploadStorage(wgpu::Buffer& b, const void* data, size_t bytes);
  void emptyRayScene();

  // Each new shape's own tree, in its own space; built once.
  void buildRayMeshes(const Scene& scene);

  // The top tree over every placed copy; cheap, rebuilt as things move.
  void buildRayInstances(const Scene& scene);
  std::shared_ptr<GpuState> gs_;
  int raysBuiltFor_ = -1;  // the quality the top ray tree was built for
  std::vector<Mat4> lastPlaces_;
  // automatic resolution: fewer pixels when the GPU falls behind
  float renderScale_ = 1, sinceScale_ = 0;
  ScaleLearner scaleLearned_;
  double judgedAt_ = 0;
  float displayHz_ = 60;
  bool fresh_ = true;  // next frame starts a new measurement
  // the frame's steps (draw); what a frame finds already there: the scene and
  // its lists, lamps, sky and rays, last frame's history
  FrameGraph graph_{{"scene", "sun list", "eye list", "lamps", "sky", "rays", "history", "shadow map", "depth pyramid", "lamp maps", "far shadow map"}};
  // what the frame is made from, versioned; the sun's shadow as last drawn
  ChangeTracker changes_;
  ChangeTracker::Made shadowMade_;
  bool graphChecked_ = false;
  // the chip's timers (when it has them): a frame's stamps, two per pass
  static constexpr uint32_t kStampPairs = 48;
  wgpu::QuerySet stampSet_;
  wgpu::Buffer stampResolve_;
  std::vector<std::string> stampSteps_;  // this frame's: each pair's step
  int stampSlot_ = -1;                    // where they're read back (-1: not this frame)
  int perfSkipped_ = 0, perfDrawn_ = 0, calmSeconds_ = 0, longCalm_ = 0, step_ = 0, ceilStep_ = 0;
  uint64_t presented_ = 0;
  std::function<void(int, int, const uint8_t*)> captureDone_;
};

}  // namespace impl
}  // namespace loop::gpu
