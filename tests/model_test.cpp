// Models from files: our JPEG decoder, the .glb reader and the model word.
#include <cstdio>
#include <cstring>

#include "engine/core/file.h"
#include "engine/core/jpeg.h"
#include "engine/core/json.h"
#include "engine/geometry/gltf.h"
#include "engine/script/scene_api.h"
#include "tests/test.h"

using namespace loop;
using namespace loop::script;

namespace {

// A 16 x 16 colour gradient saved as a baseline JPEG with 4:2:0 colour
// (as Blender writes them), and pixels a standard decoder gives for it.
const uint8_t kJpeg[] = {255,216,255,224,0,16,74,70,73,70,0,1,1,0,0,1,0,1,0,0,255,219,0,67,0,2,1,1,1,1,1,2,1,1,1,2,2,2,2,2,4,3,2,2,2,2,5,4,4,3,4,6,5,6,6,6,5,6,6,6,7,9,8,6,7,9,7,6,6,8,11,8,9,10,10,10,10,10,6,8,11,12,11,10,12,9,10,10,10,255,219,0,67,1,2,2,2,2,2,2,5,3,3,5,10,7,6,7,10,10,10,10,10,10,10,10,10,10,10,10,10,10,10,10,10,10,10,10,10,10,10,10,10,10,10,10,10,10,10,10,10,10,10,10,10,10,10,10,10,10,10,10,10,10,10,10,10,10,255,192,0,17,8,0,16,0,16,3,1,34,0,2,17,1,3,17,1,255,196,0,31,0,0,1,5,1,1,1,1,1,1,0,0,0,0,0,0,0,0,1,2,3,4,5,6,7,8,9,10,11,255,196,0,181,16,0,2,1,3,3,2,4,3,5,5,4,4,0,0,1,125,1,2,3,0,4,17,5,18,33,49,65,6,19,81,97,7,34,113,20,50,129,145,161,8,35,66,177,193,21,82,209,240,36,51,98,114,130,9,10,22,23,24,25,26,37,38,39,40,41,42,52,53,54,55,56,57,58,67,68,69,70,71,72,73,74,83,84,85,86,87,88,89,90,99,100,101,102,103,104,105,106,115,116,117,118,119,120,121,122,131,132,133,134,135,136,137,138,146,147,148,149,150,151,152,153,154,162,163,164,165,166,167,168,169,170,178,179,180,181,182,183,184,185,186,194,195,196,197,198,199,200,201,202,210,211,212,213,214,215,216,217,218,225,226,227,228,229,230,231,232,233,234,241,242,243,244,245,246,247,248,249,250,255,196,0,31,1,0,3,1,1,1,1,1,1,1,1,1,0,0,0,0,0,0,1,2,3,4,5,6,7,8,9,10,11,255,196,0,181,17,0,2,1,2,4,4,3,4,7,5,4,4,0,1,2,119,0,1,2,3,17,4,5,33,49,6,18,65,81,7,97,113,19,34,50,129,8,20,66,145,161,177,193,9,35,51,82,240,21,98,114,209,10,22,36,52,225,37,241,23,24,25,26,38,39,40,41,42,53,54,55,56,57,58,67,68,69,70,71,72,73,74,83,84,85,86,87,88,89,90,99,100,101,102,103,104,105,106,115,116,117,118,119,120,121,122,130,131,132,133,134,135,136,137,138,146,147,148,149,150,151,152,153,154,162,163,164,165,166,167,168,169,170,178,179,180,181,182,183,184,185,186,194,195,196,197,198,199,200,201,202,210,211,212,213,214,215,216,217,218,226,227,228,229,230,231,232,233,234,242,243,244,245,246,247,248,249,250,255,218,0,12,3,1,0,2,17,3,17,0,63,0,248,131,225,199,236,203,159,47,254,37,255,0,248,237,123,231,195,143,217,147,62,95,252,75,255,0,241,218,250,83,225,199,236,201,254,175,254,37,255,0,248,237,123,231,195,159,217,147,253,95,252,75,253,63,134,189,239,30,190,149,95,197,253,255,0,126,166,95,70,31,27,191,129,251,222,221,79,255,217};
struct Px { int x, y, r, g, b; };
const Px kExpected[] = {
  {0, 0, 3, 2, 243},
  {15, 0, 230, 5, 133},
  {0, 15, 9, 235, 137},
  {8, 8, 128, 128, 126},
  {15, 15, 239, 239, 29},
  {3, 12, 49, 193, 134},
};

void put32(std::vector<uint8_t>& b, uint32_t v) {
  for (int i = 0; i < 4; ++i) b.push_back(static_cast<uint8_t>(v >> (8 * i)));
}

// A tiny .glb: one square (two triangles) moved 2 m up by its node, a
// see-through tinted material, and the JPEG above as its picture.
std::vector<uint8_t> tinyGlb() {
  std::vector<uint8_t> bin;
  auto f32 = [&](float v) { uint8_t t[4]; std::memcpy(t, &v, 4); bin.insert(bin.end(), t, t + 4); };
  float pos[] = {0, 0, 0, 1, 0, 0, 1, 1, 0, 0, 1, 0};
  float uv[] = {0, 1, 1, 1, 1, 0, 0, 0};
  for (float v : pos) f32(v);
  for (float v : uv) f32(v);
  uint16_t idx[] = {0, 1, 2, 0, 2, 3};
  for (uint16_t i : idx) bin.push_back(static_cast<uint8_t>(i)), bin.push_back(static_cast<uint8_t>(i >> 8));
  size_t imgOff = bin.size();
  bin.insert(bin.end(), kJpeg, kJpeg + sizeof(kJpeg));
  while (bin.size() % 4) bin.push_back(0);
  char json[2048];
  std::snprintf(json, sizeof(json),
      R"({"asset":{"version":"2.0"},"scene":0,"scenes":[{"nodes":[0]}],)"
      R"("nodes":[{"mesh":0,"translation":[0,2,0]}],)"
      R"("meshes":[{"primitives":[{"attributes":{"POSITION":0,"TEXCOORD_0":1},"indices":2,"material":0}]}],)"
      R"("materials":[{"name":"Tinted glass","alphaMode":"BLEND","pbrMetallicRoughness":{"baseColorFactor":[0.5,0.9,0.5,0.3],"baseColorTexture":{"index":0},"metallicFactor":0,"roughnessFactor":0.1}}],)"
      R"("textures":[{"source":0}],"images":[{"bufferView":3,"mimeType":"image/jpeg"}],)"
      R"("accessors":[{"bufferView":0,"componentType":5126,"count":4,"type":"VEC3"},{"bufferView":1,"componentType":5126,"count":4,"type":"VEC2"},{"bufferView":2,"componentType":5123,"count":6,"type":"SCALAR"}],)"
      R"("bufferViews":[{"buffer":0,"byteOffset":0,"byteLength":48},{"buffer":0,"byteOffset":48,"byteLength":32},{"buffer":0,"byteOffset":80,"byteLength":12},{"buffer":0,"byteOffset":%zu,"byteLength":%zu}],)"
      R"("buffers":[{"byteLength":%zu}]})",
      imgOff, sizeof(kJpeg), bin.size());
  std::string j = json;
  while (j.size() % 4) j += ' ';
  std::vector<uint8_t> glb = {'g', 'l', 'T', 'F'};
  put32(glb, 2);
  put32(glb, static_cast<uint32_t>(12 + 8 + j.size() + 8 + bin.size()));
  put32(glb, static_cast<uint32_t>(j.size()));
  put32(glb, 0x4E4F534A);
  glb.insert(glb.end(), j.begin(), j.end());
  put32(glb, static_cast<uint32_t>(bin.size()));
  put32(glb, 0x004E4942);
  glb.insert(glb.end(), bin.begin(), bin.end());
  return glb;
}

}  // namespace

TEST(jpeg_decodes_baseline_like_a_standard_decoder) {
  std::vector<uint8_t> px;
  int w = 0, h = 0;
  std::string err;
  CHECK(decodeJpeg(kJpeg, sizeof(kJpeg), px, w, h, &err));
  CHECK(w == 16 && h == 16);
  for (const Px& e : kExpected) {
    const uint8_t* p = &px[(static_cast<size_t>(e.y) * w + e.x) * 4];
    CHECK(std::abs(p[0] - e.r) <= 6 && std::abs(p[1] - e.g) <= 6 && std::abs(p[2] - e.b) <= 6);
    CHECK(p[3] == 255);
  }
  // not a JPEG, and a cut-off one: refused with a reason, never a crash
  uint8_t junk[] = {1, 2, 3, 4, 5};
  CHECK(!decodeJpeg(junk, sizeof(junk), px, w, h, &err) && !err.empty());
  CHECK(!decodeJpeg(kJpeg, 200, px, w, h, &err));
}

TEST(glb_reads_meshes_materials_and_pictures) {
  GltfModel m;
  std::string err;
  CHECK(loadGlb(tinyGlb(), m, &err));
  CHECK(m.parts.size() == 1);
  CHECK(m.parts[0].mesh.triangleCount() == 2);
  CHECK_NEAR(m.bounds().lo.y, 2.0f, 1e-5f);  // the node's move is baked in
  CHECK_NEAR(m.bounds().hi.x, 1.0f, 1e-5f);
  CHECK(m.materials.size() == 1 && m.materials[0].alpha == GltfMaterial::Alpha::kBlend);
  CHECK_NEAR(m.materials[0].baseColor.w, 0.3f, 1e-5f);
  CHECK(m.textures.size() == 1 && m.textures[0].width == 16);
  // damaged files are refused with a reason
  std::vector<uint8_t> cut = tinyGlb();
  cut.resize(cut.size() / 2);
  CHECK(!loadGlb(cut, m, &err) && !err.empty());
  CHECK(!loadGlb({'n', 'o', 'p', 'e'}, m, &err));
}

TEST(model_word_places_every_part_and_makes_glass) {
  std::string dir = "build/model_test";
  std::vector<uint8_t> glb = tinyGlb();
  std::remove((dir + "/models/tiny.glb").c_str());
  std::string mk = "mkdir -p " + dir + "/models";
  CHECK(std::system(mk.c_str()) == 0);
  CHECK(writeFile(dir + "/models/tiny.glb", glb.data(), glb.size()));
  Vm vm;
  Stage st;
  installStandardLibrary(vm);
  installSceneApi(vm, st);
  st.folder = dir;
  bool ok = vm.load("let a = model(\"tiny.glb\", at: vec(1, 0, 0))\nmodel \"tiny.glb\", at: vec(-1, 0, 0)\na.position = vec(3, 0, 0)\n", "models.loop");
  for (auto& p : vm.problems()) std::printf("  %s\n", p.text().c_str());
  CHECK(ok);
  if (!ok || st.placed.size() < 2) return;
  CHECK(st.modelCache.size() == 1);   // read once, placed twice
  CHECK(st.scene.textures.size() == 1);
  const MeshInstance& inst = st.scene.instances[st.placed[0].instance];
  CHECK_NEAR(inst.transform.point(Vec3(0)).x, 3.0f, 1e-5f);
  const Material& mat = st.scene.materials[inst.material];
  CHECK(mat.transmission > 0.99f);    // see-through and clear: real glass
  CHECK(mat.absorption.y < mat.absorption.x);  // tinted green
  // a missing model is a plain problem with its line
  Vm vm2;
  Stage st2;
  installStandardLibrary(vm2);
  installSceneApi(vm2, st2);
  st2.folder = dir;
  CHECK(!vm2.load("model \"nope.glb\"\n", "missing.loop"));
  CHECK(!vm2.problems().empty() && vm2.problems()[0].what.find("no model called") != std::string::npos);
}

TEST(blocker_stops_walking_and_is_never_drawn) {
  Vm vm;
  Stage st;
  installStandardLibrary(vm);
  installSceneApi(vm, st);
  CHECK(vm.load("blocker size: vec(4, 2.6, 0.2), at: vec(0, 1.3, -2)\n", "walls.loop"));
  CHECK(st.scene.instances.empty());  // nothing to draw
  st.update(1.0f / 60);
  Vec3 c{0, 0.8f, 0};
  for (int i = 0; i < 500; ++i) st.world.moveCharacter(c, Vec3(0, 0, -0.01f), 0.25f, 0.3f, 0.3f);
  CHECK(c.z > -2.0f && c.z < -1.5f);  // stopped by the invisible wall
}
