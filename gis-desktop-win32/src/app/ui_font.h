#pragma once

#include <windows.h>

/** 主窗口 WM_CREATE 中创建，WM_DESTROY 中 UiFontShutdown 释放；子窗口控件用 WM_SETFONT 引用同一逻辑字体。 */
HFONT UiGetAppFont();
/** 日志等 monospace 区域；无 Cascadia/Consolas 时回退为应用 UI 字体。 */
HFONT UiGetLogFont();
void UiFontInit();
void UiFontShutdown();
