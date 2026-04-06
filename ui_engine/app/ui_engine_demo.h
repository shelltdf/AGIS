#pragma once

/**
 * ui_engine 演示：构建含主框架壳层（菜单栏 / 工具栏 / 状态栏与多 Dock）的 Widget 树。
 * Windows 下 `AGIS_BUILD_UI_ENGINE_DEMO` 时 GDI+ 演示壳见 `ui_engine/src/platform/platform_windows.cpp`。
 */

#include <memory>

#include "ui_engine/widgets_all.h"

namespace agis::ui {

/**
 * 构造 `MainFrame`：菜单栏、工具栏（`ToolButton` + `Label` 分隔）、多 `DockArea`（缘条可折叠）、
 * 中间 `MapCanvas2D`（网格 / 中键平移 / 滚轮缩放 + 左上快捷键 / 右上可见性 / 左下缩放条叠层）、状态栏（双击演示 Log 提示）。
 */
std::unique_ptr<MainFrame> BuildUiEngineDemoWidgetTree();

/** 按客户区宽高重算演示壳层几何（供 `WM_SIZE` / `App::notifyClientResize` 使用）。 */
void RelayoutMainFrameForClientSize(MainFrame* root, int client_w, int client_h);

/** 工具栏内子控件（如 `ToolButton`）横向排布。 */
void RelayoutToolBarChildren(ToolBarWidget* bar, int bar_w, int bar_h);

}  // namespace agis::ui
