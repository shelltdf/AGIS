#pragma once

#include <cstdint>
#include <windows.h>

/**
 * 2D 地图客户区绘制后端。
 * - kGdi / kGdiPlus：CPU 光栅化后直接在 WM_PAINT 中呈现（GDI 位图 / GDI+ Bitmap）。
 * - kD2d：CPU 光栅化 BGRA → Direct2D HwndRenderTarget 呈现。
 * - kBgfxD3d11 / kBgfxOpenGL / kBgfxAuto：CPU 光栅化 BGRA → bgfx 贴图全屏四边形（OpenGL/D3D11 由 bgfx 后端决定）。
 */
enum class MapRenderBackend {
  kGdi,
  kGdiPlus,
  kD2d,
  kBgfxD3d11,
  kBgfxOpenGL,
  kBgfxAuto,
};

bool MapGpu_Init(HWND hwnd, MapRenderBackend backend);
void MapGpu_Shutdown(HWND hwnd);
void MapGpu_OnResize(int w, int h);
/** 将自上而下 BGRA8 帧送到当前后端（与 BitBlt 同向：首行为顶行）。 */
bool MapGpu_PresentFrame(HWND hwnd, const uint8_t* bgraTopDown, int w, int h);
MapRenderBackend MapGpu_GetActiveBackend();
/**
 * 客户区坐标是否落在 bgfx 路径下 ImGui「Map」工具栏矩形内（每帧在 Present 中更新）。
 * 用于与中键平移、滚轮缩放互斥；勿使用 IO.WantCaptureMouse，其在无 WM_PAINT 时会陈旧误判。
 */
bool MapGpu_ImGuiMapToolbarHitClient(int clientX, int clientY);
