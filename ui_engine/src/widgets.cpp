#include "ui_engine/widget_core.h"

#include <utility>

#if defined(_WIN32)
#include <windows.h>
#endif

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
#if defined(_WIN32)
  HDC hdc = static_cast<HDC>(ctx.nativeDevice);
  if (!hdc || ctx.clip.w <= 0 || ctx.clip.h <= 0) {
    return;
  }
  SetBkMode(hdc, TRANSPARENT);
  SetTextColor(hdc, RGB(90, 95, 105));
  RECT rc{ctx.clip.x + 4, ctx.clip.y, ctx.clip.x + ctx.clip.w, ctx.clip.y + ctx.clip.h};
  DrawTextW(hdc, text_.empty() ? L"" : text_.c_str(), -1, &rc,
              DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX);
#else
  (void)ctx;
#endif
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

void Splitter::paintEvent(PaintContext& ctx) {
  (void)ctx;
}

void Canvas2D::paintEvent(PaintContext& ctx) {
  (void)ctx;
}

void DialogWindow::paintEvent(PaintContext& ctx) {
  (void)ctx;
}

void PopupMenu::paintEvent(PaintContext& ctx) {
  (void)ctx;
}

}  // namespace agis::ui
