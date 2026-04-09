#include "app/model_preview_bgfx.h"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <vector>

#include <bgfx/bgfx.h>
#include <bgfx/embedded_shader.h>
#include <bx/allocator.h>
#include <bx/math.h>
#include <bimg/decode.h>

#include "fs_debugdraw_fill_texture.bin.h"
#include "vs_debugdraw_fill_texture.bin.h"

namespace {

bx::DefaultAllocator s_bimgAlloc;

struct CpuImage2D {
  uint32_t width = 0;
  uint32_t height = 0;
  uint32_t bpp = 0;
  bool bgra = false;
  std::vector<uint8_t> pixels;
};

struct CpuPbrMaps {
  CpuImage2D normal;
  CpuImage2D roughness;
  CpuImage2D metallic;
  CpuImage2D ao;
};

struct PosTexColorVertex {
  float x = 0.f;
  float y = 0.f;
  float z = 0.f;
  float u = 0.f;
  float v = 0.f;
  uint32_t abgr = 0xff000000;

  static bgfx::VertexLayout& Layout() {
    static bgfx::VertexLayout s_layout;
    static bool once = false;
    if (!once) {
      s_layout.begin()
          .add(bgfx::Attrib::Position, 3, bgfx::AttribType::Float)
          .add(bgfx::Attrib::TexCoord0, 2, bgfx::AttribType::Float)
          .add(bgfx::Attrib::Color0, 4, bgfx::AttribType::Uint8, true)
          .end();
      once = true;
    }
    return s_layout;
  }
};

static uint32_t PackAbgr8(float r, float g, float b, float a = 1.0f) {
  const auto u8 = [](float x) { return static_cast<uint32_t>(std::clamp(x, 0.0f, 1.0f) * 255.0f + 0.5f); };
  return (u8(a) << 24) | (u8(b) << 16) | (u8(g) << 8) | u8(r);
}

static const bgfx::EmbeddedShader kEmbeddedShaders[] = {
    BGFX_EMBEDDED_SHADER(vs_debugdraw_fill_texture),
    BGFX_EMBEDDED_SHADER(fs_debugdraw_fill_texture),
    BGFX_EMBEDDED_SHADER_END(),
};

struct AgisBgfxPreviewContextImpl {
  bgfx::ProgramHandle program = BGFX_INVALID_HANDLE;
  bgfx::VertexBufferHandle vbh = BGFX_INVALID_HANDLE;
  bgfx::IndexBufferHandle ibhTri = BGFX_INVALID_HANDLE;
  bgfx::IndexBufferHandle ibhLine = BGFX_INVALID_HANDLE;
  bgfx::TextureHandle texture = BGFX_INVALID_HANDLE;
  bgfx::TextureHandle texBaseColor = BGFX_INVALID_HANDLE;
  bgfx::TextureHandle texNormal = BGFX_INVALID_HANDLE;
  bgfx::TextureHandle texRoughness = BGFX_INVALID_HANDLE;
  bgfx::TextureHandle texMetallic = BGFX_INVALID_HANDLE;
  bgfx::TextureHandle texAo = BGFX_INVALID_HANDLE;
  bgfx::UniformHandle s_texColor = BGFX_INVALID_HANDLE;
  uint32_t triCount = 0;
  uint32_t lineCount = 0;
  uint32_t lastResetW = 0;
  uint32_t lastResetH = 0;
  bgfx::RendererType::Enum renderer = bgfx::RendererType::Count;
  uint32_t resetFlags = BGFX_RESET_VSYNC;
  AgisBgfxRuntimeStats runtime{};
  ObjPreviewModel cachedModel{};
  bool pseudoPbrEnabled = true;
  CpuPbrMaps cpuMaps{};
  AgisBgfxPbrViewMode pbrViewMode = AgisBgfxPbrViewMode::kPbrLit;
};

static bgfx::RendererType::Enum ToBgfxType(AgisBgfxRendererKind k) {
  return (k == AgisBgfxRendererKind::kOpenGL) ? bgfx::RendererType::OpenGL : bgfx::RendererType::Direct3D11;
}

static float TicksToMs(int64_t ticks, int64_t freq) {
  if (ticks <= 0 || freq <= 0) {
    return 0.0f;
  }
  return static_cast<float>((static_cast<double>(ticks) * 1000.0) / static_cast<double>(freq));
}

static void ImageReleaseCb(void* /*_ptr*/, void* _userData) {
  bimg::imageFree(reinterpret_cast<bimg::ImageContainer*>(_userData));
}

static bool ReadWholeFileBinary(const std::wstring& path, std::vector<uint8_t>* out) {
  out->clear();
  std::ifstream f(std::filesystem::path(path), std::ios::binary | std::ios::ate);
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

static bgfx::TextureHandle CreateWhiteTexture2D() {
  const uint32_t px = 0xffffffffu;
  const bgfx::Memory* mem = bgfx::copy(&px, sizeof(px));
  return bgfx::createTexture2D(1, 1, false, 1, bgfx::TextureFormat::RGBA8, 0, mem);
}

/// 使用 bimg 解码常见图片（PNG/JPEG 等）；失败返回无效句柄。
static bgfx::TextureHandle LoadMapKdTexture(const std::wstring& path) {
  if (path.empty()) {
    return BGFX_INVALID_HANDLE;
  }
  std::vector<uint8_t> bytes;
  if (!ReadWholeFileBinary(path, &bytes) || bytes.empty()) {
    return BGFX_INVALID_HANDLE;
  }
  bimg::ImageContainer* imageContainer =
      bimg::imageParse(&s_bimgAlloc, bytes.data(), static_cast<uint32_t>(bytes.size()));
  if (!imageContainer) {
    return BGFX_INVALID_HANDLE;
  }

  if (!bgfx::isTextureValid(0, false, imageContainer->m_numLayers, bgfx::TextureFormat::Enum(imageContainer->m_format), 0)) {
    bimg::imageFree(imageContainer);
    return BGFX_INVALID_HANDLE;
  }

  const bgfx::Memory* mem = bgfx::makeRef(imageContainer->m_data, imageContainer->m_size, ImageReleaseCb, imageContainer);
  return bgfx::createTexture2D(
      uint16_t(imageContainer->m_width), uint16_t(imageContainer->m_height), 1 < imageContainer->m_numMips,
      imageContainer->m_numLayers, bgfx::TextureFormat::Enum(imageContainer->m_format), 0, mem);
}

static bool LoadCpuImage2D(const std::wstring& path, CpuImage2D* out) {
  if (!out) {
    return false;
  }
  *out = CpuImage2D{};
  if (path.empty()) {
    return false;
  }
  std::vector<uint8_t> bytes;
  if (!ReadWholeFileBinary(path, &bytes) || bytes.empty()) {
    return false;
  }
  bimg::ImageContainer* imageContainer =
      bimg::imageParse(&s_bimgAlloc, bytes.data(), static_cast<uint32_t>(bytes.size()));
  if (!imageContainer) {
    return false;
  }
  const uint32_t w = imageContainer->m_width;
  const uint32_t h = imageContainer->m_height;
  const uint32_t fmt = imageContainer->m_format;
  uint32_t bpp = 0;
  bool bgra = false;
  if (fmt == static_cast<uint32_t>(bgfx::TextureFormat::RGBA8)) {
    bpp = 4;
  } else if (fmt == static_cast<uint32_t>(bgfx::TextureFormat::BGRA8)) {
    bpp = 4;
    bgra = true;
  } else if (fmt == static_cast<uint32_t>(bgfx::TextureFormat::RGB8)) {
    bpp = 3;
  } else if (fmt == static_cast<uint32_t>(bgfx::TextureFormat::R8)) {
    bpp = 1;
  } else {
    bimg::imageFree(imageContainer);
    return false;
  }
  const uint64_t need = static_cast<uint64_t>(w) * static_cast<uint64_t>(h) * static_cast<uint64_t>(bpp);
  if (need == 0 || need > imageContainer->m_size) {
    bimg::imageFree(imageContainer);
    return false;
  }
  out->width = w;
  out->height = h;
  out->bpp = bpp;
  out->bgra = bgra;
  out->pixels.resize(static_cast<size_t>(need));
  memcpy(out->pixels.data(), imageContainer->m_data, static_cast<size_t>(need));
  bimg::imageFree(imageContainer);
  return true;
}

static float SampleImageGray01(const CpuImage2D& img, float u, float v) {
  if (img.width == 0 || img.height == 0 || img.bpp == 0 || img.pixels.empty()) {
    return 1.0f;
  }
  const float uu = u - std::floor(u);
  const float vv = v - std::floor(v);
  const uint32_t x = (std::min)(img.width - 1, static_cast<uint32_t>(uu * static_cast<float>(img.width)));
  const uint32_t y = (std::min)(img.height - 1, static_cast<uint32_t>(vv * static_cast<float>(img.height)));
  const size_t idx = (static_cast<size_t>(y) * img.width + x) * img.bpp;
  if (idx + img.bpp > img.pixels.size()) {
    return 1.0f;
  }
  if (img.bpp == 1) {
    return static_cast<float>(img.pixels[idx]) / 255.0f;
  }
  const uint8_t c0 = img.pixels[idx + 0];
  const uint8_t c1 = img.pixels[idx + 1];
  const uint8_t c2 = img.pixels[idx + 2];
  uint8_t r = c0, g = c1, b = c2;
  if (img.bgra) {
    r = c2;
    g = c1;
    b = c0;
  }
  return (0.2126f * static_cast<float>(r) + 0.7152f * static_cast<float>(g) + 0.0722f * static_cast<float>(b)) / 255.0f;
}

static void SampleImageRgb01(const CpuImage2D& img, float u, float v, float* r, float* g, float* b) {
  if (!r || !g || !b) {
    return;
  }
  *r = 0.5f;
  *g = 0.5f;
  *b = 1.0f;
  if (img.width == 0 || img.height == 0 || img.bpp < 3 || img.pixels.empty()) {
    return;
  }
  const float uu = u - std::floor(u);
  const float vv = v - std::floor(v);
  const uint32_t x = (std::min)(img.width - 1, static_cast<uint32_t>(uu * static_cast<float>(img.width)));
  const uint32_t y = (std::min)(img.height - 1, static_cast<uint32_t>(vv * static_cast<float>(img.height)));
  const size_t idx = (static_cast<size_t>(y) * img.width + x) * img.bpp;
  if (idx + img.bpp > img.pixels.size()) {
    return;
  }
  uint8_t c0 = img.pixels[idx + 0];
  uint8_t c1 = img.pixels[idx + 1];
  uint8_t c2 = img.pixels[idx + 2];
  if (img.bgra) {
    std::swap(c0, c2);
  }
  *r = static_cast<float>(c0) / 255.0f;
  *g = static_cast<float>(c1) / 255.0f;
  *b = static_cast<float>(c2) / 255.0f;
}

static void DestroyMesh(AgisBgfxPreviewContextImpl* c) {
  if (bgfx::isValid(c->ibhTri)) {
    bgfx::destroy(c->ibhTri);
    c->ibhTri = BGFX_INVALID_HANDLE;
  }
  if (bgfx::isValid(c->ibhLine)) {
    bgfx::destroy(c->ibhLine);
    c->ibhLine = BGFX_INVALID_HANDLE;
  }
  if (bgfx::isValid(c->vbh)) {
    bgfx::destroy(c->vbh);
    c->vbh = BGFX_INVALID_HANDLE;
  }
  c->triCount = 0;
  c->lineCount = 0;
}

static void DestroyTextureResources(AgisBgfxPreviewContextImpl* c) {
  if (bgfx::isValid(c->texture)) {
    bgfx::destroy(c->texture);
    c->texture = BGFX_INVALID_HANDLE;
  }
  if (bgfx::isValid(c->texBaseColor)) {
    bgfx::destroy(c->texBaseColor);
    c->texBaseColor = BGFX_INVALID_HANDLE;
  }
  if (bgfx::isValid(c->texNormal)) {
    bgfx::destroy(c->texNormal);
    c->texNormal = BGFX_INVALID_HANDLE;
  }
  if (bgfx::isValid(c->texRoughness)) {
    bgfx::destroy(c->texRoughness);
    c->texRoughness = BGFX_INVALID_HANDLE;
  }
  if (bgfx::isValid(c->texMetallic)) {
    bgfx::destroy(c->texMetallic);
    c->texMetallic = BGFX_INVALID_HANDLE;
  }
  if (bgfx::isValid(c->texAo)) {
    bgfx::destroy(c->texAo);
    c->texAo = BGFX_INVALID_HANDLE;
  }
  if (bgfx::isValid(c->s_texColor)) {
    bgfx::destroy(c->s_texColor);
    c->s_texColor = BGFX_INVALID_HANDLE;
  }
}

static bool BuildMesh(const ObjPreviewModel& model, bool pseudoPbrEnabled, AgisBgfxPbrViewMode pbrViewMode,
                      const CpuPbrMaps* cpuMaps, std::vector<PosTexColorVertex>* verts, std::vector<uint32_t>* triIdx,
                      std::vector<uint32_t>* lineIdx) {
  verts->clear();
  triIdx->clear();
  lineIdx->clear();
  if (model.vertices.empty() || model.faces.empty()) return false;
  const float inv = 1.0f / (std::max)(0.001f, model.extent);
  const size_t stride = ModelPreviewFaceStride(model.faces.size());
  const bool haveFt = model.faceTexcoord.size() == model.faces.size();
  for (size_t fi = 0; fi < model.faces.size(); fi += stride) {
    const auto& f = model.faces[fi];
    if (f[0] < 0 || f[1] < 0 || f[2] < 0 || f[0] >= static_cast<int>(model.vertices.size()) ||
        f[1] >= static_cast<int>(model.vertices.size()) || f[2] >= static_cast<int>(model.vertices.size())) {
      continue;
    }
    float cr = model.kdR, cg = model.kdG, cb = model.kdB;
    if (fi < model.faceMaterial.size()) {
      const int mi = model.faceMaterial[fi];
      if (mi >= 0 && mi < static_cast<int>(model.materials.size())) {
        cr = model.materials[mi].kdR;
        cg = model.materials[mi].kdG;
        cb = model.materials[mi].kdB;
      }
    }
    const auto& a = model.vertices[f[0]];
    const auto& b = model.vertices[f[1]];
    const auto& c = model.vertices[f[2]];
    const float ax = (a.x - model.center.x) * inv;
    const float ay = (a.y - model.center.y) * inv;
    const float az = (a.z - model.center.z) * inv;
    const float bx = (b.x - model.center.x) * inv;
    const float by = (b.y - model.center.y) * inv;
    const float bz = (b.z - model.center.z) * inv;
    const float cx = (c.x - model.center.x) * inv;
    const float cy = (c.y - model.center.y) * inv;
    const float cz = (c.z - model.center.z) * inv;
    float nx = (by - ay) * (cz - az) - (bz - az) * (cy - ay);
    float ny = (bz - az) * (cx - ax) - (bx - ax) * (cz - az);
    float nz = (bx - ax) * (cy - ay) - (by - ay) * (cx - ax);
    const float nlRaw = std::sqrt(nx * nx + ny * ny + nz * nz);
    if (nlRaw < 1e-6f) {
      // 极小三角面：避免法线数值抖动导致局部黑斑。
      nx = 0.0f;
      ny = 1.0f;
      nz = 0.0f;
    } else {
      nx /= nlRaw;
      ny /= nlRaw;
      nz /= nlRaw;
    }
    const float lx = -0.32f, ly = 0.58f, lz = 0.75f;
    const float vx = 0.0f, vy = 0.0f, vz = 1.0f;
    const uint32_t base = static_cast<uint32_t>(verts->size());
    for (int k = 0; k < 3; ++k) {
      const auto& v = model.vertices[f[k]];
      float u = 0.f, tv = 0.f;
      if (haveFt && fi < model.faceTexcoord.size()) {
        const int ti = model.faceTexcoord[fi][k];
        if (ti >= 0 && ti < static_cast<int>(model.texcoords.size())) {
          u = model.texcoords[ti].u;
          tv = model.texcoords[ti].v;
        }
      }
      const float vTex = 1.0f - tv;
      const float ndotlBase = (std::max)(0.0f, nx * lx + ny * ly + nz * lz);
      float lighting = 0.26f + ndotlBase * 0.74f;
      float roughness = 0.45f;
      float metallic = 0.15f;
      float ao = 1.0f;
      float nmR = 0.5f, nmG = 0.5f, nmB = 1.0f;
      if (pseudoPbrEnabled) {
        if (cpuMaps) {
          roughness = (std::clamp)(SampleImageGray01(cpuMaps->roughness, u, vTex), 0.04f, 1.0f);
          metallic = (std::clamp)(SampleImageGray01(cpuMaps->metallic, u, vTex), 0.0f, 1.0f);
          // 避免 AO 过度压暗导致黑斑，限制最低亮度。
          ao = (std::clamp)(SampleImageGray01(cpuMaps->ao, u, vTex), 0.65f, 1.0f);
          SampleImageRgb01(cpuMaps->normal, u, vTex, &nmR, &nmG, &nmB);
        }
        // TBN 近似：从面法线构造一个稳定切线基，再把法线贴图从切线空间转到世界空间。
        float tx = -ny, ty = nx, tz = 0.0f;
        const float tl0 = std::sqrt(tx * tx + ty * ty + tz * tz);
        if (tl0 < 1e-4f) {
          tx = 1.0f;
          ty = 0.0f;
          tz = 0.0f;
        } else {
          tx /= tl0;
          ty /= tl0;
        }
        float bx = ny * tz - nz * ty;
        float by = nz * tx - nx * tz;
        float bz = nx * ty - ny * tx;
        const float bl = (std::max)(0.0001f, std::sqrt(bx * bx + by * by + bz * bz));
        bx /= bl;
        by /= bl;
        bz /= bl;

        const float tnx = nmR * 2.0f - 1.0f;
        const float tny = nmG * 2.0f - 1.0f;
        const float tnz = nmB * 2.0f - 1.0f;
        float pnx = tx * tnx + bx * tny + nx * tnz;
        float pny = ty * tnx + by * tny + ny * tnz;
        float pnz = tz * tnx + bz * tny + nz * tnz;
        const float pnl = (std::max)(0.0001f, std::sqrt(pnx * pnx + pny * pny + pnz * pnz));
        pnx /= pnl;
        pny /= pnl;
        pnz /= pnl;
        // 近似 TBN 在无切线数据时不稳定：与几何法线混合，抑制局部黑斑。
        constexpr float kNormalBlend = 0.20f;
        pnx = nx * (1.0f - kNormalBlend) + pnx * kNormalBlend;
        pny = ny * (1.0f - kNormalBlend) + pny * kNormalBlend;
        pnz = nz * (1.0f - kNormalBlend) + pnz * kNormalBlend;
        const float pn2 = (std::max)(0.0001f, std::sqrt(pnx * pnx + pny * pny + pnz * pnz));
        pnx /= pn2;
        pny /= pn2;
        pnz /= pn2;

        const float ndotl = (std::clamp)(pnx * lx + pny * ly + pnz * lz, 0.0f, 1.0f);
        const float ndotv = (std::clamp)(pnx * vx + pny * vy + pnz * vz, 0.0f, 1.0f);
        const float hx = lx + vx, hy = ly + vy, hz = lz + vz;
        const float hl = (std::max)(0.0001f, std::sqrt(hx * hx + hy * hy + hz * hz));
        const float hnx = hx / hl, hny = hy / hl, hnz = hz / hl;
        const float ndoth = (std::clamp)(pnx * hnx + pny * hny + pnz * hnz, 0.0f, 1.0f);
        const float vdh = (std::clamp)(vx * hnx + vy * hny + vz * hnz, 0.0f, 1.0f);

        const float alpha = roughness * roughness;
        const float alpha2 = alpha * alpha;
        const float denom = ndoth * ndoth * (alpha2 - 1.0f) + 1.0f;
        const float D = alpha2 / ((std::max)(1e-4f, 3.1415926f * denom * denom));

        const float k = (alpha + 1.0f) * (alpha + 1.0f) * 0.125f;
        const float Gv = ndotv / ((std::max)(1e-4f, ndotv * (1.0f - k) + k));
        const float Gl = ndotl / ((std::max)(1e-4f, ndotl * (1.0f - k) + k));
        const float G = Gv * Gl;

        const float f0 = 0.04f * (1.0f - metallic) + metallic;
        const float F = f0 + (1.0f - f0) * std::pow(1.0f - vdh, 5.0f);
        const float spec = (D * G * F) / ((std::max)(1e-4f, 4.0f * ndotv * ndotl));
        const float kd = (1.0f - F) * (1.0f - metallic);
        const float diffuse = kd * ndotl;
        const float ambient = 0.24f * ao;
        lighting = (std::max)(0.12f, ambient + diffuse + spec * 0.70f);
      }
      float sr = (std::clamp)(cr * lighting, 0.0f, 1.0f);
      float sg = (std::clamp)(cg * lighting, 0.0f, 1.0f);
      float sb = (std::clamp)(cb * lighting, 0.0f, 1.0f);
      switch (pbrViewMode) {
        case AgisBgfxPbrViewMode::kAlbedo:
          sr = cr;
          sg = cg;
          sb = cb;
          break;
        case AgisBgfxPbrViewMode::kNormal:
          sr = nmR;
          sg = nmG;
          sb = nmB;
          break;
        case AgisBgfxPbrViewMode::kRoughness:
          sr = roughness;
          sg = roughness;
          sb = roughness;
          break;
        case AgisBgfxPbrViewMode::kMetallic:
          sr = metallic;
          sg = metallic;
          sb = metallic;
          break;
        case AgisBgfxPbrViewMode::kAo:
          sr = ao;
          sg = ao;
          sb = ao;
          break;
        case AgisBgfxPbrViewMode::kPbrLit:
        default:
          break;
      }
      const uint32_t abgr = PackAbgr8(sr, sg, sb, 1.0f);
      PosTexColorVertex p{};
      p.x = (v.x - model.center.x) * inv;
      p.y = (v.y - model.center.y) * inv;
      p.z = (v.z - model.center.z) * inv;
      p.u = u;
      p.v = vTex;
      p.abgr = abgr;
      verts->push_back(p);
    }
    triIdx->push_back(base);
    triIdx->push_back(base + 1);
    triIdx->push_back(base + 2);
    lineIdx->push_back(base);
    lineIdx->push_back(base + 1);
    lineIdx->push_back(base + 1);
    lineIdx->push_back(base + 2);
    lineIdx->push_back(base + 2);
    lineIdx->push_back(base);
  }
  return !verts->empty() && !triIdx->empty();
}

static bool RebuildMeshBuffers(AgisBgfxPreviewContextImpl* c) {
  if (!c) {
    return false;
  }
  std::vector<PosTexColorVertex> verts;
  std::vector<uint32_t> triIdx;
  std::vector<uint32_t> lineIdx;
  if (!BuildMesh(c->cachedModel, c->pseudoPbrEnabled, c->pbrViewMode, &c->cpuMaps, &verts, &triIdx, &lineIdx)) {
    return false;
  }
  DestroyMesh(c);
  const bgfx::Memory* vmem = bgfx::copy(verts.data(), static_cast<uint32_t>(verts.size() * sizeof(PosTexColorVertex)));
  c->vbh = bgfx::createVertexBuffer(vmem, PosTexColorVertex::Layout());
  const bgfx::Memory* tmem = bgfx::copy(triIdx.data(), static_cast<uint32_t>(triIdx.size() * sizeof(uint32_t)));
  c->ibhTri = bgfx::createIndexBuffer(tmem, BGFX_BUFFER_INDEX32);
  const bgfx::Memory* lmem = bgfx::copy(lineIdx.data(), static_cast<uint32_t>(lineIdx.size() * sizeof(uint32_t)));
  c->ibhLine = bgfx::createIndexBuffer(lmem, BGFX_BUFFER_INDEX32);
  c->triCount = static_cast<uint32_t>(triIdx.size());
  c->lineCount = static_cast<uint32_t>(lineIdx.size());
  return bgfx::isValid(c->vbh) && bgfx::isValid(c->ibhTri) && bgfx::isValid(c->ibhLine) && c->triCount > 0;
}

}  // namespace

static AgisBgfxPreviewContextImpl* AsImpl(AgisBgfxPreviewContext* p) {
  return reinterpret_cast<AgisBgfxPreviewContextImpl*>(p);
}

bool agis_bgfx_preview_init(HWND hwnd, AgisBgfxPreviewContext** ctx, AgisBgfxRendererKind renderer, const ObjPreviewModel& model) {
  if (!ctx || !hwnd) return false;
  if (*ctx) {
    agis_bgfx_preview_shutdown(hwnd, *ctx);
    *ctx = nullptr;
  }
  RECT cr{};
  GetClientRect(hwnd, &cr);
  const uint32_t cw = (std::max)(1L, cr.right - cr.left);
  const uint32_t ch = (std::max)(1L, cr.bottom - cr.top);

  auto* c = new AgisBgfxPreviewContextImpl();
  c->cachedModel = model;
  c->renderer = ToBgfxType(renderer);
  // 部分驱动下 OpenGL + VSync 可能出现交互停滞，默认关闭 OpenGL 的 VSync。
  c->resetFlags = (c->renderer == bgfx::RendererType::OpenGL) ? BGFX_RESET_NONE : BGFX_RESET_VSYNC;

  bgfx::Init init;
  init.type = c->renderer;
  init.platformData.ndt = nullptr;
  init.platformData.nwh = hwnd;
  init.platformData.context = nullptr;
  init.platformData.queue = nullptr;
  init.platformData.backBuffer = nullptr;
  init.platformData.backBufferDS = nullptr;
  init.platformData.type = bgfx::NativeWindowHandleType::Default;
  init.resolution.width = cw;
  init.resolution.height = ch;
  init.resolution.reset = c->resetFlags;
  if (!bgfx::init(init)) {
    delete c;
    return false;
  }

  // 默认浅灰背景（ABGR）。
  bgfx::setViewClear(0, BGFX_CLEAR_COLOR | BGFX_CLEAR_DEPTH, 0xffececec, 1.0f, 0);

  bgfx::ShaderHandle vsh = bgfx::createEmbeddedShader(kEmbeddedShaders, c->renderer, "vs_debugdraw_fill_texture");
  bgfx::ShaderHandle fsh = bgfx::createEmbeddedShader(kEmbeddedShaders, c->renderer, "fs_debugdraw_fill_texture");
  if (!bgfx::isValid(vsh) || !bgfx::isValid(fsh)) {
    if (bgfx::isValid(vsh)) bgfx::destroy(vsh);
    if (bgfx::isValid(fsh)) bgfx::destroy(fsh);
    bgfx::shutdown();
    delete c;
    return false;
  }
  c->program = bgfx::createProgram(vsh, fsh, true);
  if (!bgfx::isValid(c->program)) {
    bgfx::shutdown();
    delete c;
    return false;
  }

  c->s_texColor = bgfx::createUniform("s_texColor", bgfx::UniformType::Sampler);
  if (!bgfx::isValid(c->s_texColor)) {
    bgfx::destroy(c->program);
    c->program = BGFX_INVALID_HANDLE;
    bgfx::shutdown();
    delete c;
    return false;
  }

  c->texture = CreateWhiteTexture2D();
  if (!bgfx::isValid(c->texture)) {
    DestroyTextureResources(c);
    bgfx::destroy(c->program);
    c->program = BGFX_INVALID_HANDLE;
    bgfx::shutdown();
    delete c;
    return false;
  }
  if (!model.primaryMapKdPath.empty()) {
    const bgfx::TextureHandle mapTex = LoadMapKdTexture(model.primaryMapKdPath);
    if (bgfx::isValid(mapTex)) {
      bgfx::destroy(c->texture);
      c->texture = mapTex;
      c->texBaseColor = LoadMapKdTexture(model.primaryMapKdPath);
    }
  }

  if (!RebuildMeshBuffers(c)) {
    DestroyTextureResources(c);
    bgfx::destroy(c->program);
    c->program = BGFX_INVALID_HANDLE;
    bgfx::shutdown();
    delete c;
    return false;
  }
  c->lastResetW = cw;
  c->lastResetH = ch;

  *ctx = reinterpret_cast<AgisBgfxPreviewContext*>(c);
  return true;
}

void agis_bgfx_preview_shutdown(HWND hwnd, AgisBgfxPreviewContext* ctx) {
  (void)hwnd;
  if (!ctx) return;
  AgisBgfxPreviewContextImpl* c = AsImpl(ctx);
  DestroyMesh(c);
  DestroyTextureResources(c);
  if (bgfx::isValid(c->program)) bgfx::destroy(c->program);
  c->program = BGFX_INVALID_HANDLE;
  bgfx::shutdown();
  delete c;
}

void agis_bgfx_preview_draw(AgisBgfxPreviewContext* ctx, HWND hwnd, const RECT& viewportPx, float rotX, float rotY, float zoom,
                            bool solid, bool showGrid) {
  if (!ctx || !hwnd) return;
  AgisBgfxPreviewContextImpl* impl = AsImpl(ctx);
  if (!bgfx::isValid(impl->program)) return;

  RECT cr{};
  GetClientRect(hwnd, &cr);
  const uint32_t cw = (std::max)(1L, cr.right - cr.left);
  const uint32_t ch = (std::max)(1L, cr.bottom - cr.top);
  if (cw != impl->lastResetW || ch != impl->lastResetH) {
    bgfx::reset(cw, ch, impl->resetFlags);
    impl->lastResetW = cw;
    impl->lastResetH = ch;
  }

  const int vx = static_cast<int>(viewportPx.left);
  const int vy = static_cast<int>(viewportPx.top);
  const int vw = static_cast<int>((std::max)(1L, viewportPx.right - viewportPx.left));
  const int vh = static_cast<int>((std::max)(1L, viewportPx.bottom - viewportPx.top));
  const float aspect = static_cast<float>(vw) / static_cast<float>(vh);

  bgfx::setViewClear(0, BGFX_CLEAR_COLOR | BGFX_CLEAR_DEPTH, 0xffececec, 1.0f, 0);

  const bgfx::Caps* caps = bgfx::getCaps();
  float view[16];
  const bx::Vec3 at{0.0f, 0.0f, 0.0f};
  const bx::Vec3 eye{0.0f, 0.0f, -3.2f};
  bx::mtxLookAt(view, eye, at);
  float proj[16];
  bx::mtxProj(proj, 60.0f, aspect, 0.1f, 100.0f, caps->homogeneousDepth);
  bgfx::setViewTransform(0, view, proj);
  bgfx::setViewRect(0, static_cast<uint16_t>(vx), static_cast<uint16_t>(vy), static_cast<uint16_t>(vw), static_cast<uint16_t>(vh));
  bgfx::touch(0);

  float sc[16], rx[16], ry[16], t1[16], mod[16];
  bx::mtxScale(sc, zoom, zoom, zoom);
  bx::mtxRotateX(rx, rotX);
  bx::mtxRotateY(ry, rotY);
  bx::mtxMul(t1, ry, sc);
  bx::mtxMul(mod, rx, t1);
  bgfx::setTransform(mod);

  bgfx::setVertexBuffer(0, impl->vbh);
  if (bgfx::isValid(impl->texture) && bgfx::isValid(impl->s_texColor)) {
    bgfx::setTexture(0, impl->s_texColor, impl->texture);
  }
  constexpr uint64_t kWrite = BGFX_STATE_WRITE_RGB | BGFX_STATE_WRITE_A | BGFX_STATE_WRITE_Z | BGFX_STATE_DEPTH_TEST_LESS;
  if (solid && impl->triCount > 0) {
    bgfx::setState(kWrite | BGFX_STATE_MSAA);
    bgfx::setIndexBuffer(impl->ibhTri, 0, impl->triCount);
    bgfx::submit(0, impl->program);
  }
  // 实体模式不再叠加线框，避免“脏脏的”观感；仅线框模式显示边线。
  if (!solid && impl->lineCount > 0) {
    bgfx::setState(kWrite | BGFX_STATE_PT_LINES | BGFX_STATE_MSAA);
    bgfx::setIndexBuffer(impl->ibhLine, 0, impl->lineCount);
    bgfx::submit(0, impl->program);
  }
  if (showGrid) {
    constexpr int kHalf = 10;
    constexpr float kStep = 0.1f;
    std::vector<PosTexColorVertex> gv;
    std::vector<uint16_t> gi;
    gv.reserve((kHalf * 2 + 1) * 4);
    gi.reserve((kHalf * 2 + 1) * 4);
    const uint32_t abgr = 0xffb8b8b8;
    for (int i = -kHalf; i <= kHalf; ++i) {
      const float p = static_cast<float>(i) * kStep;
      const uint16_t b = static_cast<uint16_t>(gv.size());
      gv.push_back({-kHalf * kStep, 0.0f, p, 0.0f, 0.0f, abgr});
      gv.push_back({kHalf * kStep, 0.0f, p, 0.0f, 0.0f, abgr});
      gv.push_back({p, 0.0f, -kHalf * kStep, 0.0f, 0.0f, abgr});
      gv.push_back({p, 0.0f, kHalf * kStep, 0.0f, 0.0f, abgr});
      gi.push_back(b);
      gi.push_back(static_cast<uint16_t>(b + 1));
      gi.push_back(static_cast<uint16_t>(b + 2));
      gi.push_back(static_cast<uint16_t>(b + 3));
    }
    bgfx::TransientVertexBuffer tvb;
    bgfx::TransientIndexBuffer tib;
    if (bgfx::allocTransientBuffers(&tvb, PosTexColorVertex::Layout(), static_cast<uint32_t>(gv.size()), &tib,
                                    static_cast<uint32_t>(gi.size()))) {
      memcpy(tvb.data, gv.data(), gv.size() * sizeof(PosTexColorVertex));
      memcpy(tib.data, gi.data(), gi.size() * sizeof(uint16_t));
      bgfx::setTransform(mod);
      bgfx::setVertexBuffer(0, &tvb);
      bgfx::setIndexBuffer(&tib);
      bgfx::setState(kWrite | BGFX_STATE_PT_LINES | BGFX_STATE_MSAA);
      bgfx::submit(0, impl->program);
    }
  }
  bgfx::frame();
  if (const bgfx::Stats* stats = bgfx::getStats()) {
    impl->runtime.cpuFrameMs = TicksToMs(stats->cpuTimeEnd - stats->cpuTimeBegin, stats->cpuTimerFreq);
    impl->runtime.gpuFrameMs = TicksToMs(stats->gpuTimeEnd - stats->gpuTimeBegin, stats->gpuTimerFreq);
    impl->runtime.waitSubmitMs = TicksToMs(stats->waitSubmit, stats->cpuTimerFreq);
    impl->runtime.waitRenderMs = TicksToMs(stats->waitRender, stats->cpuTimerFreq);
    impl->runtime.drawCalls = static_cast<uint32_t>(stats->numDraw);
  }
}

bool agis_bgfx_preview_get_runtime_stats(AgisBgfxPreviewContext* ctx, AgisBgfxRuntimeStats* out) {
  if (!ctx || !out) {
    return false;
  }
  AgisBgfxPreviewContextImpl* impl = AsImpl(ctx);
  *out = impl->runtime;
  return true;
}

bool agis_bgfx_preview_set_texture(AgisBgfxPreviewContext* ctx, const std::wstring& texturePath) {
  if (!ctx) {
    return false;
  }
  AgisBgfxPreviewContextImpl* impl = AsImpl(ctx);
  bgfx::TextureHandle nextTex = BGFX_INVALID_HANDLE;
  if (!texturePath.empty()) {
    nextTex = LoadMapKdTexture(texturePath);
  }
  if (!bgfx::isValid(nextTex)) {
    nextTex = CreateWhiteTexture2D();
  }
  if (!bgfx::isValid(nextTex)) {
    return false;
  }
  if (bgfx::isValid(impl->texture)) {
    bgfx::destroy(impl->texture);
  }
  impl->texture = nextTex;
  return true;
}

bool agis_bgfx_preview_set_pbr_textures(AgisBgfxPreviewContext* ctx, const AgisBgfxPbrTexturePaths& paths) {
  if (!ctx) {
    return false;
  }
  AgisBgfxPreviewContextImpl* impl = AsImpl(ctx);
  auto replaceTex = [](bgfx::TextureHandle* target, const std::wstring& path) {
    bgfx::TextureHandle next = BGFX_INVALID_HANDLE;
    if (!path.empty()) {
      next = LoadMapKdTexture(path);
    }
    if (bgfx::isValid(*target)) {
      bgfx::destroy(*target);
      *target = BGFX_INVALID_HANDLE;
    }
    *target = next;
    return bgfx::isValid(next);
  };

  const bool haveBase = replaceTex(&impl->texBaseColor, paths.baseColorPath);
  const bool haveNormal = replaceTex(&impl->texNormal, paths.normalPath);
  const bool haveRoughness = replaceTex(&impl->texRoughness, paths.roughnessPath);
  const bool haveMetallic = replaceTex(&impl->texMetallic, paths.metallicPath);
  const bool haveAo = replaceTex(&impl->texAo, paths.aoPath);

  if (haveBase) {
    if (bgfx::isValid(impl->texture)) {
      bgfx::destroy(impl->texture);
    }
    impl->texture = LoadMapKdTexture(paths.baseColorPath);
    if (!bgfx::isValid(impl->texture)) {
      impl->texture = CreateWhiteTexture2D();
    }
  }
  impl->cpuMaps = CpuPbrMaps{};
  if (haveNormal) {
    LoadCpuImage2D(paths.normalPath, &impl->cpuMaps.normal);
  }
  if (haveRoughness) {
    LoadCpuImage2D(paths.roughnessPath, &impl->cpuMaps.roughness);
  }
  if (haveMetallic) {
    LoadCpuImage2D(paths.metallicPath, &impl->cpuMaps.metallic);
  }
  if (haveAo) {
    LoadCpuImage2D(paths.aoPath, &impl->cpuMaps.ao);
  }
  if (impl->pseudoPbrEnabled) {
    RebuildMeshBuffers(impl);
  }
  return true;
}

bool agis_bgfx_preview_set_pseudo_pbr(AgisBgfxPreviewContext* ctx, bool enabled) {
  if (!ctx) {
    return false;
  }
  AgisBgfxPreviewContextImpl* impl = AsImpl(ctx);
  if (impl->pseudoPbrEnabled == enabled) {
    return true;
  }
  impl->pseudoPbrEnabled = enabled;
  return RebuildMeshBuffers(impl);
}

bool agis_bgfx_preview_set_pbr_view_mode(AgisBgfxPreviewContext* ctx, AgisBgfxPbrViewMode mode) {
  if (!ctx) {
    return false;
  }
  AgisBgfxPreviewContextImpl* impl = AsImpl(ctx);
  if (impl->pbrViewMode == mode) {
    return true;
  }
  impl->pbrViewMode = mode;
  return RebuildMeshBuffers(impl);
}
