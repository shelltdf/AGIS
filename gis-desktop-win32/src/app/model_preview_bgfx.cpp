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
  bgfx::UniformHandle s_texColor = BGFX_INVALID_HANDLE;
  uint32_t triCount = 0;
  uint32_t lineCount = 0;
  uint32_t lastResetW = 0;
  uint32_t lastResetH = 0;
  bgfx::RendererType::Enum renderer = bgfx::RendererType::Count;
};

static bgfx::RendererType::Enum ToBgfxType(AgisBgfxRendererKind k) {
  return (k == AgisBgfxRendererKind::kOpenGL) ? bgfx::RendererType::OpenGL : bgfx::RendererType::Direct3D11;
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
  if (bgfx::isValid(c->s_texColor)) {
    bgfx::destroy(c->s_texColor);
    c->s_texColor = BGFX_INVALID_HANDLE;
  }
}

static bool BuildMesh(const ObjPreviewModel& model, std::vector<PosTexColorVertex>* verts, std::vector<uint32_t>* triIdx,
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
    const uint32_t abgr = PackAbgr8(cr, cg, cb, 1.0f);
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
      PosTexColorVertex p{};
      p.x = (v.x - model.center.x) * inv;
      p.y = (v.y - model.center.y) * inv;
      p.z = (v.z - model.center.z) * inv;
      p.u = u;
      p.v = 1.0f - tv;
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
  c->renderer = ToBgfxType(renderer);

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
  init.resolution.reset = BGFX_RESET_VSYNC;
  if (!bgfx::init(init)) {
    delete c;
    return false;
  }

  bgfx::setViewClear(0, BGFX_CLEAR_COLOR | BGFX_CLEAR_DEPTH, 0x1a1e28ff, 1.0f, 0);

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
    }
  }

  std::vector<PosTexColorVertex> verts;
  std::vector<uint32_t> triIdx;
  std::vector<uint32_t> lineIdx;
  if (!BuildMesh(model, &verts, &triIdx, &lineIdx)) {
    DestroyTextureResources(c);
    bgfx::destroy(c->program);
    c->program = BGFX_INVALID_HANDLE;
    bgfx::shutdown();
    delete c;
    return false;
  }

  const bgfx::Memory* vmem = bgfx::copy(verts.data(), static_cast<uint32_t>(verts.size() * sizeof(PosTexColorVertex)));
  c->vbh = bgfx::createVertexBuffer(vmem, PosTexColorVertex::Layout());
  const bgfx::Memory* tmem = bgfx::copy(triIdx.data(), static_cast<uint32_t>(triIdx.size() * sizeof(uint32_t)));
  c->ibhTri = bgfx::createIndexBuffer(tmem, BGFX_BUFFER_INDEX32);
  const bgfx::Memory* lmem = bgfx::copy(lineIdx.data(), static_cast<uint32_t>(lineIdx.size() * sizeof(uint32_t)));
  c->ibhLine = bgfx::createIndexBuffer(lmem, BGFX_BUFFER_INDEX32);
  c->triCount = static_cast<uint32_t>(triIdx.size());
  c->lineCount = static_cast<uint32_t>(lineIdx.size());
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
                            bool solid) {
  if (!ctx || !hwnd) return;
  AgisBgfxPreviewContextImpl* impl = AsImpl(ctx);
  if (!bgfx::isValid(impl->program)) return;

  RECT cr{};
  GetClientRect(hwnd, &cr);
  const uint32_t cw = (std::max)(1L, cr.right - cr.left);
  const uint32_t ch = (std::max)(1L, cr.bottom - cr.top);
  if (cw != impl->lastResetW || ch != impl->lastResetH) {
    bgfx::reset(cw, ch, BGFX_RESET_VSYNC);
    impl->lastResetW = cw;
    impl->lastResetH = ch;
  }

  const int vx = static_cast<int>(viewportPx.left);
  const int vy = static_cast<int>(viewportPx.top);
  const int vw = static_cast<int>((std::max)(1L, viewportPx.right - viewportPx.left));
  const int vh = static_cast<int>((std::max)(1L, viewportPx.bottom - viewportPx.top));
  const float aspect = static_cast<float>(vw) / static_cast<float>(vh);

  bgfx::setViewClear(0, BGFX_CLEAR_COLOR | BGFX_CLEAR_DEPTH, 0x1a1e28ff, 1.0f, 0);

  const bgfx::Caps* caps = bgfx::getCaps();
  float view[16];
  const bx::Vec3 at{0.0f, 0.0f, 0.0f};
  const bx::Vec3 eye{0.0f, 0.0f, -6.0f};
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
  if (impl->lineCount > 0) {
    bgfx::setState(kWrite | BGFX_STATE_PT_LINES | BGFX_STATE_MSAA);
    bgfx::setIndexBuffer(impl->ibhLine, 0, impl->lineCount);
    bgfx::submit(0, impl->program);
  }
  bgfx::frame();
}
