#include "ui_engine/widget_core.h"

#include "ui_engine/app.h"

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
#if defined(_WIN32)
  HDC hdc = static_cast<HDC>(ctx.nativeDevice);
  if (!hdc || ctx.clip.w <= 0 || ctx.clip.h <= 0) {
    return;
  }
  RECT rc{ctx.clip.x, ctx.clip.y, ctx.clip.x + ctx.clip.w, ctx.clip.y + ctx.clip.h};
  HBRUSH bg = CreateSolidBrush(App::instance().themeColorSurfaceAlt());
  FillRect(hdc, &rc, bg);
  DeleteObject(bg);
  HBRUSH ed = CreateSolidBrush(App::instance().themeColorMuted());
  FrameRect(hdc, &rc, ed);
  DeleteObject(ed);
#else
  (void)ctx;
#endif
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
  SetTextColor(hdc, App::instance().themeColorText());
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
#if defined(_WIN32)
  HDC hdc = static_cast<HDC>(ctx.nativeDevice);
  if (!hdc || ctx.clip.w <= 0 || ctx.clip.h <= 0) {
    return;
  }
  const App& app = App::instance();
  const bool hot = app.hoverWidget() == this;
  RECT rc{ctx.clip.x, ctx.clip.y, ctx.clip.x + ctx.clip.w, ctx.clip.y + ctx.clip.h};
  HBRUSH bg = CreateSolidBrush(hot ? app.themeColorAccent() : app.themeColorSurfaceAlt());
  FillRect(hdc, &rc, bg);
  DeleteObject(bg);
  HBRUSH ed = CreateSolidBrush(app.themeColorMuted());
  FrameRect(hdc, &rc, ed);
  DeleteObject(ed);
  SetBkMode(hdc, TRANSPARENT);
  SetTextColor(hdc, hot ? RGB(255, 255, 255) : app.themeColorText());
  RECT trc = rc;
  trc.left += 6;
  DrawTextW(hdc, text_.empty() ? L"Button" : text_.c_str(), -1, &trc,
            DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX);
#else
  (void)ctx;
#endif
}

void LineEdit::setText(std::wstring t) {
  text_ = std::move(t);
}

void LineEdit::paintEvent(PaintContext& ctx) {
#if defined(_WIN32)
  HDC hdc = static_cast<HDC>(ctx.nativeDevice);
  if (!hdc || ctx.clip.w <= 0 || ctx.clip.h <= 0) {
    return;
  }
  const App& app = App::instance();
  RECT rc{ctx.clip.x, ctx.clip.y, ctx.clip.x + ctx.clip.w, ctx.clip.y + ctx.clip.h};
  HBRUSH bg = CreateSolidBrush(app.themeColorSurfaceAlt());
  FillRect(hdc, &rc, bg);
  DeleteObject(bg);
  HBRUSH ed = CreateSolidBrush(app.themeColorAccent());
  FrameRect(hdc, &rc, ed);
  DeleteObject(ed);
  SetBkMode(hdc, TRANSPARENT);
  SetTextColor(hdc, app.themeColorText());
  RECT trc = rc;
  trc.left += 8;
  DrawTextW(hdc, text_.empty() ? L"" : text_.c_str(), -1, &trc,
            DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX);
#else
  (void)ctx;
#endif
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
#if defined(_WIN32)
  HDC hdc = static_cast<HDC>(ctx.nativeDevice);
  if (!hdc || ctx.clip.w <= 0 || ctx.clip.h <= 0) {
    return;
  }
  const App& app = App::instance();
  RECT rc{ctx.clip.x, ctx.clip.y, ctx.clip.x + ctx.clip.w, ctx.clip.y + ctx.clip.h};
  HBRUSH bg = CreateSolidBrush(app.themeColorSurfaceAlt());
  FillRect(hdc, &rc, bg);
  DeleteObject(bg);
  HBRUSH ed = CreateSolidBrush(app.themeColorMuted());
  FrameRect(hdc, &rc, ed);
  DeleteObject(ed);
  if (content_) {
    PaintContext inner = ctx;
    const Rect g = content_->geometry();
    inner.clip = {ctx.clip.x + g.x, ctx.clip.y + g.y, g.w, g.h};
    content_->paintEvent(inner);
  }
#else
  (void)ctx;
#endif
}

void Splitter::paintEvent(PaintContext& ctx) {
#if defined(_WIN32)
  HDC hdc = static_cast<HDC>(ctx.nativeDevice);
  if (!hdc || ctx.clip.w <= 0 || ctx.clip.h <= 0) {
    return;
  }
  RECT rc{ctx.clip.x, ctx.clip.y, ctx.clip.x + ctx.clip.w, ctx.clip.y + ctx.clip.h};
  HBRUSH bg = CreateSolidBrush(App::instance().themeColorMuted());
  FillRect(hdc, &rc, bg);
  DeleteObject(bg);
#else
  (void)ctx;
#endif
}

void Canvas2D::paintEvent(PaintContext& ctx) {
#if defined(_WIN32)
  HDC hdc = static_cast<HDC>(ctx.nativeDevice);
  if (!hdc || ctx.clip.w <= 0 || ctx.clip.h <= 0) {
    return;
  }
  RECT rc{ctx.clip.x, ctx.clip.y, ctx.clip.x + ctx.clip.w, ctx.clip.y + ctx.clip.h};
  HBRUSH bg = CreateSolidBrush(App::instance().themeColorSurface());
  FillRect(hdc, &rc, bg);
  DeleteObject(bg);
#else
  (void)ctx;
#endif
}

void DialogWindow::paintEvent(PaintContext& ctx) {
  (void)ctx;
}

void PopupMenu::paintEvent(PaintContext& ctx) {
  (void)ctx;
}

}  // namespace agis::ui
