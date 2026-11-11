// The live renderer: its shadows part (see renderer_internal.h).
#include "engine/gpu/renderer_internal.h"

namespace loop::gpu::impl {

void GpuRenderer::makeShadowMap() {
  changes_.bump("settings");  // a new shadow picture holds nothing yet
  if (shadowTex_) shadowTex_.Destroy();
  if (stillTex_) stillTex_.Destroy();
  if (shadowDummyTex_) shadowDummyTex_.Destroy();
  wgpu::TextureDescriptor td;
  td.size = {static_cast<uint32_t>(shadowSize_), static_cast<uint32_t>(shadowSize_), 3};  // near, far (two: one shown, one being drawn)
  td.format = wgpu::TextureFormat::Depth32Float;
  td.usage = wgpu::TextureUsage::RenderAttachment | wgpu::TextureUsage::TextureBinding | wgpu::TextureUsage::CopyDst;
  shadowTex_ = device_.CreateTexture(&td);
  {
    wgpu::TextureViewDescriptor vd;
    vd.dimension = wgpu::TextureViewDimension::e2DArray;
    shadowView_ = shadowTex_.CreateView(&vd);
    for (uint32_t l = 0; l < 3; ++l) {
      wgpu::TextureViewDescriptor ld;
      ld.dimension = wgpu::TextureViewDimension::e2D;
      ld.baseArrayLayer = l, ld.arrayLayerCount = 1;
      shadowLayerView_[l] = shadowTex_.CreateView(&ld);
    }
  }
  sunFarValid_ = false;
  td.size.depthOrArrayLayers = 1;
  // the shadow of what stands still, kept while things move (copied, then the movers drawn on it)
  td.usage = wgpu::TextureUsage::RenderAttachment | wgpu::TextureUsage::CopySrc;
  stillTex_ = device_.CreateTexture(&td);
  stillView_ = stillTex_.CreateView();
  stillDrawn_ = false;
  td.format = wgpu::TextureFormat::R8Unorm;
  td.usage = wgpu::TextureUsage::RenderAttachment;
  shadowDummyTex_ = device_.CreateTexture(&td);
  shadowDummy_ = shadowDummyTex_.CreateView();
  bindGroupsDirty_ = true;
}

void GpuRenderer::startSunFar() {
  farBuildViewProj_ = sunFarViewProj_, farBuildEye_ = sunFarEye_;
  const Frustum fr = Frustum::fromViewProj(farBuildViewProj_);
  sunFarSlots_.clear();
  for (uint32_t slot = 0; slot < instData_.size() && slot < instBox_.size(); ++slot) {
    if (slot < instHidden_.size() && instHidden_[slot]) continue;
    if (slot < isMover_.size() && isMover_[slot]) continue;
    if (fr.visible(instBox_[slot])) sunFarSlots_.push_back(slot);
  }
  // as many parts as keep each near kFarPartMs (8 until it has been timed), at most 16
  const float ms = farMsPerSlot_ > 0 ? farMsPerSlot_ * static_cast<float>(sunFarSlots_.size()) : 8 * kFarPartMs;
  farParts_ = std::clamp(static_cast<int>(std::ceil(ms / kFarPartMs)), 1, 16);
  farParts_ = std::max(1, std::min(farParts_, static_cast<int>(sunFarSlots_.size())));
  farPart_ = 0, farTarget_ = farShown_ == 1 ? 2 : 1, farBuilding_ = true;
}

void GpuRenderer::prepareSunFar(float perPixel) {
  const size_t n = sunFarSlots_.size(), a = n * farPart_ / farParts_, b = n * (farPart_ + 1) / farParts_;
  farPartSlots_.assign(sunFarSlots_.begin() + static_cast<long>(a), sunFarSlots_.begin() + static_cast<long>(b));
  prepareSlots(sunFarList_, farPartSlots_, farBuildViewProj_, farBuildEye_, perPixel);
  if (!sunFarUbo_) sunFarUbo_ = buffer(sizeof(Mat4), wgpu::BufferUsage::Uniform | wgpu::BufferUsage::CopyDst), bindGroupsDirty_ = true;
  queue_.WriteBuffer(sunFarUbo_, 0, &farBuildViewProj_, sizeof(Mat4));
}

void GpuRenderer::makeLampMap(int size) {
  lampMapSize_ = size;
  if (lampMapTex_) lampMapTex_.Destroy();
  if (lampDummyTex_) lampDummyTex_.Destroy();
  wgpu::TextureDescriptor td;
  td.size = {size ? static_cast<uint32_t>(size) * 6 : 1u, size ? static_cast<uint32_t>(size) : 1u, kLampMapsMost};
  td.format = wgpu::TextureFormat::Depth32Float;
  td.usage = wgpu::TextureUsage::RenderAttachment | wgpu::TextureUsage::TextureBinding;
  lampMapTex_ = device_.CreateTexture(&td);
  wgpu::TextureViewDescriptor vd;
  vd.dimension = wgpu::TextureViewDimension::e2DArray;
  lampMapView_ = lampMapTex_.CreateView(&vd);
  for (int l = 0; l < kLampMapsMost; ++l) {  // each lamp's layer, to draw into
    wgpu::TextureViewDescriptor ld;
    ld.dimension = wgpu::TextureViewDimension::e2D;
    ld.baseArrayLayer = static_cast<uint32_t>(l), ld.arrayLayerCount = 1;
    lampLayerView_[l] = lampMapTex_.CreateView(&ld);
  }
  td.size.depthOrArrayLayers = 1;
  td.format = wgpu::TextureFormat::R8Unorm;
  td.usage = wgpu::TextureUsage::RenderAttachment;
  lampDummyTex_ = device_.CreateTexture(&td);
  lampDummyView_ = lampDummyTex_.CreateView();
  changes_.bump("settings");
  bindGroupsDirty_ = true;
}

void GpuRenderer::keepLampRows(std::vector<std::pair<float, uint32_t>>& rank) {
  // a lamp set `shadows: no` goes behind the panels, so neither a map row nor
  // one of the shader's shadow rays is ever given to it
  auto noShadow = [&](const std::pair<float, uint32_t>& e) { return e.second < lampNoShadow_.size() && lampNoShadow_[e.second]; };
  std::stable_partition(rank.begin(), rank.end(), [&](const auto& e) { return !noShadow(e); });
  if (!lampMapsWanted()) return;
  const int want = std::clamp(config().render.lampMaps, 0, kLampMapsMost);
  std::vector<uint32_t> top;
  for (size_t i = 0; i < rank.size() && static_cast<int>(top.size()) < want; ++i) {
    if (lights_[rank[i].second].dir.w > 1.5f) break;  // panels go last: no more bulbs
    top.push_back(rank[i].second);
  }
  uint32_t row[kLampMapsMost] = {~0u, ~0u};
  std::vector<bool> placed(top.size(), false);
  for (size_t r = 0; r < top.size(); ++r)
    for (size_t t = 0; t < top.size(); ++t)
      if (!placed[t] && top[t] == lampRowLamp_[r]) row[r] = top[t], placed[t] = true;
  for (size_t r = 0; r < top.size(); ++r)
    for (size_t t = 0; t < top.size() && row[r] == ~0u; ++t)
      if (!placed[t]) row[r] = top[t], placed[t] = true;
  for (int r = 0; r < kLampMapsMost; ++r) lampRowLamp_[r] = r < static_cast<int>(top.size()) ? row[r] : ~0u;
  // the rows' lamps first, in row order, then the rest as they were
  std::vector<std::pair<float, uint32_t>> out;
  for (size_t r = 0; r < top.size(); ++r)
    for (auto& e : rank)
      if (e.second == row[r]) out.push_back(e);
  for (auto& e : rank)
    if (std::find(top.begin(), top.end(), e.second) == top.end()) out.push_back(e);
  rank.swap(out);
}

float GpuRenderer::lampMapReach(const LightU& L) {
  return std::clamp(std::sqrt(std::max(luminance(L.color.xyz()), 0.0f) / 0.25f), 2.0f, 15.0f);
}

void GpuRenderer::chooseLampMaps(FrameU& f) {
  const auto& r = config().render;
  const bool want = lampMapsWanted();
  const int size = want ? std::clamp(r.lampMapSize, 64, 1024) : 0;
  if (!lampMapTex_ || size != lampMapSize_) makeLampMap(size);
  int count = 0;
  while (want && count < std::min(r.lampMaps, kLampMapsMost) && count < static_cast<int>(sortedLights_.size()) &&
         sortedLights_[static_cast<size_t>(count)].dir.w < 1.5f)
    ++count;
  lampMapCount_ = count;
  static const Vec3 axis[6] = {{1, 0, 0}, {-1, 0, 0}, {0, 1, 0}, {0, -1, 0}, {0, 0, 1}, {0, 0, -1}};
  lampRowDrawing_ = -1;
  for (int l = 0; l < count; ++l) {
    const LightU& L = sortedLights_[static_cast<size_t>(l)];
    const Vec3 p = L.pos.xyz();
    // its reach worked out when it takes the row (a flickering flame keeps it)
    if (length(lampRowPos_[l] - p) > 0.01f || lampRowReach_[l] <= 0) lampRowPos_[l] = p, lampRowReach_[l] = lampMapReach(L);
    const float reach = lampRowReach_[l];
    Mat4 proj = Mat4::perspective(kPi / 2, 1.0f, std::max(L.pos.w * 1.2f, 0.05f), reach);
    for (int a = 0; a < 6; ++a) {
      Vec3 up = a == 2 || a == 3 ? Vec3(0, 0, 1) : Vec3(0, 1, 0);
      lampFaceMat_[l * 6 + a] = proj * Mat4::lookAt(p, p + axis[a], up);
      f.lampFaces[l * 6 + a] = lampFaceMat_[l * 6 + a];
    }
    // each row kept until its own lamp or the scene changes
    struct {
      Vec4 lamp;
      float reach;
      int size;
    } key{L.pos, reach, size};
    const std::string name = "lamp row " + std::to_string(l);
    changes_.set(name, key);
    const bool stale = changes_.stale(lampRowMade_[l], {"placements", "shapes", "settings", "movers", name});
    if (stale) lampRowValid_[l] = 0;
    if (stale && lampRowDrawing_ < 0) lampRowDrawing_ = l;
  }
  for (int l = count; l < kLampMapsMost; ++l) lampRowValid_[l] = 0;
  // z: High's lamps with no shadow of their own share one traced shadow ray (one picked a pixel)
  f.lampMaps = {static_cast<float>(count), static_cast<float>(std::max(size, 1)), sharedLampShadows() ? 1.0f : 0.0f, 0};
  f.lampValid = {lampRowValid_[0], lampRowValid_[1], 0, 0};
}

void GpuRenderer::prepareLampMaps() {
  const int l = lampRowDrawing_;
  if (l < 0) return;
  const LightU& L = sortedLights_[static_cast<size_t>(l)];
  const Vec3 p = L.pos.xyz();
  const float reach = lampRowReach_[l];
  lampSlots_.clear();
  for (uint32_t slot = 0; slot < instData_.size() && slot < instBox_.size(); ++slot) {
    if (slot < instHidden_.size() && instHidden_[slot]) continue;
    if (slot < isMover_.size() && isMover_[slot]) continue;
    const AABB& b = instBox_[slot];
    if (length(p - vmin(vmax(p, b.lo), b.hi)) > reach) continue;
    lampSlots_.push_back(slot);
  }
  for (int a = 0; a < 6; ++a) {
    prepareSlots(lampLists_[l * 6 + a], lampSlots_, lampFaceMat_[l * 6 + a], p, static_cast<float>(lampMapSize_) * 0.5f);
    if (!lampFaceUbo_[l * 6 + a]) lampFaceUbo_[l * 6 + a] = buffer(sizeof(Mat4), wgpu::BufferUsage::Uniform | wgpu::BufferUsage::CopyDst), bindGroupsDirty_ = true;
    queue_.WriteBuffer(lampFaceUbo_[l * 6 + a], 0, &lampFaceMat_[l * 6 + a], sizeof(Mat4));
  }
}

void GpuRenderer::updateMovers() {
  const uint64_t now = presented_ + 1;
  std::vector<uint32_t> next;
  for (uint32_t slot : movers_)
    if (slot < lastMoved_.size() && lastMoved_[slot] && now - lastMoved_[slot] < kMoverFrames) next.push_back(slot);
  for (uint32_t slot : movedNow_)
    if (slot < lastMoved_.size() && lastMoved_[slot] == now) next.push_back(slot);
  std::sort(next.begin(), next.end());
  next.erase(std::unique(next.begin(), next.end()), next.end());
  if (next != movers_) {
    // the light cache: a thing that came to rest is relit round where it was
    // and where it is now
    if (moverFrom_.size() < instBox_.size()) moverFrom_.resize(instBox_.size());
    for (uint32_t slot : next)
      if (!std::binary_search(movers_.begin(), movers_.end(), slot) && slot < instBox_.size()) moverFrom_[slot] = instBox_[slot];
    for (uint32_t slot : movers_)
      if (!std::binary_search(next.begin(), next.end(), slot) && slot < instBox_.size()) {
        AABB b = instBox_[slot];
        const AABB& from = moverFrom_[slot];
        // only a real move (a sway or a breath in place changes nothing)
        if (length((b.lo + b.hi) * 0.5f - (from.lo + from.hi) * 0.5f) < 0.25f) continue;
        b.grow(from);
        relight_.push_back(AABB(b.lo - Vec3(1), b.hi + Vec3(1))), relightFrom_.push_back(Vec3(std::numeric_limits<float>::quiet_NaN()));
      }
    movers_.swap(next);
    changes_.bump("movers");
    isMover_.assign(instData_.size(), 0);
    for (uint32_t slot : movers_)
      if (slot < isMover_.size()) isMover_[slot] = 1;
  }
}

GpuRenderer::HzbSize GpuRenderer::hzbSizeNow() const {
  HzbSize h;
  h.x = std::max(1u, (width_ + 1) / 2), h.y = std::max(1u, (height_ + 1) / 2);
  h.w0 = std::max(0.5f, width_ * 0.5f), h.h0 = std::max(0.5f, height_ * 0.5f);
  uint32_t m = std::max(h.x, h.y), l = 1;
  while ((1u << (l - 1)) < m) ++l;
  h.z = std::min(l, std::max(hzbLevels_, 1u));
  return h;
}

void GpuRenderer::buildHzb(wgpu::CommandEncoder& enc) {
  const int i = hzbIndex_;
  HzbSize h = hzbSizeNow();
  uint32_t sw = width_, sh = height_;
  for (uint32_t l = 0; l < h.z && l < hzbGroup_[i].size(); ++l) {
    uint32_t dw = std::max(1u, (h.x + (1u << l) - 1) >> l), dh = std::max(1u, (h.y + (1u << l) - 1) >> l);
    float u[8] = {static_cast<float>(sw), static_cast<float>(sh), 0, 0, static_cast<float>(dw), static_cast<float>(dh), 0, 0};
    queue_.WriteBuffer(hzbUbo_[l], 0, u, sizeof u);
    wgpu::RenderPassColorAttachment ca;
    ca.view = hzbMip_[i][l];
    ca.loadOp = wgpu::LoadOp::Clear;
    ca.storeOp = wgpu::StoreOp::Store;
    ca.clearValue = {1, 0, 0, 1};
    wgpu::RenderPassDescriptor rp;
    rp.colorAttachmentCount = 1;
    rp.colorAttachments = &ca;
    wgpu::PassTimestampWrites tw;
    rp.timestampWrites = stamp(tw);
    wgpu::RenderPassEncoder pass = enc.BeginRenderPass(&rp);
    pass.SetViewport(0, 0, static_cast<float>(dw), static_cast<float>(dh), 0, 1);
    pass.SetPipeline(l == 0 ? hzbDepthPipe_ : hzbLevelPipe_);
    pass.SetBindGroup(0, hzbGroup_[i][l]);
    pass.Draw(3);
    pass.End();
    sw = dw, sh = dh;
  }
  hzbSize_[i] = h;
}

}  // namespace loop::gpu::impl
