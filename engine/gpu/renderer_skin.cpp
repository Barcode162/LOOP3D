// The live renderer: its skinning part (see renderer_internal.h, "live skinning").
// The film bends a skin on the processor before building its tree; live, the
// GPU bends it: its rest points and their bones wait in a buffer, and a
// compute pass writes each bent point over the drawn one whenever the bones
// move. Both make the same sum (engine/anim/skin.cpp, shaders/skin.wgsl.h).
#include "engine/gpu/renderer_internal.h"

namespace loop::gpu::impl {

namespace {

// 20 numbers a point, as the shader's `Rest`: place, normal, eight bones two to
// a number, eight pulls.
std::vector<uint32_t> restOf(const Mesh& m, std::vector<uint16_t>& used) {
  std::vector<uint32_t> out(m.positions.size() * 20, 0);
  std::vector<char> seen;
  auto word = [](float f) {
    uint32_t u;
    std::memcpy(&u, &f, 4);
    return u;
  };
  for (size_t i = 0; i < m.positions.size(); ++i) {
    uint32_t* r = &out[i * 20];
    const Vec3 p = m.positions[i], n = i < m.normals.size() ? m.normals[i] : Vec3(0, 1, 0);
    r[0] = word(p.x), r[1] = word(p.y), r[2] = word(p.z);
    r[4] = word(n.x), r[5] = word(n.y), r[6] = word(n.z);
    const Pulls& pl = m.pulls[i];
    for (int k = 0; k < 8; ++k) {
      r[8 + k / 2] |= static_cast<uint32_t>(pl.bone[k]) << (k % 2 ? 16 : 0);
      r[12 + k] = word(pl.pull[k]);
      if (pl.pull[k] > 0) {
        if (seen.size() <= pl.bone[k]) seen.resize(pl.bone[k] + 1, 0);
        seen[pl.bone[k]] = 1;
      }
    }
  }
  for (size_t b = 0; b < seen.size(); ++b)
    if (seen[b]) used.push_back(static_cast<uint16_t>(b));
  return out;
}

// The largest a bone's matrix stretches anything (its longest column).
float stretchOf(const Mat4& m) {
  float s = 0;
  for (int c = 0; c < 3; ++c) s = std::max(s, std::sqrt(m.m[c][0] * m.m[c][0] + m.m[c][1] * m.m[c][1] + m.m[c][2] * m.m[c][2]));
  return s;
}

bool same(const std::vector<Mat4>& a, const std::vector<Mat4>& b) {
  return a.size() == b.size() && (a.empty() || std::memcmp(a.data(), b.data(), a.size() * sizeof(Mat4)) == 0);
}

}  // namespace

void GpuRenderer::makeSkins(const Scene& scene) {
  if (!skinPipe_) return;  // made with the pipelines (renderer_setup.cpp)
  for (size_t mi = skinOf_.size(); mi < meshes_.size() && mi < scene.meshes.size(); ++mi) {
    skinOf_.push_back(-1);
    const Mesh& m = scene.meshes[mi];
    if (m.pulls.empty() || m.pulls.size() != m.positions.size()) continue;
    GpuSkin k;
    k.mesh = static_cast<int>(mi);
    for (const MeshInstance& in : scene.instances)
      if (in.mesh == k.mesh && in.bend >= 0) k.bend = in.bend;
    if (k.bend < 0) continue;  // pulls but no bones: drawn as it rests
    k.points = static_cast<uint32_t>(m.positions.size());
    k.restBox = m.bounds();
    std::vector<uint32_t> r = restOf(m, k.used);
    k.rest = buffer(r.size() * 4, wgpu::BufferUsage::Storage | wgpu::BufferUsage::CopyDst);
    queue_.WriteBuffer(k.rest, 0, r.data(), r.size() * 4);
    const uint32_t u[4] = {k.points, 0, 0, 0};
    k.uMesh = buffer(16, wgpu::BufferUsage::Uniform | wgpu::BufferUsage::CopyDst);
    queue_.WriteBuffer(k.uMesh, 0, u, 16);
    skinOf_.back() = static_cast<int>(skins_.size());
    skins_.push_back(std::move(k));
  }
  // which copies show each skin (the list of copies may have changed)
  for (GpuSkin& k : skins_) k.instances.clear();
  for (size_t i = 0; i < scene.instances.size(); ++i) {
    const int mi = scene.instances[i].mesh;
    if (mi >= 0 && mi < static_cast<int>(skinOf_.size()) && skinOf_[mi] >= 0) skins_[skinOf_[mi]].instances.push_back(static_cast<int>(i));
  }
}

void GpuRenderer::poolSkin(const Mesh& m, size_t mesh, uint32_t vertexBase, const ClusterTree& tree, std::vector<ClusterU>& clusters, uint32_t first) {
  if (mesh >= skinOf_.size() || skinOf_[mesh] < 0) return;
  GpuSkin& k = skins_[skinOf_[mesh]];
  // full detail always: a simpler level was made from the rest pose and would
  // not bend with the points (each finest cluster drawn, nothing coarser)
  k.clusterBones.clear();
  for (size_t j = 0; j < tree.clusters.size(); ++j) {
    ClusterU& u = clusters[first + j];
    const Cluster& c = tree.clusters[j];
    std::vector<uint16_t> bones;
    if (c.level == 0) {
      u.error = 0, u.parentError = 3.0e38f;
      std::vector<char> seen;
      for (uint32_t t = 0; t < c.count * 3; ++t) {
        const uint32_t v = tree.indices[c.first + t];
        if (v >= m.pulls.size()) continue;
        for (int b = 0; b < 8 && m.pulls[v].pull[b] > 0; ++b) {
          const uint16_t id = m.pulls[v].bone[b];
          if (seen.size() <= id) seen.resize(id + 1, 0);
          if (!seen[id]) seen[id] = 1, bones.push_back(id);
        }
      }
    } else {
      u.error = 3.0e38f;  // never chosen
    }
    k.clusterBones.push_back(std::move(bones));
  }
  k.clusters.assign(clusters.begin() + first, clusters.begin() + first + tree.clusters.size());
  k.pooled = true, k.poolBase = vertexBase, k.clusterFirst = first;
  k.gPool = nullptr, k.dirty = true;
  const uint32_t u[4] = {k.points, 2 + vertexBase * 9, 0, 0};  // the pool's first two numbers say where its parts start
  if (!k.uPool) k.uPool = buffer(16, wgpu::BufferUsage::Uniform | wgpu::BufferUsage::CopyDst);
  queue_.WriteBuffer(k.uPool, 0, u, 16);
}

void GpuRenderer::bendSkins(const Scene& scene) {
  if (skins_.empty() || !skinPipe_) return;
  wgpu::CommandEncoder enc;
  wgpu::ComputePassEncoder pass;
  bool open = false;
  for (GpuSkin& k : skins_) {
    if (k.bend < 0 || k.bend >= static_cast<int>(scene.bends.size()) || k.mesh >= static_cast<int>(meshes_.size())) continue;
    const std::vector<Mat4>& B = scene.bends[k.bend];
    if (B.empty() || (!k.dirty && same(B, k.sent))) continue;
    if (!k.bones || B.size() > k.bonesCap) {
      if (k.bones) k.bones.Destroy();
      k.bonesCap = B.size();
      k.bones = buffer(B.size() * sizeof(Mat4), wgpu::BufferUsage::Storage | wgpu::BufferUsage::CopyDst);
      k.gMesh = nullptr, k.gPool = nullptr;
    }
    queue_.WriteBuffer(k.bones, 0, B.data(), B.size() * sizeof(Mat4));
    if (!open) {
      enc = device_.CreateCommandEncoder();
      pass = enc.BeginComputePass();
      pass.SetPipeline(skinPipe_);
      open = true;
    }
    const uint32_t groups = (k.points + 63) / 64;
    if (!k.gMesh) k.gMesh = group(skinLayout_, {bufE(0, k.uMesh), bufE(1, k.rest), bufE(2, k.bones), bufE(3, meshes_[k.mesh].vb)});
    pass.SetBindGroup(0, k.gMesh);
    pass.DispatchWorkgroups(groups);
    if (k.pooled && poolVerts_) {
      if (!k.gPool) k.gPool = group(skinLayout_, {bufE(0, k.uPool), bufE(1, k.rest), bufE(2, k.bones), bufE(3, poolVerts_)});
      pass.SetBindGroup(0, k.gPool);
      pass.DispatchWorkgroups(groups);
      // each cluster's sphere round where its bones carry it: a point is a
      // mix of its bones' carryings, so it stays inside what holds them all
      for (size_t j = 0; j < k.clusters.size() && j < k.clusterBones.size(); ++j) {
        if (k.clusterBones[j].empty()) continue;
        const Vec4 rest = k.clusters[j].bounds;
        AABB box;
        std::vector<Vec4> spheres;
        for (uint16_t b : k.clusterBones[j]) {
          if (b >= B.size()) continue;
          const Vec3 c = B[b].point(rest.xyz());
          const float r = rest.w * stretchOf(B[b]);
          spheres.push_back({c, r});
          box.grow(AABB(c - Vec3(r), c + Vec3(r)));
        }
        if (spheres.empty()) continue;
        const Vec3 mid = (box.lo + box.hi) * 0.5f;
        float r = 0;
        for (const Vec4& s : spheres) r = std::max(r, length(s.xyz() - mid) + s.w);
        k.clusters[j].bounds = {mid, r};
      }
      // only the spheres change: write the clusters' first four numbers each
      for (size_t j = 0; j < k.clusters.size(); ++j)
        if (!k.clusterBones[j].empty())
          queue_.WriteBuffer(poolVerts_, (static_cast<uint64_t>(poolClusterWord_) + (k.clusterFirst + j) * 16) * 4, &k.clusters[j].bounds, sizeof(Vec4));
    }
    // the mesh's box round where its bones carry it, and its copies' boxes
    AABB box;
    for (uint16_t b : k.used)
      if (b < B.size()) box.grow(k.restBox.transformed(B[b]));
    if (!box.valid()) box = k.restBox;
    meshes_[k.mesh].box = box;
    const bool moved = !k.sent.empty();
    for (int id : k.instances) {
      if (id < 0 || id >= static_cast<int>(slotOf_.size()) || slotOf_[id] < 0) continue;
      const int slot = slotOf_[id];
      if (slot < static_cast<int>(instBox_.size()) && slot < static_cast<int>(instData_.size())) instBox_[slot] = box.transformed(instData_[slot].model);
      if (!moved) continue;
      // bending counts as moving: its shadows are drawn fresh, not kept
      movedNow_.push_back(static_cast<uint32_t>(slot));
      bentNow_.push_back(static_cast<uint32_t>(slot));
      if (lastMoved_.size() <= static_cast<size_t>(slot)) lastMoved_.resize(slot + 1, 0);
      lastMoved_[slot] = presented_ + 1;
    }
    k.sent = B;
    k.dirty = false;
  }
  if (!open) return;
  changes_.bump("moves");
  pass.End();
  wgpu::CommandBuffer cb = enc.Finish();
  queue_.Submit(1, &cb);
}

}  // namespace loop::gpu::impl
