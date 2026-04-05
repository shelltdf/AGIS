#pragma once

/**
 * AGIS 桌面端专用 Widget：与当前 `main.cpp` / `map_engine` 中 HWND、资源 ID、布局一一对应的类型。
 * 通用基元（Window、Label、Splitter、Canvas2D、DialogWindow、PopupMenu 等）见 `widget_core.h`。
 * 与主窗口 HWND 强绑定的部分类型（图层 Dock、地图画布、地图叠层与菜单、图层对话框等）见 `app/ui_private.h`。
 */

#include "ui_engine/export.h"
#include "ui_engine/widget_core.h"

namespace agis::ui {

/** 停靠区所在边（与本产品 Dock Area 四边模型一致）。 */
enum class DockEdge { kLeft, kRight, kTop, kBottom };

/**
 * 主框架窗口（逻辑根）。
 * 实现侧：`CreateWindowW` 主类 `AGISMainFrame`，内含菜单、工具栏、客户区与状态栏。
 */
class AGIS_UI_API MainFrame : public Window {
 public:
  MainFrame() = default;
  void paintEvent(PaintContext& ctx) override;
};

/** 顶层菜单栏区域（实现侧：`SetMenu` / `BuildMenu` 绑定的 HMENU 条带）。 */
class AGIS_UI_API MenuBarWidget : public Widget {
 public:
  MenuBarWidget() = default;
  void paintEvent(PaintContext& ctx) override;
  void mouseMoveEvent(int client_x, int client_y, unsigned buttons) override;
  void mousePressEvent(int client_x, int client_y, int button) override;
};

/** 主工具栏（实现侧：`CreateToolbarEx` / `IDC_MAIN_TOOLBAR`）。 */
class AGIS_UI_API ToolBarWidget : public Widget {
 public:
  ToolBarWidget() = default;
  void paintEvent(PaintContext& ctx) override;
  void mouseMoveEvent(int client_x, int client_y, unsigned buttons) override;
  void mousePressEvent(int client_x, int client_y, int button) override;
};

/** 状态栏（实现侧：底部 `STATUSCLASSNAME`，双击打开日志）。 */
class AGIS_UI_API StatusBarWidget : public Widget {
 public:
  StatusBarWidget() = default;
  void paintEvent(PaintContext& ctx) override;
};

/**
 * 单侧 Dock 区域容器：折叠按钮条与 Dock View 并列。
 * 实现侧：左侧 `g_hwndLayerStrip`+`g_hwndLayer` 或右侧 `g_hwndPropsStrip`+`g_hwndProps` 所占布局带。
 */
class AGIS_UI_API DockArea : public Widget {
 public:
  DockArea() = default;

  void setDockEdge(DockEdge e) { edge_ = e; }
  DockEdge dockEdge() const { return edge_; }

  void paintEvent(PaintContext& ctx) override;

 private:
  DockEdge edge_{DockEdge::kLeft};
};

/** 折叠按钮区域 / Dock Button Bar（实现侧：`IDC_LAYER_DOCK_STRIP_BTN` / `IDC_PROPS_DOCK_STRIP_BTN`）。 */
class AGIS_UI_API DockButtonStrip : public Widget {
 public:
  DockButtonStrip() = default;
  void paintEvent(PaintContext& ctx) override;
};

/** Dock View：展开时承载具体面板内容（不含缘条）。 */
class AGIS_UI_API DockView : public Widget {
 public:
  DockView() = default;
  void paintEvent(PaintContext& ctx) override;
};

/**
 * 单个 Dock 内容单元（与缘条按钮联动的可折叠面板主体）。
 * 本工程左右各一：图层列表、图层属性。
 */
class AGIS_UI_API DockPanel : public Widget {
 public:
  DockPanel() = default;
  void paintEvent(PaintContext& ctx) override;
};

/** 画布左上角：快捷键说明折叠区（`IDC_MAP_SHORTCUT_TOGGLE` / `IDC_MAP_SHORTCUT_EDIT`）。 */
class AGIS_UI_API ShortcutHelpPanel : public Widget {
 public:
  ShortcutHelpPanel() = default;
  void paintEvent(PaintContext& ctx) override;
};

/** 日志窗口顶层壳（`kLogClass` / `AGISLogWindow`，`ShowLogDialog`）；正文区见 `LogPanel`。 */
class AGIS_UI_API LogWindow : public Window {
 public:
  LogWindow() = default;
  void paintEvent(PaintContext& ctx) override;
};

/** 日志窗口可编辑区与复制（`IDC_LOG_EDIT` / `IDC_LOG_COPY`，宿主 `AGISLogWindow`）。 */
class AGIS_UI_API LogPanel : public Widget {
 public:
  LogPanel() = default;
  void paintEvent(PaintContext& ctx) override;
};

}  // namespace agis::ui
