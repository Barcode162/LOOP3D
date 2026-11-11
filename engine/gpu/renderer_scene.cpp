// The live renderer: its scene part (see renderer_internal.h).
#include "engine/gpu/renderer_internal.h"

#include "engine/core/random.h"

namespace loop::gpu::impl {

void GpuRenderer::sync(const Scene& scene, bool geometryChanged, bool skyChanged) {
  changes_.bump("shapes");
  if (scene.textures.size() != texUploaded_ || texSize_ != std::clamp(config().render.textureSize, 16, 2048) || surfacesChanged(scene))
    uploadModelTextures(scene);
  if (geometryChanged || meshes_.size() != scene.meshes.size()) {
    const std::vector<char> marked = meshesInSurfaces(scene);
    for (size_t i = meshes_.size(); i < scene.meshes.size(); ++i) {
      std::shared_ptr<const Marks> mk = marked[i] ? Cook::shared().marks(scene.meshes[i]) : nullptr;
      meshes_.push_back(upload(scene.meshes[i], mk.get()));
    }
  }
  wantClusters(scene);
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
    u.extra = {m.subsurface, m.translucency, m.leafShape ? 1.0f : 0.0f, m.ior};
    u.coat = {m.clearcoatRoughness, m.specular, 0, 0};
    u.sss = {m.subsurfaceColor, 0};
    if (m.surface >= 0 && m.surface < static_cast<int>(scene.surfaces.size()) && m.surface < static_cast<int>(surfLayer_.size()) && surfLayer_[m.surface] >= 0)
      surfaceNumbers(scene.surfaces[m.surface], surfLayer_[m.surface], wanderLayer_, m.surface < static_cast<int>(surfDetail_.size()) ? surfDetail_[m.surface] : -1, u);
    auto layer = [&](int t) { return t >= 0 && t < static_cast<int>(texLayer_.size()) ? static_cast<float>(texLayer_[t]) : -1.0f; };
    u.tex = {layer(m.baseTexture), layer(m.metalRoughTexture), layer(m.normalTexture), layer(m.emissiveTexture)};
    u.tex2 = {m.alphaCutoff, m.roughnessMin, 0, 0};
    if (m.pattern.kind == Pattern::Kind::kWindows) {
      // windows uses no scale, bump, contrast or roughness variation, so its numbers ride in those slots
      // (the Material struct stays one size; the shader's windowsAt reads them): pattern.y floor height,
      // pattern.z bay, pattern.w window width, tex2.z window height, tex2.w share lit, colorB.w the seed
      u.pattern = {static_cast<float>(static_cast<int>(m.pattern.kind)), m.pattern.floorHeight, m.pattern.bay, m.pattern.window.x};
      u.tex2.z = m.pattern.window.y;
      u.tex2.w = m.pattern.lit;
      u.colorB.w = static_cast<float>(m.pattern.seed);
    }
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

void GpuRenderer::syncLights(const Scene& scene) {
  readLights(scene);
  fog_ = scene.fog;
}

void GpuRenderer::reset() {
  hzbValid_ = false;
  changes_.bump("shapes"), changes_.bump("placements");
  scaleLearned_.forget();  // a new scene: its steps cost what they cost
  for (wgpu::Buffer* b : {&poolVerts_, &poolMeshes_})
    if (*b) b->Destroy(), *b = nullptr;
  poolIdx_ = poolClusters_ = nullptr;
  pooledMeshes_ = 0, poolClusterCount_ = 0;
  histValid_ = false;  // a new scene: no past to blend with
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

bool GpuRenderer::sharedLampShadows() const {
  return shadowLamps_ > 0 && quality_ == Quality::kHigh && config().render.allLampShadows && !(g_skipPasses & 32768);
}

bool GpuRenderer::lampMapsWanted() const {
  return (shadowLamps_ == 0 || sharedLampShadows()) && quality_ != Quality::kUltra && clustersReady() && config().render.lampMaps > 0 && lampShadowPipe_;
}

void GpuRenderer::syncTransforms(const Scene& scene) {
  changes_.bump("placements");
  lastMoved_.clear();
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
  std::vector<InstanceU>& data = instData_;
  data.clear();
  data.reserve(order.size());
  batches_.clear();
  instBox_.clear(), instHidden_.clear();
  slotOf_.assign(scene.instances.size(), -1);
  for (uint32_t idx : order) {
    const MeshInstance& in = scene.instances[idx];
    if (in.mesh < 0 || in.mesh >= static_cast<int>(meshes_.size())) continue;
    slotOf_[idx] = static_cast<int>(data.size());
    InstanceU u;
    u.model = in.transform;
    Mat3 nm = in.transform.linear().inverse().transposed();
    u.n0 = {nm.c[0], 0}, u.n1 = {nm.c[1], 0}, u.n2 = {nm.c[2], 0};
    u.material[0] = static_cast<uint32_t>(std::max(0, in.material));
    u.material[1] = static_cast<uint32_t>(in.mesh);  // its shape (the cluster drawing finds its clusters by it)
    // each thing starts at its own place in a surface's tile: the same hash of its number as the film's;
    // and its surface's stretch runs along the axis the film works out from the mesh's bounds
    u.material[2] = surfaceStartKey(idx);
    u.material[3] = 0;
    if (!scene.materials.empty()) {
      const Material& m = scene.materials[std::clamp(in.material, 0, static_cast<int>(scene.materials.size()) - 1)];
      if (m.surface >= 0 && m.surface < static_cast<int>(scene.surfaces.size()))
        u.material[3] = static_cast<uint32_t>(alongAxis(scene.surfaces[m.surface], meshes_[in.mesh].box));
    }
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
  stillAgain(data.size());  // the whole list again: a cut, every copy starts still
}

void GpuRenderer::stillAgain(size_t slots) {
  if (ensureStorage(backBuf_, backCap_, sizeof(Mat4), std::max<size_t>(1, slots))) bindGroupsDirty_ = true;
  std::vector<Mat4> still(std::max<size_t>(1, slots));
  queue_.WriteBuffer(backBuf_, 0, still.data(), still.size() * sizeof(Mat4));
  drawnModel_.resize(instData_.size());
  for (size_t i = 0; i < instData_.size(); ++i) drawnModel_[i] = instData_[i].model;
  movedNow_.clear(), movedBefore_.clear();
}

void GpuRenderer::writeMotion() {
  std::sort(movedNow_.begin(), movedNow_.end());
  movedNow_.erase(std::unique(movedNow_.begin(), movedNow_.end()), movedNow_.end());
  auto write = [&](uint32_t slot, const Mat4& m) { queue_.WriteBuffer(backBuf_, static_cast<uint64_t>(slot) * sizeof(Mat4), &m, sizeof(Mat4)); };
  for (uint32_t s : movedBefore_)
    if (!std::binary_search(movedNow_.begin(), movedNow_.end(), s) && s < drawnModel_.size()) write(s, Mat4{});  // stopped: still again
  for (uint32_t s : movedNow_) {
    if (s >= drawnModel_.size() || s >= instData_.size()) continue;
    write(s, drawnModel_[s] * instData_[s].model.inverse());
    drawnModel_[s] = instData_[s].model;
  }
  movedBefore_.swap(movedNow_);
  movedNow_.clear();
}

void GpuRenderer::moveInstances(const Scene& scene, const std::vector<int>& ids, const std::vector<Mat4>& xfs) {
  if (ids.empty()) return;
  changes_.bump("moves");
  lastScene_ = &scene;
  if (slotOf_.size() != scene.instances.size()) {  // the copies changed: the whole list again
    syncTransforms(scene);
    return;
  }
  float moved = 0;
  touched_.clear();
  for (size_t k = 0; k < ids.size() && k < xfs.size(); ++k) {
    int id = ids[k];
    if (id < 0 || id >= static_cast<int>(slotOf_.size()) || slotOf_[id] < 0) continue;
    int slot = slotOf_[id];
    const Mat4& xf = xfs[k];
    InstanceU& u = instData_[slot];
    if (moved < 0.002f)
      for (int c = 0; c < 4; ++c)
        for (int r = 0; r < 3; ++r) moved = std::max(moved, std::fabs(u.model.m[c][r] - xf.m[c][r]));
    u.model = xf;
    Mat3 nm = xf.linear().inverse().transposed();
    u.n0 = {nm.c[0], 0}, u.n1 = {nm.c[1], 0}, u.n2 = {nm.c[2], 0};
    instBox_[slot] = meshes_[scene.instances[id].mesh].box.transformed(xf);
    instHidden_[slot] = std::fabs(xf.linear().determinant()) < 1e-12f ? 1 : 0;
    touched_.push_back(slot);
    movedNow_.push_back(static_cast<uint32_t>(slot));
    if (lastMoved_.size() <= static_cast<size_t>(slot)) lastMoved_.resize(slot + 1, 0);
    lastMoved_[slot] = presented_ + 1;
  }
  // something moved more than a couple of millimetres (not just breathing):
  // Ultra lets the old picture fade quickly instead of smearing
  if (moved >= 0.002f && ultraFrame_ > 8) ultraFrame_ = 8;
  if (needRays()) {
    buildRayMeshes(scene);
    buildRayInstances(scene);
    raysBuiltFor_ = static_cast<int>(quality_);
  } else {
    raysBuiltFor_ = -1;
  }
  // runs of places close together go as one write
  std::sort(touched_.begin(), touched_.end());
  touched_.erase(std::unique(touched_.begin(), touched_.end()), touched_.end());
  for (size_t a = 0; a < touched_.size();) {
    size_t b = a;
    while (b + 1 < touched_.size() && touched_[b + 1] <= touched_[b] + 16) ++b;
    int first = touched_[a], last = touched_[b];
    queue_.WriteBuffer(instancesBuf_, static_cast<uint64_t>(first) * sizeof(InstanceU), &instData_[first], static_cast<size_t>(last - first + 1) * sizeof(InstanceU));
    a = b + 1;
  }
}

int GpuRenderer::skipBits() const {
  const Config::Render& r = config().render;
  return g_skipPasses | (r.sunShadows ? 0 : 1) | (r.passScene ? 0 : 2) | (r.passExposure ? 0 : 4) | (r.passBloom ? 0 : 8) | (r.passFilm ? 0 : 16) |
         (r.passShadow ? 0 : 32) | (r.passDraw ? 0 : 64) | (r.lampShadows ? 0 : 512);
}

void GpuRenderer::readLights(const Scene& scene) {
  lights_.clear();
  lampNoShadow_.clear();
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
      u.cone = {1, 0, l.size.x, l.size.y};  // x: a true rectangle (a group of far panels sent as one lamp has 0)
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
    lampNoShadow_.push_back(l.castShadows ? 0 : 1);
  }
  lightsDirty_ = true;
  // the light grid: laid out again when lamps move (a lamp's light changing
  // only changes its groups' light, sent with the lamps)
  std::vector<GridLamp> lamps = gridLamps(scene, kMaxLights);
  bool same = lamps.size() == gridLampsCpu_.size();
  for (size_t k = 0; same && k < lamps.size(); ++k) same = length(lamps[k].position - gridLampsCpu_[k].position) < 0.02f;
  gridLampsCpu_ = std::move(lamps);
  if (!same) gridKey_ = -1, changes_.bump("lamp places");
}

// A surface's numbers for the shader (steps 4 to 7 of docs/SURFACES.md section 7;
// steps 1 to 3 are its baked tile, at `layer` and the one after).
void surfaceNumbers(const SurfaceRecipe& r, int layer, int wander, int detail, MaterialU& u) {
  u.surf0 = {static_cast<float>(layer), static_cast<float>(layer + 1), r.tile, r.varyLarge > 0 ? static_cast<float>(wander) : -1.0f};
  u.surf1 = {r.varyLarge, r.varyAmount, r.varyRoughness, r.worldSpace ? 1.0f : 0.0f};
  u.surf2 = {r.varyTint, static_cast<float>(static_cast<int>(r.project))};
  u.det0 = {static_cast<float>(detail), r.detailTile, r.detailFade, r.detailRoughness};
  auto layerOf = [](const SurfaceLayer& L, Vec4& a, Vec4& b, float amount) {
    a = {L.color, L.roughness};
    b = {L.metal, L.on ? amount : 0.0f, L.ragged, L.soft};
  };
  layerOf(r.wear, u.wear0, u.wear1, r.wear.amount);
  layerOf(r.dirt, u.dirt0, u.dirt1, r.dirt.amount);
  u.coat0 = {}, u.coat1 = {}, u.coat2 = {};
  for (const SurfaceLayer& c : r.coat)
    if (c.on && c.facing != SurfaceLayer::Facing::kAny) {
      layerOf(c, u.coat0, u.coat1, c.cover);
      u.coat2 = {static_cast<float>(static_cast<int>(c.facing)), static_cast<float>(static_cast<int>(c.sits)), 0, 0};
    }
}

// The surfaces a scene's materials use, by fingerprint: changed since their pictures went up?
bool GpuRenderer::surfacesChanged(const Scene& scene) const {
  if (scene.surfaces.size() != surfUploaded_.size()) return true;
  for (size_t i = 0; i < scene.surfaces.size(); ++i)
    if (scene.surfaces[i].fingerprint() != surfUploaded_[i]) return true;
  return false;
}

// Which meshes are drawn in a surface somewhere (they carry their marks to the GPU).
std::vector<char> meshesInSurfaces(const Scene& scene) {
  std::vector<char> in(scene.meshes.size(), 0);
  for (const MeshInstance& i : scene.instances) {
    if (i.mesh < 0 || i.mesh >= static_cast<int>(in.size()) || scene.materials.empty()) continue;
    const Material& m = scene.materials[std::clamp(i.material, 0, static_cast<int>(scene.materials.size()) - 1)];
    if (m.surface >= 0) in[i.mesh] = 1;
  }
  return in;
}

// A corner's two marks as the four bytes of one vertex number (edge, hollow, 0, 0).
float packedMarks(const Marks* marks, size_t i) {
  uint32_t w = 0;
  if (marks && i < marks->edge.size() && i < marks->hollow.size()) w = marks->edge[i] | (static_cast<uint32_t>(marks->hollow[i]) << 8);
  float f;
  std::memcpy(&f, &w, 4);
  return f;
}

GpuMesh GpuRenderer::upload(const Mesh& m, const Marks* marks) {
  std::vector<float> v;
  v.reserve(m.positions.size() * 9);
  for (size_t i = 0; i < m.positions.size(); ++i) {
    Vec3 n = i < m.normals.size() ? m.normals[i] : Vec3{0, 1, 0};
    Vec2 uv = i < m.uvs.size() ? m.uvs[i] : Vec2{};
    v.insert(v.end(), {m.positions[i].x, m.positions[i].y, m.positions[i].z, n.x, n.y, n.z, uv.x, uv.y, packedMarks(marks, i)});
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

int GpuRenderer::lodOf(int mesh, int level) const {
  if (level <= 0 || mesh < 0 || mesh >= static_cast<int>(meshLods_.size())) return mesh;
  const auto& l = meshLods_[mesh];
  int m = level >= 2 && l[1] >= 0 ? l[1] : l[0];
  return m >= 0 && m < static_cast<int>(meshes_.size()) && meshes_[m].indexCount ? m : mesh;
}

void GpuRenderer::buildDrawLists(const Frustum& eye, const Frustum& sun, bool sunOn, Vec3 eyePos, float pixelsPerRadian) {
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

void GpuRenderer::uploadModelTextures(const Scene& scene) {
  int S = std::clamp(config().render.textureSize, 16, 2048);
  S = std::min<int>(S, static_cast<int>(maxTexture_));
  uint32_t n = static_cast<uint32_t>(std::min<size_t>(scene.textures.size(), maxLayers_));
  if (scene.textures.size() > n) reportGpuError("more model pictures than this GPU can hold; the rest show plain colours");
  texLayer_.assign(scene.textures.size(), -1);
  // after the model pictures: each surface's colour and shape tiles (docs/SURFACES.md section 8), then
  // the one wander picture its slow change reads
  const uint32_t models = n;
  surfLayer_.assign(scene.surfaces.size(), -1);
  wanderLayer_ = -1;
  bool wander = false;
  for (size_t i = 0; i < scene.surfaces.size(); ++i) {
    if (n + 2 > maxLayers_) {
      reportGpuError("more surfaces than this GPU can hold pictures for; the rest show their base colour");
      break;
    }
    surfLayer_[i] = static_cast<int>(n);
    n += 2;
    if (scene.surfaces[i].varyLarge > 0) wander = true;
  }
  if (wander && n < maxLayers_) wanderLayer_ = static_cast<int>(n++);
  // then each distinct detail's picture, shared by every recipe with the same detail (order M7)
  surfDetail_.assign(scene.surfaces.size(), -1);
  std::unordered_map<uint64_t, int> detailLayers;
  for (size_t i = 0; i < scene.surfaces.size(); ++i) {
    const SurfaceRecipe& r = scene.surfaces[i];
    if (surfLayer_[i] < 0 || r.detail.kind == SurfacePattern::Kind::kNone || !(r.detailTile > 0)) continue;
    const uint64_t key = detailFingerprint(r);
    auto it = detailLayers.find(key);
    if (it == detailLayers.end()) {
      if (n >= maxLayers_) continue;  // no room: its detail is left out
      it = detailLayers.emplace(key, static_cast<int>(n++)).first;
    }
    surfDetail_[i] = it->second;
  }
  surfUploaded_.clear();
  for (const SurfaceRecipe& r : scene.surfaces) surfUploaded_.push_back(r.fingerprint());
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
  // a picture made by the engine (a surface's tile, the wander picture) at its own layer: its own smaller
  // copies where it has them, else each copy the average of four
  auto putPicture = [&](uint32_t layer, const std::vector<Image>& chain, bool encoded) {
    std::vector<uint8_t> px;
    int w = S;
    for (int l = 0; l < levels; ++l, w = std::max(1, w / 2)) {
      px.assign(static_cast<size_t>(w) * w * 4, 0);
      const Image* src = nullptr;
      for (const Image& im : chain)
        if (im.width == w && im.height == w) src = &im;
      for (int y = 0; y < w; ++y)
        for (int x = 0; x < w; ++x) {
          // its own copy of this size, else read from the largest (wrapping, smooth)
          Vec4 c = src ? src->at(x, y) : chain.front().sample((x + 0.5f) / w, (y + 0.5f) / w);
          uint8_t* d = &px[(static_cast<size_t>(y) * w + x) * 4];
          const float v[4] = {c.x, c.y, c.z, c.w};
          for (int k = 0; k < 4; ++k) d[k] = static_cast<uint8_t>(std::lround(std::clamp(encoded ? v[k] : v[k], 0.0f, 1.0f) * 255));
        }
      wgpu::TexelCopyTextureInfo dst;
      dst.texture = modelTex_;
      dst.mipLevel = static_cast<uint32_t>(l);
      dst.origin = {0, 0, layer};
      wgpu::TexelCopyBufferLayout lay;
      lay.bytesPerRow = static_cast<uint32_t>(w * 4);
      lay.rowsPerImage = static_cast<uint32_t>(w);
      wgpu::Extent3D ext{static_cast<uint32_t>(w), static_cast<uint32_t>(w), 1};
      queue_.WriteTexture(&dst, px.data(), px.size(), &lay, &ext);
      if (w == 1) break;
    }
  };
  for (size_t i = 0; i < scene.surfaces.size(); ++i) {
    if (surfLayer_[i] < 0) continue;
    std::shared_ptr<const SurfacePictures> pics = bakeSurface(scene.surfaces[i], S);
    if (!pics || pics->colour.empty() || pics->shape.empty()) {
      surfLayer_[i] = -1;
      continue;
    }
    putPicture(static_cast<uint32_t>(surfLayer_[i]), pics->colour, true);
    putPicture(static_cast<uint32_t>(surfLayer_[i] + 1), pics->shape, false);
  }
  if (wanderLayer_ >= 0) putPicture(static_cast<uint32_t>(wanderLayer_), {wanderPicture()}, false);
  {
    std::unordered_set<int> sent;  // a detail picture goes up once, however many recipes read it
    for (size_t i = 0; i < scene.surfaces.size(); ++i) {
      if (surfDetail_[i] < 0 || !sent.insert(surfDetail_[i]).second) continue;
      std::shared_ptr<const std::vector<Image>> chain = bakeDetail(scene.surfaces[i], S);
      if (chain && !chain->empty()) putPicture(static_cast<uint32_t>(surfDetail_[i]), *chain, false);
    }
  }
  for (uint32_t layer = 0; layer < models; ++layer) {
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

void GpuRenderer::uploadSky(const SkyModel& m) {
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

}  // namespace loop::gpu::impl
