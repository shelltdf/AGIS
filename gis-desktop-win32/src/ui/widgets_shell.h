#pragma once

#include "ui/widgets.h"

#include <string>

namespace agis::ui {

/** 停靠区所在边（与 Dock Area 四边模型一致）。 */
enum class DockEdge { kLeft, kRight, kTop, kBottom };

/** 分割条方向：竖条调节左右宽度，横条调节上下高度。 */
enum class SplitterOrientation { kVertical, kHorizontal };

// --- 主窗口壳层（对应 main.cpp：菜单栏、工具栏、状态栏、中央区布局）---

/**
 * 主框架窗口（逻辑根）。
 * 实现侧：`CreateWindowW` 主类 `AGISMainFrame`，内含菜单、工具栏、客户区与状态栏。
 */
class MainFrame : public Window {
 public:
  MainFrame() = default;
  void paintEvent(PaintContext& ctx) override;
};

/** 顶层菜单栏区域（实现侧：`SetMenu` / `BuildMenu` 绑定的 HMENU 条带）。 */
class MenuBarWidget : public Widget {
 public:
  MenuBarWidget() = default;
  void paintEvent(PaintContext& ctx) override;
};

/** 主工具栏（实现侧：`CreateToolbarEx` / `IDC_MAIN_TOOLBAR`）。 */
class ToolBarWidget : public Widget {
 public:
  ToolBarWidget() = default;
  void paintEvent(PaintContext& ctx) override;
};

/** 状态栏（实现侧：底部 `STATUSCLASSNAME`，双击打开日志）。 */
class StatusBarWidget : public Widget {
 public:
  StatusBarWidget() = default;
  void paintEvent(PaintContext& ctx) override;
};

// --- Dock 模型（与产品 GUI 规则：Dock Area = 缘条 + Dock View）---

/**
 * 单侧 Dock 区域容器：折叠按钮条与 Dock View 并列。
 * 实现侧：左侧 `g_hwndLayerStrip`+`g_hwndLayer` 或右侧 `g_hwndPropsStrip`+`g_hwndProps` 所占布局带。
 */
class DockArea : public Widget {
 public:
  DockArea() = default;

  void setDockEdge(DockEdge e) { edge_ = e; }
  DockEdge dockEdge() const { return edge_; }

  void paintEvent(PaintContext& ctx) override;

 private:
  DockEdge edge_{DockEdge::kLeft};
};

/** 折叠按钮区域 / Dock Button Bar（实现侧：`IDC_LAYER_DOCK_STRIP_BTN` / `IDC_PROPS_DOCK_STRIP_BTN`）。 */
class DockButtonStrip : public Widget {
 public:
  DockButtonStrip() = default;
  void paintEvent(PaintContext& ctx) override;
};

/** Dock View：展开时承载具体面板内容（不含缘条）。 */
class DockView : public Widget {
 public:
  DockView() = default;
  void paintEvent(PaintContext& ctx) override;
};

/**
 * 单个 Dock 内容单元（规则中的「Dock」：与缘条按钮联动的可折叠面板主体）。
 * 本工程左右各一：图层列表、图层属性。
 */
class DockPanel : public Widget {
 public:
  DockPanel() = default;
  void paintEvent(PaintContext& ctx) override;
};

/** 左侧图层 Dock 内容：自绘列表等（`IDC_LAYER_LIST`，宿主 `AGISLayerPane`）。 */
class LayerDockPanel : public DockPanel {
 public:
  LayerDockPanel() = default;
  void paintEvent(PaintContext& ctx) override;
};

/** 右侧图层属性 Dock 内容（`AGISPropsPane`，含驱动/数据源 EDIT 与按钮）。 */
class PropsDockPanel : public DockPanel {
 public:
  PropsDockPanel() = default;
  void paintEvent(PaintContext& ctx) override;
};

/** 主行分割条：地图与 Dock 之间可拖拽调宽（`kSplitterW` 逻辑）。 */
class Splitter : public Widget {
 public:
  Splitter() = default;

  void setOrientation(SplitterOrientation o) { orient_ = o; }
  SplitterOrientation orientation() const { return orient_; }

  void paintEvent(PaintContext& ctx) override;

 private:
  SplitterOrientation orient_{SplitterOrientation::kVertical};
};

// --- 地图 / 2D 画布（map_engine：MapHostProc / AGISMapHost）---

/** 通用二维画布表面（平移/缩放语义由宿主实现）。 */
class Canvas2D : public Widget {
 public:
  Canvas2D() = default;
  void paintEvent(PaintContext& ctx) override;
};

/**
 * 地图宿主 2D 视图：栅格/矢量绘制、中键平移、滚轮缩放等。
 * 实现侧：`g_hwndMap`，类 `AGISMapHost`。
 */
class MapCanvas2D : public Canvas2D {
 public:
  MapCanvas2D() = default;
  void paintEvent(PaintContext& ctx) override;
};

/** 画布左上角：快捷键说明折叠区（`IDC_MAP_SHORTCUT_TOGGLE` / `IDC_MAP_SHORTCUT_EDIT`）。 */
class ShortcutHelpPanel : public Widget {
 public:
  ShortcutHelpPanel() = default;
  void paintEvent(PaintContext& ctx) override;
};

/** 画布右上角：要素可见性面板（`IDC_MAP_VIS_TOGGLE` / `IDC_MAP_VIS_GRID`）。 */
class LayerVisibilityPanel : public Widget {
 public:
  LayerVisibilityPanel() = default;
  void paintEvent(PaintContext& ctx) override;
};

/** 画布左下：比例与适应/原点/还原、加减（`IDC_MAP_SCALE_TEXT` 等）。 */
class MapZoomBar : public Widget {
 public:
  MapZoomBar() = default;
  void paintEvent(PaintContext& ctx) override;
};

/** 画布右下：半透明提示条（`UiPaintMapHintOverlay` 绘制区）。 */
class MapHintOverlay : public Widget {
 public:
  MapHintOverlay() = default;
  void paintEvent(PaintContext& ctx) override;
};

/** 无 GDAL 等时在地图区中央的提示卡片（`UiPaintMapCenterHint`）。 */
class MapCenterHintOverlay : public Widget {
 public:
  MapCenterHintOverlay() = default;
  void paintEvent(PaintContext& ctx) override;
};

// --- 对话框（map_engine：模态添加图层等）---

/**
 * 对话框窗口基类（实现侧：`WS_POPUP` | `WS_CAPTION` | `WS_SYSMENU`，owner 上模态消息循环）。
 */
class DialogWindow : public Window {
 public:
  DialogWindow() = default;
  void paintEvent(PaintContext& ctx) override;
};

/**
 * 「添加图层 — 数据源」对话框（`kLayerDriverDlgClass` / `LayerDriverDlgProc`，控件 `IDC_LAYER_DRV_*`、`IDC_LAYER_URL`、确定/取消）。
 */
class LayerDriverDialog : public DialogWindow {
 public:
  LayerDriverDialog() = default;
  void paintEvent(PaintContext& ctx) override;
};

/** 日志窗口顶层壳（`kLogClass` / `AGISLogWindow`，`ShowLogDialog`）；正文区见 `LogPanel`。 */
class LogWindow : public Window {
 public:
  LogWindow() = default;
  void paintEvent(PaintContext& ctx) override;
};

// --- 菜单：顶层 HMENU 与右键弹出 ---

/**
 * 弹出菜单抽象（实现侧：`CreatePopupMenu` / `TrackPopupMenu`）。
 * 不具持久矩形，仅占位类型以便与 HMENU 生命周期在设计上对齐。
 */
class PopupMenu : public Widget {
 public:
  PopupMenu() = default;
  void paintEvent(PaintContext& ctx) override;
};

/** 图层列表上右键菜单（`LayerListSubclassProc` → `ID_LAYER_CTX_*`）。 */
class LayerListContextMenu : public PopupMenu {
 public:
  LayerListContextMenu() = default;
  void paintEvent(PaintContext& ctx) override;
};

/**
 * 地图区右键菜单（预留：`MapHostProc` 当前对 `WM_CONTEXTMENU` 直接 `return 0`，无弹出项）。
 */
class MapContextMenu : public PopupMenu {
 public:
  MapContextMenu() = default;
  void paintEvent(PaintContext& ctx) override;
};

/** 日志窗口可编辑区与复制（`IDC_LOG_EDIT` / `IDC_LOG_COPY`，宿主 `AGISLogWindow`）。 */
class LogPanel : public Widget {
 public:
  LogPanel() = default;
  void paintEvent(PaintContext& ctx) override;
};

}  // namespace agis::ui
