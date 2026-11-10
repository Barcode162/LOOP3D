// The live renderer: its draw part (see renderer_internal.h).
#include "engine/gpu/renderer_internal.h"

namespace loop::gpu::impl {

bool GpuRenderer::draw(const Camera& cam, const Look& look, float dt, float time, float fade) {
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
  {
    const bool up = quality_ != Quality::kUltra && clustersReady() && eyeList_.cullGroup && blending() && !(skip & 64) && taaGroup_[histIndex_];
    const uint32_t pw = up ? outW_ : width_, ph = up ? outH_ : height_;
    if (up != upscaling_ || pw != postW_ || ph != postH_) upscaling_ = up, postW_ = pw, postH_ = ph, passUniformsDirty_ = true, histValid_ = false;
  }
  wgpu::SurfaceTexture st;
  if (!(skip & 256)) {  // 256: off-screen only (tests in browsers that can't show WebGPU)
    surface_.GetCurrentTexture(&st);
    if (st.status != wgpu::SurfaceGetCurrentTextureStatus::SuccessOptimal && st.status != wgpu::SurfaceGetCurrentTextureStatus::SuccessSuboptimal) {
      outW_ = 0;  // the surface needs configuring again
      return false;
    }
  }
  if (lastScene_ && cookJob_ && cookJob_->done) wantClusters(*lastScene_);  // the background cook finished: switch over
  if (bindGroupsDirty_) makeBindGroups();
  keepGrid();
  keepLightCache(cam.position);
  uploadLights(cam.position);
  drawCalls_ = 0;

  // ---- frame constants
  FrameU f{};
  float aspect = static_cast<float>(width_) / height_;
  Mat4 view = Mat4::lookAt(cam.position, cam.target, cam.up);
  float fovY = cam.fovY();
  Mat4 proj = Mat4::perspective(fovY, aspect, 0.05f, std::max(C.world.drawDistance, 1.0f));
  const Mat4 steadyViewProj = proj * view;  // without frame blending's shift
  // a photograph (capture: the picture tests, a screenshot) is taken from
  // the unshifted view: which of the 8 shifts it landed on decided thin
  // bright lines (the bar's string lights) and failed one test run in two
  if (blending() && !captureDone_) {
    // frame blending: this frame's fraction-of-a-pixel shift (Halton 2, 3: 8 spots)
    auto halton = [](uint32_t i, uint32_t b) {
      float f = 1, r = 0;
      for (; i > 0; i /= b) f /= b, r += f * (i % b);
      return r;
    };
    uint32_t k = jitterFrame_++ % 16 + 1;  // 16 spots: the blending upscales, so more places to fill in
    proj.m[2][0] += (halton(k, 2) - 0.5f) * 2.0f / width_;
    proj.m[2][1] += (halton(k, 3) - 0.5f) * 2.0f / height_;
    // where this frame's samples land, in drawn pixels (x right, y down) from each pixel's middle
    jitterPx_ = Vec2(halton(k, 2) - 0.5f, -(halton(k, 3) - 0.5f));
  } else {
    jitterPx_ = Vec2(0, 0);
  }
  f.viewProj = proj * view;
  f.invViewProj = f.viewProj.inverse();
  // the sun's shadow: a box round what the camera sees nearby, snapped to texels
  // (two of them: a sharp one near the eye, and a coarse one five times as
  // far, beyond it: phase 5's sun shadows in steps)
  float R = C.render.shadowDistance > 0 ? C.render.shadowDistance : quality_ == Quality::kPhone ? 25.0f : 40.0f;
  // the box sits ahead of where you look, but follows the look only once it
  // has turned 30 degrees away: looking round no longer redraws the shadow and
  // sweeps its edges over the ground (your phone, the world: "the shadows on the
  // ground moving constantly")
  const Vec3 lookDir = normalize(cam.target - cam.position);
  if (dot(lookDir, shadowLook_) < std::cos(30.0f * kPi / 180.0f)) shadowLook_ = lookDir;
  const Vec3 fwd = shadowLook_;
  Vec3 center = cam.position + fwd * (R * 0.6f);
  Vec3 up = std::fabs(sunDir_.y) > 0.99f ? Vec3(1, 0, 0) : Vec3(0, 1, 0);
  Mat4 lv = Mat4::lookAt(Vec3(0), -sunDir_, up);
  Vec3 lc = lv.point(center);
  // the box moves in steps of an eighth of its size (whole texels), and is an
  // eighth bigger so what's near the eye stays covered: walking or turning
  // redraws the shadow every few metres, not every frame (phase 5)
  const float B = R * 1.125f;
  float texel = 2 * B / shadowSize_;
  float step = std::max(1.0f, std::round(R / 8 / texel)) * texel;
  lc.x = std::floor(lc.x / step + 0.5f) * step, lc.y = std::floor(lc.y / step + 0.5f) * step;
  lc.z = std::floor(lc.z / step + 0.5f) * step;
  // the shadow's pieces pick their detail from the eye, rounded to the same steps
  sunEye_ = Vec3(std::floor(cam.position.x / step + 0.5f), std::floor(cam.position.y / step + 0.5f), std::floor(cam.position.z / step + 0.5f)) * step;
  Mat4 lp = Mat4::ortho(lc.x - B, lc.x + B, lc.y - B, lc.y + B, -lc.z - R * 4, -lc.z + R * 4);
  f.shadowViewProj = lp * lv;
  {
    const float RF = R * kFarShadow, BF = RF * 1.125f;
    const float texelF = 2 * BF / shadowSize_;
    const float stepF = std::max(1.0f, std::round(RF / 8 / texelF)) * texelF;
    Vec3 lf = lv.point(cam.position + fwd * (RF * 0.6f));
    lf.x = std::floor(lf.x / stepF + 0.5f) * stepF, lf.y = std::floor(lf.y / stepF + 0.5f) * stepF;
    lf.z = std::floor(lf.z / stepF + 0.5f) * stepF;
    sunFarEye_ = Vec3(std::floor(cam.position.x / stepF + 0.5f), std::floor(cam.position.y / stepF + 0.5f), std::floor(cam.position.z / stepF + 0.5f)) * stepF;
    sunFarViewProj_ = Mat4::ortho(lf.x - BF, lf.x + BF, lf.y - BF, lf.y + BF, -lf.z - RF * 4, -lf.z + RF * 4) * lv;
    f.shadowFarViewProj = farShown_ ? farShownViewProj_ : sunFarViewProj_;
    f.shadowFar = {B / BF, static_cast<float>(farShown_), upscaling_ ? 1.0f : 0.0f, upscaling_ ? static_cast<float>(width_) / std::max(outW_, 1u) : 1.0f};
  }
  f.camPos = {cam.position, time};
  f.sunDir = {sunDir_, sunOn_ ? 1.0f : 0.0f};
  f.sunColor = {sunIllum_, C.render.skyLight};
  // screen reflections: High, with frame blending's past picture to read
  const bool ssr = quality_ == Quality::kHigh && upscaling_ && histValid_ && C.render.ssr && !(skip & 16384);
  f.ao = {(skip & 8192) ? 0.0f : C.render.ao, std::max(C.render.aoReach, 0.05f), static_cast<float>(jitterFrame_ % 64), ssr ? 1.0f : 0.0f};
  f.prevViewProj = histValid_ ? prevViewProj_ : steadyViewProj;
  f.prevCam = {histValid_ ? prevCam_ : cam.position, 0};
  f.prevScreen = {static_cast<float>(histValid_ ? prevScreen_[0] : width_), static_cast<float>(histValid_ ? prevScreen_[1] : height_), 0, 0};
  f.cache0 = {cache_.origin, cache_.spacing};
  f.cache1 = {static_cast<float>(cache_.nx), static_cast<float>(cache_.ny), static_cast<float>(cache_.nz), cache_.on && cacheTex_[0] ? 1.0f : 0.0f};
  for (int i = 0; i < 9; ++i) f.sh[i] = {sh_[i], 0};
  f.sh[0].w = C.render.shadowSoftness, f.sh[1].w = C.world.patternFade;
  f.fog = {fog_.density, fog_.top, fog_.anisotropy, fog_.density > 0 ? 1.0f : 0.0f};
  f.fogColor = {fog_.albedo, C.render.shadowBias};
  f.grid0 = {grid_.origin, grid_.cell};
  f.grid1 = {static_cast<float>(grid_.nx), static_cast<float>(grid_.ny), static_cast<float>(grid_.nz), grid_.on ? 1.0f : 0.0f};
  chooseLampMaps(f);
  // with every lamp's shadow shared (High), one lamp fewer is traced on its own: the same rays a pixel
  // and with the main lamps' shadow maps (as on Phone) none at all: your phone's
  // recording, the bar on High: its lighting 17-21 ms for two rays a pixel
  const bool sharedShadows = sharedLampShadows();
  const int ownRays = sharedShadows ? (lampMapsWanted() ? 0 : std::max(shadowLamps_ - 1, 0)) : shadowLamps_;
  f.params = {static_cast<float>(lightCount()), (skip & 512) ? 0.0f : static_cast<float>(ownRays), static_cast<float>(shadowSize_), cam.autoExposure ? 1.0f : 0.0f};
  f.screen = {static_cast<float>(width_), static_cast<float>(height_), 1.0f / width_, 1.0f / height_};
  // a first guess for the eyes: the camera's own settings, or a sunny / indoor guess
  float guess = cam.autoExposure ? (sunOn_ && sunLux_ > 1000 ? 1.0f / 6000 : 1.0f / 30) : exposureFromEv100(cam.ev100());
  // lamp shadows: rays for the big soft lights (one for a small bulb)
  f.fixedExposure = {guess * std::exp2(0.0f), static_cast<float>(shadowRays_), static_cast<float>(C.render.view), C.render.occlusion};
  queue_.WriteBuffer(frameUbo_, 0, &f, sizeof f);

  PostU p{};
  float fixedExp = exposureFromEv100(cam.ev100());
  p.exposure = {fixedExp, cam.exposureCompensation, cam.autoExposure ? 1.0f : 0.0f, look.sharpen + (upscaling_ ? C.render.sharpen : 0.0f)};
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
  ExposureU e{{static_cast<float>(postW_), static_cast<float>(postH_), dt, f.fixedExposure.x},
              {exposureFromEv100(C.camera.maxEv), exposureFromEv100(C.camera.minEv), 3.0f * C.camera.adaptSpeed, 1.2f * C.camera.adaptSpeed}};
  queue_.WriteBuffer(expUbo_, 0, &e, sizeof e);
  lastExposure_ = fixedExp;

  wgpu::CommandEncoder enc = device_.CreateCommandEncoder();
  // the eyes' exposure from last frame into the frame's constants (on the GPU)
  enc.CopyBufferToBuffer(exposureBuf_, 0, frameUbo_, offsetof(FrameU, exposure), 16);
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
                 (U.reuse ? 32u : 0u) | (static_cast<uint32_t>(std::clamp(U.spp, 1, 16)) << 8);
    // ReSTIR GI's reservoirs: two halves (this frame's, last frame's), for the drawn size
    const uint32_t bw = (width_ + ultraScale_ - 1) / ultraScale_, bh = (height_ + ultraScale_ - 1) / ultraScale_;
    if (U.reuse && static_cast<size_t>(bw) * bh > resBlocks_) {
      resBlocks_ = static_cast<size_t>(bw) * bh;
      resBuf_ = buffer(resBlocks_ * 2 * 64, wgpu::BufferUsage::Storage);
      ultraGroup_ = group(ultraLayout_, {bufE(0, accumBuf_), texE(1, hdrView_), texE(2, skyView_), sampE(3, linear_), bufE(4, ultraUbo_), bufE(5, resBuf_)});
      resValid_ = false;
    }
    if (bw != resDims_[0] || bh != resDims_[1]) resValid_ = false;
    resDims_[0] = bw, resDims_[1] = bh;
    u.prevPos = resPrev_[0], u.prevFwd = resPrev_[1], u.prevRight = resPrev_[2], u.prevUp = resPrev_[3];
    u.reuse[0] = resHalf_, u.reuse[1] = resValid_ ? 1u : 0u, u.reuse[2] = bw, u.reuse[3] = bh;
    queue_.WriteBuffer(ultraUbo_, 0, &u, sizeof u);
    resPrev_[0] = u.camPos, resPrev_[1] = u.camFwd, resPrev_[2] = u.camRight, resPrev_[3] = u.camUp;
    resHalf_ ^= 1u, resValid_ = U.reuse;
    ++ultraFrame_;
  }
  // ---- the sun's shadow is drawn again only when something it is made from
  // changed (engine/render/change_tracker.h): the placements, the shapes,
  // the settings, or the sun's view (where the sun is, the box it covers,
  // and the eye, which picks each cluster's detail)
  {
    struct {
      Mat4 view;
      Vec4 eye;
      int on, clusters;
    } sunView{f.shadowViewProj, Vec4(sunEye_, static_cast<float>(shadowSize_)), sunOn_ && !(skip & 1) ? 1 : 0, clustersReady() ? 1 : 0};
    if (!sunView.on) sunView.view = Mat4(), sunView.eye = Vec4(0, 0, 0, sunView.eye.w);  // no sun: cleared once, kept
    changes_.set("sun view", sunView);
  }
  // with clusters, what moved lately (movers) is drawn every frame on a copy
  // of the kept shadow of the rest; without, any move draws it all again
  updateMovers();
  const bool clusterSun = raster && sunOn_ && !(skip & 1) && clustersReady() && sunList_.cullGroup;
  const bool splitting = clusterSun && !movers_.empty() && !(skip & 2048);  // 2048: draw it all every time (tests)
  static const std::vector<std::string> kShadowFrom = {"placements", "shapes", "settings", "sun view", "movers"};
  static const std::vector<std::string> kShadowFromAll = {"placements", "shapes", "settings", "sun view", "moves"};
  // what's hidden (phase 4): with clusters, unless render.occlude is off
  const bool occluding = raster && clustersReady() && C.render.occlude && hzbTex_[0] && !(skip & 1024);
  if (!occluding) hzbValid_ = false;
  const std::vector<std::string>& shadowFrom = clustersReady() ? kShadowFrom : kShadowFromAll;
  const bool lampMapsStale = raster && lampRowDrawing_ >= 0;  // one row a frame (chooseLampMaps)
  // the far sun shadow: with clusters only, moving things left out; kept
  // until its box (which moves in steps of 5 times the near one's) or the scene changes
  {
    struct {
      Mat4 view;
      Vec4 eye;
    } farView{sunFarViewProj_, Vec4(sunFarEye_, static_cast<float>(shadowSize_))};
    changes_.set("sun far view", farView);
  }
  static const std::vector<std::string> kFarFrom = {"placements", "shapes", "settings", "sun far view", "movers"};
  // not on Phone: your phone's recording, the Woods: 54 ms each time it was
  // drawn again (its trees' simplest pieces, over 25 times the near area),
  // for shadows only seen beyond 25 m
  const bool farWanted = clusterSun && quality_ == Quality::kHigh && !(skip & 4096);  // 4096: no far shadow (tests)
  static const std::vector<std::string> kFarHard = {"shapes", "settings"};
  if (!farWanted || !sunFarValid_ || changes_.stale(farShownHard_, kFarHard)) farShown_ = 0;
  if (!farWanted || !sunFarValid_) farBuilding_ = false;
  sunFarValid_ = farWanted;  // false only until the shadow picture is made again (makeShadowMap)
  if (farWanted && changes_.stale(sunFarMade_, kFarFrom) && (!farBuilding_ || changes_.stale(farBuildMade_, kFarFrom))) {
    changes_.made(farBuildMade_, kFarFrom), changes_.made(farBuildHard_, kFarHard);
    startSunFar();
  }
  // the far shadow's last part timed: what a thing costs it
  for (const FrameGraph::Time& t : gs_->steps.recent())
    if (t.step == "far shadow" && farTimedSlots_ > 0) {
      const float per = t.ms / static_cast<float>(farTimedSlots_);
      farMsPerSlot_ = farMsPerSlot_ > 0 ? farMsPerSlot_ * 0.5f + per * 0.5f : per;
    }
  const bool farStale = farWanted && farBuilding_;
  const bool shadowStale = changes_.stale(shadowMade_, shadowFrom) || (splitting && !stillDrawn_) || ((skip & 2048) && !movers_.empty());
  // ---- what each pass draws: the copies its view can see
  if (raster) {
    buildDrawLists(Frustum::fromViewProj(f.viewProj), Frustum::fromViewProj(f.shadowViewProj), sunOn_ && !(skip & 1), cam.position,
                   static_cast<float>(height_) / (2 * std::tan(cam.fovY() * 0.5f)));
    if (clustersReady()) {
      float perPixel = static_cast<float>(height_) / (2 * std::tan(cam.fovY() * 0.5f));
      prepareList(eyeList_, sceneDraws_, f.viewProj, cam.position, perPixel, occluding && hzbValid_ ? 1.0f : 0.0f);
      if (occluding && hzbValid_) prepareList(eyeList2_, sceneDraws_, f.viewProj, cam.position, perPixel, 2.0f);
      // the sun's shadows pick their pieces' detail by the shadow picture's own
      // sharpness (its texels a metre, at a middling distance), not the
      // screen's: a 2x screen asked them for 7 times the detail they can show
      const float shadowPerPixel = static_cast<float>(shadowSize_) * 0.22f;
      if (sunOn_ && !(skip & 1) && shadowStale) prepareList(sunList_, shadowDraws_, f.shadowViewProj, sunEye_, shadowPerPixel, 0, splitting ? 1 : 0);
      if (splitting) prepareList(sunMovers_, shadowDraws_, f.shadowViewProj, sunEye_, shadowPerPixel, 0, 2);
      if (lampMapsStale) prepareLampMaps();
      if (farStale) prepareSunFar(shadowPerPixel / kFarShadow);  // its texels are kFarShadow times bigger: so may its pieces' detail be
    }
    if (bindGroupsDirty_) makeBindGroups();
  }
  // ---- the frame's steps (engine/render/frame_graph.h): each says what it
  // reads and writes, in the order they run; the order is checked on a
  // renderer's first frame. The bits of `skip` switch steps off (tests).
  writeMotion();
  graph_.clear();
  stampSteps_.clear(), stampSlot_ = -1;
  if (stampSet_)
    for (int k = 0; k < 6; ++k)
      if (!gs_->slots[k].busy) { stampSlot_ = k; break; }
  const bool clusterPath = raster && clustersReady() && eyeList_.cullGroup;
  const int rays = shadowLamps_ > 0 && !(skip & 512) ? 1 : 0;
  if (passUniformsDirty_) writePassUniforms();
  // 1. the sun's shadow
  graph_.add("shadow", {"scene", "sun list"}, {"shadow map"}, [&] {
    // one depth pass: into `view`, cleared or kept, drawing one cluster list
    // (or, without clusters, the batches)
    auto shadowPass = [&](const wgpu::TextureView& view, bool clear, const ClusterList* L) {
      wgpu::RenderPassDepthStencilAttachment da;
      da.view = view;
      da.depthLoadOp = clear ? wgpu::LoadOp::Clear : wgpu::LoadOp::Load;
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
      if (L) runCull(enc, *L);
      wgpu::PassTimestampWrites tw;
      rp.timestampWrites = stamp(tw);
      wgpu::RenderPassEncoder pass = enc.BeginRenderPass(&rp);
      if (L) {
        pass.SetPipeline(shadowVisPipe_);
        pass.SetBindGroup(0, sceneGroup0_);
        pass.SetBindGroup(1, emptyGroup_);
        pass.SetBindGroup(2, emptyGroup_);
        pass.SetBindGroup(3, L->drawGroup);
        pass.DrawIndirect(L->args, 0);
        ++drawCalls_;
      } else if (sunOn_ && !(skip & 1)) {
        pass.SetPipeline(shadowPipe_);
        pass.SetBindGroup(0, sceneGroup0_);
        drawBatches(pass, false, true);
      }
      pass.End();
    };
    if (splitting && sunMovers_.cullGroup) {
      // what stands still: drawn again only when it changed; then copied,
      // and what moved lately drawn on the copy, every frame
      if (shadowStale) {
        shadowPass(stillView_, true, &sunList_);
        changes_.made(shadowMade_, shadowFrom);
        stillDrawn_ = true;
        ++shadowRedraws_;
      }
      wgpu::TexelCopyTextureInfo from, to;
      from.texture = stillTex_, to.texture = shadowTex_;
      wgpu::Extent3D size{static_cast<uint32_t>(shadowSize_), static_cast<uint32_t>(shadowSize_), 1};
      enc.CopyTextureToTexture(&from, &to, &size);
      shadowPass(shadowLayerView_[0], false, &sunMovers_);
    } else {
      shadowPass(shadowLayerView_[0], true, clusterSun ? &sunList_ : nullptr);
      changes_.made(shadowMade_, shadowFrom);
      stillDrawn_ = false;
      ++shadowRedraws_;
    }
  }, raster && !(skip & 32) && (shadowStale || splitting));
  // the sun's far shadow: its pieces chosen, drawn into the shadow picture's second layer
  graph_.add("far shadow", {"scene"}, {"far shadow map"}, [&] {
    runCull(enc, sunFarList_);
    wgpu::RenderPassDepthStencilAttachment da;
    da.view = shadowLayerView_[farTarget_];
    da.depthLoadOp = farPart_ == 0 ? wgpu::LoadOp::Clear : wgpu::LoadOp::Load;
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
    wgpu::PassTimestampWrites tw;
    rp.timestampWrites = stamp(tw);
    wgpu::RenderPassEncoder pass = enc.BeginRenderPass(&rp);
    pass.SetPipeline(lampShadowPipe_);
    pass.SetBindGroup(0, sceneGroup0_);
    pass.SetBindGroup(1, sunFarGroup_);
    pass.SetBindGroup(2, emptyGroup_);
    pass.SetBindGroup(3, sunFarList_.drawGroup);
    pass.DrawIndirect(sunFarList_.args, 0);
    ++drawCalls_;
    pass.End();
    farTimedSlots_ = farPartSlots_.size();
    if (++farPart_ >= farParts_) {  // every part in: show it
      farShown_ = farTarget_, farShownViewProj_ = farBuildViewProj_;
      sunFarMade_ = farBuildMade_, farShownHard_ = farBuildHard_;
      farBuilding_ = false;
      ++shadowRedraws_;
    }
  }, raster && farStale && sunFarList_.cullGroup && sunFarList_.drawGroup && sunFarGroup_ && lampShadowPipe_);
  // the main lamps' shadow maps (Phone): each view's pieces chosen, then all
  // twelve views drawn into their tiles of one depth picture
  graph_.add("lamp shadows", {"scene"}, {"lamp maps"}, [&] {
    const int row = lampRowDrawing_;
    for (int i = row * 6; i < row * 6 + 6; ++i) runCull(enc, lampLists_[i]);
    wgpu::RenderPassDepthStencilAttachment da;
    da.view = lampLayerView_[row];
    da.depthLoadOp = wgpu::LoadOp::Clear;
    da.depthStoreOp = wgpu::StoreOp::Store;
    da.depthClearValue = 1;
    wgpu::RenderPassColorAttachment dc;
    dc.view = lampDummyView_;
    dc.loadOp = wgpu::LoadOp::Clear;
    dc.storeOp = wgpu::StoreOp::Discard;
    wgpu::RenderPassDescriptor rp;
    rp.colorAttachmentCount = 1;
    rp.colorAttachments = &dc;
    rp.depthStencilAttachment = &da;
    wgpu::PassTimestampWrites tw;
    rp.timestampWrites = stamp(tw);
    wgpu::RenderPassEncoder pass = enc.BeginRenderPass(&rp);
    pass.SetPipeline(lampShadowPipe_);
    pass.SetBindGroup(0, sceneGroup0_);
    pass.SetBindGroup(2, emptyGroup_);
    const float S = static_cast<float>(lampMapSize_);
    for (int i = row * 6; i < row * 6 + 6; ++i) {
      const float x = static_cast<float>(i % 6) * S, y = 0;
      pass.SetViewport(x, y, S, S, 0, 1);
      pass.SetScissorRect(static_cast<uint32_t>(x), static_cast<uint32_t>(y), static_cast<uint32_t>(S), static_cast<uint32_t>(S));
      pass.SetBindGroup(1, lampFaceGroup_[i]);
      pass.SetBindGroup(3, lampLists_[i].drawGroup);
      pass.DrawIndirect(lampLists_[i].args, 0);
      ++drawCalls_;
    }
    pass.End();
    const std::string name = "lamp row " + std::to_string(row);
    changes_.made(lampRowMade_[row], {"placements", "shapes", "settings", "movers", name});
    lampRowValid_[row] = 1;
    ++lampMapDraws_;
  }, lampMapsStale && lampListsReady());
  // the Ultra quality: the path tracer, then its noise cleaned, keeping edges
  graph_.add("ultra", {"scene", "rays", "lamps", "sky"}, {"hdr"}, [&] {
    wgpu::PassTimestampWrites tw;
    wgpu::ComputePassDescriptor cd;
    cd.timestampWrites = stamp(tw);
    wgpu::ComputePassEncoder cp = enc.BeginComputePass(&cd);
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
  }, ultra);
  // 2. the scene, in light (HDR). With clusters: the GPU culls and picks
  // each cluster's level, one draw writes which triangle each pixel sees,
  // then every pixel is lit once; the sky and glass follow as before.
  graph_.add("visibility", {"scene", "eye list", "depth pyramid"}, {"visibility", "depth", "depth pyramid"}, [&] {
    // one draw of a list's clusters into the visibility picture (the first
    // clears it, the second adds to it)
    auto visPass = [&](const ClusterList& L, bool first) {
      wgpu::RenderPassColorAttachment ca;
      ca.view = visView_;
      ca.loadOp = first ? wgpu::LoadOp::Clear : wgpu::LoadOp::Load;
      ca.storeOp = wgpu::StoreOp::Store;
      ca.clearValue = {0, 0, 0, 0};
      wgpu::RenderPassDepthStencilAttachment da;
      da.view = depthView_;
      da.depthLoadOp = first ? wgpu::LoadOp::Clear : wgpu::LoadOp::Load;
      da.depthStoreOp = wgpu::StoreOp::Store;
      da.depthClearValue = 1;
      wgpu::RenderPassDescriptor rp;
      rp.colorAttachmentCount = 1;
      rp.colorAttachments = &ca;
      rp.depthStencilAttachment = &da;
      wgpu::PassTimestampWrites tw;
      rp.timestampWrites = stamp(tw);
      wgpu::RenderPassEncoder pass = enc.BeginRenderPass(&rp);
      pass.SetViewport(0, 0, static_cast<float>(width_), static_cast<float>(height_), 0, 1);
      pass.SetBindGroup(0, sceneGroup0_);
      pass.SetBindGroup(1, sceneGroup1_);
      pass.SetBindGroup(2, rayGroup_);
      pass.SetBindGroup(3, L.drawGroup);
      pass.SetPipeline(visPipe_);
      pass.DrawIndirect(L.args, 0);
      ++drawCalls_;
      pass.End();
    };
    // what's hidden (phase 4): the first pass skips what last frame's walls
    // hid; this frame's walls are then made from it, and what was skipped is
    // tried against them, so nothing newly in view goes missing
    // choosing the pieces (the GPU's culling) timed apart from drawing them:
    // "finding" costs the same at any size, the drawing doesn't
    stampAs_ = "finding";
    runCull(enc, eyeList_);
    stampAs_ = nullptr;
    visPass(eyeList_, true);
    secondDrawn_ = false;
    if (occluding) {
      const bool second = hzbValid_ && eyeList2_.cullGroup && eyeList2_.drawGroup;
      buildHzb(enc);
      if (second) {
        stampAs_ = "finding";
        runCull(enc, eyeList2_);
        stampAs_ = nullptr;
        visPass(eyeList2_, false);
        secondDrawn_ = true;
      }
      hzbView_[hzbIndex_] = f.viewProj;
      hzbValid_ = true;
      hzbIndex_ ^= 1;
    }
  }, clusterPath && !(skip & 64));
  graph_.add("light", {"visibility", "depth", "shadow map", "far shadow map", "lamp maps", "lamps", "sky", "rays"}, {"hdr"}, [&] {
    {
      wgpu::RenderPassColorAttachment ca;
      ca.view = hdrView_;
      ca.loadOp = wgpu::LoadOp::Clear;
      ca.storeOp = wgpu::StoreOp::Store;
      ca.clearValue = {0, 0, 0, 1};
      wgpu::RenderPassDepthStencilAttachment da;
      da.view = depthView_;
      da.depthReadOnly = true;  // nothing here writes it; the ambient occlusion reads it
      da.depthClearValue = 1;   // unused, but the browser wants a number
      wgpu::RenderPassDescriptor rp;
      rp.colorAttachmentCount = 1;
      rp.colorAttachments = &ca;
      rp.depthStencilAttachment = &da;
      wgpu::PassTimestampWrites tw;
      rp.timestampWrites = stamp(tw);
      wgpu::RenderPassEncoder pass = enc.BeginRenderPass(&rp);
      pass.SetViewport(0, 0, static_cast<float>(width_), static_cast<float>(height_), 0, 1);
      pass.SetBindGroup(0, sceneGroup0_);
      pass.SetBindGroup(1, sceneGroup1_);
      pass.SetBindGroup(2, rayGroup_);
      pass.SetBindGroup(3, clusterResolveGroup_[histIndex_ * 2 + shadowIndex_]);
      pass.SetPipeline(resolvePipe_[rays]);
      pass.Draw(3);
      pass.SetPipeline(skyPipe_);
      pass.Draw(3);
      pass.SetPipeline(glassPipe_[rays]);
      drawBatches(pass, true, false);
      pass.End();
      shadowIndex_ ^= 1;  // this frame's kept shadow is next frame's past
    }
  }, clusterPath && !(skip & 64));
  // frame blending: this frame with the past, into the other picture of
  // the past, which then becomes the frame
  graph_.add("blend", {"hdr", "depth", "history"}, {"hdr", "history"}, [&] {
    struct {
      Mat4 prev, cur, inv;
      Vec4 screen, full, jitter;
    } u;
    // the picture written is the full size (outW_ x outH_), its pixels
    // unshifted: found through the steady view
    u.prev = histValid_ ? prevViewProj_ : steadyViewProj, u.cur = steadyViewProj, u.inv = steadyViewProj.inverse();
    u.screen = Vec4(static_cast<float>(width_), static_cast<float>(height_), 1.0f / width_, 1.0f / height_);
    u.full = Vec4(static_cast<float>(outW_), static_cast<float>(outH_), 0.2f, histValid_ ? 0.0f : 1.0f);
    u.jitter = Vec4(jitterPx_.x, jitterPx_.y, static_cast<float>(hdrTex_.GetWidth()), static_cast<float>(hdrTex_.GetHeight()));
    queue_.WriteBuffer(taaUbo_, 0, &u, sizeof u);
    int next = 1 - histIndex_;
    wgpu::RenderPassColorAttachment ca;
    ca.view = histView_[next];
    ca.loadOp = wgpu::LoadOp::Clear;
    ca.storeOp = wgpu::StoreOp::Store;
    ca.clearValue = {0, 0, 0, 1};
    wgpu::RenderPassDescriptor rp;
    rp.colorAttachmentCount = 1;
    rp.colorAttachments = &ca;
    wgpu::PassTimestampWrites tw;
    rp.timestampWrites = stamp(tw);
    wgpu::RenderPassEncoder pass = enc.BeginRenderPass(&rp);
    pass.SetViewport(0, 0, static_cast<float>(outW_), static_cast<float>(outH_), 0, 1);
    pass.SetPipeline(taaPipe_);
    pass.SetBindGroup(0, taaGroup_[histIndex_]);
    pass.Draw(3);
    pass.End();
    wgpu::TexelCopyTextureInfo from, to;
    from.texture = histTex_[next], to.texture = hdrTex_;
    wgpu::Extent3D size{outW_, outH_, 1};
    enc.CopyTextureToTexture(&from, &to, &size);
    histIndex_ = next;
    histValid_ = true;
    prevViewProj_ = steadyViewProj;
    prevCam_ = cam.position, prevScreen_[0] = width_, prevScreen_[1] = height_;
  }, clusterPath && upscaling_);
  // the drawing before clusters: each thing drawn and lit where it lands
  graph_.add("forward", {"scene", "shadow map", "far shadow map", "lamp maps", "lamps", "sky", "rays"}, {"hdr", "depth"}, [&] {
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
    wgpu::PassTimestampWrites tw;
    rp.timestampWrites = stamp(tw);
    wgpu::RenderPassEncoder pass = enc.BeginRenderPass(&rp);
    pass.SetViewport(0, 0, static_cast<float>(width_), static_cast<float>(height_), 0, 1);
    pass.SetBindGroup(0, sceneGroup0_);
    pass.SetBindGroup(1, sceneGroup1_);
    pass.SetBindGroup(2, rayGroup_);
    if (!(skip & 2)) {
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
  }, raster && !clusterPath && !(skip & 64));
  // 3. how bright it is: the eyes adjust
  graph_.add("exposure", {"hdr"}, {"exposure"}, [&] {
    wgpu::PassTimestampWrites tw;
    wgpu::ComputePassDescriptor cd;
    cd.timestampWrites = stamp(tw);
    wgpu::ComputePassEncoder cp = enc.BeginComputePass(&cd);
    cp.SetPipeline(exposurePipe_);
    cp.SetBindGroup(0, exposureGroup_);
    cp.DispatchWorkgroups(1);
    cp.End();
  }, !(skip & 4));
  // 4. bloom: down the chain, then back up, adding
  graph_.add("bloom", {"hdr"}, {"bloom"}, [&] {
    for (int i = 0; i < kBloomLevels; ++i) postPass(enc, bloomViews_[i], downPipe_, downGroups_[i], false, levelW(i), levelH(i));
    for (int i = kBloomLevels - 2; i >= 0; --i) postPass(enc, bloomViews_[i], upPipe_, upGroups_[i], true, levelW(i), levelH(i));
  }, !(skip & 8));
  // 5. the film, onto the screen
  graph_.add("film", {"hdr", "bloom", "exposure"}, {"screen"}, [&] { postPass(enc, st.texture.CreateView(), finalPipe_, finalGroup_, false); },
             !(skip & (16 | 256)));
  if (!graphChecked_) {
    graphChecked_ = true;
    for (const std::string& fault : graph_.check()) reportGpuError("the frame's steps: " + fault);
  }
  graph_.run();
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
  const bool stamped = stampSlot_ >= 0 && !stampSteps_.empty();
  if (stamped) {
    GpuState::StampSlot& slot = gs_->slots[stampSlot_];
    const uint32_t n = 2 * static_cast<uint32_t>(stampSteps_.size());
    enc.ResolveQuerySet(stampSet_, 0, n, stampResolve_, 0);
    enc.CopyBufferToBuffer(stampResolve_, 0, slot.buf, 0, 8ull * n);
    slot.steps = stampSteps_, slot.frame = presented_ + 1, slot.busy = true;
  }
  // now and then, how many pieces the view drew (each list's count of copies)
  const bool counting = piecesWanted_ && !gs_->piecesBusy && eyeList_.args;
  if (counting) {
    if (!gs_->piecesBuf) gs_->piecesBuf = buffer(16, wgpu::BufferUsage::MapRead | wgpu::BufferUsage::CopyDst);
    enc.CopyBufferToBuffer(eyeList_.args, 4, gs_->piecesBuf, 0, 4);
    if (secondDrawn_ && eyeList2_.args) enc.CopyBufferToBuffer(eyeList2_.args, 4, gs_->piecesBuf, 4, 4);
    else enc.ClearBuffer(gs_->piecesBuf, 4, 4);
    if (sunList_.args) enc.CopyBufferToBuffer(sunList_.args, 4, gs_->piecesBuf, 8, 4);
    else enc.ClearBuffer(gs_->piecesBuf, 8, 4);
    gs_->piecesBusy = true;
  }
  wgpu::CommandBuffer cb = enc.Finish();
  queue_.Submit(1, &cb);
  if (stamped) {
    // read when the chip is done, a frame or two later: the frame never waits for it
    const size_t bytes = 16 * stampSteps_.size();
    const int k = stampSlot_;
    gs_->slots[k].buf.MapAsync(wgpu::MapMode::Read, 0, bytes, wgpu::CallbackMode::AllowSpontaneous,
                               [gs = gs_, k, bytes](wgpu::MapAsyncStatus status, wgpu::StringView) {
                                 GpuState::StampSlot& sl = gs->slots[k];
                                 if (status == wgpu::MapAsyncStatus::Success) {
                                   const uint64_t* tick = static_cast<const uint64_t*>(sl.buf.GetConstMappedRange(0, bytes));
                                   std::vector<FrameGraph::Stamp> st;
                                   if (tick)
                                     for (size_t i = 0; i < sl.steps.size(); ++i) st.push_back({sl.steps[i], tick[2 * i], tick[2 * i + 1]});
                                   sl.buf.Unmap();
                                   gs->steps.addTimes(st, sl.frame);
                                 }
                                 sl.busy = false;
                               });
  }
  if (counting)
    gs_->piecesBuf.MapAsync(wgpu::MapMode::Read, 0, 16, wgpu::CallbackMode::AllowSpontaneous,
                            [gs = gs_](wgpu::MapAsyncStatus status, wgpu::StringView) {
                              if (status == wgpu::MapAsyncStatus::Success) {
                                if (const uint32_t* n = static_cast<const uint32_t*>(gs->piecesBuf.GetConstMappedRange(0, 16)))
                                  gs->pieces[0] = n[0], gs->pieces[1] = n[1], gs->pieces[2] = n[2];
                                gs->piecesBuf.Unmap();
                              }
                              gs->piecesBusy = false;
                            });
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
    capturePixels(device_.Get(), captureTex.Get(), static_cast<int>(outW_), static_cast<int>(outH_), format_ == wgpu::TextureFormat::BGRA8Unorm ? 1 : 0);
  }
  ++presented_;
  return true;
}

bool GpuRenderer::lampListsReady() const {
  if (lampRowDrawing_ < 0) return false;
  for (int i = lampRowDrawing_ * 6; i < lampRowDrawing_ * 6 + 6; ++i)
    if (!lampLists_[i].cullGroup || !lampLists_[i].drawGroup || !lampFaceGroup_[i]) return false;
  return lampMapView_ != nullptr;
}

std::vector<std::pair<std::string, float>> GpuRenderer::stepMs() const {
  std::vector<std::pair<std::string, float>> out;
  for (const FrameGraph::Time& t : gs_->steps.recent()) out.push_back({t.step, t.ms});
  return out;
}

std::array<uint32_t, 3> GpuRenderer::piecesDrawn() const {
  piecesWanted_ = true;  // counted only once someone asks
  return {gs_->pieces[0], gs_->pieces[1], gs_->pieces[2]};
}

size_t GpuRenderer::gpuBytes() const {
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
                                &denoiseA_, &denoiseB_, &rayTris_, &rayBlas_, &rayInst_, &rayTlas_, &poolVerts_, &poolMeshes_, &eyeList_.drawn, &eyeList_.stretch, &eyeList_.cullList, &eyeList_.counts})
    n += buf(*b);
  for (const wgpu::Buffer& b : passUbo_) n += buf(b);
  for (const wgpu::Buffer& b : denoiseUbo_) n += buf(b);
  for (const wgpu::Texture* t : {&modelTex_, &shadowTex_, &shadowDummyTex_, &skyTex_, &msaaTex_, &depthTex_, &hdrTex_, &visTex_, &histTex_[0], &histTex_[1]}) n += tex(*t);
  for (const wgpu::Texture& t : bloomTex_) n += tex(t);
  return n;
}

void GpuRenderer::adaptResolution(double now) {
  static constexpr float kSteps[] = {1.0f, 0.87f, 0.75f, 0.65f, 0.56f, 0.5f, 0.42f, 0.35f, 0.3f, 0.25f, 0.2f, 0.175f};
  constexpr int kLast = static_cast<int>(sizeof kSteps / sizeof kSteps[0]) - 1;
  const Config::Render& cr = config().render;
  if (cr.res > 0) {
    renderScale_ = std::clamp(cr.res, 0.2f, 1.0f);
    return;
  }
  if (judgedAt_ == 0) judgedAt_ = now;
  double secs = (now - judgedAt_) / 1000.0;
  // render.floor / floorUltra: the smallest size it may drop to
  // (of 1 pixel a point: with frame blending upscaling into 2 a point, the drawing
  // may go as small as it could before, the picture staying sharper than it was)
  float floorScale = quality_ == Quality::kUltra ? cr.floorUltra : cr.floor / std::max(upscaling_ ? ratio_ : 1.0f, 1.0f);
  int lowest = 0;
  while (lowest < kLast && kSteps[lowest + 1] >= floorScale - 1e-3f) ++lowest;
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
  // with the chip's timers: only the steps that work per drawn pixel shrink
  // with the scale (engine/render/resolution.h); the rest (shadow maps, and
  // since frame blending upscales, blending, bloom and the film) stays
  const std::vector<FrameGraph::Time> recent = gs_->steps.recent();
  // each step's own share that shrinks, learned from its timings at the sizes it has had
  if (sinceScale_ >= 1.0f) scaleLearned_.observe(recent, kSteps[step_]);
  FrameSplit split = splitFrame(recent, upscaling_, scaleLearned_, kSteps[step_]);
  const bool timed = !recent.empty() && split.scaledMs > 0 && split.fixedMs + split.scaledMs > gpu * 0.5f;
  if (stuck || (gpuBound && fps < aim * 0.88f)) {
    if (step_ < lowest && (sinceScale_ > 1.2f || stuck || gpu > budget * 2)) {
      // cost goes with the number of pixels, the square of the scale
      float fit = slowGpu ? kSteps[step_] * std::sqrt(budget * 0.85f / gpu) : kSteps[step_] * 0.9f;
      bool hold = false;
      if (timed && !stuck) {
        // the steady work's own fit; when shrinking can't save the time (the
        // part that doesn't scale fills the budget alone) the size holds
        // (only a frame over twice the budget still steps down)
        fit = scaleToFit(split, kSteps[step_], budget * 0.85f);
        hold = fit >= kSteps[step_] - 1e-3f && gpu <= budget * 2;
      }
      int to = stuck ? lowest : hold ? step_ : std::max(step_ + 1, stepFor(fit));
      if (!hold) ceilStep_ = std::max(ceilStep_, step_ + 1);  // this size was too much: don't come straight back
      step_ = std::min(lowest, to);
      calmSeconds_ = longCalm_ = 0, sinceScale_ = 0;
    }
  } else if (fps >= aim * 0.97f) {
    ++calmSeconds_, ++longCalm_;
    float up = step_ > 0 ? kSteps[step_ - 1] / kSteps[step_] : 1.0f;
    bool fits = timed ? msAtScale(split, kSteps[step_], kSteps[std::max(step_ - 1, 0)]) < budget * cr.climbUnder / 100
                      : gpu * up * up < budget * cr.climbUnder / 100;
    if (calmSeconds_ >= cr.calm && step_ > ceilStep_ && fits) --step_, calmSeconds_ = 0, sinceScale_ = 0;
    // Ultra restarts its picture on every change: it doesn't go looking for more
    if (longCalm_ >= 20 && ceilStep_ > 0 && quality_ != Quality::kUltra) --ceilStep_, longCalm_ = 0;
  } else {
    calmSeconds_ = 0;
  }
  step_ = std::min(step_, lowest);
  renderScale_ = kSteps[step_];
}

}  // namespace loop::gpu::impl
