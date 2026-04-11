#include "map_engine/map_gpu.h"

#if defined(_WIN32)

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstring>

#include <windows.h>

#include <bgfx/bgfx.h>
#include <bgfx/embedded_shader.h>
#include <bx/bx.h>
#include <bx/math.h>

#include "imgui.h"

#include "fs_debugdraw_fill_texture.bin.h"
#include "vs_debugdraw_fill_texture.bin.h"

#include "core/resource.h"
#include "map_engine/map_engine.h"

namespace map_gpu_bgfx {

namespace {

bool g_inited = false;
bool g_bgfxDeviceReady = false;
HWND g_hwnd = nullptr;
int g_resetW = 0;
int g_resetH = 0;
uint32_t g_texW = 0;
uint32_t g_texH = 0;

bgfx::ProgramHandle g_program = BGFX_INVALID_HANDLE;
bgfx::TextureHandle g_tex = BGFX_INVALID_HANDLE;
bgfx::VertexBufferHandle g_quadVbh = BGFX_INVALID_HANDLE;
bgfx::UniformHandle g_s_texColor = BGFX_INVALID_HANDLE;
bgfx::RendererType::Enum g_renderer = bgfx::RendererType::Count;

uint32_t g_resetFlags = BGFX_RESET_VSYNC;
RECT g_imguiMapToolbarClient{};

struct PosTexColorVertex {
  float x = 0.f;
  float y = 0.f;
  float z = 0.f;
  float u = 0.f;
  float v = 0.f;
  uint32_t abgr = 0xffffffffu;

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

static const bgfx::EmbeddedShader kEmbeddedShaders[] = {
    BGFX_EMBEDDED_SHADER(vs_debugdraw_fill_texture),
    BGFX_EMBEDDED_SHADER(fs_debugdraw_fill_texture),
    BGFX_EMBEDDED_SHADER_END(),
};

void ReleaseGpuResources() {
  if (bgfx::isValid(g_quadVbh)) {
    bgfx::destroy(g_quadVbh);
    g_quadVbh = BGFX_INVALID_HANDLE;
  }
  if (bgfx::isValid(g_tex)) {
    bgfx::destroy(g_tex);
    g_tex = BGFX_INVALID_HANDLE;
  }
  g_texW = 0;
  g_texH = 0;
  if (bgfx::isValid(g_s_texColor)) {
    bgfx::destroy(g_s_texColor);
    g_s_texColor = BGFX_INVALID_HANDLE;
  }
  if (bgfx::isValid(g_program)) {
    bgfx::destroy(g_program);
    g_program = BGFX_INVALID_HANDLE;
  }
}

void DestroyAll() {
  SetRectEmpty(&g_imguiMapToolbarClient);
  if (g_inited) {
    imguiDestroy();
  }
  g_inited = false;
  ReleaseGpuResources();
  if (g_bgfxDeviceReady) {
    bgfx::shutdown();
    g_bgfxDeviceReady = false;
  }
  g_hwnd = nullptr;
  g_resetW = 0;
  g_resetH = 0;
  g_renderer = bgfx::RendererType::Count;
}

bool PickRenderer(bgfx::RendererType::Enum* out) {
  bgfx::RendererType::Enum supported[16];
  const uint8_t n = bgfx::getSupportedRenderers(BX_COUNTOF(supported), supported);
  const bgfx::RendererType::Enum prefer[] = {
      bgfx::RendererType::Direct3D11,
      bgfx::RendererType::Direct3D12,
      bgfx::RendererType::Vulkan,
      bgfx::RendererType::OpenGL,
  };
  for (bgfx::RendererType::Enum p : prefer) {
    for (uint8_t i = 0; i < n; ++i) {
      if (supported[i] == p) {
        *out = p;
        return true;
      }
    }
  }
  return false;
}

bool EnsureTexture(uint32_t w, uint32_t h) {
  if (w == 0 || h == 0) {
    return false;
  }
  if (bgfx::isValid(g_tex) && g_texW == w && g_texH == h) {
    return true;
  }
  if (bgfx::isValid(g_tex)) {
    bgfx::destroy(g_tex);
    g_tex = BGFX_INVALID_HANDLE;
  }
  g_tex = bgfx::createTexture2D(static_cast<uint16_t>(w), static_cast<uint16_t>(h), false, 1, bgfx::TextureFormat::BGRA8,
                                BGFX_TEXTURE_NONE);
  if (!bgfx::isValid(g_tex)) {
    return false;
  }
  g_texW = w;
  g_texH = h;
  return true;
}

static uint8_t PackMouseButtons() {
  uint8_t mask = 0u;
  if ((GetAsyncKeyState(VK_LBUTTON) & 0x8000) != 0) {
    mask |= IMGUI_MBUT_LEFT;
  }
  if ((GetAsyncKeyState(VK_RBUTTON) & 0x8000) != 0) {
    mask |= IMGUI_MBUT_RIGHT;
  }
  if ((GetAsyncKeyState(VK_MBUTTON) & 0x8000) != 0) {
    mask |= IMGUI_MBUT_MIDDLE;
  }
  return mask;
}

void RenderImGuiToolbar(HWND mapHwnd) {
  MapEngine& eng = MapEngine::Instance();
  ImGui::SetNextWindowPos(ImVec2(8, 8), ImGuiCond_FirstUseEver);
  ImGui::SetNextWindowBgAlpha(0.92f);
  ImGuiWindowFlags wflags = ImGuiWindowFlags_AlwaysAutoResize | ImGuiWindowFlags_NoSavedSettings;
  if (!ImGui::Begin("Map##agis_bgfx_map", nullptr, wflags)) {
    SetRectEmpty(&g_imguiMapToolbarClient);
    ImGui::End();
    return;
  }
  if (ImGui::CollapsingHeader("Shortcuts###sc", ImGuiTreeNodeFlags_DefaultOpen)) {
      ImGui::TextUnformatted(
          "MMB drag: pan\nWheel: zoom (cursor anchor)\nLower-left: fit / origin / reset / +/-\nNo global shortcuts yet.");
      if (ImGui::SmallButton(eng.IsMapShortcutHelpExpanded() ? "Hide details" : "Show details")) {
        PostMessageW(mapHwnd, WM_COMMAND, MAKEWPARAM(IDC_MAP_SHORTCUT_TOGGLE, BN_CLICKED), 0);
      }
    }
    if (ImGui::CollapsingHeader("Visibility", ImGuiTreeNodeFlags_DefaultOpen)) {
      bool grid = eng.Document().GetShowLatLonGrid();
      if (ImGui::Checkbox("Show lat/lon grid", &grid)) {
        eng.Document().SetShowLatLonGrid(grid);
        InvalidateRect(mapHwnd, nullptr, FALSE);
      }
    }
    ImGui::Separator();
    if (ImGui::Button("Fit")) {
      PostMessageW(mapHwnd, WM_COMMAND, MAKEWPARAM(IDC_MAP_FIT, BN_CLICKED), 0);
    }
    ImGui::SameLine();
    if (ImGui::Button("Origin")) {
      PostMessageW(mapHwnd, WM_COMMAND, MAKEWPARAM(IDC_MAP_ORIGIN, BN_CLICKED), 0);
    }
    ImGui::SameLine();
    if (ImGui::Button("Reset")) {
      PostMessageW(mapHwnd, WM_COMMAND, MAKEWPARAM(IDC_MAP_RESET, BN_CLICKED), 0);
    }
    wchar_t scaleBuf[32]{};
    GetWindowTextW(GetDlgItem(mapHwnd, IDC_MAP_SCALE_TEXT), scaleBuf, 32);
    char scaleUtf8[64]{};
    WideCharToMultiByte(CP_UTF8, 0, scaleBuf, -1, scaleUtf8, static_cast<int>(sizeof(scaleUtf8)), nullptr, nullptr);
    ImGui::Text("Scale: %s", scaleUtf8[0] ? scaleUtf8 : "—");
    if (ImGui::Button(" - ")) {
      PostMessageW(mapHwnd, WM_COMMAND, MAKEWPARAM(IDC_MAP_ZOOM_OUT, BN_CLICKED), 0);
    }
    ImGui::SameLine();
    if (ImGui::Button(" + ")) {
      PostMessageW(mapHwnd, WM_COMMAND, MAKEWPARAM(IDC_MAP_ZOOM_IN, BN_CLICKED), 0);
    }
    const ImVec2 wp = ImGui::GetWindowPos();
    const ImVec2 ws = ImGui::GetWindowSize();
    g_imguiMapToolbarClient.left = static_cast<LONG>(std::floor(wp.x));
    g_imguiMapToolbarClient.top = static_cast<LONG>(std::floor(wp.y));
    g_imguiMapToolbarClient.right = static_cast<LONG>(std::ceil(wp.x + ws.x));
    g_imguiMapToolbarClient.bottom = static_cast<LONG>(std::ceil(wp.y + ws.y));
  ImGui::End();
}

}  // namespace

bool Init(HWND hwnd, bgfx::RendererType::Enum preferredRenderer) {
  if (!hwnd) {
    return false;
  }
  DestroyAll();

  RECT cr{};
  GetClientRect(hwnd, &cr);
  g_resetW = (std::max)(1, static_cast<int>(cr.right - cr.left));
  g_resetH = (std::max)(1, static_cast<int>(cr.bottom - cr.top));
  g_hwnd = hwnd;

  if (preferredRenderer != bgfx::RendererType::Count) {
    bgfx::RendererType::Enum supported[16];
    const uint8_t n = bgfx::getSupportedRenderers(BX_COUNTOF(supported), supported);
    bool ok = false;
    for (uint8_t i = 0; i < n; ++i) {
      if (supported[i] == preferredRenderer) {
        ok = true;
        break;
      }
    }
    if (!ok) {
      g_hwnd = nullptr;
      return false;
    }
    g_renderer = preferredRenderer;
  } else {
    if (!PickRenderer(&g_renderer)) {
      g_hwnd = nullptr;
      return false;
    }
  }

  g_resetFlags = (g_renderer == bgfx::RendererType::OpenGL) ? BGFX_RESET_NONE : BGFX_RESET_VSYNC;

  bgfx::Init init;
  init.type = g_renderer;
  init.platformData.ndt = nullptr;
  init.platformData.nwh = hwnd;
  init.platformData.context = nullptr;
  init.platformData.backBuffer = nullptr;
  init.platformData.backBufferDS = nullptr;
  init.resolution.width = static_cast<uint32_t>(g_resetW);
  init.resolution.height = static_cast<uint32_t>(g_resetH);
  init.resolution.reset = g_resetFlags;
  if (!bgfx::init(init)) {
    g_hwnd = nullptr;
    g_renderer = bgfx::RendererType::Count;
    return false;
  }
  g_bgfxDeviceReady = true;

  bgfx::ShaderHandle vsh = bgfx::createEmbeddedShader(kEmbeddedShaders, g_renderer, "vs_debugdraw_fill_texture");
  bgfx::ShaderHandle fsh = bgfx::createEmbeddedShader(kEmbeddedShaders, g_renderer, "fs_debugdraw_fill_texture");
  if (!bgfx::isValid(vsh) || !bgfx::isValid(fsh)) {
    if (bgfx::isValid(vsh)) {
      bgfx::destroy(vsh);
    }
    if (bgfx::isValid(fsh)) {
      bgfx::destroy(fsh);
    }
    DestroyAll();
    return false;
  }
  g_program = bgfx::createProgram(vsh, fsh, true);
  if (!bgfx::isValid(g_program)) {
    DestroyAll();
    return false;
  }

  g_s_texColor = bgfx::createUniform("s_texColor", bgfx::UniformType::Sampler);
  if (!bgfx::isValid(g_s_texColor)) {
    DestroyAll();
    return false;
  }

  PosTexColorVertex quad[4] = {
      {-1.0f, -1.0f, 0.0f, 0.0f, 1.0f, 0xffffffffu},
      {1.0f, -1.0f, 0.0f, 1.0f, 1.0f, 0xffffffffu},
      {-1.0f, 1.0f, 0.0f, 0.0f, 0.0f, 0xffffffffu},
      {1.0f, 1.0f, 0.0f, 1.0f, 0.0f, 0xffffffffu},
  };
  const bgfx::Memory* vmem = bgfx::copy(quad, static_cast<uint32_t>(sizeof(quad)));
  g_quadVbh = bgfx::createVertexBuffer(vmem, PosTexColorVertex::Layout());
  if (!bgfx::isValid(g_quadVbh)) {
    DestroyAll();
    return false;
  }

  imguiCreate(16.0f, nullptr);
  g_inited = true;
  return true;
}

void Shutdown() {
  DestroyAll();
}

void OnResize(int w, int h) {
  if (w <= 0 || h <= 0 || !g_hwnd) {
    return;
  }
  g_resetW = w;
  g_resetH = h;
  if (g_bgfxDeviceReady) {
    bgfx::reset(static_cast<uint32_t>(w), static_cast<uint32_t>(h), g_resetFlags);
  }
}

bool Present(HWND hwnd, const uint8_t* bgraTopDown, int w, int h) {
  if (!g_inited || !g_bgfxDeviceReady || !hwnd || !bgraTopDown || w <= 0 || h <= 0) {
    return false;
  }

  const uint32_t uw = static_cast<uint32_t>(w);
  const uint32_t uh = static_cast<uint32_t>(h);
  if (!EnsureTexture(uw, uh)) {
    return false;
  }

  const bgfx::Memory* mem = bgfx::alloc(static_cast<uint32_t>(static_cast<size_t>(w) * static_cast<size_t>(h) * 4u));
  if (!mem) {
    return false;
  }
  std::memcpy(mem->data, bgraTopDown, mem->size);
  bgfx::updateTexture2D(g_tex, 0, 0, 0, 0, static_cast<uint16_t>(w), static_cast<uint16_t>(h), mem, UINT16_MAX);

  const uint16_t vw = static_cast<uint16_t>((std::max)(1, g_resetW));
  const uint16_t vh = static_cast<uint16_t>((std::max)(1, g_resetH));

  bgfx::setViewRect(0, 0, 0, vw, vh);
  // bgfx::setViewClear 颜色为 0xRRGGBBAA（与模型预览一致）
  constexpr uint32_t kClear = 0x2e2e48ffu;
  bgfx::setViewClear(0, BGFX_CLEAR_COLOR | BGFX_CLEAR_DEPTH, kClear, 1.0f, 0);
  bgfx::touch(0);

  float viewM[16];
  bx::mtxIdentity(viewM);
  float projM[16];
  const bgfx::Caps* caps = bgfx::getCaps();
  bx::mtxOrtho(projM, -1.0f, 1.0f, -1.0f, 1.0f, 0.0f, 100.0f, 0.0f, caps->homogeneousDepth);
  bgfx::setViewTransform(0, viewM, projM);

  float id[16];
  bx::mtxIdentity(id);
  bgfx::setTransform(id);

  if (!bgfx::isValid(g_quadVbh)) {
    return false;
  }
  bgfx::setVertexBuffer(0, g_quadVbh);
  bgfx::setTexture(0, g_s_texColor, g_tex);
  constexpr uint64_t kState =
      BGFX_STATE_WRITE_RGB | BGFX_STATE_WRITE_A | BGFX_STATE_DEPTH_TEST_ALWAYS | BGFX_STATE_PT_TRISTRIP;
  bgfx::setState(kState);
  bgfx::submit(0, g_program);

  POINT pt{};
  GetCursorPos(&pt);
  ScreenToClient(hwnd, &pt);
  const int mx = static_cast<int>(pt.x);
  const int my = static_cast<int>(pt.y);
  imguiBeginFrame(mx, my, PackMouseButtons(), 0, vw, vh, -1, 254);
  RenderImGuiToolbar(hwnd);
  imguiEndFrame();

  bgfx::frame();
  return true;
}

bool ImGuiMapToolbarHitClient(int clientX, int clientY) {
  if (!g_inited) {
    return false;
  }
  const POINT pt{clientX, clientY};
  return PtInRect(&g_imguiMapToolbarClient, pt) != FALSE;
}

}  // namespace map_gpu_bgfx

#else  // !_WIN32

namespace map_gpu_bgfx {

bool Init(HWND, bgfx::RendererType::Enum) { return false; }
void Shutdown() {}
void OnResize(int, int) {}
bool Present(HWND, const uint8_t*, int, int) { return false; }
bool ImGuiMapToolbarHitClient(int, int) { return false; }

}  // namespace map_gpu_bgfx

#endif
