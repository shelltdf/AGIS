#include "ui_engine/widgets_mainframe.h"
#include "app/ui_private.h"

#if defined(_WIN32)
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
  Win32Fill(ctx, RGB(248, 249, 250));
  Win32Edge(ctx, RGB(210, 214, 220));
#else
  (void)ctx;
#endif
}

void ToolBarWidget::paintEvent(PaintContext& ctx) {
#if defined(_WIN32)
  Win32Fill(ctx, RGB(235, 238, 242));
  Win32Edge(ctx, RGB(200, 204, 210));
#else
  (void)ctx;
#endif
}

void StatusBarWidget::paintEvent(PaintContext& ctx) {
#if defined(_WIN32)
  Win32Fill(ctx, RGB(220, 223, 228));
  Win32Edge(ctx, RGB(180, 184, 192));
#else
  (void)ctx;
#endif
}

void DockArea::paintEvent(PaintContext& ctx) {
#if defined(_WIN32)
  Win32Fill(ctx, RGB(250, 251, 252));
  Win32Edge(ctx, RGB(190, 195, 202));
#else
  (void)ctx;
#endif
}

void DockButtonStrip::paintEvent(PaintContext& ctx) {
#if defined(_WIN32)
  Win32Fill(ctx, RGB(225, 228, 232));
  Win32Edge(ctx, RGB(170, 175, 185));
#else
  (void)ctx;
#endif
}

void DockView::paintEvent(PaintContext& ctx) {
#if defined(_WIN32)
  Win32Fill(ctx, RGB(255, 255, 255));
  Win32Edge(ctx, RGB(200, 204, 210));
#else
  (void)ctx;
#endif
}

void DockPanel::paintEvent(PaintContext& ctx) {
#if defined(_WIN32)
  Win32Fill(ctx, RGB(245, 246, 248));
  Win32Edge(ctx, RGB(200, 204, 210));
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
  Win32Fill(ctx, RGB(245, 248, 255));
  Win32Edge(ctx, RGB(160, 175, 200));
#else
  (void)ctx;
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
