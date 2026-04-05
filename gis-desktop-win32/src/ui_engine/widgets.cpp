#include "ui_engine/widgets.h"

#include <utility>

namespace agis::ui {

void Window::setTitle(std::wstring title) {
  title_ = std::move(title);
}

void Window::paintEvent(PaintContext& ctx) {
  (void)ctx;
}

void Frame::paintEvent(PaintContext& ctx) {
  (void)ctx;
}

void Label::setText(std::wstring t) {
  text_ = std::move(t);
}

void Label::paintEvent(PaintContext& ctx) {
  (void)ctx;
}

void PushButton::setText(std::wstring t) {
  text_ = std::move(t);
}

void PushButton::click() {
  if (onClicked_) {
    onClicked_();
  }
}

void PushButton::setOnClicked(std::function<void()> fn) {
  onClicked_ = std::move(fn);
}

void PushButton::paintEvent(PaintContext& ctx) {
  (void)ctx;
}

void LineEdit::setText(std::wstring t) {
  text_ = std::move(t);
}

void LineEdit::paintEvent(PaintContext& ctx) {
  (void)ctx;
}

void ScrollArea::setContentWidget(std::unique_ptr<Widget> w) {
  if (content_) {
    content_->parent_ = nullptr;
  }
  content_ = std::move(w);
  if (content_) {
    content_->parent_ = this;
  }
}

void ScrollArea::paintEvent(PaintContext& ctx) {
  (void)ctx;
}

}  // namespace agis::ui
