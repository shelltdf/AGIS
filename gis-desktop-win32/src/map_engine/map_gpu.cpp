#include "map_engine/map_gpu.h"

#if defined(_WIN32)
#include <bgfx/bgfx.h>

namespace map_gpu_bgfx {
bool Init(HWND hwnd, bgfx::RendererType::Enum preferredRenderer);
void Shutdown();
void OnResize(int w, int h);
bool Present(HWND hwnd, const uint8_t* bgraTopDown, int w, int h);
bool ImGuiMapToolbarHitClient(int clientX, int clientY);
}  // namespace map_gpu_bgfx

namespace map_gpu_d2d {
bool Init(HWND hwnd);
void Shutdown();
void OnResize(int w, int h);
bool Present(HWND hwnd, const uint8_t* bgraTopDown, int w, int h);
}  // namespace map_gpu_d2d
#endif

#include <windows.h>

#include <algorithm>

namespace {

MapRenderBackend g_active = MapRenderBackend::kGdi;
HWND g_hwnd = nullptr;
int g_bufW = 0;
int g_bufH = 0;

}  // namespace

bool MapGpu_Init(HWND hwnd, MapRenderBackend backend) {
  MapGpu_Shutdown(hwnd);
  g_hwnd = hwnd;
  g_active = MapRenderBackend::kGdi;
  g_bufW = 0;
  g_bufH = 0;
  if (!hwnd) {
    return true;
  }
  RECT r{};
  GetClientRect(hwnd, &r);
  g_bufW = r.right - r.left;
  g_bufH = r.bottom - r.top;

  if (backend == MapRenderBackend::kGdi || backend == MapRenderBackend::kGdiPlus) {
    g_active = backend;
    return true;
  }
#if defined(_WIN32)
  if (backend == MapRenderBackend::kD2d) {
    if (map_gpu_d2d::Init(hwnd)) {
      g_active = MapRenderBackend::kD2d;
      return true;
    }
    return false;
  }
  if (backend == MapRenderBackend::kBgfxD3d11) {
    if (map_gpu_bgfx::Init(hwnd, bgfx::RendererType::Direct3D11)) {
      g_active = MapRenderBackend::kBgfxD3d11;
      return true;
    }
    return false;
  }
  if (backend == MapRenderBackend::kBgfxOpenGL) {
    if (map_gpu_bgfx::Init(hwnd, bgfx::RendererType::OpenGL)) {
      g_active = MapRenderBackend::kBgfxOpenGL;
      return true;
    }
    return false;
  }
  if (backend == MapRenderBackend::kBgfxAuto) {
    if (map_gpu_bgfx::Init(hwnd, bgfx::RendererType::Count)) {
      g_active = MapRenderBackend::kBgfxAuto;
      return true;
    }
    return false;
  }
#endif
  return true;
}

void MapGpu_Shutdown(HWND hwnd) {
  (void)hwnd;
#if defined(_WIN32)
  map_gpu_d2d::Shutdown();
  map_gpu_bgfx::Shutdown();
#endif
  g_hwnd = nullptr;
  g_active = MapRenderBackend::kGdi;
  g_bufW = 0;
  g_bufH = 0;
}

void MapGpu_OnResize(int w, int h) {
  g_bufW = w;
  g_bufH = h;
#if defined(_WIN32)
  if (g_active == MapRenderBackend::kD2d) {
    map_gpu_d2d::OnResize(w, h);
    return;
  }
  if (g_active == MapRenderBackend::kBgfxD3d11 || g_active == MapRenderBackend::kBgfxOpenGL ||
      g_active == MapRenderBackend::kBgfxAuto) {
    map_gpu_bgfx::OnResize(w, h);
  }
#endif
}

bool MapGpu_PresentFrame(HWND hwnd, const uint8_t* bgraTopDown, int w, int h) {
  if (!bgraTopDown || w <= 0 || h <= 0) {
    return false;
  }
  if (g_active == MapRenderBackend::kGdi || g_active == MapRenderBackend::kGdiPlus) {
    return false;
  }
#if defined(_WIN32)
  if (g_active == MapRenderBackend::kD2d) {
    return map_gpu_d2d::Present(hwnd, bgraTopDown, w, h);
  }
  if (g_active == MapRenderBackend::kBgfxD3d11 || g_active == MapRenderBackend::kBgfxOpenGL ||
      g_active == MapRenderBackend::kBgfxAuto) {
    return map_gpu_bgfx::Present(hwnd, bgraTopDown, w, h);
  }
#endif
  return false;
}

MapRenderBackend MapGpu_GetActiveBackend() { return g_active; }

bool MapGpu_ImGuiMapToolbarHitClient(int clientX, int clientY) {
#if defined(_WIN32)
  if (g_active == MapRenderBackend::kBgfxD3d11 || g_active == MapRenderBackend::kBgfxOpenGL ||
      g_active == MapRenderBackend::kBgfxAuto) {
    return map_gpu_bgfx::ImGuiMapToolbarHitClient(clientX, clientY);
  }
#endif
  (void)clientX;
  (void)clientY;
  return false;
}
