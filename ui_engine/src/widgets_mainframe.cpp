#include "ui_engine/widgets_mainframe.h"
#include "app/ui_private.h"

#include "ui_engine/app.h"

#if defined(_WIN32)
#include <stdio.h>
#include <windows.h>
#endif

namespace agis::ui {

namespace {
#if defined(_WIN32)
void Win32Fill(PaintContext& ctx, COLORREF rgb) {
  HDC hdc = static_cast<HDC>(ctx.nativeDevice);
  if (!hdc || ctx.clip.w <= 0 || ctx.clip.h <= 0) {
    return;
  }
  RECT rc{ctx.clip.x, ctx.clip.y, ctx.clip.x + ctx.clip.w, ctx.clip.y + ctx.clip.h};
  HBRUSH br = CreateSolidBrush(rgb);
  FillRect(hdc, &rc, br);
  DeleteObject(br);
}

void Win32Edge(PaintContext& ctx, COLORREF rgb) {
  HDC hdc = static_cast<HDC>(ctx.nativeDevice);
  if (!hdc || ctx.clip.w <= 0 || ctx.clip.h <= 0) {
    return;
  }
  RECT rc{ctx.clip.x, ctx.clip.y, ctx.clip.x + ctx.clip.w, ctx.clip.y + ctx.clip.h};
  HBRUSH br = CreateSolidBrush(rgb);
  FrameRect(hdc, &rc, br);
  DeleteObject(br);
}

void Win32DrawTextLeft(PaintContext& ctx, const wchar_t* text, COLORREF fg) {
  HDC hdc = static_cast<HDC>(ctx.nativeDevice);
  if (!hdc || !text) {
    return;
  }
  SetBkMode(hdc, TRANSPARENT);
  SetTextColor(hdc, fg);
  RECT rc{ctx.clip.x + 8, ctx.clip.y, ctx.clip.x + ctx.clip.w, ctx.clip.y + ctx.clip.h};
  DrawTextW(hdc, text, -1, &rc, DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX);
}

void Win32DrawTextCenter(PaintContext& ctx, const wchar_t* text, COLORREF fg) {
  HDC hdc = static_cast<HDC>(ctx.nativeDevice);
  if (!hdc || !text) {
    return;
  }
  SetBkMode(hdc, TRANSPARENT);
  SetTextColor(hdc, fg);
  RECT rc{ctx.clip.x, ctx.clip.y, ctx.clip.x + ctx.clip.w, ctx.clip.y + ctx.clip.h};
  DrawTextW(hdc, text, -1, &rc, DT_CENTER | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX);
}

void WidgetClientOrigin(const Widget* w, int* ox, int* oy) {
  int x = 0;
  int y = 0;
  for (const Widget* p = w; p; p = p->parent()) {
    const Rect g = p->geometry();
    x += g.x;
    y += g.y;
  }
  *ox = x;
  *oy = y;
}
#endif
}  // namespace

void MainFrame::paintEvent(PaintContext& ctx) {
#if defined(_WIN32)
  Win32Fill(ctx, RGB(240, 242, 245));
#else
  (void)ctx;
#endif
}

void MenuBarWidget::paintEvent(PaintContext& ctx) {
#if defined(_WIN32)
  const bool hot = App::instance().hoverWidget() == this;
  Win32Fill(ctx, hot ? RGB(232, 235, 240) : RGB(248, 249, 250));
  Win32Edge(ctx, RGB(210, 214, 220));
  Win32DrawTextLeft(ctx, L"File    Language    Theme    Help", RGB(28, 28, 32));
#else
  (void)ctx;
#endif
}

void MenuBarWidget::mouseMoveEvent(int client_x, int client_y, unsigned buttons) {
  (void)client_x;
  (void)client_y;
  (void)buttons;
  App::instance().setStatusHint(L"Menu: File, Language, Theme, Help");
}

void MenuBarWidget::mousePressEvent(int client_x, int client_y, int button) {
  (void)client_x;
  (void)client_y;
  (void)button;
  App::instance().setStatusHint(L"Menu bar pressed (demo)");
}

void ToolBarWidget::paintEvent(PaintContext& ctx) {
#if defined(_WIN32)
  const bool hot = App::instance().hoverWidget() == this;
  Win32Fill(ctx, hot ? RGB(220, 224, 230) : RGB(235, 238, 242));
  Win32Edge(ctx, RGB(200, 204, 210));
  Win32DrawTextLeft(ctx, L"New    Open    Save    —    tools", RGB(35, 38, 45));
#else
  (void)ctx;
#endif
}

void ToolBarWidget::mouseMoveEvent(int client_x, int client_y, unsigned buttons) {
  (void)client_x;
  (void)client_y;
  (void)buttons;
  App::instance().setStatusHint(L"Toolbar: New, Open, Save (demo)");
}

void ToolBarWidget::mousePressEvent(int client_x, int client_y, int button) {
  (void)client_x;
  (void)client_y;
  (void)button;
  App::instance().setStatusHint(L"Toolbar pressed (demo)");
}

void StatusBarWidget::paintEvent(PaintContext& ctx) {
#if defined(_WIN32)
  Win32Fill(ctx, RGB(220, 223, 228));
  Win32Edge(ctx, RGB(180, 184, 192));
  wchar_t buf[512]{};
  const App& app = App::instance();
  const std::wstring& hint = app.statusHint();
  _snwprintf_s(buf, _TRUNCATE, L"Pointer: (%d, %d)   %ls", app.pointerClientX(), app.pointerClientY(), hint.c_str());
  Win32DrawTextLeft(ctx, buf, RGB(25, 28, 35));
#else
  (void)ctx;
#endif
}

void DockArea::paintEvent(PaintContext& ctx) {
#if defined(_WIN32)
  Win32Fill(ctx, RGB(250, 251, 252));
  Win32Edge(ctx, RGB(190, 195, 202));
  const wchar_t* cap = L"Dock Area";
  switch (dockEdge()) {
    case DockEdge::kLeft:
      cap = L"Dock Area (Left)";
      break;
    case DockEdge::kRight:
      cap = L"Dock Area (Right)";
      break;
    case DockEdge::kTop:
      cap = L"Dock Area (Top)";
      break;
    case DockEdge::kBottom:
      cap = L"Dock Area (Bottom)";
      break;
  }
  Win32DrawTextLeft(ctx, cap, RGB(90, 95, 105));
#else
  (void)ctx;
#endif
}

void DockButtonStrip::paintEvent(PaintContext& ctx) {
#if defined(_WIN32)
  Win32Fill(ctx, RGB(225, 228, 232));
  Win32Edge(ctx, RGB(170, 175, 185));
  Win32DrawTextCenter(ctx, L"||", RGB(55, 60, 70));
#else
  (void)ctx;
#endif
}

void DockView::paintEvent(PaintContext& ctx) {
#if defined(_WIN32)
  Win32Fill(ctx, RGB(255, 255, 255));
  Win32Edge(ctx, RGB(200, 204, 210));
  Win32DrawTextLeft(ctx, L"Dock View", RGB(110, 115, 125));
#else
  (void)ctx;
#endif
}

void DockPanel::paintEvent(PaintContext& ctx) {
#if defined(_WIN32)
  Win32Fill(ctx, RGB(245, 246, 248));
  Win32Edge(ctx, RGB(200, 204, 210));
  Win32DrawTextLeft(ctx, L"Panel content (demo)", RGB(95, 100, 110));
#else
  (void)ctx;
#endif
}

void LayerDockPanel::paintEvent(PaintContext& ctx) {
#if defined(_WIN32)
  Win32Fill(ctx, RGB(242, 244, 247));
  Win32Edge(ctx, RGB(160, 170, 185));
#else
  (void)ctx;
#endif
}

void PropsDockPanel::paintEvent(PaintContext& ctx) {
#if defined(_WIN32)
  Win32Fill(ctx, RGB(244, 245, 247));
  Win32Edge(ctx, RGB(160, 170, 185));
#else
  (void)ctx;
#endif
}

void MapCanvas2D::paintEvent(PaintContext& ctx) {
#if defined(_WIN32)
  const bool hot = App::instance().hoverWidget() == this;
  Win32Fill(ctx, hot ? RGB(232, 238, 250) : RGB(245, 248, 255));
  Win32Edge(ctx, RGB(160, 175, 200));
  Win32DrawTextLeft(ctx, L"2D map canvas — middle drag / wheel zoom (demo)", RGB(45, 55, 75));
#else
  (void)ctx;
#endif
}

void MapCanvas2D::mouseMoveEvent(int client_x, int client_y, unsigned buttons) {
#if defined(_WIN32)
  (void)buttons;
  int ox = 0;
  int oy = 0;
  WidgetClientOrigin(this, &ox, &oy);
  const int lx = client_x - ox;
  const int ly = client_y - oy;
  wchar_t buf[160]{};
  _snwprintf_s(buf, _TRUNCATE, L"Canvas (%d, %d) — map view", lx, ly);
  App::instance().setStatusHint(buf);
#else
  (void)client_x;
  (void)client_y;
  (void)buttons;
#endif
}

void ShortcutHelpPanel::paintEvent(PaintContext& ctx) {
#if defined(_WIN32)
  Win32Fill(ctx, RGB(255, 255, 255));
  Win32Edge(ctx, RGB(180, 188, 200));
#else
  (void)ctx;
#endif
}

void LayerVisibilityPanel::paintEvent(PaintContext& ctx) {
#if defined(_WIN32)
  Win32Fill(ctx, RGB(255, 255, 255));
  Win32Edge(ctx, RGB(180, 188, 200));
#else
  (void)ctx;
#endif
}

void MapZoomBar::paintEvent(PaintContext& ctx) {
#if defined(_WIN32)
  Win32Fill(ctx, RGB(248, 249, 251));
  Win32Edge(ctx, RGB(190, 195, 205));
#else
  (void)ctx;
#endif
}

void MapHintOverlay::paintEvent(PaintContext& ctx) {
#if defined(_WIN32)
  Win32Fill(ctx, RGB(250, 252, 255));
  Win32Edge(ctx, RGB(170, 185, 205));
#else
  (void)ctx;
#endif
}

void MapCenterHintOverlay::paintEvent(PaintContext& ctx) {
#if defined(_WIN32)
  Win32Fill(ctx, RGB(255, 255, 255));
  Win32Edge(ctx, RGB(150, 165, 190));
#else
  (void)ctx;
#endif
}

void LayerDriverDialog::paintEvent(PaintContext& ctx) {
#if defined(_WIN32)
  Win32Fill(ctx, RGB(252, 252, 253));
  Win32Edge(ctx, RGB(120, 130, 150));
#else
  (void)ctx;
#endif
}

void LogWindow::paintEvent(PaintContext& ctx) {
#if defined(_WIN32)
  Win32Fill(ctx, RGB(250, 250, 250));
  Win32Edge(ctx, RGB(140, 145, 155));
#else
  (void)ctx;
#endif
}

void LayerListContextMenu::paintEvent(PaintContext& ctx) {
#if defined(_WIN32)
  Win32Fill(ctx, RGB(255, 255, 255));
#else
  (void)ctx;
#endif
}

void MapContextMenu::paintEvent(PaintContext& ctx) {
#if defined(_WIN32)
  Win32Fill(ctx, RGB(255, 255, 255));
#else
  (void)ctx;
#endif
}

void LogPanel::paintEvent(PaintContext& ctx) {
#if defined(_WIN32)
  Win32Fill(ctx, RGB(252, 252, 252));
  Win32Edge(ctx, RGB(200, 205, 215));
#else
  (void)ctx;
#endif
}

}  // namespace agis::ui
