#pragma once

/**
 * 主程序 / 宿主 HWND 强绑定的 Widget 声明（与 `main.cpp` 资源 ID、子类过程对应）。
 * 基类与共用壳层类型见 `ui_engine/widgets_mainframe.h`；实现见 `ui_engine/widgets_mainframe.cpp`。
 * 需要完整类型表时与 `widgets_mainframe.h` 一并包含本头，或使用 `ui_engine/widgets_all.h`。
 */

#include "ui_engine/export.h"
#include "ui_engine/widgets_mainframe.h"

namespace agis::ui {

/** 左侧图层 Dock 内容（`IDC_LAYER_LIST`，宿主 `AGISLayerPane`）。 */
class AGIS_UI_API LayerDockPanel : public DockPanel {
 public:
  LayerDockPanel() = default;
  void paintEvent(PaintContext& ctx) override;
};

/** 右侧图层属性 Dock 内容（`AGISPropsPane`，含驱动/数据源 EDIT 与按钮）。 */
class AGIS_UI_API PropsDockPanel : public DockPanel {
 public:
  PropsDockPanel() = default;
  void paintEvent(PaintContext& ctx) override;
};

/**
 * 地图宿主 2D 视图：栅格/矢量绘制、中键平移、滚轮缩放等。
 * 实现侧：`g_hwndMap`，类 `AGISMapHost`。
 */
class AGIS_UI_API MapCanvas2D : public Canvas2D {
 public:
  MapCanvas2D() = default;
  void paintEvent(PaintContext& ctx) override;
  void mouseMoveEvent(int client_x, int client_y, unsigned buttons) override;

  /** 客户区坐标；`delta` 为 Win32 `GET_WHEEL_DELTA_WPARAM` 有符号值。 */
  void wheelAt(int client_x, int client_y, int wheel_delta);
  void middleDown(int client_x, int client_y);
  void middleUp();

  void setShowGrid(bool on) { show_grid_ = on; }
  bool showGrid() const { return show_grid_; }

  /** 以画布几何中心为锚点缩放（演示左下缩放条 ±）。 */
  void zoomAtCenter(double factor);

  /** 演示：重置平移与比例为 100%。 */
  void fitViewDemo();

  /** 演示：将视图原点对齐到视口中心附近（无图层数据时的占位语义）。 */
  void originDemo();

  /** 比例设为 100%，并以视口中心为锚点（与滚轮缩放同一套平移修正）。 */
  void resetZoom100();

 private:
  double pan_x_{0.0};
  double pan_y_{0.0};
  double scale_{1.0};
  bool show_grid_{true};
  bool mdrag_{false};
  int mlast_x_{0};
  int mlast_y_{0};
};

/**
 * 「添加图层 — 数据源」对话框（`kLayerDriverDlgClass` / `LayerDriverDlgProc`，控件 `IDC_LAYER_DRV_*`、`IDC_LAYER_URL`、确定/取消）。
 */
class AGIS_UI_API LayerDriverDialog : public DialogWindow {
 public:
  LayerDriverDialog() = default;
  void paintEvent(PaintContext& ctx) override;
};

/** 图层列表上右键菜单（`LayerListSubclassProc` → `ID_LAYER_CTX_*`）。 */
class AGIS_UI_API LayerListContextMenu : public PopupMenu {
 public:
  LayerListContextMenu() = default;
  void paintEvent(PaintContext& ctx) override;
};

/** 画布右上角：要素可见性面板（`IDC_MAP_VIS_TOGGLE` / `IDC_MAP_VIS_GRID`）。 */
class AGIS_UI_API LayerVisibilityPanel : public Widget {
 public:
  LayerVisibilityPanel() = default;
  void paintEvent(PaintContext& ctx) override;
  void mouseMoveEvent(int client_x, int client_y, unsigned buttons) override;
  void mousePressEvent(int client_x, int client_y, int button) override;
};

/** 画布左下：比例与适应/原点/还原、加减（`IDC_MAP_SCALE_TEXT` 等）。 */
class AGIS_UI_API MapZoomBar : public Widget {
 public:
  MapZoomBar() = default;
  void paintEvent(PaintContext& ctx) override;
  void mouseMoveEvent(int client_x, int client_y, unsigned buttons) override;
  void mousePressEvent(int client_x, int client_y, int button) override;
};

/** 画布右下：半透明提示条（`UiPaintMapHintOverlay` 绘制区）。 */
class AGIS_UI_API MapHintOverlay : public Widget {
 public:
  MapHintOverlay() = default;
  void paintEvent(PaintContext& ctx) override;
};

/** 无 GDAL 等时在地图区中央的提示卡片（`UiPaintMapCenterHint`）。 */
class AGIS_UI_API MapCenterHintOverlay : public Widget {
 public:
  MapCenterHintOverlay() = default;
  void paintEvent(PaintContext& ctx) override;
};

/**
 * 地图区右键菜单（预留：`MapHostProc` 当前对 `WM_CONTEXTMENU` 直接 `return 0`，无弹出项）。
 */
class AGIS_UI_API MapContextMenu : public PopupMenu {
 public:
  MapContextMenu() = default;
  void paintEvent(PaintContext& ctx) override;
};

}  // namespace agis::ui
