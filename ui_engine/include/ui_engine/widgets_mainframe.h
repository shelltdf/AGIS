#pragma once

/**
 * AGIS 桌面端专用 Widget：与当前 `main.cpp` / `map_engine` 中 HWND、资源 ID、布局一一对应的类型。
 * 通用基元（Window、Label、Splitter、Canvas2D、DialogWindow、PopupMenu 等）见 `widget_core.h`。
 * 与主窗口 HWND 强绑定的部分类型（图层 Dock、地图画布、地图叠层与菜单、图层对话框等）见 `app/ui_private.h`。
 */

#include "ui_engine/export.h"
#include "ui_engine/widget_core.h"

#include <string>

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

/** 顶层菜单栏区域（实现侧：`SetMenu` / `BuildMenu` 绑定的 HMENU 条带）。子节点为若干 `Menu`（横向排列）。 */
class AGIS_UI_API MenuBarWidget : public Widget {
 public:
  MenuBarWidget() = default;

  /**
   * 命中测试用：条带矩形 **或** 当前展开下拉的 `Menu` 完整几何（叠在下方 UI 之上，**不**参与主布局增高）。
   * `origin_*` 为父链上已叠加到本控件左上角的客户区坐标。
   */
  bool containsPointForHitTest(int client_x, int client_y, int origin_x, int origin_y) const;

  void paintEvent(PaintContext& ctx) override;
  void mouseMoveEvent(int client_x, int client_y, unsigned buttons) override;
  void mousePressEvent(int client_x, int client_y, int button) override;
};

/**
 * 菜单栏上的一个顶层菜单项（如 File / Language）。
 * 子节点为 `MenuItem`（下拉项）；展开时 `geometry` 向下延伸以包含下拉区（叠层绘制；**不**推高主壳层布局）。
 */
class AGIS_UI_API Menu : public Widget {
 public:
  Menu() = default;

  void setTitle(std::wstring title);
  const std::wstring& title() const { return title_; }

  bool dropDownOpen() const { return drop_down_open_; }

  /** 由 `RelayoutMenuBarChildren` 调用：在栏内单元格 (x,y,w) 与条带高度 `bar_h` 下同步整控件几何与子项布局。 */
  void syncGeometryWithBarCell(int x, int y, int w, int bar_h);

  void paintEvent(PaintContext& ctx) override;
  void mouseMoveEvent(int client_x, int client_y, unsigned buttons) override;
  void mousePressEvent(int client_x, int client_y, int button) override;

 private:
  friend class App;
  void openDropDownVisual();
  void closeDropDownVisual();

  std::wstring title_;
  bool drop_down_open_{false};
  int bar_strip_h_{28};
};

/** 菜单条目（`Menu` 的子节点）。 */
class AGIS_UI_API MenuItem : public Widget {
 public:
  MenuItem();

  void setText(std::wstring text);
  const std::wstring& text() const { return text_; }

  void setEnabled(bool on) { enabled_ = on; }
  bool enabled() const { return enabled_; }

  void paintEvent(PaintContext& ctx) override;
  void mouseMoveEvent(int client_x, int client_y, unsigned buttons) override;
  void mousePressEvent(int client_x, int client_y, int button) override;

 private:
  std::wstring text_;
  bool enabled_{true};
};

/** 主工具栏（实现侧：`CreateToolbarEx` / `IDC_MAIN_TOOLBAR`）。 */
class AGIS_UI_API ToolBarWidget : public Widget {
 public:
  ToolBarWidget() = default;
  void paintEvent(PaintContext& ctx) override;
  void mouseMoveEvent(int client_x, int client_y, unsigned buttons) override;
  void mousePressEvent(int client_x, int client_y, int button) override;

 private:
  /** 演示文案四段：New / Open / Save / 其余；`-1` 表示未落在栏内。 */
  int toolbar_hover_segment_{-1};
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
