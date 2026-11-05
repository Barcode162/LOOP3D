#include "engine/geometry/gltf.h"

#include <cstring>
#include <functional>

#include "engine/core/file.h"
#include "engine/core/jpeg.h"
#include "engine/core/json.h"

namespace loop {
namespace {

uint32_t le32(const uint8_t* p) { return p[0] | (p[1] << 8) | (p[2] << 16) | (static_cast<uint32_t>(p[3]) << 24); }

struct Reader {
  const Json& j;
  const uint8_t* bin;
  size_t binSize;
  std::string err;

  bool fail(const std::string& why) {
    if (err.empty()) err = why;
    return false;
  }

  // The bytes of a buffer view (only the .glb's own buffer is read).
  bool view(int index, const uint8_t*& data, size_t& size, size_t& stride) {
    const Json& views = j["bufferViews"];
    if (index < 0 || index >= static_cast<int>(views.size())) return fail("a model points at a missing buffer view");
    const Json& v = views[static_cast<size_t>(index)];
    if (v["buffer"].number(0) != 0) return fail("a model uses a second buffer (only .glb files with one are read)");
    size_t off = static_cast<size_t>(v["byteOffset"].number(0)), len = static_cast<size_t>(v["byteLength"].number(0));
    if (off + len > binSize) return fail("a model's data runs past its end");
    data = bin + off, size = len, stride = static_cast<size_t>(v["byteStride"].number(0));
    return true;
  }

  // An accessor as floats, `comps` numbers per item (normalised ints scaled).
  bool floats(int index, int comps, std::vector<float>& out) {
    const Json& acc = j["accessors"];
    if (index < 0 || index >= static_cast<int>(acc.size())) return fail("a model points at a missing accessor");
    const Json& a = acc[static_cast<size_t>(index)];
    size_t count = static_cast<size_t>(a["count"].number(0));
    int type = static_cast<int>(a["componentType"].number(0));
    bool norm = a["normalized"].boolean(false);
    static const std::pair<const char*, int> kTypes[] = {{"SCALAR", 1}, {"VEC2", 2}, {"VEC3", 3}, {"VEC4", 4}};
    int have = 0;
    for (auto& [name, c] : kTypes)
      if (a["type"].string() == name) have = c;
    if (have == 0) return fail("a model has an accessor of a type we don't read (" + a["type"].string() + ")");
    int bytes = type == 5126 || type == 5125 ? 4 : type == 5123 || type == 5122 ? 2 : 1;
    out.assign(count * comps, 0);
    if (!a.has("bufferView")) return true;  // all zeros
    const uint8_t* data;
    size_t size, stride;
    if (!view(static_cast<int>(a["bufferView"].number(0)), data, size, stride)) return false;
    size_t off = static_cast<size_t>(a["byteOffset"].number(0));
    size_t elem = static_cast<size_t>(bytes) * have;
    if (stride == 0) stride = elem;
    if (count && off + (count - 1) * stride + elem > size) return fail("a model's accessor runs past its data");
    for (size_t i = 0; i < count; ++i) {
      const uint8_t* p = data + off + i * stride;
      for (int c = 0; c < std::min(comps, have); ++c) {
        const uint8_t* q = p + c * bytes;
        float v = 0;
        switch (type) {
          case 5126: std::memcpy(&v, q, 4); break;
          case 5125: v = static_cast<float>(le32(q)); break;
          case 5123: { uint16_t u = static_cast<uint16_t>(q[0] | (q[1] << 8)); v = norm ? u / 65535.0f : u; break; }
          case 5122: { int16_t s = static_cast<int16_t>(q[0] | (q[1] << 8)); v = norm ? std::max(s / 32767.0f, -1.0f) : s; break; }
          case 5121: v = norm ? q[0] / 255.0f : q[0]; break;
          case 5120: { int8_t s = static_cast<int8_t>(q[0]); v = norm ? std::max(s / 127.0f, -1.0f) : s; break; }
          default: return fail("a model has numbers of a kind we don't read");
        }
        out[i * comps + c] = v;
      }
    }
    return true;
  }

  bool indices(int index, std::vector<uint32_t>& out) {
    const Json& a = j["accessors"][static_cast<size_t>(index)];
    size_t count = static_cast<size_t>(a["count"].number(0));
    int type = static_cast<int>(a["componentType"].number(0));
    const uint8_t* data;
    size_t size, stride;
    if (!view(static_cast<int>(a["bufferView"].number(-1)), data, size, stride)) return false;
    size_t off = static_cast<size_t>(a["byteOffset"].number(0));
    int bytes = type == 5125 ? 4 : type == 5123 ? 2 : type == 5121 ? 1 : 0;
    if (!bytes) return fail("a model's indices are of a kind we don't read");
    if (off + count * bytes > size) return fail("a model's indices run past their data");
    out.resize(count);
    for (size_t i = 0; i < count; ++i) {
      const uint8_t* q = data + off + i * bytes;
      out[i] = bytes == 4 ? le32(q) : bytes == 2 ? static_cast<uint32_t>(q[0] | (q[1] << 8)) : q[0];
    }
    return true;
  }

  Mat4 nodeMatrix(const Json& n) {
    if (n.has("matrix") && n["matrix"].size() == 16) {
      Mat4 m;
      for (int c = 0; c < 4; ++c)
        for (int r = 0; r < 4; ++r) m.m[c][r] = static_cast<float>(n["matrix"][static_cast<size_t>(c * 4 + r)].number(0));
      return m;
    }
    Vec3 t{0}, s{1};
    Quat q;
    if (n["translation"].size() == 3) t = {float(n["translation"][0].number()), float(n["translation"][1].number()), float(n["translation"][2].number())};
    if (n["scale"].size() == 3) s = {float(n["scale"][0].number(1)), float(n["scale"][1].number(1)), float(n["scale"][2].number(1))};
    if (n["rotation"].size() == 4) q = Quat(float(n["rotation"][0].number()), float(n["rotation"][1].number()), float(n["rotation"][2].number()), float(n["rotation"][3].number(1)));
    return Mat4::trs(t, q, s);
  }

  bool addMesh(int meshIndex, const Mat4& xf, GltfModel& out, std::vector<int>& partOfMaterial) {
    const Json& meshes = j["meshes"];
    if (meshIndex < 0 || meshIndex >= static_cast<int>(meshes.size())) return fail("a model's node points at a missing mesh");
    for (const Json& prim : meshes[static_cast<size_t>(meshIndex)]["primitives"].items()) {
      int mode = static_cast<int>(prim["mode"].number(4));
      if (mode != 4) continue;  // only triangle lists (lines and points are left out)
      const Json& at = prim["attributes"];
      if (!at.has("POSITION")) continue;
      std::vector<float> pos, nor, uv, tan;
      if (!floats(static_cast<int>(at["POSITION"].number()), 3, pos)) return false;
      size_t n = pos.size() / 3;
      if (at.has("NORMAL") && !floats(static_cast<int>(at["NORMAL"].number()), 3, nor)) return false;
      if (at.has("TEXCOORD_0") && !floats(static_cast<int>(at["TEXCOORD_0"].number()), 2, uv)) return false;
      if (at.has("TANGENT") && !floats(static_cast<int>(at["TANGENT"].number()), 4, tan)) return false;
      Mesh m;
      m.positions.resize(n), m.normals.resize(n), m.uvs.resize(n);
      if (tan.size() == n * 4) m.tangents.resize(n);
      for (size_t i = 0; i < n; ++i) {
        m.positions[i] = {pos[i * 3], pos[i * 3 + 1], pos[i * 3 + 2]};
        m.normals[i] = nor.size() == n * 3 ? Vec3{nor[i * 3], nor[i * 3 + 1], nor[i * 3 + 2]} : Vec3{0, 1, 0};
        m.uvs[i] = uv.size() == n * 2 ? Vec2{uv[i * 2], uv[i * 2 + 1]} : Vec2{};
        if (!m.tangents.empty()) m.tangents[i] = {tan[i * 4], tan[i * 4 + 1], tan[i * 4 + 2], tan[i * 4 + 3]};
      }
      if (prim.has("indices")) {
        if (!indices(static_cast<int>(prim["indices"].number()), m.indices)) return false;
        for (uint32_t ix : m.indices)
          if (ix >= n) return fail("a model's triangle points at a corner it doesn't have");
      } else {
        m.indices.resize(n - n % 3);
        for (size_t i = 0; i < m.indices.size(); ++i) m.indices[i] = static_cast<uint32_t>(i);
      }
      if (nor.size() != n * 3) m.computeNormals();
      int mat = prim.has("material") ? static_cast<int>(prim["material"].number()) : -1;
      int slot = mat + 1;  // -1 (the default material) gets slot 0
      if (slot < 0 || slot >= static_cast<int>(partOfMaterial.size())) return fail("a model's mesh points at a missing material");
      if (partOfMaterial[slot] < 0) {
        partOfMaterial[slot] = static_cast<int>(out.parts.size());
        out.parts.push_back({Mesh(), mat});
      }
      Mesh& dst = out.parts[partOfMaterial[slot]].mesh;
      dst.append(m, xf);
      if (xf.linear().determinant() < 0) {  // a mirrored node: keep the triangles facing out
        size_t first = dst.indices.size() - m.indices.size();
        for (size_t i = first; i + 2 < dst.indices.size(); i += 3) std::swap(dst.indices[i + 1], dst.indices[i + 2]);
      }
    }
    return true;
  }

  int textureIndex(const Json& info) { return info.isObject() && info.has("index") ? static_cast<int>(info["index"].number(-1)) : -1; }

  bool read(GltfModel& out) {
    // materials
    for (const Json& m : j["materials"].items()) {
      GltfMaterial g;
      g.name = m["name"].string();
      const Json& pbr = m["pbrMetallicRoughness"];
      if (pbr["baseColorFactor"].size() == 4)
        g.baseColor = {float(pbr["baseColorFactor"][0].number(1)), float(pbr["baseColorFactor"][1].number(1)), float(pbr["baseColorFactor"][2].number(1)), float(pbr["baseColorFactor"][3].number(1))};
      g.metallic = static_cast<float>(pbr["metallicFactor"].number(1));
      g.roughness = static_cast<float>(pbr["roughnessFactor"].number(1));
      g.baseTexture = textureIndex(pbr["baseColorTexture"]);
      g.metalRoughTexture = textureIndex(pbr["metallicRoughnessTexture"]);
      g.normalTexture = textureIndex(m["normalTexture"]);
      g.emissiveTexture = textureIndex(m["emissiveTexture"]);
      if (m["emissiveFactor"].size() == 3) g.emissive = {float(m["emissiveFactor"][0].number()), float(m["emissiveFactor"][1].number()), float(m["emissiveFactor"][2].number())};
      g.emissive = g.emissive * static_cast<float>(m["extensions"]["KHR_materials_emissive_strength"]["emissiveStrength"].number(1));
      const std::string& mode = m["alphaMode"].string();
      g.alpha = mode == "BLEND" ? GltfMaterial::Alpha::kBlend : mode == "MASK" ? GltfMaterial::Alpha::kMask : GltfMaterial::Alpha::kOpaque;
      g.alphaCutoff = static_cast<float>(m["alphaCutoff"].number(0.5));
      g.doubleSided = m["doubleSided"].boolean(false);
      out.materials.push_back(g);
    }
    // pictures: decode each image once, then one texture per glTF texture
    std::vector<Texture> images;
    for (const Json& im : j["images"].items()) {
      Texture t;
      if (im.has("bufferView")) {
        const uint8_t* data;
        size_t size, stride;
        if (!view(static_cast<int>(im["bufferView"].number()), data, size, stride)) return false;
        std::string why;
        bool ok = size > 3 && data[0] == 0xFF && data[1] == 0xD8 ? decodeJpeg(data, size, t.rgba, t.width, t.height, &why)
                                                                 : decodePng(data, size, t.rgba, t.width, t.height);
        if (!ok) return fail("a model's picture can't be read" + (why.empty() ? std::string() : ": " + why));
      } else {
        return fail("a model's picture is a separate file (only pictures inside the .glb are read)");
      }
      images.push_back(std::move(t));
    }
    for (const Json& tx : j["textures"].items()) {
      int src = static_cast<int>(tx["source"].number(-1));
      out.textures.push_back(src >= 0 && src < static_cast<int>(images.size()) ? images[static_cast<size_t>(src)] : Texture());
    }
    // the node tree of the scene (or every root when there's no scene)
    std::vector<int> partOfMaterial(out.materials.size() + 1, -1);
    const Json& nodes = j["nodes"];
    std::function<bool(int, const Mat4&, int)> walk = [&](int i, const Mat4& parent, int depth) {
      if (i < 0 || i >= static_cast<int>(nodes.size()) || depth > 64) return fail("a model's node tree is broken");
      const Json& n = nodes[static_cast<size_t>(i)];
      Mat4 xf = parent * nodeMatrix(n);
      if (n.has("mesh") && !addMesh(static_cast<int>(n["mesh"].number()), xf, out, partOfMaterial)) return false;
      for (const Json& c : n["children"].items())
        if (!walk(static_cast<int>(c.number(-1)), xf, depth + 1)) return false;
      return true;
    };
    const Json& scenes = j["scenes"];
    int sceneIndex = static_cast<int>(j["scene"].number(0));
    if (scenes.size() > 0) {
      for (const Json& r : scenes[static_cast<size_t>(std::min<int>(sceneIndex, static_cast<int>(scenes.size()) - 1))]["nodes"].items())
        if (!walk(static_cast<int>(r.number(-1)), Mat4(), 0)) return false;
    } else {
      std::vector<bool> child(nodes.size(), false);
      for (const Json& n : nodes.items())
        for (const Json& c : n["children"].items())
          if (c.number(-1) >= 0 && c.number(-1) < nodes.size()) child[static_cast<size_t>(c.number())] = true;
      for (size_t i = 0; i < nodes.size(); ++i)
        if (!child[i] && !walk(static_cast<int>(i), Mat4(), 0)) return false;
    }
    for (GltfModel::Part& p : out.parts)
      if (p.mesh.tangents.size() != p.mesh.positions.size()) p.mesh.tangents.clear();
    return true;
  }
};

}  // namespace

AABB GltfModel::bounds() const {
  AABB b;
  for (const Part& p : parts) {
    b.grow(p.mesh.bounds());
  }
  return b;
}

bool loadGlb(const std::vector<uint8_t>& bytes, GltfModel& out, std::string* error) {
  auto fail = [&](const std::string& why) {
    if (error) *error = why;
    return false;
  };
  out = GltfModel();
  if (bytes.size() < 20 || std::memcmp(bytes.data(), "glTF", 4) != 0) return fail("not a .glb model (it doesn't start with glTF)");
  if (le32(&bytes[4]) != 2) return fail("only glTF 2 models are read");
  size_t total = std::min<size_t>(le32(&bytes[8]), bytes.size());
  size_t pos = 12;
  std::string_view jsonText;
  const uint8_t* bin = nullptr;
  size_t binSize = 0;
  while (pos + 8 <= total) {
    uint32_t len = le32(&bytes[pos]), type = le32(&bytes[pos + 4]);
    if (pos + 8 + len > total) return fail("a damaged .glb (a chunk runs past the end)");
    if (type == 0x4E4F534A) jsonText = std::string_view(reinterpret_cast<const char*>(&bytes[pos + 8]), len);
    if (type == 0x004E4942) bin = &bytes[pos + 8], binSize = len;
    pos += 8 + len;
  }
  if (jsonText.empty()) return fail("a .glb with no description (JSON chunk)");
  Json j;
  std::string why;
  if (!Json::parse(jsonText, j, &why)) return fail("a .glb whose description can't be read: " + why);
  for (const Json& e : j["extensionsRequired"].items())
    if (e.string() != "KHR_materials_emissive_strength") return fail("a model needs the glTF extension " + e.string() + ", which we don't read yet");
  Reader r{j, bin, binSize, {}};
  if (!r.read(out)) return fail(r.err);
  return true;
}

bool loadGlbFile(const std::string& path, GltfModel& out, std::string* error) {
  std::vector<uint8_t> bytes;
  if (!readFile(path, bytes)) {
    if (error) *error = "can't open " + path;
    return false;
  }
  return loadGlb(bytes, out, error);
}

}  // namespace loop
