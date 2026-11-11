// The live renderer: its light part (see renderer_internal.h).
#include "engine/gpu/renderer_internal.h"

namespace loop::gpu::impl {

void GpuRenderer::watchLampsForCache() {
  const double nowMs = emscripten_get_now();
  const float blend = lampWatchedAt_ > 0 ? 1.0f - std::exp(-static_cast<float>(nowMs - lampWatchedAt_) / 700.0f) : 1.0f;  // ~0.7 s
  lampWatchedAt_ = nowMs;
  // a lamp's light: its brightness (a panel's times its size)
  auto lightOf = [](const LightU& L) { return luminance(L.color.xyz()) * (L.dir.w > 1.5f ? L.cone.z * L.cone.w : 1.0f); };
  if (lampAvg_.size() != lights_.size()) {
    lampAvg_.resize(lights_.size());
    for (size_t k = 0; k < lights_.size(); ++k) lampAvg_[k] = lightOf(lights_[k]);
    lampBaked_ = lampAvg_;
    return;
  }
  if (lampBaked_.size() != lights_.size()) lampBaked_ = lampAvg_;
  for (size_t k = 0; k < lights_.size(); ++k) {
    const LightU& L = lights_[k];
    const float now = lightOf(L);
    lampAvg_[k] += (now - lampAvg_[k]) * blend;
    const float was = lampBaked_[k];
    if (std::fabs(lampAvg_[k] - was) > 0.35f * std::max(was, 1e-3f)) {
      // where its light makes a difference: out to half a lux (a candle about
      // 2 m, a 900 lm fire about 12 m)
      const float reach = std::clamp(std::sqrt(std::max(lampAvg_[k], was) / 0.5f), 2.0f, 30.0f);
      relight_.push_back(AABB(L.pos.xyz() - Vec3(reach), L.pos.xyz() + Vec3(reach))), relightFrom_.push_back(L.pos.xyz());
      lampBaked_[k] = lampAvg_[k];
    }
  }
}

void GpuRenderer::makeCacheTextures() {
  const uint32_t x = cache_.on ? cache_.nx : 1, y = cache_.on ? cache_.ny : 1, z = cache_.on ? cache_.nz : 1;
  for (int i = 0; i < 6; ++i) {
    wgpu::TextureDescriptor td;
    td.size = {x, y, z};
    td.dimension = wgpu::TextureDimension::e3D;
    td.format = wgpu::TextureFormat::RGBA16Float;
    td.usage = wgpu::TextureUsage::TextureBinding | wgpu::TextureUsage::CopyDst;
    cacheTex_[i] = device_.CreateTexture(&td);
    cacheView_[i] = cacheTex_[i].CreateView();
  }
  bindGroupsDirty_ = true;
}

Vec3 GpuRenderer::skyIrradiance(Vec3 n) const {
  const float c1 = 0.429043f, c2 = 0.511664f, c3 = 0.743125f, c4 = 0.886227f, c5 = 0.247708f;
  const Vec3* L = sh_;
  Vec3 e = L[8] * (c1 * (n.x * n.x - n.y * n.y)) + L[6] * (c3 * n.z * n.z) + L[0] * c4 - L[6] * c5 +
           (L[4] * (n.x * n.y) + L[7] * (n.x * n.z) + L[5] * (n.y * n.z)) * (2 * c1) + (L[3] * n.x + L[1] * n.y + L[2] * n.z) * (2 * c2);
  return vmax(e, Vec3(0));
}

void GpuRenderer::skyProbe(int i) {
  static const Vec3 axis[6] = {{1, 0, 0}, {-1, 0, 0}, {0, 1, 0}, {0, -1, 0}, {0, 0, 1}, {0, 0, -1}};
  for (int f = 0; f < 6; ++f) cache_.face[static_cast<size_t>(i)][static_cast<size_t>(f)] = skyIrradiance(axis[f]);
  if (cache_.sky.size() == cache_.face.size()) cache_.sky[static_cast<size_t>(i)] = {1, 1, 1, 1, 1, 1};  // not baked yet: as if open
}

void GpuRenderer::sendCache() {
  if (!cache_.on) return;
  const LightCache sent = smoothed(cache_);
  std::vector<uint16_t> px[6];
  const size_t n = static_cast<size_t>(cache_.probes());
  for (auto& v : px) v.resize(n * 4);
  for (size_t i = 0; i < n; ++i)
    for (int f = 0; f < 6; ++f) {
      uint16_t* o = &px[f][i * 4];
      const Vec3 e = sent.face[i][static_cast<size_t>(f)];
      // alpha: how much of the face's view is open sky
      o[0] = toHalf(e.x), o[1] = toHalf(e.y), o[2] = toHalf(e.z), o[3] = toHalf(sent.sky.size() == n ? sent.sky[i][static_cast<size_t>(f)] : 1.0f);
    }
  for (int c = 0; c < 6; ++c) {
    wgpu::TexelCopyTextureInfo dst;
    dst.texture = cacheTex_[c];
    wgpu::TexelCopyBufferLayout lay;
    lay.bytesPerRow = static_cast<uint32_t>(cache_.nx) * 8, lay.rowsPerImage = static_cast<uint32_t>(cache_.ny);
    wgpu::Extent3D ext{static_cast<uint32_t>(cache_.nx), static_cast<uint32_t>(cache_.ny), static_cast<uint32_t>(cache_.nz)};
    queue_.WriteTexture(&dst, px[c].data(), px[c].size() * 2, &lay, &ext);
  }
  cacheUnsent_ = 0;
}

void GpuRenderer::keepLightCache(Vec3 eye) {
  const auto& r = config().render;
  if (!r.lightCache || !lastScene_ || quality_ == Quality::kUltra) {
    if (cache_.on) cache_ = LightCache(), makeCacheTextures();
    tracer_.reset();
    return;
  }
  static const std::vector<std::string> kFrom = {"shapes", "placements", "lamp places", "settings"};
  // started again at most every two seconds (a carried lamp moves every
  // frame); but the old bake stops at once: its path tracer points into the
  // scene as it was, and a changed scene may have moved or freed that
  const bool changed = changes_.stale(cacheMade_, kFrom);
  if (changed) tracer_.reset();
  if (changed && emscripten_get_now() - cacheStarted_ > 2000) {
    cacheStarted_ = emscripten_get_now();
    changes_.made(cacheMade_, kFrom);
    // the probes over every copy (a probe within reach of the scene's things)
    AABB box;
    for (size_t i = 0; i < instBox_.size(); ++i)
      if (i >= instHidden_.size() || !instHidden_[i]) box.grow(instBox_[i]);
    LightCache next = layOutLightCache(box, std::clamp(r.lightCacheProbes, 64, 8192), 1.0f);
    const bool same = next.on && cache_.on && next.nx == cache_.nx && next.ny == cache_.ny && next.nz == cache_.nz &&
                      length(next.origin - cache_.origin) < 1e-3f && std::fabs(next.spacing - cache_.spacing) < 1e-4f;
    if (!same) {
      cache_ = std::move(next);
      for (int i = 0; i < cache_.probes(); ++i) skyProbe(i);
      makeCacheTextures();
      sendCache();
    } else {
      std::fill(cache_.state.begin(), cache_.state.end(), LightCache::kWaiting);  // baked again, keeping their light meanwhile
    }
    tracer_ = std::make_unique<PathTracer>();
    tracer_->setScene(*lastScene_);
    cacheOrder_ = bakeOrder(cache_, eye);
    cacheNext_ = 0;
    relight_.clear(), relightFrom_.clear();
    lampBaked_ = lampAvg_;
  }
  watchLampsForCache();
  // relit where something changed (a lamp's light, a thing that moved and
  // came to rest): only the probes near it, nearest the eye first, with a
  // fresh path tracer (it holds the lamps and placements as they were);
  // at most every two seconds, like a full bake
  if (!relight_.empty() && !changed && emscripten_get_now() - cacheStarted_ > 2000 && cache_.on) {
    int marked = 0;
    // a lamp's light only where it can reach through the rooms (phase 10); a moved thing's box as it is
    for (size_t k = 0; k < relight_.size(); ++k) {
      const Vec3 at = k < relightFrom_.size() ? relightFrom_[k] : Vec3(std::numeric_limits<float>::quiet_NaN());
      marked += std::isnan(at.x) ? markNear(cache_, relight_[k]) : markReached(cache_, relight_[k], at);
    }
    relight_.clear(), relightFrom_.clear();
    if (marked > 0 || (tracer_ && cacheNext_ < cacheOrder_.size())) {
      cacheStarted_ = emscripten_get_now();
      tracer_ = std::make_unique<PathTracer>();
      tracer_->setScene(*lastScene_);
      cacheOrder_ = waitingOrder(cache_, eye);
      cacheNext_ = 0;
      cacheRelit_ += static_cast<uint64_t>(marked);
    }
  }
  if (!tracer_ || !cache_.on) return;
  // a few milliseconds of probes (at least one), then sent now and then
  const double start = emscripten_get_now(), budget = std::clamp(r.lightCacheMs, 0.25f, 60000.0f);  // tests: everything at once
  while (cacheNext_ < cacheOrder_.size()) {
    const int i = cacheOrder_[cacheNext_++];
    bakeProbe(cache_, i, *tracer_, 64, 2, static_cast<uint32_t>(cacheBaked_ + 1));
    ++cacheBaked_, ++cacheUnsent_;
    if (emscripten_get_now() - start > budget) break;
  }
  const bool done = cacheNext_ >= cacheOrder_.size();
  if (cacheUnsent_ && (++cacheFrames_ % 15 == 0 || done)) {
    fillInside(cache_);
    sendCache();
  }
  if (done) tracer_.reset();  // its tens of MB back
}

wgpu::Texture GpuRenderer::gridTexture(wgpu::TextureDimension dim, uint32_t x, uint32_t y, uint32_t z) {
  wgpu::TextureDescriptor td;
  td.size = {std::max(1u, x), std::max(1u, y), std::max(1u, z)};
  td.format = wgpu::TextureFormat::R32Uint, td.dimension = dim;
  td.usage = wgpu::TextureUsage::TextureBinding | wgpu::TextureUsage::CopyDst;
  return device_.CreateTexture(&td);
}

void GpuRenderer::writeGridTexture(const wgpu::Texture& t, const void* data, uint32_t x, uint32_t y, uint32_t z) {
  wgpu::TexelCopyTextureInfo dst;
  dst.texture = t;
  wgpu::TexelCopyBufferLayout lay;
  lay.bytesPerRow = x * 4, lay.rowsPerImage = y;
  wgpu::Extent3D ext{x, y, z};
  queue_.WriteTexture(&dst, data, static_cast<size_t>(x) * y * z * 4, &lay, &ext);
}

float GpuRenderer::gridSetting() {
  const auto& r = config().render;
  return r.lightGrid ? std::clamp(r.lampGrouping, 1.0f, 64.0f) : 0.0f;
}

void GpuRenderer::layOutGrid() {
  gridKey_ = gridSetting();
  grid_ = gridKey_ > 0 ? buildLightGrid(gridLampsCpu_, gridKey_, 8, kGridCells) : LightGrid();
  if (grid_.on && (gridLampsCpu_.size() != lights_.size() || grid_.entries.size() >= (1u << 20) || grid_.mostEntries >= 4096 ||
                   lights_.size() + grid_.groups.size() > 2 * static_cast<size_t>(kMaxLights)))
    grid_ = LightGrid();
  const uint32_t nx = grid_.on ? grid_.nx : 1, ny = grid_.on ? grid_.ny : 1, nz = grid_.on ? grid_.nz : 1;
  std::vector<uint32_t> cells(static_cast<size_t>(nx) * ny * nz, 0);
  if (grid_.on)
    for (int c = 0; c < grid_.cells(); ++c)
      cells[static_cast<size_t>(c)] = grid_.start[static_cast<size_t>(c)] | ((grid_.start[static_cast<size_t>(c) + 1] - grid_.start[static_cast<size_t>(c)]) << 20);
  gridCellsTex_ = gridTexture(wgpu::TextureDimension::e3D, nx, ny, nz);
  writeGridTexture(gridCellsTex_, cells.data(), nx, ny, nz);
  const uint32_t rows = std::max<uint32_t>(1, static_cast<uint32_t>((grid_.entries.size() + 1023) / 1024));
  gridListTex_ = gridTexture(wgpu::TextureDimension::e2D, 1024, rows, 1);
  gridCellsView_ = gridCellsTex_.CreateView(), gridListView_ = gridListTex_.CreateView();
  lightsDirty_ = true;
  bindGroupsDirty_ = true;
}

LightU GpuRenderer::groupLamp(const LightGrid::Group& grp) const {
  LightU u{};
  const LightU& f = lights_[grp.members[0]];
  Vec3 col(0), dir(0);
  float r = grp.radius;
  for (uint32_t k : grp.members) {
    const LightU& m = lights_[k];
    r = std::max(r, length(m.pos.xyz() - grp.center) + m.pos.w);
    if (f.dir.w > 1.5f) col = col + m.color.xyz() * (m.cone.z * m.cone.w), dir = dir + m.dir.xyz();
    else col = col + m.color.xyz();
  }
  u.pos = {grp.center, std::max(r, 0.01f)};
  u.color = {col, 0};
  if (f.dir.w > 1.5f) u.dir = {normalize(dir), 2}, u.cone = {0, 0, 1, 1};
  else u.dir = {0, -1, 0, 0};
  return u;
}

void GpuRenderer::keepGrid() {
  if (!gridCellsTex_ || gridSetting() != gridKey_) layOutGrid();
}

void GpuRenderer::uploadLights(const Vec3& eye) {
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
  keepLampRows(rank);
  sortedLights_.resize(lights_.size());
  for (size_t i = 0; i < rank.size(); ++i) sortedLights_[i] = lights_[rank[i].second];
  queue_.WriteBuffer(lightsBuf_, 0, sortedLights_.data(), sortedLights_.size() * sizeof(LightU));
  // the groups after the lamps; the grid's lists name lamps by where they
  // now sit in that order, groups by their place after them
  if (grid_.on && gridListTex_) {
    std::vector<LightU> groups;
    for (const LightGrid::Group& grp : grid_.groups) groups.push_back(groupLamp(grp));
    queue_.WriteBuffer(lightsBuf_, sortedLights_.size() * sizeof(LightU), groups.data(), groups.size() * sizeof(LightU));
    std::vector<uint32_t> at(lights_.size());
    for (size_t i = 0; i < rank.size(); ++i) at[rank[i].second] = static_cast<uint32_t>(i);
    const uint32_t rows = std::max<uint32_t>(1, static_cast<uint32_t>((grid_.entries.size() + 1023) / 1024));
    std::vector<uint32_t> list(static_cast<size_t>(rows) * 1024, 0);
    const uint32_t n = static_cast<uint32_t>(lights_.size());
    for (size_t j = 0; j < grid_.entries.size(); ++j) list[j] = grid_.entries[j] < n ? at[grid_.entries[j]] : grid_.entries[j];
    writeGridTexture(gridListTex_, list.data(), 1024, rows, 1);
  }
}

Vec3 GpuRenderer::whiteBalanceGains(float temperature, float tint) {
  Vec3 g = kelvin(6500 - temperature * 3000) / kelvin(6500);
  if (temperature < 0) g = kelvin(6500 / (1 + temperature * 0.6f)) / kelvin(6500);
  g = g * Vec3(1 + tint * 0.08f, 1 - tint * 0.12f, 1 + tint * 0.08f);
  return g / luminance(g);
}

wgpu::Buffer GpuRenderer::buffer(size_t size, wgpu::BufferUsage usage) {
  wgpu::BufferDescriptor bd;
  bd.size = (size + 15) & ~size_t(15);
  bd.usage = usage;
  return device_.CreateBuffer(&bd);
}

bool GpuRenderer::ensureStorage(wgpu::Buffer& b, size_t& cap, size_t stride, size_t count) {
  if (b && count <= cap) return false;
  cap = std::max<size_t>(count, cap * 2);
  if (b) b.Destroy();
  b = buffer(stride * cap, wgpu::BufferUsage::Storage | wgpu::BufferUsage::CopyDst);
  bindGroupsDirty_ = true;
  return true;
}

}  // namespace loop::gpu::impl
