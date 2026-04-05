#pragma once

#include <windows.h>

/** 主窗口 WM_CREATE 中创建，WM_DESTROY 中释放；子窗口控件用 WM_SETFONT 引用同一逻辑字体。 */
HFONT UiGetAppFont();
