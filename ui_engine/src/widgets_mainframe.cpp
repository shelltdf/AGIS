#include "ui_engine/widgets_mainframe.h"
#include "app/ui_private.h"

#include "ui_engine/app.h"

#include <algorithm>
#include <utility>

#if defined(_WIN32)
#include <stdio.h>
#include <windows.h>
#endif

namespace agis::ui {

namespace {

/** 本控件左上角在顶层客户区中的坐标（与 `WM_MOUSEMOVE` 中坐标系一致）。 */
void WidgetRootClientOrigin(const Widget* w, int* ox, int* oy) {
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

/** 与 `ToolBarWidget` 演示文案四段 + `Win32DrawTextLeft` 左内边距 8px 对齐的近似分段。 */
int ToolbarSegmentIndexFromLocalX(int lx, int bar_w) {
  if (bar_w <= 0) {
    return -1;
  }
  if (lx < 0 || lx >= bar_w) {
    return -1;
  }
  constexpr int kPad = 8;
  const int inner = std::max(0, bar_w - kPad * 2);
  if (inner <= 0) {
    return -1;
  }
  const int q = inner / 4;
  if (q <= 0) {
    return -1;
  }
  const int rel = lx - kPad;
  if (rel < 0) {
    return 0;
  }
  if (rel < q) {
    return 0;
  }
  if (rel < 2 * q) {
    return 1;
  }
  if (rel < 3 * q) {
    return 2;
  }
  return 3;
}

/** 与 `ui_engine_demo` 中菜单栏行高、`syncGeometryWithBarCell(..., bar_h)` 一致。 */
constexpr int kMenuDropItemRowHeight = 24;

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

bool MenuBarWidget::containsPointForHitTest(int client_x, int client_y, int origin_x, int origin_y) const {
  const Rect g = geometry();
  const int ax = origin_x + g.x;
  const int ay = origin_y + g.y;
  if (client_x >= ax && client_y >= ay && client_x < ax + g.w && client_y < ay + g.h) {
    return true;
  }
  Menu* open = App::instance().openDropDownMenu();
  if (!open) {
    return false;
  }
  const Widget* p = open->parent();
  while (p && p != this) {
    p = p->parent();
  }
  if (p != this) {
    return false;
  }
  const Rect mg = open->geometry();
  const int mx = ax + mg.x;
  const int my = ay + mg.y;
  return client_x >= mx && client_y >= my && client_x < mx + mg.w && client_y < my + mg.h;
}

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
#else
  (void)ctx;
#endif
}

void MenuBarWidget::mouseMoveEvent(int client_x, int client_y, unsigned buttons) {
  (void)client_x;
  (void)client_y;
  (void)buttons;
  App::instance().setStatusHint(L"Menu bar");
}

void MenuBarWidget::mousePressEvent(int client_x, int client_y, int button) {
  (void)client_x;
  (void)client_y;
  (void)button;
  App::instance().setStatusHint(L"Menu bar (demo)");
}

void Menu::setTitle(std::wstring title) {
  title_ = std::move(title);
}

void Menu::syncGeometryWithBarCell(int x, int y, int w, int bar_h) {
  bar_strip_h_ = bar_h > 0 ? bar_h : 28;
  const int n = static_cast<int>(children().size());
  const int drop_h = drop_down_open_ ? n * kMenuDropItemRowHeight : 0;
  setGeometry({x, y, w, bar_strip_h_ + drop_h});
  int iy = bar_strip_h_;
  for (auto& ch : children()) {
    if (auto* mi = dynamic_cast<MenuItem*>(ch.get())) {
      if (drop_down_open_) {
        mi->setVisible(true);
        mi->setGeometry({0, iy, w, kMenuDropItemRowHeight});
        iy += kMenuDropItemRowHeight;
      } else {
        mi->setVisible(false);
      }
    }
  }
}

void Menu::openDropDownVisual() {
  drop_down_open_ = true;
  const Rect g = geometry();
  syncGeometryWithBarCell(g.x, g.y, g.w, bar_strip_h_);
}

void Menu::closeDropDownVisual() {
  drop_down_open_ = false;
  const Rect g = geometry();
  syncGeometryWithBarCell(g.x, g.y, g.w, bar_strip_h_);
}

void Menu::paintEvent(PaintContext& ctx) {
#if defined(_WIN32)
  const bool hot = App::instance().hoverWidget() == this;
  const int strip = std::min(bar_strip_h_, ctx.clip.h);
  RECT rc_full{ctx.clip.x, ctx.clip.y, ctx.clip.x + ctx.clip.w, ctx.clip.y + ctx.clip.h};
  HDC hdc = static_cast<HDC>(ctx.nativeDevice);
  if (strip > 0 && strip <= ctx.clip.h) {
    RECT rc_top{ctx.clip.x, ctx.clip.y, ctx.clip.x + ctx.clip.w, ctx.clip.y + strip};
    HBRUSH bt = CreateSolidBrush(hot ? RGB(220, 228, 238) : RGB(248, 249, 250));
    FillRect(hdc, &rc_top, bt);
    DeleteObject(bt);
    SetBkMode(hdc, TRANSPARENT);
    SetTextColor(hdc, RGB(28, 28, 32));
    RECT rc_title = rc_top;
    rc_title.left += 8;
    DrawTextW(hdc, title_.empty() ? L"Menu" : title_.c_str(), -1, &rc_title,
              DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX);
  }
  if (ctx.clip.h > strip) {
    RECT rc_drop{ctx.clip.x, ctx.clip.y + strip, ctx.clip.x + ctx.clip.w, ctx.clip.y + ctx.clip.h};
    HBRUSH bd = CreateSolidBrush(RGB(252, 252, 254));
    FillRect(hdc, &rc_drop, bd);
    DeleteObject(bd);
    HBRUSH be = CreateSolidBrush(RGB(200, 204, 212));
    FrameRect(hdc, &rc_full, be);
    DeleteObject(be);
  } else {
    HBRUSH be = CreateSolidBrush(RGB(210, 214, 220));
    FrameRect(hdc, &rc_full, be);
    DeleteObject(be);
  }
#else
  (void)ctx;
#endif
}

void Menu::mouseMoveEvent(int client_x, int client_y, unsigned buttons) {
  (void)client_x;
  (void)client_y;
  (void)buttons;
  std::wstring hint = L"Menu: ";
  hint += title_.empty() ? L"(untitled)" : title_;
  App::instance().setStatusHint(std::move(hint));
}

void Menu::mousePressEvent(int client_x, int client_y, int button) {
  (void)client_x;
  (void)client_y;
  (void)button;
  if (drop_down_open_ && App::instance().openDropDownMenu() == this) {
    App::instance().setOpenDropDownMenu(nullptr);
  } else {
    App::instance().setOpenDropDownMenu(this);
  }
}

MenuItem::MenuItem() {
  setVisible(false);
}

void MenuItem::setText(std::wstring text) {
  text_ = std::move(text);
}

void MenuItem::paintEvent(PaintContext& ctx) {
#if defined(_WIN32)
  if (!visible()) {
    return;
  }
  const bool hot = App::instance().hoverWidget() == this;
  Win32Fill(ctx, hot ? RGB(230, 236, 245) : RGB(252, 252, 253));
  Win32DrawTextLeft(ctx, text_.empty() ? L"Item" : text_.c_str(), enabled_ ? RGB(25, 28, 35) : RGB(160, 160, 165));
#else
  (void)ctx;
#endif
}

void MenuItem::mouseMoveEvent(int client_x, int client_y, unsigned buttons) {
  (void)client_x;
  (void)client_y;
  (void)buttons;
  if (!enabled()) {
    return;
  }
  std::wstring hint;
  if (auto* menu = dynamic_cast<Menu*>(parent())) {
    hint = menu->title().empty() ? L"(menu)" : menu->title();
    hint += L" → ";
  }
  hint += text_.empty() ? L"(empty)" : text_;
  App::instance().setStatusHint(std::move(hint));
}

void MenuItem::mousePressEvent(int client_x, int client_y, int button) {
  (void)client_x;
  (void)client_y;
  (void)button;
  if (!enabled()) {
    return;
  }
  std::wstring hint = L"Choose: ";
  hint += text_.empty() ? L"Item" : text_;
  App::instance().setStatusHint(std::move(hint));
  App::instance().setOpenDropDownMenu(nullptr);
}

void ToolBarWidget::paintEvent(PaintContext& ctx) {
#if defined(_WIN32)
  const bool hot = App::instance().hoverWidget() == this;
  Win32Fill(ctx, hot ? RGB(220, 224, 230) : RGB(235, 238, 242));
  if (hot) {
    const int seg = toolbar_hover_segment_;
    if (seg >= 0 && seg < 4) {
      HDC hdc = static_cast<HDC>(ctx.nativeDevice);
      constexpr int kPad = 8;
      const int inner = std::max(0, ctx.clip.w - kPad * 2);
      const int q = inner / 4;
      if (q > 0) {
        RECT rc_seg{ctx.clip.x + kPad + seg * q, ctx.clip.y + 2, ctx.clip.x + kPad + (seg + 1) * q,
                    ctx.clip.y + ctx.clip.h - 2};
        HBRUSH br = CreateSolidBrush(RGB(200, 208, 218));
        FillRect(hdc, &rc_seg, br);
        DeleteObject(br);
      }
    }
  }
  Win32Edge(ctx, RGB(200, 204, 210));
  Win32DrawTextLeft(ctx, L"New    Open    Save    —    tools", RGB(35, 38, 45));
#else
  (void)ctx;
#endif
}

void ToolBarWidget::mouseMoveEvent(int client_x, int client_y, unsigned buttons) {
  (void)client_y;
  (void)buttons;
  int ox = 0;
  int oy = 0;
  WidgetRootClientOrigin(this, &ox, &oy);
  const int lx = client_x - ox;
  const int w = geometry().w;
  toolbar_hover_segment_ = ToolbarSegmentIndexFromLocalX(lx, w);
  switch (toolbar_hover_segment_) {
    case 0:
      App::instance().setStatusHint(L"Toolbar: New (demo)");
      break;
    case 1:
      App::instance().setStatusHint(L"Toolbar: Open (demo)");
      break;
    case 2:
      App::instance().setStatusHint(L"Toolbar: Save (demo)");
      break;
    case 3:
      App::instance().setStatusHint(L"Toolbar: more tools (demo)");
      break;
    default:
      App::instance().setStatusHint(L"Toolbar (demo)");
      break;
  }
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
