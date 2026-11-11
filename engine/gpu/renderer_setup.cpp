// The live renderer: its setup part (see renderer_internal.h).
#include "engine/gpu/renderer_internal.h"

namespace loop::gpu::impl {

GpuRenderer::GpuRenderer(std::string canvas, wgpu::Instance inst, wgpu::Surface surface, wgpu::Adapter adapter, wgpu::Device device, std::shared_ptr<GpuState> state) : canvas_(std::move(canvas)), instance_(inst), surface_(surface), adapter_(adapter), device_(device), queue_(device.GetQueue()), gs_(std::move(state)) {
  wgpu::Limits lim;
  if (device_.GetLimits(&lim)) {
    maxBinding_ = lim.maxStorageBufferBindingSize;
    maxBufferSize_ = lim.maxBufferSize;
    maxTexture_ = lim.maxTextureDimension2D;
    maxLayers_ = lim.maxTextureArrayLayers;
  }
  if (device_.HasFeature(wgpu::FeatureName::TimestampQuery)) {
    wgpu::QuerySetDescriptor qd;
    qd.type = wgpu::QueryType::Timestamp;
    qd.count = 2 * kStampPairs;
    stampSet_ = device_.CreateQuerySet(&qd);
    stampResolve_ = buffer(16 * kStampPairs, wgpu::BufferUsage::QueryResolve | wgpu::BufferUsage::CopySrc);
    for (auto& s : gs_->slots) s.buf = buffer(16 * kStampPairs, wgpu::BufferUsage::MapRead | wgpu::BufferUsage::CopyDst);
  }
  wgpu::SurfaceCapabilities caps;
  surface_.GetCapabilities(adapter_, &caps);
  format_ = caps.formatCount > 0 ? caps.formats[0] : wgpu::TextureFormat::BGRA8Unorm;
  if (g_skipPasses & 256) format_ = wgpu::TextureFormat::RGBA8Unorm;
  frameUbo_ = buffer(sizeof(FrameU), wgpu::BufferUsage::Uniform | wgpu::BufferUsage::CopyDst);
  postUbo_ = buffer(sizeof(PostU), wgpu::BufferUsage::Uniform | wgpu::BufferUsage::CopyDst);
  expUbo_ = buffer(sizeof(ExposureU), wgpu::BufferUsage::Uniform | wgpu::BufferUsage::CopyDst);
  lightsBuf_ = buffer(sizeof(LightU) * kMaxLights * 2, wgpu::BufferUsage::Storage | wgpu::BufferUsage::CopyDst);
  exposureBuf_ = buffer(16, wgpu::BufferUsage::Storage | wgpu::BufferUsage::CopyDst | wgpu::BufferUsage::CopySrc);
  float zero[4] = {0, 0, 0, 0};
  queue_.WriteBuffer(exposureBuf_, 0, zero, 16);
  ensureStorage(materialsBuf_, materialsCap_, sizeof(MaterialU), 16);
  ensureStorage(instancesBuf_, instancesCap_, sizeof(InstanceU), 16);
  stillAgain(16);
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
  g_samples = config().render.msaa > 1 && !config().render.clusters ? 4 : 1;
  g_shading = config().render.shading >= 0 ? std::min(config().render.shading, 3) : quality_ == Quality::kHigh ? 2 : 1;
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

void GpuRenderer::setQuality(Quality q) {
  hzbValid_ = false;
  changes_.bump("settings");
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
  scaleLearned_.forget();
  judgedAt_ = 0, perfSkipped_ = perfDrawn_ = 0;
  fresh_ = true;
  renderScale_ = q == Quality::kUltra ? 0.65f : 0.87f;
  lightsDirty_ = true;
  applySettings();
  outW_ = 0;  // re-make targets for this quality
}

void GpuRenderer::applySettings() {
  changes_.bump("settings");
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
  int samples = r.msaa > 1 && !r.clusters ? 4 : 1;  // clusters: frame blending's job (rebuild step 4)
  if (lastScene_) wantClusters(*lastScene_);
  if (r.clusters != !!visView_) outW_ = 0, bindGroupsDirty_ = true;  // the visibility picture made or dropped
  // how much the shader does (render.shading: -1 the quality's own, Phone 1, High 2), compiled into the pipelines
  int shading = r.shading >= 0 ? std::min(r.shading, 3) : q == Quality::kHigh ? 2 : 1;
  if (samples != g_samples || shading != g_shading) {
    g_shading = shading;
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

wgpu::ShaderModule GpuRenderer::module(const std::string& code, const char* label) {
  wgpu::ShaderSourceWGSL src;
  src.code = code.c_str();
  wgpu::ShaderModuleDescriptor md;
  md.nextInChain = &src;
  md.label = label;
  return device_.CreateShaderModule(&md);
}

wgpu::BindGroupLayout GpuRenderer::layout(std::vector<wgpu::BindGroupLayoutEntry> e) {
  wgpu::BindGroupLayoutDescriptor d;
  d.entryCount = e.size();
  d.entries = e.data();
  wgpu::BindGroupLayout l = device_.CreateBindGroupLayout(&d);
  // the storage buffers it gives each stage, for the iPhone's limit (pipeLayout)
  FrameGraph::StageBuffers sb;
  auto has = [](wgpu::ShaderStage v, wgpu::ShaderStage s) { return (v & s) != wgpu::ShaderStage::None; };
  for (const auto& x : e)
    if (x.buffer.type == wgpu::BufferBindingType::Storage || x.buffer.type == wgpu::BufferBindingType::ReadOnlyStorage) {
      sb.vertex += has(x.visibility, wgpu::ShaderStage::Vertex);
      sb.fragment += has(x.visibility, wgpu::ShaderStage::Fragment);
      sb.compute += has(x.visibility, wgpu::ShaderStage::Compute);
    }
  stageBuffers_.push_back({l, sb});
  return l;
}

wgpu::BindGroupLayoutEntry GpuRenderer::ubo(uint32_t b, wgpu::ShaderStage s) {
  wgpu::BindGroupLayoutEntry e;
  e.binding = b, e.visibility = s, e.buffer.type = wgpu::BufferBindingType::Uniform;
  return e;
}

wgpu::BindGroupLayoutEntry GpuRenderer::ssbo(uint32_t b, wgpu::ShaderStage s, bool write) {
  wgpu::BindGroupLayoutEntry e;
  e.binding = b, e.visibility = s, e.buffer.type = write ? wgpu::BufferBindingType::Storage : wgpu::BufferBindingType::ReadOnlyStorage;
  return e;
}

wgpu::BindGroupLayoutEntry GpuRenderer::tex(uint32_t b, wgpu::ShaderStage s, bool depth, bool unfilterable) {
  wgpu::BindGroupLayoutEntry e;
  e.binding = b, e.visibility = s;
  e.texture.sampleType = depth ? wgpu::TextureSampleType::Depth : unfilterable ? wgpu::TextureSampleType::UnfilterableFloat : wgpu::TextureSampleType::Float;
  e.texture.viewDimension = wgpu::TextureViewDimension::e2D;
  return e;
}

wgpu::BindGroupLayoutEntry GpuRenderer::samp(uint32_t b, wgpu::ShaderStage s, bool compare) {
  wgpu::BindGroupLayoutEntry e;
  e.binding = b, e.visibility = s, e.sampler.type = compare ? wgpu::SamplerBindingType::Comparison : wgpu::SamplerBindingType::Filtering;
  return e;
}

void GpuRenderer::buildPipelines() {
  const auto VF = wgpu::ShaderStage::Vertex | wgpu::ShaderStage::Fragment;
  const auto F = wgpu::ShaderStage::Fragment;
  const auto VFC = VF | wgpu::ShaderStage::Compute;
  scene0Layout_ = layout({ubo(0, VFC), ssbo(1, VFC), ssbo(2, VFC), ssbo(3, VF), ssbo(5, wgpu::ShaderStage::Vertex)});
  rayLayout_ = layout({ssbo(0, F | wgpu::ShaderStage::Compute), ssbo(1, F | wgpu::ShaderStage::Compute), ssbo(2, F | wgpu::ShaderStage::Compute),
                       ssbo(3, F | wgpu::ShaderStage::Compute)});
  {
    const auto C = wgpu::ShaderStage::Compute;
    wgpu::BindGroupLayoutEntry st;
    st.binding = 1, st.visibility = C;
    st.storageTexture.access = wgpu::StorageTextureAccess::WriteOnly;
    st.storageTexture.format = wgpu::TextureFormat::RGBA16Float;
    st.storageTexture.viewDimension = wgpu::TextureViewDimension::e2D;
    ultraLayout_ = layout({ssbo(0, C, true), st, tex(2, C), samp(3, C), ubo(4, C), ssbo(5, C, true)});
    wgpu::BindGroupLayoutEntry st2 = st;
    st2.binding = 4;
    denoiseLayout_ = layout({ubo(0, C), ssbo(1, C), ssbo(2, C), ssbo(3, C, true), st2, ssbo(5, C)});
  }
  wgpu::BindGroupLayoutEntry arr = tex(4, F);
  arr.texture.viewDimension = wgpu::TextureViewDimension::e2DArray;
  // the light grid's: its cells (3D) and their lists
  wgpu::BindGroupLayoutEntry gCells = tex(6, F), gList = tex(7, F);
  gCells.texture.sampleType = gList.texture.sampleType = wgpu::TextureSampleType::Uint;
  gCells.texture.viewDimension = wgpu::TextureViewDimension::e3D;
  wgpu::BindGroupLayoutEntry lampArr = tex(8, F, true);
  lampArr.texture.viewDimension = wgpu::TextureViewDimension::e2DArray;
  wgpu::BindGroupLayoutEntry sunArr = tex(0, F, true);
  sunArr.texture.viewDimension = wgpu::TextureViewDimension::e2DArray;
  std::vector<wgpu::BindGroupLayoutEntry> s1 = {sunArr, samp(1, F, true), tex(2, F), samp(3, F), arr, samp(5, F), gCells, gList, lampArr};
  for (uint32_t b = 10; b < 16; ++b) {  // the light cache's probes, a face each (3D)
    wgpu::BindGroupLayoutEntry c = tex(b, F);
    c.texture.viewDimension = wgpu::TextureViewDimension::e3D;
    s1.push_back(c);
  }
  scene1Layout_ = layout(s1);
  postLayout_ = layout({ubo(0, F), tex(1, F), tex(2, F), samp(3, F), ssbo(4, F), ubo(5, F)});
  expLayout_ = layout({ubo(0, wgpu::ShaderStage::Compute), tex(1, wgpu::ShaderStage::Compute, false, true), ssbo(2, wgpu::ShaderStage::Compute, true)});
  std::string sceneCode = std::string("diagnostic(off, derivative_uniformity);\n") + kCommonWgsl + kRayWgsl + kSceneWgsl + kClusterDrawWgsl;
  wgpu::ShaderModule sm = module(sceneCode, "scene");
  wgpu::ShaderModule pm = module(kPostWgsl, "post");
  wgpu::ShaderModule em = module(kExposureWgsl, "exposure");

  // a pipeline's layout, its storage buffers counted against the iPhone's
  // limit (a stage over 8 left the phone's screen black on 2026-11-06)
  auto pipeLayout = [&](std::vector<wgpu::BindGroupLayout> ls, const char* step) {
    std::vector<FrameGraph::StageBuffers> groups;
    for (const wgpu::BindGroupLayout& l : ls)
      for (const auto& [known, sb] : stageBuffers_)
        if (known.Get() == l.Get()) groups.push_back(sb);
    for (const std::string& fault : FrameGraph::storageFaults(step, groups)) reportGpuError(fault);
    wgpu::PipelineLayoutDescriptor d;
    d.bindGroupLayoutCount = ls.size();
    d.bindGroupLayouts = ls.data();
    return device_.CreatePipelineLayout(&d);
  };
  wgpu::PipelineLayout sceneL = pipeLayout({scene0Layout_, scene1Layout_, rayLayout_}, "forward");
  wgpu::PipelineLayout shadowL = pipeLayout({scene0Layout_}, "shadow");
  wgpu::PipelineLayout postL = pipeLayout({postLayout_}, "bloom and film");

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
    // compiled in: how much the shader does (render.shading), and whether lamps trace shadow rays
    wgpu::ConstantEntry ce[2];
    if (lampShadows >= 0) {  // the passes that light (the others never read these)
      ce[0].key = "kLampShadows";
      ce[0].value = lampShadows;
      ce[1].key = "kShading";
      ce[1].value = g_shading;
      fst.constantCount = 2;
      fst.constants = ce;
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
  // ---- the cluster drawing (render.clusters): the GPU's cull, one draw into
  // the visibility picture, and lighting each pixel once from it (at most 8
  // storage buffers a stage: what every WebGPU device allows, an iPhone too)
  {
    const auto C = wgpu::ShaderStage::Compute;
    clusterDrawLayout_ = layout({ssbo(0, VF), ssbo(1, wgpu::ShaderStage::Vertex)});
    wgpu::BindGroupLayoutEntry vt = tex(2, F);
    vt.texture.sampleType = wgpu::TextureSampleType::Uint;
    wgpu::BindGroupLayoutEntry shadowOut;
    shadowOut.binding = 6, shadowOut.visibility = F;
    shadowOut.storageTexture.access = wgpu::StorageTextureAccess::WriteOnly;
    shadowOut.storageTexture.format = wgpu::TextureFormat::RGBA16Float;
    shadowOut.storageTexture.viewDimension = wgpu::TextureViewDimension::e2D;
    clusterResolveLayout_ = layout({ssbo(0, F), vt, tex(3, F, true), tex(4, F), tex(5, F, false, true), shadowOut});
    cullLayout_ = layout({ubo(0, C), ssbo(1, C), ssbo(2, C), ssbo(3, C), ssbo(4, C), ssbo(5, C, true), ssbo(6, C, true), ssbo(7, C, true), ssbo(8, C, true),
                          tex(9, C, false, true), tex(10, C, false, true)});
    // the depth pyramid (phase 4)
    hzbDepthLayout_ = layout({ubo(0, F), tex(1, F, true)});
    hzbLevelLayout_ = layout({ubo(0, F), tex(2, F, false, true)});
    {
      wgpu::ShaderModule hm = module(kHzbWgsl, "depth pyramid");
      for (int k = 0; k < 2; ++k) {
        wgpu::RenderPipelineDescriptor d;
        d.layout = pipeLayout({k == 0 ? hzbDepthLayout_ : hzbLevelLayout_}, "depth pyramid");
        d.vertex.module = hm;
        d.vertex.entryPoint = "vsHzb";
        wgpu::ColorTargetState ct;
        ct.format = wgpu::TextureFormat::R32Float;
        wgpu::FragmentState fst;
        fst.module = hm;
        fst.entryPoint = k == 0 ? "fsFromDepth" : "fsFromLevel";
        fst.targetCount = 1;
        fst.targets = &ct;
        d.fragment = &fst;
        (k == 0 ? hzbDepthPipe_ : hzbLevelPipe_) = device_.CreateRenderPipeline(&d);
      }
    }
    {
      wgpu::RenderPipelineDescriptor d;
      d.layout = pipeLayout({scene0Layout_, scene1Layout_, rayLayout_, clusterDrawLayout_}, "visibility");
      d.vertex.module = sm;
      d.vertex.entryPoint = "vsVis";
      wgpu::ColorTargetState ct;
      ct.format = wgpu::TextureFormat::RG32Uint;
      wgpu::FragmentState fst;
      fst.module = sm;
      fst.entryPoint = "fsVis";
      fst.targetCount = 1;
      fst.targets = &ct;
      d.fragment = &fst;
      d.primitive.cullMode = wgpu::CullMode::None;
      wgpu::DepthStencilState ds;
      ds.format = wgpu::TextureFormat::Depth32Float;
      ds.depthWriteEnabled = true;
      ds.depthCompare = wgpu::CompareFunction::Less;
      d.depthStencil = &ds;
      visPipe_ = device_.CreateRenderPipeline(&d);
    }
    {
      // the sun's shadow from clusters (groups 1 and 2 empty: the shadow map
      // it draws into is in group 1 of the scene's)
      emptyLayout_ = layout({});
      emptyGroup_ = group(emptyLayout_, {});
      wgpu::RenderPipelineDescriptor d;
      d.layout = pipeLayout({scene0Layout_, emptyLayout_, emptyLayout_, clusterDrawLayout_}, "shadow (clusters)");
      d.vertex.module = sm;
      d.vertex.entryPoint = "vsVisShadow";
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
      ds.format = wgpu::TextureFormat::Depth32Float;
      ds.depthWriteEnabled = true;
      ds.depthCompare = wgpu::CompareFunction::Less;
      ds.depthBias = 2;
      ds.depthBiasSlopeScale = 2.0f;
      d.depthStencil = &ds;
      shadowVisPipe_ = device_.CreateRenderPipeline(&d);
      // the main lamps' shadow maps: the same, each view's matrix in group 1
      lampFaceLayout_ = layout({ubo(9, wgpu::ShaderStage::Vertex)});
      d.layout = pipeLayout({scene0Layout_, lampFaceLayout_, emptyLayout_, clusterDrawLayout_}, "lamp shadows");
      d.vertex.entryPoint = "vsVisLampShadow";
      lampShadowPipe_ = device_.CreateRenderPipeline(&d);
    }
    for (int r = 0; r < 2; ++r) {
      wgpu::RenderPipelineDescriptor d;
      d.layout = pipeLayout({scene0Layout_, scene1Layout_, rayLayout_, clusterResolveLayout_}, "light");
      d.vertex.module = sm;
      d.vertex.entryPoint = "vsResolve";
      wgpu::ColorTargetState ct;
      ct.format = wgpu::TextureFormat::RGBA16Float;
      wgpu::FragmentState fst;
      fst.module = sm;
      fst.entryPoint = "fsResolve";
      fst.targetCount = 1;
      fst.targets = &ct;
      wgpu::ConstantEntry ce[2];
      ce[0].key = "kLampShadows";
      ce[0].value = r;
      ce[1].key = "kShading";
      ce[1].value = g_shading;
      fst.constantCount = 2;
      fst.constants = ce;
      d.fragment = &fst;
      wgpu::DepthStencilState ds;
      ds.format = wgpu::TextureFormat::Depth32Float;
      ds.depthWriteEnabled = false;
      ds.depthCompare = wgpu::CompareFunction::Always;
      d.depthStencil = &ds;
      resolvePipe_[r] = device_.CreateRenderPipeline(&d);
    }
    wgpu::ShaderModule cm = module(kClusterCullWgsl, "cluster cull");
    wgpu::ComputePipelineDescriptor cd;
    cd.layout = pipeLayout({cullLayout_}, "cluster cull");
    cd.compute.module = cm;
    cd.compute.entryPoint = "cull";
    cullPipe_ = device_.CreateComputePipeline(&cd);
    cd.compute.entryPoint = "count";
    countPipe_ = device_.CreateComputePipeline(&cd);
    cd.compute.entryPoint = "pack";
    packPipe_ = device_.CreateComputePipeline(&cd);
    // frame blending
    wgpu::BindGroupLayoutEntry seen = tex(5, F);  // the visibility picture: which copy each pixel shows
    seen.texture.sampleType = wgpu::TextureSampleType::Uint;
    taaLayout_ = layout({ubo(0, F), tex(1, F), tex(2, F), tex(3, F, true), samp(4, F), seen, ssbo(6, F)});
    {
      wgpu::ShaderModule tm = module(kTaaWgsl, "frame blending");
      wgpu::RenderPipelineDescriptor d;
      d.layout = pipeLayout({taaLayout_}, "blend");
      d.vertex.module = tm;
      d.vertex.entryPoint = "vsTaa";
      wgpu::ColorTargetState ct;
      ct.format = wgpu::TextureFormat::RGBA16Float;
      wgpu::FragmentState fst;
      fst.module = tm;
      fst.entryPoint = "fsTaa";
      fst.targetCount = 1;
      fst.targets = &ct;
      d.fragment = &fst;
      taaPipe_ = device_.CreateRenderPipeline(&d);
    }
    if (!taaUbo_) taaUbo_ = buffer(sizeof(Mat4) * 3 + 48, wgpu::BufferUsage::Uniform | wgpu::BufferUsage::CopyDst);
  }
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
    ds.format = wgpu::TextureFormat::Depth32Float;
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
    d.layout = pipeLayout({scene0Layout_, ultraLayout_, rayLayout_}, "ultra");
    d.compute.module = module(code, "ultra");
    d.compute.entryPoint = "main";
    ultraPipe_ = device_.CreateComputePipeline(&d);
  }
  {
    wgpu::ComputePipelineDescriptor d;
    d.layout = pipeLayout({denoiseLayout_}, "ultra denoise");
    d.compute.module = module(kDenoiseWgsl, "denoise");
    d.compute.entryPoint = "main";
    denoisePipe_ = device_.CreateComputePipeline(&d);
    for (auto& u : denoiseUbo_) u = buffer(48, wgpu::BufferUsage::Uniform | wgpu::BufferUsage::CopyDst);
  }
  {
    wgpu::ComputePipelineDescriptor d;
    d.layout = pipeLayout({expLayout_}, "exposure");
    d.compute.module = em;
    d.compute.entryPoint = "main";
    exposurePipe_ = device_.CreateComputePipeline(&d);
  }
}

void GpuRenderer::resize() {
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

void GpuRenderer::makeTargets(uint32_t ow, uint32_t oh) {
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
    if (config().render.clusters) {
      visView_ = target(ow, oh, wgpu::TextureFormat::RG32Uint, 1, attach | wgpu::TextureUsage::TextureBinding, visTex_);
      // frame blending reads the depth, and two pictures of the past
      depthView_ = target(ow, oh, wgpu::TextureFormat::Depth32Float, 1, attach | wgpu::TextureUsage::TextureBinding, depthTex_);
      for (int i = 0; i < 2; ++i)
        histView_[i] = target(ow, oh, wgpu::TextureFormat::RGBA16Float, 1, attach | wgpu::TextureUsage::TextureBinding | wgpu::TextureUsage::CopySrc, histTex_[i]);
      for (int i = 0; i < 2; ++i)
        shadowHistView_[i] = target(ow, oh, wgpu::TextureFormat::RGBA16Float, 1, wgpu::TextureUsage::TextureBinding | wgpu::TextureUsage::StorageBinding, shadowHistTex_[i]);
      histValid_ = false;
      // the depth pyramids (phase 4): half the screen, rounded up to a power of two, every level down to one texel
      uint32_t pw = 1, ph = 1;
      while (pw < (ow + 1) / 2) pw *= 2;
      while (ph < (oh + 1) / 2) ph *= 2;
      hzbLevels_ = 1;
      while ((1u << (hzbLevels_ - 1)) < std::max(pw, ph)) ++hzbLevels_;
      hzbUbo_.clear();
      for (uint32_t l = 0; l < hzbLevels_; ++l) hzbUbo_.push_back(buffer(32, wgpu::BufferUsage::Uniform | wgpu::BufferUsage::CopyDst));
      for (int i = 0; i < 2; ++i) {
        wgpu::TextureDescriptor td;
        td.size = {pw, ph, 1};
        td.format = wgpu::TextureFormat::R32Float;
        td.mipLevelCount = hzbLevels_;
        td.usage = attach | wgpu::TextureUsage::TextureBinding;
        hzbTex_[i] = device_.CreateTexture(&td);
        hzbAll_[i] = hzbTex_[i].CreateView();
        hzbMip_[i].clear();
        for (uint32_t l = 0; l < hzbLevels_; ++l) {
          wgpu::TextureViewDescriptor vd;
          vd.baseMipLevel = l, vd.mipLevelCount = 1;
          hzbMip_[i].push_back(hzbTex_[i].CreateView(&vd));
        }
      }
      hzbValid_ = false;
    } else {
      visView_ = nullptr, visTex_ = nullptr;
      histView_[0] = histView_[1] = nullptr, histTex_[0] = histTex_[1] = nullptr;
    }
  }
  hdrView_ = target(ow, oh, wgpu::TextureFormat::RGBA16Float, 1, attach | wgpu::TextureUsage::TextureBinding | wgpu::TextureUsage::StorageBinding | wgpu::TextureUsage::CopyDst, hdrTex_);
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

void GpuRenderer::writePassUniforms() {
  auto part = [](uint32_t vw, uint32_t vh, uint32_t aw, uint32_t ah) {
    aw = std::max(1u, aw), ah = std::max(1u, ah);
    return Vec4(static_cast<float>(vw) / aw, static_cast<float>(vh) / ah, (vw - 0.5f) / aw, (vh - 0.5f) / ah);
  };
  Vec4 hdr = part(postW_, postH_, outW_, outH_);
  Vec4 lv[kBloomLevels];
  for (int i = 0; i < kBloomLevels; ++i) lv[i] = part(levelW(i), levelH(i), std::max(1u, outW_ >> (i + 1)), std::max(1u, outH_ >> (i + 1)));
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

wgpu::BindGroup GpuRenderer::group(wgpu::BindGroupLayout l, std::vector<wgpu::BindGroupEntry> e) {
  wgpu::BindGroupDescriptor d;
  d.layout = l;
  d.entryCount = e.size();
  d.entries = e.data();
  return device_.CreateBindGroup(&d);
}

wgpu::BindGroupEntry GpuRenderer::bufE(uint32_t b, wgpu::Buffer buf) {
  wgpu::BindGroupEntry e;
  e.binding = b, e.buffer = buf;
  return e;
}

wgpu::BindGroupEntry GpuRenderer::texE(uint32_t b, wgpu::TextureView v) {
  wgpu::BindGroupEntry e;
  e.binding = b, e.textureView = v;
  return e;
}

wgpu::BindGroupEntry GpuRenderer::sampE(uint32_t b, wgpu::Sampler s) {
  wgpu::BindGroupEntry e;
  e.binding = b, e.sampler = s;
  return e;
}

void GpuRenderer::makeBindGroups() {
  if (!hdrView_) return;
  std::vector<ClusterList*> lists = {&eyeList_, &sunList_, &eyeList2_, &sunMovers_, &sunFarList_};
  if (sunFarUbo_ && lampFaceLayout_) sunFarGroup_ = group(lampFaceLayout_, {bufE(9, sunFarUbo_)});
  for (ClusterList& L : lampLists_) lists.push_back(&L);
  for (int i = 0; i < kLampMapsMost * 6; ++i)
    if (lampFaceUbo_[i] && lampFaceLayout_) lampFaceGroup_[i] = group(lampFaceLayout_, {bufE(9, lampFaceUbo_[i])});
  for (ClusterList* L : lists)
    if (poolVerts_ && L->drawn && L->cullList && L->stretch && L->counts && hzbAll_[0]) {
      L->drawGroup = group(clusterDrawLayout_, {bufE(0, poolVerts_), bufE(1, L->drawn)});
      // old = last frame's pyramid, new = this frame's: which is which flips every frame
      for (int k = 0; k < 2; ++k)
        (k == 0 ? L->cullGroup : L->cullGroupB) =
            group(cullLayout_, {bufE(0, L->ubo), bufE(1, instancesBuf_), bufE(2, L->cullList), bufE(3, poolMeshes_), bufE(4, poolVerts_), bufE(5, L->drawn),
                                bufE(6, L->args), bufE(7, L->stretch), bufE(8, L->counts), texE(9, hzbAll_[k ^ 1]), texE(10, hzbAll_[k])});
    }
  for (int i = 0; i < 2; ++i) {
    hzbGroup_[i].clear();
    if (hzbTex_[i] && depthView_)
      for (uint32_t l = 0; l < hzbLevels_; ++l)
        hzbGroup_[i].push_back(l == 0 ? group(hzbDepthLayout_, {bufE(0, hzbUbo_[0]), texE(1, depthView_)})
                                      : group(hzbLevelLayout_, {bufE(0, hzbUbo_[l]), texE(2, hzbMip_[i][l - 1])}));
  }
  if (poolVerts_ && visView_ && eyeList_.drawn) {
    // the light pass reads last frame's picture (the frame blending's newest past): which is which flips
    if (!pastDummyTex_) {
      wgpu::TextureDescriptor td;
      td.size = {1, 1, 1};
      td.format = wgpu::TextureFormat::RGBA16Float;
      td.usage = wgpu::TextureUsage::TextureBinding;
      pastDummyTex_ = device_.CreateTexture(&td);
    }
    // [past picture][kept shadow]: each pair swaps on its own
    for (int i = 0; i < 2; ++i)
      for (int k = 0; k < 2; ++k)
        clusterResolveGroup_[i * 2 + k] = group(clusterResolveLayout_, {bufE(0, poolVerts_), texE(2, visView_), texE(3, depthView_),
                                                                       texE(4, histView_[i] ? histView_[i] : pastDummyTex_.CreateView()),
                                                                       texE(5, shadowHistView_[k]), texE(6, shadowHistView_[k ^ 1])});
    if (histView_[0])
      for (int i = 0; i < 2; ++i)
        taaGroup_[i] = group(taaLayout_, {bufE(0, taaUbo_), texE(1, hdrView_), texE(2, histView_[i]), texE(3, depthView_), sampE(4, linear_),
                                          texE(5, visView_), bufE(6, backBuf_)});
  }
  sceneGroup0_ = group(scene0Layout_, {bufE(0, frameUbo_), bufE(1, lightsBuf_), bufE(2, materialsBuf_), bufE(3, instancesBuf_), bufE(5, visibleBuf_)});
  if (!modelView_) uploadModelTextures(Scene());
  if (!gridCellsTex_) layOutGrid();
  if (!cacheTex_[0]) makeCacheTextures();
  if (!lampMapTex_) makeLampMap(0);
  sceneGroup1_ = group(scene1Layout_, {texE(0, shadowView_), sampE(1, shadowSampler_), texE(2, skyView_), sampE(3, linear_), texE(4, modelView_), sampE(5, repeat_),
                                       texE(6, gridCellsView_), texE(7, gridListView_), texE(8, lampMapView_),
                                       texE(10, cacheView_[0]), texE(11, cacheView_[1]), texE(12, cacheView_[2]),
                                       texE(13, cacheView_[3]), texE(14, cacheView_[4]), texE(15, cacheView_[5])});
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
    if (!resBuf_) resBuf_ = buffer(2 * 64, wgpu::BufferUsage::Storage), resBlocks_ = 1;
    ultraGroup_ = group(ultraLayout_, {bufE(0, accumBuf_), texE(1, hdrView_), texE(2, skyView_), sampE(3, linear_), bufE(4, ultraUbo_), bufE(5, resBuf_)});
    resValid_ = false;
    // passes: average → A, A → B (step 1), B → A (step 2), A → B (step 4, onto the picture)
    wgpu::Buffer srcs[4] = {denoiseB_, denoiseA_, denoiseB_, denoiseA_}, dsts[4] = {denoiseA_, denoiseB_, denoiseA_, denoiseB_};
    for (int k = 0; k < 4; ++k)
      denoiseGroup_[k] = group(denoiseLayout_, {bufE(0, denoiseUbo_[k]), bufE(1, accumBuf_), bufE(2, srcs[k]), bufE(3, dsts[k]), texE(4, hdrView_), bufE(5, exposureBuf_)});
  }
  bindGroupsDirty_ = false;
}

}  // namespace loop::gpu::impl
