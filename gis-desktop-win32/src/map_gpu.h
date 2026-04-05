#pragma once

#include <cstdint>
#include <windows.h>

/** 2D 地图客户区绘制后端（GDI 与 GPU 呈现可切换）。 */
enum class MapRenderBackend { kGdi, kD3d11, kOpenGL };

bool MapGpu_Init(HWND hwnd, MapRenderBackend backend);
void MapGpu_Shutdown(HWND hwnd);
void MapGpu_OnResize(int w, int h);
/** 将自上而下 BGRA8 帧送到当前后端（与 BitBlt 同向：首行为顶行）。 */
bool MapGpu_PresentFrame(HWND hwnd, const uint8_t* bgraTopDown, int w, int h);
MapRenderBackend MapGpu_GetActiveBackend();
