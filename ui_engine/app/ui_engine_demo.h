#pragma once

/**
 * ui_engine 演示：构建含主框架壳层（菜单栏 / 工具栏 / 状态栏与多 Dock）的 Widget 树。
 * Windows 下 `AGIS_BUILD_UI_ENGINE_DEMO` 时 GDI+ 演示壳见 `ui_engine/src/platform/platform_windows.cpp`。
 */

#include <memory>

#include "ui_engine/widgets_all.h"

namespace agis::ui {

/** 构造 `MainFrame`：菜单栏、工具栏、状态栏；左 1、右 3、上 1、下 1 共六个 `DockArea`，中间为 `MapCanvas2D`。 */
std::unique_ptr<MainFrame> BuildUiEngineDemoWidgetTree();

/** 按客户区宽高重算演示壳层几何（供 `WM_SIZE` / `App::notifyClientResize` 使用）。 */
void RelayoutMainFrameForClientSize(MainFrame* root, int client_w, int client_h);

}  // namespace agis::ui
