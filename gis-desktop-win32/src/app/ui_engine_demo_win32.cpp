/**
 * ui_engine 演示 — Windows 入口：平台相关壳（GDI+、演示窗口、消息循环）见 `platform_windows.cpp` 中的
 * `RunUiEngineDemoWin32`；此处仅保留 `wWinMain`。
 */

#include <windows.h>

#include "ui_engine/platform_windows.h"

int APIENTRY wWinMain(HINSTANCE hInst, HINSTANCE, LPWSTR, int show) {
  return agis::ui::RunUiEngineDemoWin32(hInst, show);
}
