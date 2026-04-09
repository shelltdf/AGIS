#include "app/preview/tiles_gltf_loader.h"

#include "app/preview/model_preview_types.h"

#include <Windows.h>

#include <algorithm>
#include <array>
#include <cstdint>
#include <cctype>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <string>
#include <vector>

#define TINYGLTF_NO_INCLUDE_JSON
#include <nlohmann/json.hpp>
#define TINYGLTF_NO_STB_IMAGE
#define TINYGLTF_NO_STB_IMAGE_WRITE
#define TINYGLTF_IMPLEMENTATION
#include <tinygltf/tiny_gltf.h>

namespace {

constexpr size_t kMax3DTilesFaces = 1'800'000;
constexpr size_t kMaxRawTileBytes = 256ull * 1024 * 1024;
constexpr int kMaxResolvedContentFiles = 64;
constexpr int kMaxTilesetNestingDepth = 4;

static uint32_t Le32(const uint8_t* p) {
  uint32_t v = 0;
  std::memcpy(&v, p, 4);
  return v;
}

static size_t Pad8(size_t n) {
  return (n + 7u) & ~size_t(7);
}

static std::string WideToUtf8(const std::wstring& w) {
  if (w.empty()) {
    return {};
  }
  const int n = WideCharToMultiByte(CP_UTF8, 0, w.c_str(), static_cast<int>(w.size()), nullptr, 0, nullptr, nullptr);
  if (n <= 0) {
    return {};
  }
  std::string s(static_cast<size_t>(n), '\0');
  WideCharToMultiByte(CP_UTF8, 0, w.c_str(), static_cast<int>(w.size()), s.data(), n, nullptr, nullptr);
  return s;
}

static std::wstring Utf8ToWide(const std::string& u) {
  if (u.empty()) {
    return {};
  }
  const int n = MultiByteToWideChar(CP_UTF8, 0, u.data(), static_cast<int>(u.size()), nullptr, 0);
  if (n <= 0) {
    return std::wstring(u.begin(), u.end());
  }
  std::wstring w(static_cast<size_t>(n), L'\0');
  MultiByteToWideChar(CP_UTF8, 0, u.data(), static_cast<int>(u.size()), w.data(), n);
  return w;
}

static std::string DecodeUriPercent(const std::string& in) {
  std::string out;
  out.reserve(in.size());
  auto hex = [](char c) -> int {
    if (c >= '0' && c <= '9') {
      return c - '0';
    }
    if (c >= 'a' && c <= 'f') {
      return 10 + c - 'a';
    }
    if (c >= 'A' && c <= 'F') {
      return 10 + c - 'A';
    }
    return -1;
  };
  for (size_t i = 0; i < in.size(); ++i) {
    if (in[i] == '%' && i + 2 < in.size()) {
      const int hi = hex(in[i + 1]);
      const int lo = hex(in[i + 2]);
      if (hi >= 0 && lo >= 0) {
        out.push_back(static_cast<char>((hi << 4) | lo));
        i += 2;
        continue;
      }
    }
    out.push_back(in[i]);
  }
  return out;
}

static bool ReadWholeFileBytesW(const std::wstring& pathW, std::vector<uint8_t>* out) {
  out->clear();
  std::ifstream f(std::filesystem::path(pathW), std::ios::binary | std::ios::ate);
  if (!f) {
    return false;
  }
  const auto end = f.tellg();
  if (end <= 0) {
    return true;
  }
  out->resize(static_cast<size_t>(end));
  f.seekg(0, std::ios::beg);
  f.read(reinterpret_cast<char*>(out->data()), static_cast<std::streamsize>(out->size()));
  return true;
}

static bool PayloadOffsetB3dmI3dm(const uint8_t* d, size_t len, bool i3dm, size_t* payloadOff, uint32_t* i3dmGltfFormatOut,
                                  std::wstring* err) {
  const size_t hdr = i3dm ? 32u : 28u;
  if (len < hdr) {
    if (err) {
      *err = L"b3dm/i3dm 头过短";
    }
    return false;
  }
  const uint32_t byteLength = Le32(d + 8);
  if (byteLength < hdr || byteLength > len) {
    if (err) {
      *err = L"b3dm/i3dm byteLength 无效";
    }
    return false;
  }
  const uint32_t ftJ = Le32(d + 12);
  const uint32_t ftB = Le32(d + 16);
  const uint32_t btJ = Le32(d + 20);
  const uint32_t btB = Le32(d + 24);
  uint32_t gltfFormat = 1;
  if (i3dm) {
    gltfFormat = Le32(d + 28);
  }
  if (i3dmGltfFormatOut) {
    *i3dmGltfFormatOut = gltfFormat;
  }
  size_t pos = hdr;
  const auto step = [&](uint32_t chunkLen) {
    pos += chunkLen;
    pos = Pad8(pos);
  };
  step(ftJ);
  step(ftB);
  step(btJ);
  step(btB);
  if (pos > len) {
    if (err) {
      *err = L"b3dm/i3dm 体部长度异常";
    }
    return false;
  }
  *payloadOff = pos;
  return true;
}

static void Mat4Mul(const double* a, const double* b, double* o) {
  for (int c = 0; c < 4; ++c) {
    for (int r = 0; r < 4; ++r) {
      o[c * 4 + r] = a[0 * 4 + r] * b[c * 4 + 0] + a[1 * 4 + r] * b[c * 4 + 1] + a[2 * 4 + r] * b[c * 4 + 2] +
                     a[3 * 4 + r] * b[c * 4 + 3];
    }
  }
}

static void NodeLocalMatrix(const tinygltf::Node& n, double* m) {
  if (n.matrix.size() == 16) {
    for (int i = 0; i < 16; ++i) {
      m[i] = n.matrix[static_cast<size_t>(i)];
    }
    return;
  }
  double tx = 0, ty = 0, tz = 0;
  if (n.translation.size() == 3) {
    tx = n.translation[0];
    ty = n.translation[1];
    tz = n.translation[2];
  }
  double qx = 0, qy = 0, qz = 0, qw = 1;
  if (n.rotation.size() == 4) {
    qx = n.rotation[0];
    qy = n.rotation[1];
    qz = n.rotation[2];
    qw = n.rotation[3];
  }
  double sx = 1, sy = 1, sz = 1;
  if (n.scale.size() == 3) {
    sx = n.scale[0];
    sy = n.scale[1];
    sz = n.scale[2];
  }
  const double x2 = qx + qx, y2 = qy + qy, z2 = qz + qz;
  const double xx = qx * x2, xy = qx * y2, xz = qx * z2;
  const double yy = qy * y2, yz = qy * z2, zz = qz * z2;
  const double wx = qw * x2, wy = qw * y2, wz = qw * z2;
  const double r00 = (1.0 - (yy + zz)) * sx;
  const double r01 = (xy - wz) * sy;
  const double r02 = (xz + wy) * sz;
  const double r10 = (xy + wz) * sx;
  const double r11 = (1.0 - (xx + zz)) * sy;
  const double r12 = (yz - wx) * sz;
  const double r20 = (xz - wy) * sx;
  const double r21 = (yz + wx) * sy;
  const double r22 = (1.0 - (xx + yy)) * sz;
  m[0] = r00;
  m[1] = r10;
  m[2] = r20;
  m[3] = 0.0;
  m[4] = r01;
  m[5] = r11;
  m[6] = r21;
  m[7] = 0.0;
  m[8] = r02;
  m[9] = r12;
  m[10] = r22;
  m[11] = 0.0;
  m[12] = tx;
  m[13] = ty;
  m[14] = tz;
  m[15] = 1.0;
}

static void MapGltfYupToAgis(float x, float y, float z, PreviewVec3* o) {
  o->x = x;
  o->y = z;
  o->z = y;
}

static void Mat4TransformPoint(const double* m, double x, double y, double z, double* ox, double* oy, double* oz) {
  *ox = m[0] * x + m[4] * y + m[8] * z + m[12];
  *oy = m[1] * x + m[5] * y + m[9] * z + m[13];
  *oz = m[2] * x + m[6] * y + m[10] * z + m[14];
}

static void FinalizeBounds(ObjPreviewModel* m) {
  if (!m || m->vertices.empty()) {
    return;
  }
  float vminx = 1e30f, vminy = 1e30f, vminz = 1e30f;
  float vmaxx = -1e30f, vmaxy = -1e30f, vmaxz = -1e30f;
  for (const auto& v : m->vertices) {
    vminx = (std::min)(vminx, v.x);
    vminy = (std::min)(vminy, v.y);
    vminz = (std::min)(vminz, v.z);
    vmaxx = (std::max)(vmaxx, v.x);
    vmaxy = (std::max)(vmaxy, v.y);
    vmaxz = (std::max)(vmaxz, v.z);
  }
  m->center = {(vminx + vmaxx) * 0.5f, (vminy + vmaxy) * 0.5f, (vminz + vmaxz) * 0.5f};
  const float ex = (std::max)(1e-6f, vmaxx - vminx);
  const float ey = (std::max)(1e-6f, vmaxy - vminy);
  const float ez = (std::max)(1e-6f, vmaxz - vminz);
  m->extentHoriz = (std::max)((std::max)(ex, ey), 1e-6f);
  m->extent = (std::max)(m->extentHoriz, ez);
}

static bool AccessorToFloat3(const tinygltf::Model& model, int accessorIndex, std::vector<std::array<double, 3>>* out) {
  out->clear();
  if (accessorIndex < 0 || static_cast<size_t>(accessorIndex) >= model.accessors.size()) {
    return false;
  }
  const tinygltf::Accessor& accessor = model.accessors[accessorIndex];
  if (accessor.type != TINYGLTF_TYPE_VEC3 || accessor.componentType != TINYGLTF_COMPONENT_TYPE_FLOAT) {
    return false;
  }
  if (accessor.bufferView < 0) {
    return false;
  }
  const tinygltf::BufferView& view = model.bufferViews[accessor.bufferView];
  if (view.buffer < 0) {
    return false;
  }
  const tinygltf::Buffer& buf = model.buffers[view.buffer];
  const size_t stride = accessor.ByteStride(view);
  const size_t elStride = stride < sizeof(float) * 3 ? sizeof(float) * 3 : stride;
  const size_t base = view.byteOffset + accessor.byteOffset;
  if (base + elStride * accessor.count > buf.data.size()) {
    return false;
  }
  out->resize(accessor.count);
  for (size_t i = 0; i < accessor.count; ++i) {
    const uint8_t* p = &buf.data[base + i * elStride];
    float v[3];
    std::memcpy(v, p, sizeof(float) * 3);
    (*out)[i] = {static_cast<double>(v[0]), static_cast<double>(v[1]), static_cast<double>(v[2])};
  }
  return true;
}

static bool AccessorToFloat2(const tinygltf::Model& model, int accessorIndex, std::vector<std::array<double, 2>>* out) {
  out->clear();
  if (accessorIndex < 0 || static_cast<size_t>(accessorIndex) >= model.accessors.size()) {
    return false;
  }
  const tinygltf::Accessor& accessor = model.accessors[accessorIndex];
  if (accessor.type != TINYGLTF_TYPE_VEC2 || accessor.componentType != TINYGLTF_COMPONENT_TYPE_FLOAT) {
    return false;
  }
  if (accessor.bufferView < 0) {
    return false;
  }
  const tinygltf::BufferView& view = model.bufferViews[accessor.bufferView];
  if (view.buffer < 0) {
    return false;
  }
  const tinygltf::Buffer& buf = model.buffers[view.buffer];
  const size_t stride = accessor.ByteStride(view);
  const size_t elStride = stride < sizeof(float) * 2 ? sizeof(float) * 2 : stride;
  const size_t base = view.byteOffset + accessor.byteOffset;
  if (base + elStride * accessor.count > buf.data.size()) {
    return false;
  }
  out->resize(accessor.count);
  for (size_t i = 0; i < accessor.count; ++i) {
    const uint8_t* p = &buf.data[base + i * elStride];
    float v[2];
    std::memcpy(v, p, sizeof(float) * 2);
    (*out)[i] = {static_cast<double>(v[0]), static_cast<double>(v[1])};
  }
  return true;
}

static bool ReadTriangleIndices(const tinygltf::Model& model, const tinygltf::Primitive& prim, std::vector<uint32_t>* idx) {
  idx->clear();
  if (prim.indices < 0) {
    return false;
  }
  const tinygltf::Accessor& accessor = model.accessors[prim.indices];
  if (accessor.bufferView < 0) {
    return false;
  }
  const tinygltf::BufferView& view = model.bufferViews[accessor.bufferView];
  const tinygltf::Buffer& buf = model.buffers[view.buffer];
  const size_t base = view.byteOffset + accessor.byteOffset;
  idx->resize(accessor.count);
  if (accessor.componentType == TINYGLTF_COMPONENT_TYPE_UNSIGNED_INT) {
    if (base + accessor.count * 4 > buf.data.size()) {
      return false;
    }
    for (size_t i = 0; i < accessor.count; ++i) {
      uint32_t v = 0;
      std::memcpy(&v, &buf.data[base + i * 4], 4);
      (*idx)[i] = v;
    }
    return true;
  }
  if (accessor.componentType == TINYGLTF_COMPONENT_TYPE_UNSIGNED_SHORT) {
    if (base + accessor.count * 2 > buf.data.size()) {
      return false;
    }
    for (size_t i = 0; i < accessor.count; ++i) {
      uint16_t v = 0;
      std::memcpy(&v, &buf.data[base + i * 2], 2);
      (*idx)[i] = v;
    }
    return true;
  }
  return false;
}

static std::wstring ResolveTexturePath(const tinygltf::Model& model, int primMaterialIndex,
                                       const std::wstring& gltfBaseDirW) {
  if (primMaterialIndex < 0 || static_cast<size_t>(primMaterialIndex) >= model.materials.size()) {
    return {};
  }
  const tinygltf::Material& mat = model.materials[primMaterialIndex];
  const int ti = mat.pbrMetallicRoughness.baseColorTexture.index;
  if (ti < 0 || static_cast<size_t>(ti) >= model.textures.size()) {
    return {};
  }
  const int si = model.textures[ti].source;
  if (si < 0 || static_cast<size_t>(si) >= model.images.size()) {
    return {};
  }
  const std::string& uri = model.images[si].uri;
  if (uri.empty() || (uri.size() >= 5 && uri.compare(0, 5, "data:") == 0)) {
    return {};
  }
  const std::wstring rel = Utf8ToWide(DecodeUriPercent(uri));
  if (rel.empty()) {
    return {};
  }
  const std::filesystem::path p = std::filesystem::path(gltfBaseDirW) / rel;
  std::error_code ec;
  if (std::filesystem::is_regular_file(p, ec)) {
    return p.lexically_normal().wstring();
  }
  return {};
}

static bool AppendPrimitive(const tinygltf::Model& model, const tinygltf::Primitive& prim, const double* world,
                            ObjPreviewModel* out, std::wstring* firstTex, const std::wstring& gltfBaseDirW,
                            size_t* facesAdded) {
  *facesAdded = 0;
#ifndef TINYGLTF_ENABLE_DRACO
  if (prim.extensions.find("KHR_draco_mesh_compression") != prim.extensions.end()) {
    return false;
  }
#endif
  const int mode = prim.mode == -1 ? TINYGLTF_MODE_TRIANGLES : prim.mode;
  if (mode != TINYGLTF_MODE_TRIANGLES) {
    return false;
  }
  auto it = prim.attributes.find("POSITION");
  if (it == prim.attributes.end()) {
    return false;
  }
  std::vector<std::array<double, 3>> pos;
  if (!AccessorToFloat3(model, it->second, &pos) || pos.empty()) {
    return false;
  }
  int uvAcc = -1;
  auto itu = prim.attributes.find("TEXCOORD_0");
  if (itu != prim.attributes.end()) {
    uvAcc = itu->second;
  }
  std::vector<std::array<double, 2>> uvs;
  const bool haveUv = uvAcc >= 0 && AccessorToFloat2(model, uvAcc, &uvs) && uvs.size() == pos.size();
  const int matIdx = prim.material;
  std::vector<uint32_t> indices;
  const bool indexed = prim.indices >= 0 && ReadTriangleIndices(model, prim, &indices);
  const size_t triCount = indexed ? (indices.size() / 3) : (pos.size() / 3);
  if (triCount == 0) {
    return false;
  }
  const size_t v0 = out->vertices.size();
  out->vertices.resize(v0 + pos.size());
  for (size_t i = 0; i < pos.size(); ++i) {
    double wx = 0, wy = 0, wz = 0;
    Mat4TransformPoint(world, pos[i][0], pos[i][1], pos[i][2], &wx, &wy, &wz);
    PreviewVec3 ag{};
    MapGltfYupToAgis(static_cast<float>(wx), static_cast<float>(wy), static_cast<float>(wz), &ag);
    out->vertices[v0 + i] = ag;
  }

  size_t t0 = 0;
  if (haveUv) {
    t0 = out->texcoords.size();
    out->texcoords.resize(t0 + uvs.size());
    for (size_t i = 0; i < uvs.size(); ++i) {
      out->texcoords[t0 + i].u = static_cast<float>(uvs[i][0]);
      out->texcoords[t0 + i].v = static_cast<float>(1.0 - uvs[i][1]);
    }
    out->hasVertexTexcoords = true;
  }

  for (size_t t = 0; t < triCount; ++t) {
    int i0 = 0, i1 = 0, i2 = 0;
    if (indexed) {
      i0 = static_cast<int>(indices[t * 3]);
      i1 = static_cast<int>(indices[t * 3 + 1]);
      i2 = static_cast<int>(indices[t * 3 + 2]);
    } else {
      i0 = static_cast<int>(t * 3);
      i1 = static_cast<int>(t * 3 + 1);
      i2 = static_cast<int>(t * 3 + 2);
    }
    if (i0 < 0 || i1 < 0 || i2 < 0 || static_cast<size_t>(i0) >= pos.size() || static_cast<size_t>(i1) >= pos.size() ||
        static_cast<size_t>(i2) >= pos.size()) {
      continue;
    }
    out->faces.push_back({static_cast<int>(v0 + i0), static_cast<int>(v0 + i1), static_cast<int>(v0 + i2)});
    if (haveUv) {
      out->faceTexcoord.push_back({static_cast<int>(t0 + i0), static_cast<int>(t0 + i1), static_cast<int>(t0 + i2)});
    } else {
      out->faceTexcoord.push_back({-1, -1, -1});
    }
    out->faceMaterial.push_back(0);
    ++(*facesAdded);
    if (out->faces.size() >= kMax3DTilesFaces) {
      return true;
    }
  }
  (void)matIdx;
  return *facesAdded > 0;
}

static void VisitNodeRecursive(const tinygltf::Model& gltf, int nodeIndex, const double* parentWorld,
                               ObjPreviewModel* out, std::wstring* firstTex, const std::wstring& gltfBaseDirW,
                               bool* anyGeom) {
  if (nodeIndex < 0 || static_cast<size_t>(nodeIndex) >= gltf.nodes.size()) {
    return;
  }
  double local[16];
  NodeLocalMatrix(gltf.nodes[static_cast<size_t>(nodeIndex)], local);
  double world[16];
  Mat4Mul(parentWorld, local, world);
  const tinygltf::Node& node = gltf.nodes[static_cast<size_t>(nodeIndex)];
  if (node.mesh >= 0 && static_cast<size_t>(node.mesh) < gltf.meshes.size()) {
    const tinygltf::Mesh& mesh = gltf.meshes[static_cast<size_t>(node.mesh)];
    for (const auto& prim : mesh.primitives) {
      size_t add = 0;
      if (AppendPrimitive(gltf, prim, world, out, firstTex, gltfBaseDirW, &add) && add > 0) {
        *anyGeom = true;
        if (firstTex && firstTex->empty()) {
          *firstTex = ResolveTexturePath(gltf, prim.material, gltfBaseDirW);
        }
      }
      if (out->faces.size() >= kMax3DTilesFaces) {
        return;
      }
    }
  }
  for (int ch : node.children) {
    VisitNodeRecursive(gltf, ch, world, out, firstTex, gltfBaseDirW, anyGeom);
    if (out->faces.size() >= kMax3DTilesFaces) {
      return;
    }
  }
}

static bool AppendGltfModelScene(const tinygltf::Model& gltff, ObjPreviewModel* out, std::wstring* firstTex,
                                 const std::wstring& gltfBaseDirW) {
  double identity[16] = {
      1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1,
  };
  bool any = false;
  int sceneIdx = gltff.defaultScene;
  if (sceneIdx < 0 && !gltff.scenes.empty()) {
    sceneIdx = 0;
  }
  if (sceneIdx < 0 || static_cast<size_t>(sceneIdx) >= gltff.scenes.size()) {
    return false;
  }
  for (int rootNode : gltff.scenes[static_cast<size_t>(sceneIdx)].nodes) {
    VisitNodeRecursive(gltff, rootNode, identity, out, firstTex, gltfBaseDirW, &any);
    if (out->faces.size() >= kMax3DTilesFaces) {
      break;
    }
  }
  return any;
}

static bool LoadGltfFromGlbBytes(const std::vector<uint8_t>& bytes, ObjPreviewModel* out, std::wstring* firstTex,
                                 const std::wstring& baseDirW, std::wstring* err) {
  tinygltf::TinyGLTF loader;
  tinygltf::Model gltf;
  std::string e, w;
  const std::string baseUtf8 = WideToUtf8(baseDirW);
  if (!loader.LoadBinaryFromMemory(&gltf, &e, &w, bytes.data(), static_cast<unsigned int>(bytes.size()), baseUtf8)) {
    if (err) {
      *err = Utf8ToWide(e.empty() ? "glb 解析失败" : e);
    }
    return false;
  }
  std::wstring tex;
  if (!AppendGltfModelScene(gltf, out, &tex, baseDirW)) {
    if (err) {
      *err = L"glb 内无三角网格（或均为 Draco/非 triangles）";
    }
    return false;
  }
  if (firstTex && firstTex->empty()) {
    *firstTex = tex;
  }
  return true;
}

static bool LoadGltfFromJsonBytes(const std::vector<uint8_t>& utf8Json, ObjPreviewModel* out, std::wstring* firstTex,
                                  const std::wstring& baseDirW, std::wstring* err) {
  tinygltf::TinyGLTF loader;
  tinygltf::Model gltf;
  std::string e, w;
  const std::string baseUtf8 = WideToUtf8(baseDirW);
  const std::string jsonStr(reinterpret_cast<const char*>(utf8Json.data()), utf8Json.size());
  if (!loader.LoadASCIIFromString(&gltf, &e, &w, jsonStr.data(), static_cast<unsigned int>(jsonStr.size()), baseUtf8,
                                  tinygltf::REQUIRE_VERSION)) {
    if (err) {
      *err = Utf8ToWide(e.empty() ? "gltf JSON 解析失败" : e);
    }
    return false;
  }
  std::wstring tex;
  if (!AppendGltfModelScene(gltf, out, &tex, baseDirW)) {
    if (err) {
      *err = L"gltf 内无可用三角网格";
    }
    return false;
  }
  if (firstTex && firstTex->empty()) {
    *firstTex = tex;
  }
  return true;
}

static bool ReadFtJsonDouble3(const nlohmann::json& j, const char* key, double o[3]) {
  if (!j.contains(key) || !j[key].is_array() || j[key].size() < 3) {
    return false;
  }
  try {
    o[0] = j[key][0].get<double>();
    o[1] = j[key][1].get<double>();
    o[2] = j[key][2].get<double>();
    return true;
  } catch (...) {
    return false;
  }
}

/// 3D Tiles 1.0 `pnts`：Feature Table 中 FLOAT `POSITION` 或 `POSITION_QUANTIZED`；点展开为小四边形（与 LAS 预览类似）。
static bool ProcessPntsTile(const uint8_t* d, size_t len, ObjPreviewModel* out, std::wstring* err) {
  constexpr size_t kHdr = 28;
  if (len < kHdr || std::memcmp(d, "pnts", 4) != 0) {
    return false;
  }
  if (Le32(d + 4) != 1u) {
    if (err) {
      *err = L"pnts 版本非 1";
    }
    return false;
  }
  const uint32_t byteLength = Le32(d + 8);
  if (byteLength < kHdr || byteLength > len) {
    if (err) {
      *err = L"pnts byteLength 无效";
    }
    return false;
  }
  const uint32_t ftJ = Le32(d + 12);
  const uint32_t ftB = Le32(d + 16);
  const uint32_t btJ = Le32(d + 20);
  const uint32_t btB = Le32(d + 24);
  size_t cur = kHdr;
  if (cur + ftJ > byteLength) {
    if (err) {
      *err = L"pnts FeatureTable JSON 长度越界";
    }
    return false;
  }
  const std::string ftJsonStr(reinterpret_cast<const char*>(d + cur), static_cast<size_t>(ftJ));
  cur += ftJ;
  cur = Pad8(cur);
  if (cur + ftB > byteLength) {
    if (err) {
      *err = L"pnts FeatureTable 二进制越界";
    }
    return false;
  }
  const uint8_t* ftBin = d + cur;
  cur += ftB;
  cur = Pad8(cur);
  if (cur + btJ > byteLength) {
    if (err) {
      *err = L"pnts BatchTable JSON 越界";
    }
    return false;
  }
  cur += btJ;
  cur = Pad8(cur);
  if (cur + btB > byteLength) {
    if (err) {
      *err = L"pnts BatchTable 二进制越界";
    }
    return false;
  }
  cur += btB;
  cur = Pad8(cur);
  if (cur > byteLength) {
    if (err) {
      *err = L"pnts 头与表长度不一致";
    }
    return false;
  }

  nlohmann::json fj;
  try {
    fj = nlohmann::json::parse(ftJsonStr);
  } catch (...) {
    if (err) {
      *err = L"pnts Feature Table JSON 解析失败";
    }
    return false;
  }
  if (!fj.contains("POINTS") || !fj["POINTS"].is_number()) {
    if (err) {
      *err = L"pnts 缺少 POINTS";
    }
    return false;
  }
  uint64_t pointsTotal64 = 0;
  try {
    pointsTotal64 = static_cast<uint64_t>(fj["POINTS"].get<double>());
  } catch (...) {
    if (err) {
      *err = L"pnts POINTS 无效";
    }
    return false;
  }
  if (pointsTotal64 == 0 || pointsTotal64 > 0xFFFFFFFFull) {
    return false;
  }
  const uint32_t pointsTotal = static_cast<uint32_t>(pointsTotal64);

  double rtc[3] = {0, 0, 0};
  (void)ReadFtJsonDouble3(fj, "RTC_CENTER", rtc);

  const bool haveQuant = fj.contains("POSITION_QUANTIZED") && fj["POSITION_QUANTIZED"].is_object();
  const bool haveFloatPos = fj.contains("POSITION") && fj["POSITION"].is_object();
  if (!haveQuant && !haveFloatPos) {
    if (err) {
      *err = L"pnts 无 POSITION / POSITION_QUANTIZED";
    }
    return false;
  }

  double qOff[3] = {0, 0, 0};
  double qScale[3] = {1, 1, 1};
  if (haveQuant) {
    if (!ReadFtJsonDouble3(fj, "QUANTIZED_VOLUME_OFFSET", qOff) || !ReadFtJsonDouble3(fj, "QUANTIZED_VOLUME_SCALE", qScale)) {
      if (err) {
        *err = L"pnts 量化体积字段缺失";
      }
      return false;
    }
  }

  size_t rgbStrideBytes = 0;
  size_t rgbOff = 0;
  try {
    if (fj.contains("RGB") && fj["RGB"].is_object() && fj["RGB"].contains("byteOffset")) {
      rgbOff = static_cast<size_t>(fj["RGB"]["byteOffset"].get<double>());
      rgbStrideBytes = 3;
    } else if (fj.contains("RGBA") && fj["RGBA"].is_object() && fj["RGBA"].contains("byteOffset")) {
      rgbOff = static_cast<size_t>(fj["RGBA"]["byteOffset"].get<double>());
      rgbStrideBytes = 4;
    }
  } catch (...) {
    rgbStrideBytes = 0;
    rgbOff = 0;
  }

  const size_t budgetFaces = kMax3DTilesFaces - out->faces.size();
  const size_t budgetPts = budgetFaces / 2;
  if (budgetPts == 0) {
    return false;
  }
  uint32_t stride = 1;
  if (pointsTotal > budgetPts) {
    stride = (pointsTotal + static_cast<uint32_t>(budgetPts) - 1u) / static_cast<uint32_t>(budgetPts);
  }

  size_t posOff = 0;
  if (haveFloatPos) {
    try {
      posOff = static_cast<size_t>(fj["POSITION"]["byteOffset"].get<double>());
    } catch (...) {
      if (err) {
        *err = L"pnts POSITION.byteOffset 无效";
      }
      return false;
    }
    if (posOff + static_cast<size_t>(pointsTotal) * 12u > ftB) {
      if (err) {
        *err = L"pnts POSITION 越界";
      }
      return false;
    }
    if (rgbStrideBytes && rgbOff + static_cast<size_t>(pointsTotal) * rgbStrideBytes > ftB) {
      if (err) {
        *err = L"pnts RGB/A 越界";
      }
      return false;
    }
  } else {
    try {
      posOff = static_cast<size_t>(fj["POSITION_QUANTIZED"]["byteOffset"].get<double>());
    } catch (...) {
      if (err) {
        *err = L"pnts POSITION_QUANTIZED.byteOffset 无效";
      }
      return false;
    }
    if (posOff + static_cast<size_t>(pointsTotal) * 6u > ftB) {
      if (err) {
        *err = L"pnts POSITION_QUANTIZED 越界";
      }
      return false;
    }
    if (rgbStrideBytes && rgbOff + static_cast<size_t>(pointsTotal) * rgbStrideBytes > ftB) {
      if (err) {
        *err = L"pnts RGB/A 越界";
      }
      return false;
    }
  }

  std::vector<PreviewVec3> agisPts;
  agisPts.reserve((std::min)(static_cast<size_t>(pointsTotal / stride + 1u), budgetPts));

  constexpr double kInvQ = 1.0 / 65535.0;
  if (haveFloatPos) {
    for (uint32_t i = 0; i < pointsTotal; i += stride) {
      const uint8_t* p = ftBin + posOff + static_cast<size_t>(i) * 12u;
      float fx = 0, fy = 0, fz = 0;
      std::memcpy(&fx, p, 4);
      std::memcpy(&fy, p + 4, 4);
      std::memcpy(&fz, p + 8, 4);
      const double wx = static_cast<double>(fx) + rtc[0];
      const double wy = static_cast<double>(fy) + rtc[1];
      const double wz = static_cast<double>(fz) + rtc[2];
      PreviewVec3 ag{};
      MapGltfYupToAgis(static_cast<float>(wx), static_cast<float>(wy), static_cast<float>(wz), &ag);
      agisPts.push_back(ag);
    }
  } else {
    for (uint32_t i = 0; i < pointsTotal; i += stride) {
      const uint8_t* p = ftBin + posOff + static_cast<size_t>(i) * 6u;
      uint16_t qv[3];
      std::memcpy(qv, p, 6);
      const double wx = qOff[0] + static_cast<double>(qv[0]) * kInvQ * qScale[0] + rtc[0];
      const double wy = qOff[1] + static_cast<double>(qv[1]) * kInvQ * qScale[1] + rtc[1];
      const double wz = qOff[2] + static_cast<double>(qv[2]) * kInvQ * qScale[2] + rtc[2];
      PreviewVec3 ag{};
      MapGltfYupToAgis(static_cast<float>(wx), static_cast<float>(wy), static_cast<float>(wz), &ag);
      agisPts.push_back(ag);
    }
  }

  if (agisPts.empty()) {
    return false;
  }

  if (rgbStrideBytes && rgbOff + rgbStrideBytes <= ftB) {
    const uint8_t* c0 = ftBin + rgbOff;
    out->kdR = static_cast<float>(c0[0]) / 255.0f;
    out->kdG = static_cast<float>(c0[1]) / 255.0f;
    out->kdB = static_cast<float>(c0[2]) / 255.0f;
  }

  float vminx = 1e30f, vminy = 1e30f, vminz = 1e30f;
  float vmaxx = -1e30f, vmaxy = -1e30f, vmaxz = -1e30f;
  for (const auto& p : agisPts) {
    vminx = (std::min)(vminx, p.x);
    vminy = (std::min)(vminy, p.y);
    vminz = (std::min)(vminz, p.z);
    vmaxx = (std::max)(vmaxx, p.x);
    vmaxy = (std::max)(vmaxy, p.y);
    vmaxz = (std::max)(vmaxz, p.z);
  }
  const float ex = (std::max)(1e-6f, vmaxx - vminx);
  const float ey = (std::max)(1e-6f, vmaxy - vminy);
  const float ez = (std::max)(1e-6f, vmaxz - vminz);
  const float half = (std::max)(1e-5f, (std::max)(ex, (std::max)(ey, ez)) * 0.0015f);

  for (const auto& c : agisPts) {
    const int b0 = static_cast<int>(out->vertices.size());
    out->vertices.push_back({c.x - half, c.y - half, c.z});
    out->vertices.push_back({c.x + half, c.y - half, c.z});
    out->vertices.push_back({c.x + half, c.y + half, c.z});
    out->vertices.push_back({c.x - half, c.y + half, c.z});
    out->faces.push_back({b0, b0 + 1, b0 + 2});
    out->faces.push_back({b0, b0 + 2, b0 + 3});
    out->faceTexcoord.push_back({-1, -1, -1});
    out->faceTexcoord.push_back({-1, -1, -1});
    out->faceMaterial.push_back(0);
    out->faceMaterial.push_back(0);
    if (out->faces.size() >= kMax3DTilesFaces) {
      break;
    }
  }
  return true;
}

static bool ProcessTileBlob(const uint8_t* d, size_t len, ObjPreviewModel* out, std::wstring* firstTex,
                            const std::wstring& tileSiblingDirW, std::wstring* err);

static bool ProcessCmpt(const uint8_t* d, size_t len, ObjPreviewModel* out, std::wstring* firstTex,
                        const std::wstring& tileSiblingDirW, std::wstring* err) {
  if (len < 12 || std::memcmp(d, "cmpt", 4) != 0) {
    return false;
  }
  const uint32_t total = Le32(d + 8);
  if (total < 12 || total > len) {
    if (err) {
      *err = L"cmpt byteLength 无效";
    }
    return false;
  }
  size_t off = 12;
  bool okInner = false;
  while (off + 12 <= total) {
    const uint32_t innerLen = Le32(d + off + 8);
    if (innerLen < 12 || off + innerLen > total) {
      break;
    }
    std::wstring subErr;
    if (ProcessTileBlob(d + off, innerLen, out, firstTex, tileSiblingDirW, &subErr)) {
      okInner = true;
    }
    off += innerLen;
    if (out->faces.size() >= kMax3DTilesFaces) {
      break;
    }
  }
  return okInner;
}

static bool ProcessTileBlob(const uint8_t* d, size_t len, ObjPreviewModel* out, std::wstring* firstTex,
                            const std::wstring& tileSiblingDirW, std::wstring* err) {
  if (len < 12) {
    return false;
  }
  if (std::memcmp(d, "cmpt", 4) == 0) {
    return ProcessCmpt(d, len, out, firstTex, tileSiblingDirW, err);
  }
  if (std::memcmp(d, "glTF", 4) == 0) {
    std::vector<uint8_t> copy(d, d + len);
    return LoadGltfFromGlbBytes(copy, out, firstTex, tileSiblingDirW, err);
  }
  if (std::memcmp(d, "b3dm", 4) == 0) {
    size_t pay = 0;
    uint32_t fmt = 0;
    if (!PayloadOffsetB3dmI3dm(d, len, false, &pay, &fmt, err)) {
      return false;
    }
    if (pay >= len) {
      return false;
    }
    return ProcessTileBlob(d + pay, len - pay, out, firstTex, tileSiblingDirW, err);
  }
  if (std::memcmp(d, "i3dm", 4) == 0) {
    size_t pay = 0;
    uint32_t gltfFmt = 1;
    if (!PayloadOffsetB3dmI3dm(d, len, true, &pay, &gltfFmt, err)) {
      return false;
    }
    if (pay >= len) {
      return false;
    }
    const uint8_t* payload = d + pay;
    const size_t payloadLen = len - pay;
    if (gltfFmt == 1u) {
      return ProcessTileBlob(payload, payloadLen, out, firstTex, tileSiblingDirW, err);
    }
    std::vector<uint8_t> jsonVec(payload, payload + payloadLen);
    return LoadGltfFromJsonBytes(jsonVec, out, firstTex, tileSiblingDirW, err);
  }
  if (std::memcmp(d, "pnts", 4) == 0) {
    std::wstring perr;
    if (!ProcessPntsTile(d, len, out, &perr)) {
      if (err && !perr.empty()) {
        *err = std::move(perr);
      }
      return false;
    }
    return true;
  }
  return false;
}

static void CollectUrisFromTileJson(const nlohmann::json& tile, int depthLeft, std::vector<std::string>* out) {
  if (!tile.is_object() || depthLeft <= 0) {
    return;
  }
  if (tile.contains("content") && tile["content"].is_object()) {
    const auto& c = tile["content"];
    if (c.contains("uri") && c["uri"].is_string()) {
      const std::string u = c["uri"].get<std::string>();
      if (!u.empty() && u.rfind("http://", 0) != 0 && u.rfind("https://", 0) != 0) {
        out->push_back(u);
      }
    }
  }
  if (tile.contains("children") && tile["children"].is_array()) {
    for (const auto& ch : tile["children"]) {
      CollectUrisFromTileJson(ch, depthLeft - 1, out);
    }
  }
}

static bool ResolveTilesetJsonPath(const std::wstring& rootW, std::wstring* tilesetPathW) {
  std::error_code ec;
  const std::filesystem::path p(rootW);
  if (std::filesystem::is_regular_file(p, ec)) {
    if (_wcsicmp(p.filename().c_str(), L"tileset.json") == 0) {
      *tilesetPathW = p.wstring();
      return true;
    }
    return false;
  }
  if (!std::filesystem::is_directory(p, ec)) {
    return false;
  }
  const auto tj = p / L"tileset.json";
  if (std::filesystem::is_regular_file(tj, ec)) {
    *tilesetPathW = tj.wstring();
    return true;
  }
  return false;
}

static void ExpandNestedTilesetUris(const std::wstring& tilesetFileW, int nestDepth, std::vector<std::string>* uris,
                                    std::wstring* errNote) {
  if (nestDepth <= 0) {
    return;
  }
  std::string raw;
  {
    std::ifstream f(std::filesystem::path(tilesetFileW), std::ios::binary | std::ios::ate);
    if (!f) {
      return;
    }
    const auto end = f.tellg();
    if (end <= 0) {
      return;
    }
    raw.resize(static_cast<size_t>(end));
    f.seekg(0);
    f.read(raw.data(), static_cast<std::streamsize>(raw.size()));
  }
  try {
    nlohmann::json j = nlohmann::json::parse(raw);
    if (!j.contains("root")) {
      return;
    }
    CollectUrisFromTileJson(j["root"], 128, uris);
  } catch (...) {
    (void)errNote;
  }
}

}  // namespace

bool AgisLoad3DTilesForPreview(const std::wstring& tilesetRootOrFile, ObjPreviewModel* outModel, std::wstring* errOut,
                               int* progressPct) {
  if (errOut) {
    errOut->clear();
  }
  if (!outModel) {
    return false;
  }
  outModel->vertices.clear();
  outModel->texcoords.clear();
  outModel->faces.clear();
  outModel->faceTexcoord.clear();
  outModel->faceMaterial.clear();
  outModel->materials.clear();
  outModel->hasVertexTexcoords = false;
  outModel->center = {};
  outModel->extentHoriz = 1.0f;
  outModel->extent = 1.0f;
  outModel->kdR = 0.30f;
  outModel->kdG = 0.62f;
  outModel->kdB = 0.92f;
  outModel->primaryMapKdPath.clear();
  if (progressPct) {
    *progressPct = 0;
  }
  std::wstring tilesetPathW;
  if (!ResolveTilesetJsonPath(tilesetRootOrFile, &tilesetPathW)) {
    if (errOut) {
      *errOut = L"未找到 tileset.json（请选目录或 tileset.json 文件）";
    }
    return false;
  }
  const std::filesystem::path tsPath(tilesetPathW);
  const std::wstring baseDirW = tsPath.parent_path().wstring();
  std::string raw;
  {
    std::ifstream f(tsPath, std::ios::binary | std::ios::ate);
    if (!f) {
      if (errOut) {
        *errOut = L"无法读取 tileset.json";
      }
      return false;
    }
    const auto end = f.tellg();
    if (end <= 0) {
      if (errOut) {
        *errOut = L"tileset.json 为空";
      }
      return false;
    }
    raw.resize(static_cast<size_t>(end));
    f.seekg(0);
    f.read(raw.data(), static_cast<std::streamsize>(raw.size()));
  }

  nlohmann::json rootJ;
  try {
    rootJ = nlohmann::json::parse(raw);
  } catch (const std::exception& e) {
    if (errOut) {
      *errOut = L"tileset.json 解析失败: ";
      errOut->append(Utf8ToWide(e.what()));
    }
    return false;
  }
  std::vector<std::string> uris;
  if (rootJ.contains("root")) {
    CollectUrisFromTileJson(rootJ["root"], 128, &uris);
  }

  for (size_t ni = 0; ni < uris.size() && ni < 2000u; ++ni) {
    const std::string u0 = uris[ni];
    std::string low = u0;
    for (char& c : low) {
      c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    }
    if (low.size() >= 5 && low.compare(low.size() - 5, 5, ".json") == 0) {
      const std::wstring childTs = (std::filesystem::path(baseDirW) / Utf8ToWide(DecodeUriPercent(u0))).wstring();
      std::error_code ec;
      if (std::filesystem::is_regular_file(childTs, ec) && uris.size() < 2000) {
        ExpandNestedTilesetUris(childTs, kMaxTilesetNestingDepth - 1, &uris, errOut);
      }
    }
  }
  if (uris.size() > 800) {
    uris.resize(800);
  }

  auto lowerEqSuffix = [](const std::string& a, const char* suf) -> bool {
    const size_t n = std::strlen(suf);
    if (a.size() < n) {
      return false;
    }
    std::string tail = a.substr(a.size() - n);
    for (char& c : tail) {
      c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    }
    std::string sl = suf;
    for (char& c : sl) {
      c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    }
    return tail == sl;
  };

  std::wstring firstTexAgg;
  int processed = 0;
  for (const std::string& uriRaw : uris) {
    if (processed >= kMaxResolvedContentFiles || outModel->faces.size() >= kMax3DTilesFaces) {
      break;
    }
    const std::string uriDec = DecodeUriPercent(uriRaw);
    if (uriDec.rfind("http://", 0) == 0 || uriDec.rfind("https://", 0) == 0) {
      continue;
    }
    if (lowerEqSuffix(uriDec, ".json")) {
      continue;
    }
    const std::wstring fileW = (std::filesystem::path(baseDirW) / Utf8ToWide(uriDec)).lexically_normal().wstring();
    std::error_code ec;
    if (!std::filesystem::is_regular_file(fileW, ec)) {
      continue;
    }
    const uintmax_t fsz = std::filesystem::file_size(fileW, ec);
    if (ec || fsz > kMaxRawTileBytes) {
      continue;
    }
    std::vector<uint8_t> bytes;
    if (!ReadWholeFileBytesW(fileW, &bytes) || bytes.empty()) {
      continue;
    }
    const std::wstring sib = std::filesystem::path(fileW).parent_path().wstring();
    std::wstring oneErr;
    if (lowerEqSuffix(uriDec, ".glb")) {
      if (LoadGltfFromGlbBytes(bytes, outModel, &firstTexAgg, sib, &oneErr)) {
        ++processed;
      }
    } else {
      if (ProcessTileBlob(bytes.data(), bytes.size(), outModel, &firstTexAgg, sib, &oneErr)) {
        ++processed;
      }
    }
    if (progressPct) {
      *progressPct = (std::min)(99, (processed + 1) * 100 / (std::max)(8, static_cast<int>(uris.size())));
    }
  }

  if (outModel->faces.empty()) {
    if (errOut) {
      *errOut =
          L"未解析到可渲染网格。支持本地 b3dm/i3dm(glb)/glb/cmpt、pnts（FLOAT/量化 POSITION）；"
#ifndef TINYGLTF_ENABLE_DRACO
          L"裁剪包未启用 Draco；"
#endif
          L"仅 http(s) 外链仍跳过。";
    }
    return false;
  }

  ObjPreviewModel::MaterialInfo m0{};
  m0.name = L"3dtiles";
  if (!firstTexAgg.empty()) {
    m0.mapKdPath = firstTexAgg;
    outModel->primaryMapKdPath = firstTexAgg;
  }
  outModel->materials.push_back(m0);

  FinalizeBounds(outModel);
  if (progressPct) {
    *progressPct = 100;
  }
  return true;
}
