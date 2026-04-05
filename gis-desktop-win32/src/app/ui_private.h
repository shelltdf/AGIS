#pragma once

/**
 * 主程序 / 宿主 HWND 强绑定的 Widget 声明（与 `main.cpp` 资源 ID、子类过程对应）。
 * 基类与共用壳层类型见 `ui_engine/widgets_shell.h`；实现见 `ui_engine/widgets_shell.cpp`。
 * 需要完整类型表时与 `widgets_shell.h` 一并包含本头，或使用 `ui_engine/widgets_all.h`。
 */

#include "ui_engine/widgets_shell.h"

namespace agis::ui {

/** 左侧图层 Dock 内容（`IDC_LAYER_LIST`，宿主 `AGISLayerPane`）。 */
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

/**
 * 地图宿主 2D 视图：栅格/矢量绘制、中键平移、滚轮缩放等。
 * 实现侧：`g_hwndMap`，类 `AGISMapHost`。
 */
class MapCanvas2D : public Canvas2D {
 public:
  MapCanvas2D() = default;
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

/** 图层列表上右键菜单（`LayerListSubclassProc` → `ID_LAYER_CTX_*`）。 */
class LayerListContextMenu : public PopupMenu {
 public:
  LayerListContextMenu() = default;
  void paintEvent(PaintContext& ctx) override;
};

}  // namespace agis::ui
