#pragma once

/**
 * ui_engine 演示：与操作系统无关的 Widget 树构建与状态摘要（供各平台壳层调用）。
 * Windows 下原生壳与 GDI+ 与 `wWinMain` 见 `ui_engine/platform_windows.cpp`（`ui_engine_demo` 目标定义 `AGIS_BUILD_UI_ENGINE_DEMO` 时编译）。
 */

#include <memory>
#include <string>

#include "ui_engine/widgets_all.h"

namespace agis::ui {

class App;

/** 构造覆盖 widget_core / widgets_mainframe / ui_private 类型的演示树。 */
std::unique_ptr<MainFrame> BuildUiEngineDemoWidgetTree();

/** 统计子树中的 Widget 节点数（含根）。 */
int CountWidgetsInTree(const Widget* root);

/**
 * 客户区底部状态行（纯文本语义）：节点数、当前 App 平台 backendId、提示文案。
 * backendId 按 ASCII 逐字符扩为 wchar（演示用）。
 */
std::wstring FormatUiEngineDemoStatusLine(const App& app, const Widget* root);

}  // namespace agis::ui
