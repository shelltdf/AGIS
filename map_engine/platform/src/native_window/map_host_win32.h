#pragma once

#include "map_engine/export.h"

#include <windows.h>

#include <vector>

/**
 * 地图宿主（``AGISMapHost`` / ``kMapClass``）的 Win32 窗口过程与相关客户区渲染辅助。
 * 实现位于同目录的 ``map_host_win32.cpp``，与 ``NativeWindowWin32`` 同属 ``platform/src/native_window``，负责本地窗口消息与叠加控件布局。
 */
AGIS_MAP_ENGINE_API LRESULT CALLBACK MapHostProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam);

/** 将当前地图客户区渲染为自上而下 BGRA（供截图等）；与 ``WM_PAINT`` 中 D2D/bgfx 路径无关。 */
bool MapHostRenderClientToTopDownBgra(HWND hwnd, const RECT& client, std::vector<std::uint8_t>* outPixels);
