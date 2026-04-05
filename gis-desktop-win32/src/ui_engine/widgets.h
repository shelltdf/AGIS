#pragma once

#include "ui_engine/widget.h"

#include <functional>
#include <string>

namespace agis::ui {

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

}  // namespace agis::ui
