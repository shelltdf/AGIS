/**
 * ui_engine 演示（可移植）：`BuildUiEngineDemoWidgetTree` 与状态摘要字符串。
 * 不包含任何操作系统 API；窗口、消息循环与 GDI+ 由平台层实现（见 ui_engine_demo_main.cpp）。
 */

#include "ui_engine_demo.h"

#include "ui_engine/app.h"

#include <string>

std::unique_ptr<agis::ui::MainFrame> BuildUiEngineDemoWidgetTree()
 { 
  return std::make_unique<agis::ui::MainFrame>();
 }
