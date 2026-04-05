#pragma once

/**
 * 通用 Widget 子类：与 Qt 基元类似，可在任意采用 `agis::ui` 的应用中复用。
 * 本工程（AGIS 主窗口、地图、Dock 等）专用类型见 `widgets_mainframe.h`。
 */

#include "ui_engine/widget.h"

#include <functional>
#include <string>

namespace agis::ui {

/** 分割条方向：竖条调节左右宽度，横条调节上下高度。 */
enum class SplitterOrientation { kVertical, kHorizontal };

/** 顶层窗口（类似 QMainWindow / QWidget 窗口顶层）。 */
class Window : public Widget {
 public:
  Window() = default;

  void setTitle(std::wstring title);
  const std::wstring& title() const { return title_; }

  void paintEvent(PaintContext& ctx) override;

 private:
  std::wstring title_;
};

/** 分组框 / 简单容器边框（类似 QGroupBox / QFrame）。 */
class Frame : public Widget {
 public:
  Frame() = default;
  void paintEvent(PaintContext& ctx) override;
};

/** 静态文本（类似 QLabel）。 */
class Label : public Widget {
 public:
  Label() = default;

  void setText(std::wstring t);
  const std::wstring& text() const { return text_; }

  void paintEvent(PaintContext& ctx) override;

 private:
  std::wstring text_;
};

/** 按钮（类似 QPushButton）。 */
class PushButton : public Widget {
 public:
  PushButton() = default;

  void setText(std::wstring t);
  const std::wstring& text() const { return text_; }

  /** 由平台后端在鼠标释放且命中时调用。 */
  void click();

  void setOnClicked(std::function<void()> fn);

  void paintEvent(PaintContext& ctx) override;

 private:
  std::wstring text_;
  std::function<void()> onClicked_;
};

/** 单行编辑（类似 QLineEdit）。 */
class LineEdit : public Widget {
 public:
  LineEdit() = default;

  void setText(std::wstring t);
  const std::wstring& text() const { return text_; }

  void paintEvent(PaintContext& ctx) override;

 private:
  std::wstring text_;
};

/** 可滚动视口容器（类似 QScrollArea 的壳；滚动条与视口内容由后端实现）。 */
class ScrollArea : public Widget {
 public:
  ScrollArea() = default;

  void setContentWidget(std::unique_ptr<Widget> w);
  Widget* contentWidget() const { return content_.get(); }

  void paintEvent(PaintContext& ctx) override;

 private:
  std::unique_ptr<Widget> content_;
};

/** 分割条（可拖拽调节相邻区域尺寸）。 */
class Splitter : public Widget {
 public:
  Splitter() = default;

  void setOrientation(SplitterOrientation o) { orient_ = o; }
  SplitterOrientation orientation() const { return orient_; }

  void paintEvent(PaintContext& ctx) override;

 private:
  SplitterOrientation orient_{SplitterOrientation::kVertical};
};

/** 通用二维画布表面（平移/缩放语义由宿主实现）。 */
class Canvas2D : public Widget {
 public:
  Canvas2D() = default;
  void paintEvent(PaintContext& ctx) override;
};

/**
 * 对话框窗口基类（实现侧：`WS_POPUP` | `WS_CAPTION` | `WS_SYSMENU`，owner 上模态消息循环）。
 */
class DialogWindow : public Window {
 public:
  DialogWindow() = default;
  void paintEvent(PaintContext& ctx) override;
};

/**
 * 弹出菜单抽象（实现侧：`CreatePopupMenu` / `TrackPopupMenu`）。
 * 不具持久矩形，仅占位类型以便与 HMENU 生命周期在设计上对齐。
 */
class PopupMenu : public Widget {
 public:
  PopupMenu() = default;
  void paintEvent(PaintContext& ctx) override;
};

}  // namespace agis::ui
