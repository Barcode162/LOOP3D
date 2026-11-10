// The live renderer: its clusters part (see renderer_internal.h).
#include "engine/gpu/renderer_internal.h"

namespace loop::gpu::impl {

void GpuRenderer::uploadClusters(const Scene& scene) {
  std::vector<const Mesh*> meshes;
  for (const Mesh& m : scene.meshes) meshes.push_back(&m);
  Cook::shared().cookAll(meshes);
  std::vector<float> verts;
  std::vector<uint32_t> idx;
  std::vector<ClusterU> clusters;
  std::vector<MeshInfoU> info;
  for (const Mesh& m : scene.meshes) {
    auto tree = Cook::shared().clusters(m);
    uint32_t base = static_cast<uint32_t>(verts.size() / 8);
    info.push_back({static_cast<uint32_t>(clusters.size()), static_cast<uint32_t>(tree->clusters.size()), base, 0});
    for (size_t i = 0; i < m.positions.size(); ++i) {
      Vec3 n = i < m.normals.size() ? m.normals[i] : Vec3{0, 1, 0};
      Vec2 uv = i < m.uvs.size() ? m.uvs[i] : Vec2{};
      verts.insert(verts.end(), {m.positions[i].x, m.positions[i].y, m.positions[i].z, n.x, n.y, n.z, uv.x, uv.y});
    }
    // the thinned pieces' bigger corners, after the mesh's own
    for (size_t k = 0; k < tree->extraPositions.size(); ++k) {
      uint32_t i = tree->extraSource[k];
      Vec3 p = tree->extraPositions[k], n = i < m.normals.size() ? m.normals[i] : Vec3{0, 1, 0};
      Vec2 uv = i < m.uvs.size() ? m.uvs[i] : Vec2{};
      verts.insert(verts.end(), {p.x, p.y, p.z, n.x, n.y, n.z, uv.x, uv.y});
    }
    for (const Cluster& c : tree->clusters) {
      ClusterU u;
      u.bounds = c.bounds, u.lod = c.lodSphere, u.parent = c.parentSphere;
      u.error = c.error, u.parentError = c.parentError >= kInf ? 3.0e38f : c.parentError;
      u.first = static_cast<uint32_t>(idx.size()), u.count = c.count;
      for (uint32_t k = 0; k < c.count * 3; ++k) idx.push_back(base + tree->indices[c.first + k]);
      clusters.push_back(u);
    }
  }
  auto send = [&](wgpu::Buffer& b, const void* data, size_t bytes) {
    if (b) b.Destroy();
    b = buffer(std::max<size_t>(bytes, 16), wgpu::BufferUsage::Storage | wgpu::BufferUsage::CopyDst);
    if (bytes) queue_.WriteBuffer(b, 0, data, (bytes + 3) & ~size_t(3));
  };
  // one pool (WebGPU promises only 8 buffers a stage): where the corners and
  // the clusters start, the vertices, the corners, the clusters
  std::vector<uint32_t> all(2);
  all.reserve(2 + verts.size() + idx.size() + clusters.size() * 16);
  for (float f : verts) {
    uint32_t u;
    std::memcpy(&u, &f, 4);
    all.push_back(u);
  }
  all[0] = static_cast<uint32_t>(all.size());
  all.insert(all.end(), idx.begin(), idx.end());
  all[1] = static_cast<uint32_t>(all.size());
  const uint32_t* cw = reinterpret_cast<const uint32_t*>(clusters.data());
  all.insert(all.end(), cw, cw + clusters.size() * 16);
  send(poolVerts_, all.data(), all.size() * 4);
  send(poolMeshes_, info.data(), info.size() * sizeof(MeshInfoU));
  poolClusters_ = poolVerts_;
  pooledMeshes_ = scene.meshes.size(), poolClusterCount_ = clusters.size();
  meshClusterCount_.clear();
  for (const MeshInfoU& m : info) meshClusterCount_.push_back(m.clusterCount);
  bindGroupsDirty_ = true;
}

void GpuRenderer::wantClusters(const Scene& scene) {
  if (!config().render.clusters || pooledMeshes_ == scene.meshes.size()) return;
#if defined(__EMSCRIPTEN_PTHREADS__)
  if (cookJob_ && !cookJob_->done) return;  // still cooking
  if (cookJob_ && cookJob_->meshes.size() == scene.meshes.size()) {
    cookJob_.reset();
    uploadClusters(scene);
    return;
  }
  auto job = std::make_shared<CookJob>();
  for (const Mesh& m : scene.meshes) {
    Mesh c;
    c.positions = m.positions, c.indices = m.indices;
    job->meshes.push_back(std::move(c));
  }
  cookJob_ = job;
  std::thread([job] {
    Cook::shared().cookAll(job->meshes);
    job->done = true;
  }).detach();
#else
  uploadClusters(scene);
#endif
}

bool GpuRenderer::clustersReady() const {
  return config().render.clusters && quality_ != Quality::kUltra && poolClusters_ && visView_ && pooledMeshes_ == meshes_.size() && pooledMeshes_ > 0;
}

void GpuRenderer::prepareList(ClusterList& L, const std::vector<Batch>& draws, const Mat4& viewProj, Vec3 eye, float perPixel, float occlusion, int movers) {
  L.cpu.clear();
  size_t room = 0;
  for (const Batch& b : draws) {
    if (b.glass) continue;
    for (uint32_t i = b.first; i < b.first + b.count; ++i) {
      uint32_t slot = visible_[i];
      if (movers && ((slot < isMover_.size() && isMover_[slot]) ? 2 : 1) != movers) continue;
      uint32_t mesh = slot < instData_.size() ? instData_[slot].material[1] : 0;
      L.cpu.push_back(slot);
      L.cpu.push_back(static_cast<uint32_t>(room));  // its stretch: room for every cluster it has
      room += mesh < meshClusterCount_.size() ? meshClusterCount_[mesh] : 0;
    }
  }
  finishList(L, room, viewProj, eye, perPixel, occlusion);
}

void GpuRenderer::prepareSlots(ClusterList& L, const std::vector<uint32_t>& slots, const Mat4& viewProj, Vec3 eye, float perPixel) {
  L.cpu.clear();
  size_t room = 0;
  for (uint32_t slot : slots) {
    uint32_t mesh = slot < instData_.size() ? instData_[slot].material[1] : 0;
    L.cpu.push_back(slot);
    L.cpu.push_back(static_cast<uint32_t>(room));
    room += mesh < meshClusterCount_.size() ? meshClusterCount_[mesh] : 0;
  }
  finishList(L, room, viewProj, eye, perPixel, 0);
}

void GpuRenderer::finishList(ClusterList& L, size_t room, const Mat4& viewProj, Vec3 eye, float perPixel, float occlusion) {
  L.count = static_cast<uint32_t>(L.cpu.size() / 2);
  if (L.cpu.empty()) L.cpu = {0, 0};
  if (!L.ubo) L.ubo = buffer(288, wgpu::BufferUsage::Uniform | wgpu::BufferUsage::CopyDst);
  if (!L.args) L.args = buffer(16, wgpu::BufferUsage::Storage | wgpu::BufferUsage::Indirect | wgpu::BufferUsage::CopyDst | wgpu::BufferUsage::CopySrc);
  if (ensureStorage(L.cullList, L.cullListCap, 8, L.cpu.size() / 2)) bindGroupsDirty_ = true;
  if (ensureStorage(L.drawn, L.drawnCap, 8, std::max<size_t>(room, 1))) bindGroupsDirty_ = true;
  if (ensureStorage(L.stretch, L.stretchCap, 8, std::max<size_t>(room, 1))) bindGroupsDirty_ = true;
  if (ensureStorage(L.counts, L.countsCap, 8, std::max<size_t>(L.count, 1))) bindGroupsDirty_ = true;
  if (!L.cullGroup) bindGroupsDirty_ = true;
  queue_.WriteBuffer(L.cullList, 0, L.cpu.data(), L.cpu.size() * sizeof(uint32_t));
  struct {
    Vec4 planes[6];
    Vec4 eye, params;
    Vec4 occl, occl2;
    Mat4 oldView, newView;
  } u;
  // what's hidden (phase 4): the old pyramid (last frame's) and the new (this frame's first pass)
  const int was = hzbIndex_ ^ 1, now = hzbIndex_;
  u.occl = Vec4(occlusion, static_cast<float>(hzbSize_[was].z), hzbSize_[was].w0, hzbSize_[was].h0);
  u.occl2 = Vec4(static_cast<float>(hzbSizeNow().z), hzbSizeNow().w0, hzbSizeNow().h0, 0.0f);
  u.oldView = hzbView_[was], u.newView = viewProj;
  (void)now;
  Frustum fr = Frustum::fromViewProj(viewProj);
  for (int i = 0; i < 6; ++i) u.planes[i] = Vec4(fr.p[i].n, fr.p[i].d);
  u.eye = Vec4(eye, perPixel);
  u.params = Vec4(config().render.clusterError, static_cast<float>(L.count), static_cast<float>(room), 0.0f);  // render.clusterError: pixels
  queue_.WriteBuffer(L.ubo, 0, &u, sizeof u);
}

const wgpu::PassTimestampWrites* GpuRenderer::stamp(wgpu::PassTimestampWrites& w) {
  if (stampSlot_ < 0 || graph_.current() < 0 || stampSteps_.size() >= kStampPairs) return nullptr;
  uint32_t i = static_cast<uint32_t>(stampSteps_.size());
  w.querySet = stampSet_, w.beginningOfPassWriteIndex = 2 * i, w.endOfPassWriteIndex = 2 * i + 1;
  stampSteps_.push_back(stampAs_ ? std::string(stampAs_) : graph_.steps()[static_cast<size_t>(graph_.current())].name);
  return &w;
}

void GpuRenderer::runCull(wgpu::CommandEncoder& enc, const ClusterList& L) {
  wgpu::PassTimestampWrites tw;
  wgpu::ComputePassDescriptor cd;
  cd.timestampWrites = stamp(tw);
  wgpu::ComputePassEncoder cp = enc.BeginComputePass(&cd);
  cp.SetBindGroup(0, hzbIndex_ && L.cullGroupB ? L.cullGroupB : L.cullGroup);
  uint32_t n = std::max(L.count, 1u), x = std::min(n, 65535u);
  cp.SetPipeline(cullPipe_);
  cp.DispatchWorkgroups(x, (n + x - 1) / x);
  cp.SetPipeline(countPipe_);
  cp.DispatchWorkgroups(1);
  cp.SetPipeline(packPipe_);
  cp.DispatchWorkgroups(x, (n + x - 1) / x);
  cp.End();
}

void GpuRenderer::drawBatches(wgpu::RenderPassEncoder& pass, bool glass, bool shadow) {
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

void GpuRenderer::postPass(wgpu::CommandEncoder& enc, wgpu::TextureView target, wgpu::RenderPipeline& pipe, wgpu::BindGroup& group, bool load, uint32_t vw, uint32_t vh) {
  wgpu::RenderPassColorAttachment ca;
  ca.view = target;
  ca.loadOp = load ? wgpu::LoadOp::Load : wgpu::LoadOp::Clear;
  ca.storeOp = wgpu::StoreOp::Store;
  ca.clearValue = {0, 0, 0, 1};
  wgpu::RenderPassDescriptor rp;
  rp.colorAttachmentCount = 1;
  rp.colorAttachments = &ca;
  wgpu::PassTimestampWrites tw;
  rp.timestampWrites = stamp(tw);
  wgpu::RenderPassEncoder pass = enc.BeginRenderPass(&rp);
  if (vw && vh) pass.SetViewport(0, 0, static_cast<float>(vw), static_cast<float>(vh), 0, 1);
  pass.SetPipeline(pipe);
  pass.SetBindGroup(0, group);
  pass.Draw(3);
  pass.End();
}

}  // namespace loop::gpu::impl
