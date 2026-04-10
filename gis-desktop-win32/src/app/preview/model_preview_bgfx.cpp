#include "app/preview/model_preview_bgfx.h"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <memory>
#include <vector>

#include <bgfx/bgfx.h>
#include <bgfx/embedded_shader.h>
#include <bx/allocator.h>
#include <bx/math.h>
#include <bimg/decode.h>

#include "fs_debugdraw_fill_texture.bin.h"
#include "vs_debugdraw_fill_texture.bin.h"

namespace {

/// 灰蓝清屏色。bgfx `setViewClear` 第三参为 **0xRRGGBBAA**（见 `bgfx_p.h` `Clear::set`，非 ABGR）。
constexpr uint32_t kPreviewBgfxClearRgba = 0xC8D4E6FFu;  // RGB(200,212,230), A=255

bx::DefaultAllocator s_bimgAlloc;

struct CpuImage2D {
  uint32_t width = 0;
  uint32_t height = 0;
  uint32_t bpp = 0;
  bool bgra = false;
  std::vector<uint8_t> pixels;
  /// 非空时像素只读引用 `workerPayloadKeep` 内缓冲区，避免主线程再拷贝一份贴图。
  const uint8_t* borrowData = nullptr;
};

static const uint8_t* CpuImageBytesPtr(const CpuImage2D& img) {
  if (img.borrowData) {
    return img.borrowData;
  }
  return img.pixels.empty() ? nullptr : img.pixels.data();
}

static bool CpuImageHasPixelData(const CpuImage2D& img) {
  return img.width > 0 && img.height > 0 && img.bpp > 0 && CpuImageBytesPtr(img) != nullptr;
}

static size_t CpuImageByteSize(const CpuImage2D& img) {
  return static_cast<size_t>(img.width) * static_cast<size_t>(img.height) * static_cast<size_t>(img.bpp);
}

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
  /// 由 `agis_bgfx_preview_init` 从 worker 包 move 入；供 `makeRef` 指向的 CPU 内存活到 `shutdown`。
  std::unique_ptr<AgisBgfxPreviewWorkerPackage> workerPayloadKeep;
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

static bool DecodeFileToCpuImage2D(const std::wstring& path, AgisBgfxCpuImage2D* out) {
  if (!out) {
    return false;
  }
  *out = AgisBgfxCpuImage2D{};
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

static bool LoadCpuImage2D(const std::wstring& path, CpuImage2D* out) {
  if (!out) {
    return false;
  }
  *out = CpuImage2D{};
  AgisBgfxCpuImage2D tmp{};
  if (!DecodeFileToCpuImage2D(path, &tmp)) {
    return false;
  }
  out->width = tmp.width;
  out->height = tmp.height;
  out->bpp = tmp.bpp;
  out->bgra = tmp.bgra;
  out->pixels = std::move(tmp.pixels);
  return true;
}

/// 将 PBR 贴图元数据挂到 `CpuImage2D`，像素指针借用 `AgisBgfxCpuImage2D::pixels`（须由 worker 包或同等生命周期保证有效）。
static void AttachBorrowCpuMap(CpuImage2D* dst, const AgisBgfxCpuImage2D& src) {
  if (!dst) {
    return;
  }
  *dst = CpuImage2D{};
  if (src.width == 0 || src.height == 0 || src.bpp == 0 || src.pixels.empty()) {
    return;
  }
  dst->width = src.width;
  dst->height = src.height;
  dst->bpp = src.bpp;
  dst->bgra = src.bgra;
  dst->borrowData = src.pixels.data();
}

static bgfx::TextureHandle CreateBgfxTextureFromCpuImageCopy(const AgisBgfxCpuImage2D& img) {
  if (img.width == 0 || img.height == 0 || img.pixels.empty() || img.bpp == 0) {
    return BGFX_INVALID_HANDLE;
  }
  bgfx::TextureFormat::Enum fmt = bgfx::TextureFormat::RGBA8;
  if (img.bpp == 4 && img.bgra) {
    fmt = bgfx::TextureFormat::BGRA8;
  } else if (img.bpp == 4) {
    fmt = bgfx::TextureFormat::RGBA8;
  } else if (img.bpp == 3) {
    fmt = bgfx::TextureFormat::RGB8;
  } else if (img.bpp == 1) {
    fmt = bgfx::TextureFormat::R8;
  } else {
    return BGFX_INVALID_HANDLE;
  }
  const bgfx::Memory* mem = bgfx::copy(img.pixels.data(), static_cast<uint32_t>(img.pixels.size()));
  return bgfx::createTexture2D(static_cast<uint16_t>(img.width), static_cast<uint16_t>(img.height), false, 1, fmt, 0,
                               mem);
}

/// 引用外部像素缓冲区（须由 `workerPayloadKeep` 等保证在 destroy 纹理前有效）。
static bgfx::TextureHandle CreateBgfxTextureFromCpuImageRef(const AgisBgfxCpuImage2D& img) {
  if (img.width == 0 || img.height == 0 || img.pixels.empty() || img.bpp == 0) {
    return BGFX_INVALID_HANDLE;
  }
  bgfx::TextureFormat::Enum fmt = bgfx::TextureFormat::RGBA8;
  if (img.bpp == 4 && img.bgra) {
    fmt = bgfx::TextureFormat::BGRA8;
  } else if (img.bpp == 4) {
    fmt = bgfx::TextureFormat::RGBA8;
  } else if (img.bpp == 3) {
    fmt = bgfx::TextureFormat::RGB8;
  } else if (img.bpp == 1) {
    fmt = bgfx::TextureFormat::R8;
  } else {
    return BGFX_INVALID_HANDLE;
  }
  const bgfx::Memory* mem =
      bgfx::makeRef(img.pixels.data(), static_cast<uint32_t>(img.pixels.size()), nullptr, nullptr);
  return bgfx::createTexture2D(static_cast<uint16_t>(img.width), static_cast<uint16_t>(img.height), false, 1, fmt, 0,
                               mem);
}

static float SampleImageGray01(const CpuImage2D& img, float u, float v) {
  if (!CpuImageHasPixelData(img)) {
    return 1.0f;
  }
  const uint8_t* data = CpuImageBytesPtr(img);
  const size_t nbytes = CpuImageByteSize(img);
  const float uu = u - std::floor(u);
  const float vv = v - std::floor(v);
  const uint32_t x = (std::min)(img.width - 1, static_cast<uint32_t>(uu * static_cast<float>(img.width)));
  const uint32_t y = (std::min)(img.height - 1, static_cast<uint32_t>(vv * static_cast<float>(img.height)));
  const size_t idx = (static_cast<size_t>(y) * img.width + x) * img.bpp;
  if (idx + img.bpp > nbytes) {
    return 1.0f;
  }
  if (img.bpp == 1) {
    return static_cast<float>(data[idx]) / 255.0f;
  }
  const uint8_t c0 = data[idx + 0];
  const uint8_t c1 = data[idx + 1];
  const uint8_t c2 = data[idx + 2];
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
  if (!CpuImageHasPixelData(img) || img.bpp < 3) {
    return;
  }
  const uint8_t* data = CpuImageBytesPtr(img);
  const size_t nbytes = CpuImageByteSize(img);
  const float uu = u - std::floor(u);
  const float vv = v - std::floor(v);
  const uint32_t x = (std::min)(img.width - 1, static_cast<uint32_t>(uu * static_cast<float>(img.width)));
  const uint32_t y = (std::min)(img.height - 1, static_cast<uint32_t>(vv * static_cast<float>(img.height)));
  const size_t idx = (static_cast<size_t>(y) * img.width + x) * img.bpp;
  if (idx + img.bpp > nbytes) {
    return;
  }
  uint8_t c0 = data[idx + 0];
  uint8_t c1 = data[idx + 1];
  uint8_t c2 = data[idx + 2];
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

static void LoadCpuPbrMapsFromPaths(const AgisBgfxPbrTexturePaths& paths, CpuPbrMaps* out) {
  if (!out) {
    return;
  }
  *out = CpuPbrMaps{};
  if (!paths.normalPath.empty()) {
    LoadCpuImage2D(paths.normalPath, &out->normal);
  }
  if (!paths.roughnessPath.empty()) {
    LoadCpuImage2D(paths.roughnessPath, &out->roughness);
  }
  if (!paths.metallicPath.empty()) {
    LoadCpuImage2D(paths.metallicPath, &out->metallic);
  }
  if (!paths.aoPath.empty()) {
    LoadCpuImage2D(paths.aoPath, &out->ao);
  }
}

static bool BuildMesh(const ObjPreviewModel& model, bool pseudoPbrEnabled, AgisBgfxPbrViewMode pbrViewMode,
                      const CpuPbrMaps* cpuMaps, std::vector<PosTexColorVertex>* verts, std::vector<uint32_t>* triIdx,
                      std::vector<uint32_t>* lineIdx, int* progressPct) {
  verts->clear();
  triIdx->clear();
  lineIdx->clear();
  // 允许空场景：无网格时仅渲染背景/网格/ImGui，不视为初始化失败。
  if (model.vertices.empty() || model.faces.empty()) return true;
  const float inv = 1.0f / (std::max)(0.001f, model.extent);
  bool likelySpherical = false;
  {
    const size_t sampleN = (std::min)(model.vertices.size(), static_cast<size_t>(2048));
    if (sampleN >= 64) {
      double sumR = 0.0;
      double sumR2 = 0.0;
      for (size_t i = 0; i < sampleN; ++i) {
        const auto& v = model.vertices[i];
        const double x = static_cast<double>(v.x - model.center.x);
        const double y = static_cast<double>(v.y - model.center.y);
        const double z = static_cast<double>(v.z - model.center.z);
        const double r = std::sqrt(x * x + y * y + z * z);
        sumR += r;
        sumR2 += r * r;
      }
      const double meanR = sumR / static_cast<double>(sampleN);
      if (meanR > 1e-6) {
        const double varR = (std::max)(0.0, sumR2 / static_cast<double>(sampleN) - meanR * meanR);
        const double stdR = std::sqrt(varR);
        likelySpherical = (stdR / meanR) < 0.08;
      }
    }
  }
  bool forceFlipUp = false;
  if (!likelySpherical) {
    const size_t sampleN = (std::min)(model.faces.size(), static_cast<size_t>(1024));
    if (sampleN >= 16) {
      double sumNz = 0.0;
      size_t cnt = 0;
      for (size_t i = 0; i < sampleN; ++i) {
        const auto& f = model.faces[i];
        if (f[0] < 0 || f[1] < 0 || f[2] < 0 || f[0] >= static_cast<int>(model.vertices.size()) ||
            f[1] >= static_cast<int>(model.vertices.size()) || f[2] >= static_cast<int>(model.vertices.size())) {
          continue;
        }
        const auto& a = model.vertices[f[0]];
        const auto& b = model.vertices[f[1]];
        const auto& c = model.vertices[f[2]];
        const double ax = static_cast<double>(a.x - model.center.x) * inv;
        const double ay = static_cast<double>(a.y - model.center.y) * inv;
        const double bx = static_cast<double>(b.x - model.center.x) * inv;
        const double by = static_cast<double>(b.y - model.center.y) * inv;
        const double cx = static_cast<double>(c.x - model.center.x) * inv;
        const double cy = static_cast<double>(c.y - model.center.y) * inv;
        const double cz = (bx - ax) * (cy - ay) - (by - ay) * (cx - ax);
        sumNz += cz;
        ++cnt;
      }
      if (cnt > 0 && sumNz < 0.0) {
        forceFlipUp = true;
      }
    }
  }
  const size_t stride = ModelPreviewFaceStride(model.faces.size());
  const bool haveFt = model.faceTexcoord.size() == model.faces.size();
  size_t faceSteps = 0;
  for (size_t fi = 0; fi < model.faces.size(); fi += stride) {
    (void)fi;
    ++faceSteps;
  }
  size_t fiStep = 0;
  for (size_t fi = 0; fi < model.faces.size(); fi += stride) {
    ++fiStep;
    if (progressPct && faceSteps > 0) {
      if ((fiStep & 0x3Fu) == 0 || fiStep == faceSteps) {
        const int pct = static_cast<int>((fiStep * 100) / faceSteps);
        *progressPct = (std::min)(99, pct);
      }
    }
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
    int ia = f[0];
    int ib = f[1];
    int ic = f[2];
    auto shouldFlipFace = [&]() -> bool {
      const auto& a0 = model.vertices[ia];
      const auto& b0 = model.vertices[ib];
      const auto& c0 = model.vertices[ic];
      const float ax0 = (a0.x - model.center.x) * inv;
      const float ay0 = (a0.y - model.center.y) * inv;
      const float az0 = (a0.z - model.center.z) * inv;
      const float bx0 = (b0.x - model.center.x) * inv;
      const float by0 = (b0.y - model.center.y) * inv;
      const float bz0 = (b0.z - model.center.z) * inv;
      const float cx0 = (c0.x - model.center.x) * inv;
      const float cy0 = (c0.y - model.center.y) * inv;
      const float cz0 = (c0.z - model.center.z) * inv;
      const float nx0 = (by0 - ay0) * (cz0 - az0) - (bz0 - az0) * (cy0 - ay0);
      const float ny0 = (bz0 - az0) * (cx0 - ax0) - (bx0 - ax0) * (cz0 - az0);
      const float nz0 = (bx0 - ax0) * (cy0 - ay0) - (by0 - ay0) * (cx0 - ax0);
      if (likelySpherical) {
        const float mx = (ax0 + bx0 + cx0) * (1.0f / 3.0f);
        const float my = (ay0 + by0 + cy0) * (1.0f / 3.0f);
        const float mz = (az0 + bz0 + cz0) * (1.0f / 3.0f);
        return (nx0 * mx + ny0 * my + nz0 * mz) < 0.0f;
      }
      if (forceFlipUp) {
        return true;
      }
      return false;
    };
    if (shouldFlipFace()) {
      std::swap(ib, ic);
    }
    const auto& a = model.vertices[ia];
    const auto& b = model.vertices[ib];
    const auto& c = model.vertices[ic];
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
      const int vi = (k == 0) ? ia : (k == 1 ? ib : ic);
      const auto& v = model.vertices[vi];
      float u = 0.f, tv = 0.f;
      if (haveFt && fi < model.faceTexcoord.size()) {
        const int ti0 = model.faceTexcoord[fi][0];
        const int ti1 = model.faceTexcoord[fi][1];
        const int ti2 = model.faceTexcoord[fi][2];
        const int ti = (k == 0) ? ti0 : (k == 1 ? (ib == f[1] ? ti1 : ti2) : (ic == f[2] ? ti2 : ti1));
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
          const bool polarBand = likelySpherical && (vTex < 0.01f || vTex > 0.99f);
          roughness = (std::clamp)(SampleImageGray01(cpuMaps->roughness, u, vTex), 0.04f, 1.0f);
          metallic = (std::clamp)(SampleImageGray01(cpuMaps->metallic, u, vTex), 0.0f, 1.0f);
          // 避免 AO 过度压暗导致黑斑，限制最低亮度。
          ao = (std::clamp)(SampleImageGray01(cpuMaps->ao, u, vTex), 0.65f, 1.0f);
          SampleImageRgb01(cpuMaps->normal, u, vTex, &nmR, &nmG, &nmB);
          if (polarBand) {
            // 极区避免边界贴图采样噪声放大为黑圈/色环。
            ao = (std::max)(ao, 0.92f);
            nmR = 0.5f;
            nmG = 0.5f;
            nmB = 1.0f;
          }
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
  if (progressPct) {
    *progressPct = 100;
  }
  return true;
}

/// 主线程 `bgfx::copy` 整块 mesh（非 worker 路径 / reload）。
static bool UploadPrebuiltMeshCopy(AgisBgfxPreviewContextImpl* c, const AgisBgfxCpuBuiltMesh& mesh) {
  static_assert(sizeof(AgisBgfxInterleavedVertex) == sizeof(PosTexColorVertex), "vertex layout mismatch");
  DestroyMesh(c);
  if (mesh.vertices.empty() || mesh.triIndices.empty()) {
    c->triCount = 0;
    c->lineCount = 0;
    return true;
  }
  const bgfx::Memory* vmem =
      bgfx::copy(mesh.vertices.data(), static_cast<uint32_t>(mesh.vertices.size() * sizeof(PosTexColorVertex)));
  c->vbh = bgfx::createVertexBuffer(vmem, PosTexColorVertex::Layout());
  const bgfx::Memory* tmem =
      bgfx::copy(mesh.triIndices.data(), static_cast<uint32_t>(mesh.triIndices.size() * sizeof(uint32_t)));
  c->ibhTri = bgfx::createIndexBuffer(tmem, BGFX_BUFFER_INDEX32);
  const bgfx::Memory* lmem =
      bgfx::copy(mesh.lineIndices.data(), static_cast<uint32_t>(mesh.lineIndices.size() * sizeof(uint32_t)));
  c->ibhLine = bgfx::createIndexBuffer(lmem, BGFX_BUFFER_INDEX32);
  c->triCount = static_cast<uint32_t>(mesh.triIndices.size());
  c->lineCount = static_cast<uint32_t>(mesh.lineIndices.size());
  return bgfx::isValid(c->vbh) && bgfx::isValid(c->ibhTri) && bgfx::isValid(c->ibhLine);
}

/// worker 包已移入 `workerPayloadKeep`：引用 CPU 缓冲，避免主线程巨型 memcpy。
static bool UploadPrebuiltMeshRef(AgisBgfxPreviewContextImpl* c, const AgisBgfxCpuBuiltMesh& mesh) {
  static_assert(sizeof(AgisBgfxInterleavedVertex) == sizeof(PosTexColorVertex), "vertex layout mismatch");
  DestroyMesh(c);
  if (mesh.vertices.empty() || mesh.triIndices.empty()) {
    c->triCount = 0;
    c->lineCount = 0;
    return true;
  }
  const bgfx::Memory* vmem = bgfx::makeRef(mesh.vertices.data(),
                                           static_cast<uint32_t>(mesh.vertices.size() * sizeof(PosTexColorVertex)),
                                           nullptr, nullptr);
  c->vbh = bgfx::createVertexBuffer(vmem, PosTexColorVertex::Layout());
  const bgfx::Memory* tmem = bgfx::makeRef(mesh.triIndices.data(),
                                          static_cast<uint32_t>(mesh.triIndices.size() * sizeof(uint32_t)), nullptr,
                                          nullptr);
  c->ibhTri = bgfx::createIndexBuffer(tmem, BGFX_BUFFER_INDEX32);
  const bgfx::Memory* lmem = bgfx::makeRef(mesh.lineIndices.data(),
                                           static_cast<uint32_t>(mesh.lineIndices.size() * sizeof(uint32_t)), nullptr,
                                           nullptr);
  c->ibhLine = bgfx::createIndexBuffer(lmem, BGFX_BUFFER_INDEX32);
  c->triCount = static_cast<uint32_t>(mesh.triIndices.size());
  c->lineCount = static_cast<uint32_t>(mesh.lineIndices.size());
  return bgfx::isValid(c->vbh) && bgfx::isValid(c->ibhTri) && bgfx::isValid(c->ibhLine);
}

static bool RebuildMeshBuffers(AgisBgfxPreviewContextImpl* c) {
  if (!c) {
    return false;
  }
  std::vector<PosTexColorVertex> verts;
  std::vector<uint32_t> triIdx;
  std::vector<uint32_t> lineIdx;
  if (!BuildMesh(c->cachedModel, c->pseudoPbrEnabled, c->pbrViewMode, &c->cpuMaps, &verts, &triIdx, &lineIdx, nullptr)) {
    return false;
  }
  DestroyMesh(c);
  if (verts.empty() || triIdx.empty()) {
    c->triCount = 0;
    c->lineCount = 0;
    return true;
  }
  const bgfx::Memory* vmem = bgfx::copy(verts.data(), static_cast<uint32_t>(verts.size() * sizeof(PosTexColorVertex)));
  c->vbh = bgfx::createVertexBuffer(vmem, PosTexColorVertex::Layout());
  const bgfx::Memory* tmem = bgfx::copy(triIdx.data(), static_cast<uint32_t>(triIdx.size() * sizeof(uint32_t)));
  c->ibhTri = bgfx::createIndexBuffer(tmem, BGFX_BUFFER_INDEX32);
  const bgfx::Memory* lmem = bgfx::copy(lineIdx.data(), static_cast<uint32_t>(lineIdx.size() * sizeof(uint32_t)));
  c->ibhLine = bgfx::createIndexBuffer(lmem, BGFX_BUFFER_INDEX32);
  c->triCount = static_cast<uint32_t>(triIdx.size());
  c->lineCount = static_cast<uint32_t>(lineIdx.size());
  return bgfx::isValid(c->vbh) && bgfx::isValid(c->ibhTri) && bgfx::isValid(c->ibhLine);
}

static bool BuildMeshOnCpuFromMaps(const ObjPreviewModel& model, bool pseudoPbr, AgisBgfxPbrViewMode mode,
                                   const CpuPbrMaps& maps, int* progressPct, AgisBgfxCpuBuiltMesh* out) {
  static_assert(sizeof(AgisBgfxInterleavedVertex) == sizeof(PosTexColorVertex), "vertex layout mismatch");
  if (!out) {
    return false;
  }
  *out = AgisBgfxCpuBuiltMesh{};
  if (model.vertices.empty() || model.faces.empty()) {
    if (progressPct) {
      *progressPct = 100;
    }
    return true;
  }
  int localPct = 0;
  int* prog = progressPct ? progressPct : &localPct;
  *prog = 0;
  std::vector<PosTexColorVertex> verts;
  std::vector<uint32_t> triIdx;
  std::vector<uint32_t> lineIdx;
  if (!BuildMesh(model, pseudoPbr, mode, &maps, &verts, &triIdx, &lineIdx, prog)) {
    return false;
  }
  if (progressPct) {
    *progressPct = 100;
  }
  out->vertices.resize(verts.size());
  if (!verts.empty()) {
    memcpy(out->vertices.data(), verts.data(), verts.size() * sizeof(PosTexColorVertex));
  }
  out->triIndices = std::move(triIdx);
  out->lineIndices = std::move(lineIdx);
  return true;
}

static bool PrepareOnWorkerImpl(const ObjPreviewModel& model, bool pseudoPbr, AgisBgfxPbrViewMode mode,
                                const AgisBgfxPbrTexturePaths& paths, int* progressPct,
                                AgisBgfxPreviewWorkerPackage* out) {
  if (!out) {
    return false;
  }
  *out = AgisBgfxPreviewWorkerPackage{};
  auto setp = [&](int p) {
    if (progressPct) {
      *progressPct = p;
    }
  };
  setp(3);
  if (!model.primaryMapKdPath.empty()) {
    (void)DecodeFileToCpuImage2D(model.primaryMapKdPath, &out->baseColor);
  }
  setp(12);
  if (!paths.normalPath.empty()) {
    (void)DecodeFileToCpuImage2D(paths.normalPath, &out->normalMap);
  }
  setp(20);
  if (!paths.roughnessPath.empty()) {
    (void)DecodeFileToCpuImage2D(paths.roughnessPath, &out->roughnessMap);
  }
  setp(26);
  if (!paths.metallicPath.empty()) {
    (void)DecodeFileToCpuImage2D(paths.metallicPath, &out->metallicMap);
  }
  setp(32);
  if (!paths.aoPath.empty()) {
    (void)DecodeFileToCpuImage2D(paths.aoPath, &out->aoMap);
  }
  setp(38);
  if (model.vertices.empty() || model.faces.empty()) {
    setp(100);
    return true;
  }
  CpuPbrMaps maps{};
  AttachBorrowCpuMap(&maps.normal, out->normalMap);
  AttachBorrowCpuMap(&maps.roughness, out->roughnessMap);
  AttachBorrowCpuMap(&maps.metallic, out->metallicMap);
  AttachBorrowCpuMap(&maps.ao, out->aoMap);
  return BuildMeshOnCpuFromMaps(model, pseudoPbr, mode, maps, progressPct, &out->mesh);
}

/// 主线程：白底纹理已建好、program/uniform 已就绪之后，从 worker 包或磁盘填充 GPU 纹理与网格。
static bool UploadPreviewTexturesAndMesh(AgisBgfxPreviewContextImpl* c, const ObjPreviewModel& model,
                                         const AgisBgfxPreviewWorkerPackage* workerPkg) {
  c->texture = CreateWhiteTexture2D();
  if (!bgfx::isValid(c->texture)) {
    return false;
  }

  if (workerPkg) {
    auto tryBaseFromPackage = [&]() -> bool {
      if (workerPkg->baseColor.pixels.empty()) {
        return false;
      }
      const bgfx::TextureHandle t0 = CreateBgfxTextureFromCpuImageRef(workerPkg->baseColor);
      const bgfx::TextureHandle t1 = CreateBgfxTextureFromCpuImageRef(workerPkg->baseColor);
      if (bgfx::isValid(t0) && bgfx::isValid(t1)) {
        bgfx::destroy(c->texture);
        c->texture = t0;
        c->texBaseColor = t1;
        return true;
      }
      if (bgfx::isValid(t0)) {
        bgfx::destroy(t0);
      }
      if (bgfx::isValid(t1)) {
        bgfx::destroy(t1);
      }
      return false;
    };
    if (!tryBaseFromPackage() && !model.primaryMapKdPath.empty()) {
      const bgfx::TextureHandle mapTex = LoadMapKdTexture(model.primaryMapKdPath);
      if (bgfx::isValid(mapTex)) {
        bgfx::destroy(c->texture);
        c->texture = mapTex;
        c->texBaseColor = LoadMapKdTexture(model.primaryMapKdPath);
      }
    }
    c->cpuMaps = CpuPbrMaps{};
    auto slot = [&](CpuImage2D* cpu, bgfx::TextureHandle* gpu, const AgisBgfxCpuImage2D& img) {
      if (img.pixels.empty()) {
        return;
      }
      AttachBorrowCpuMap(cpu, img);
      *gpu = CreateBgfxTextureFromCpuImageRef(img);
    };
    slot(&c->cpuMaps.normal, &c->texNormal, workerPkg->normalMap);
    slot(&c->cpuMaps.roughness, &c->texRoughness, workerPkg->roughnessMap);
    slot(&c->cpuMaps.metallic, &c->texMetallic, workerPkg->metallicMap);
    slot(&c->cpuMaps.ao, &c->texAo, workerPkg->aoMap);

    if (!workerPkg->mesh.vertices.empty()) {
      return UploadPrebuiltMeshRef(c, workerPkg->mesh);
    }
    return RebuildMeshBuffers(c);
  }

  if (!model.primaryMapKdPath.empty()) {
    const bgfx::TextureHandle mapTex = LoadMapKdTexture(model.primaryMapKdPath);
    if (bgfx::isValid(mapTex)) {
      bgfx::destroy(c->texture);
      c->texture = mapTex;
      c->texBaseColor = LoadMapKdTexture(model.primaryMapKdPath);
    }
  }
  return RebuildMeshBuffers(c);
}

}  // namespace

static AgisBgfxPreviewContextImpl* AsImpl(AgisBgfxPreviewContext* p) {
  return reinterpret_cast<AgisBgfxPreviewContextImpl*>(p);
}

bool agis_bgfx_preview_prepare_on_worker(const ObjPreviewModel& model, bool pseudoPbr, AgisBgfxPbrViewMode mode,
                                         const AgisBgfxPbrTexturePaths& pbrPaths, int* progressPct,
                                         AgisBgfxPreviewWorkerPackage* out) {
  return PrepareOnWorkerImpl(model, pseudoPbr, mode, pbrPaths, progressPct, out);
}

bool agis_bgfx_preview_init(HWND hwnd, AgisBgfxPreviewContext** ctx, AgisBgfxRendererKind renderer, const ObjPreviewModel& model,
                            AgisBgfxPreviewWorkerPackage* workerPkg, bool pseudoPbr, AgisBgfxPbrViewMode pbrViewMode) {
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
  c->pseudoPbrEnabled = pseudoPbr;
  c->pbrViewMode = pbrViewMode;
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

  bgfx::setViewClear(0, BGFX_CLEAR_COLOR | BGFX_CLEAR_DEPTH, kPreviewBgfxClearRgba, 1.0f, 0);

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

  if (workerPkg) {
    c->workerPayloadKeep = std::make_unique<AgisBgfxPreviewWorkerPackage>(std::move(*workerPkg));
    *workerPkg = AgisBgfxPreviewWorkerPackage{};
  }
  const AgisBgfxPreviewWorkerPackage* pkgForUpload = c->workerPayloadKeep ? c->workerPayloadKeep.get() : nullptr;
  if (!UploadPreviewTexturesAndMesh(c, model, pkgForUpload)) {
    c->workerPayloadKeep.reset();
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
  c->workerPayloadKeep.reset();
  delete c;
}

void agis_bgfx_preview_draw(AgisBgfxPreviewContext* ctx, HWND hwnd, const RECT& viewportPx, float rotX, float rotY, float zoom,
                            bool solid, bool showGrid, bool backfaceCulling) {
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

  bgfx::setViewClear(0, BGFX_CLEAR_COLOR | BGFX_CLEAR_DEPTH, kPreviewBgfxClearRgba, 1.0f, 0);

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

  constexpr uint64_t kWrite = BGFX_STATE_WRITE_RGB | BGFX_STATE_WRITE_A | BGFX_STATE_WRITE_Z | BGFX_STATE_DEPTH_TEST_LESS;
  const uint64_t cullState = backfaceCulling ? BGFX_STATE_CULL_CCW : 0ULL;
  if (bgfx::isValid(impl->vbh)) {
    bgfx::setVertexBuffer(0, impl->vbh);
    if (bgfx::isValid(impl->texture) && bgfx::isValid(impl->s_texColor)) {
      bgfx::setTexture(0, impl->s_texColor, impl->texture);
    }
    if (solid && impl->triCount > 0 && bgfx::isValid(impl->ibhTri)) {
      bgfx::setState(kWrite | cullState | BGFX_STATE_MSAA);
      bgfx::setIndexBuffer(impl->ibhTri, 0, impl->triCount);
      bgfx::submit(0, impl->program);
    }
    // 实体模式不再叠加线框，避免“脏脏的”观感；仅线框模式显示边线。
    if (!solid && impl->lineCount > 0 && bgfx::isValid(impl->ibhLine)) {
      bgfx::setState(kWrite | BGFX_STATE_PT_LINES | BGFX_STATE_MSAA);
      bgfx::setIndexBuffer(impl->ibhLine, 0, impl->lineCount);
      bgfx::submit(0, impl->program);
    }
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

bool agis_bgfx_preview_reload_model(AgisBgfxPreviewContext* ctx, const ObjPreviewModel& model) {
  if (!ctx) {
    return false;
  }
  AgisBgfxPreviewContextImpl* impl = AsImpl(ctx);
  impl->cachedModel = model;
  return RebuildMeshBuffers(impl);
}
