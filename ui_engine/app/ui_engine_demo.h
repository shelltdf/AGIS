#pragma once

/**
 * ui_engine 演示：与操作系统无关的状态摘要等（供平台层或测试使用）。
 * `BuildUiEngineDemoWidgetTree()` 仅返回**空的** `MainFrame`（与 `ui_engine_demo_main` 直接 `make_unique<MainFrame>()` 等价）。
 * Windows 下 `AGIS_BUILD_UI_ENGINE_DEMO` 时 GDI+ 演示壳见 `ui_engine/src/platform/platform_windows.cpp`。
 */

#include <memory>
#include <string>

#include "ui_engine/widgets_all.h"

/** 返回一棵仅含默认 `MainFrame`、无子控件的根树；由入口在 `exec()` 前交给 `App::addRootWidget`。 */
std::unique_ptr<agis::ui::MainFrame> BuildUiEngineDemoWidgetTree();

