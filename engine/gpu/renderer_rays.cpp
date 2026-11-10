// The live renderer: its rays part (see renderer_internal.h).
#include "engine/gpu/renderer_internal.h"

namespace loop::gpu::impl {

NodeU GpuRenderer::packNode(const AABB& box, uint32_t first, uint32_t count) {
  NodeU u;
  float f, c;
  std::memcpy(&f, &first, 4);
  std::memcpy(&c, &count, 4);
  u.lo = {box.lo, f}, u.hi = {box.hi, c};
  return u;
}

void GpuRenderer::uploadStorage(wgpu::Buffer& b, const void* data, size_t bytes) {
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

void GpuRenderer::emptyRayScene() {
  // a single empty box: every ray misses
  NodeU empty = packNode(AABB(Vec3(1), Vec3(-1)), 0, 1);
  RayInstU ri{};
  TriU t{};
  uploadStorage(rayTris_, &t, sizeof t);
  uploadStorage(rayBlas_, &empty, sizeof empty);
  uploadStorage(rayInst_, &ri, sizeof ri);
  uploadStorage(rayTlas_, &empty, sizeof empty);
}

void GpuRenderer::buildRayMeshes(const Scene& scene) {
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

void GpuRenderer::buildRayInstances(const Scene& scene) {
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

}  // namespace loop::gpu::impl
