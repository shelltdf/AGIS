#include "ui_engine/widgets_mainframe.h"
#include "app/ui_private.h"

#include "ui_engine/app.h"

#include <algorithm>
#include <cmath>
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

int ToolButton::intrinsicWidth() const {
  const int pad = 20;
  const int per = 9;
  const int w = pad + static_cast<int>(text_.size()) * per;
  return std::min(220, std::max(56, w));
}

void ToolButton::paintEvent(PaintContext& ctx) {
#if defined(_WIN32)
  const bool hot = App::instance().hoverWidget() == this;
  Win32Fill(ctx, hot ? RGB(210, 216, 226) : RGB(228, 232, 238));
  Win32Edge(ctx, RGB(180, 186, 196));
  Win32DrawTextCenter(ctx, text_.empty() ? L"?" : text_.c_str(), RGB(30, 34, 42));
#else
  (void)ctx;
#endif
}

void ToolButton::mouseMoveEvent(int client_x, int client_y, unsigned buttons) {
  (void)client_x;
  (void)client_y;
  (void)buttons;
  std::wstring h = L"Tool: ";
  h += text_.empty() ? L"(empty)" : text_;
  h += L" — 无全局快捷键（demo）";
  App::instance().setStatusHint(std::move(h));
}

void ToolButton::mousePressEvent(int client_x, int client_y, int button) {
  (void)client_x;
  (void)client_y;
  if (button != 1) {
    return;
  }
  std::wstring h = L"Toolbar 点击: ";
  h += text_.empty() ? L"(empty)" : text_;
  App::instance().setStatusHint(std::move(h));
}

void ToolBarWidget::paintEvent(PaintContext& ctx) {
#if defined(_WIN32)
  const bool hot = App::instance().hoverWidget() == this;
  Win32Fill(ctx, hot ? RGB(220, 224, 230) : RGB(235, 238, 242));
  Win32Edge(ctx, RGB(200, 204, 210));
#else
  (void)ctx;
#endif
}

void ToolBarWidget::mouseMoveEvent(int client_x, int client_y, unsigned buttons) {
  (void)client_x;
  (void)client_y;
  (void)buttons;
  App::instance().setStatusHint(L"Toolbar — 子控件为 ToolButton（demo）");
}

void ToolBarWidget::mousePressEvent(int client_x, int client_y, int button) {
  (void)client_x;
  (void)client_y;
  (void)button;
  App::instance().setStatusHint(L"Toolbar 背景（demo）");
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
  const bool hot = App::instance().hoverWidget() == this;
  Win32Fill(ctx, hot ? RGB(210, 214, 222) : RGB(225, 228, 232));
  Win32Edge(ctx, RGB(170, 175, 185));
  const wchar_t* label = L"||";
  if (auto* dock = dynamic_cast<DockArea*>(parent())) {
    const bool ex = dock->contentExpanded();
    switch (dock->dockEdge()) {
      case DockEdge::kLeft:
        label = ex ? L"‹" : L"›";
        break;
      case DockEdge::kRight:
        label = ex ? L"›" : L"‹";
        break;
      case DockEdge::kTop:
        label = ex ? L"▲" : L"▼";
        break;
      case DockEdge::kBottom:
        label = ex ? L"▼" : L"▲";
        break;
    }
  }
  Win32DrawTextCenter(ctx, label, RGB(55, 60, 70));
#else
  (void)ctx;
#endif
}

void DockButtonStrip::mouseMoveEvent(int client_x, int client_y, unsigned buttons) {
  (void)client_x;
  (void)client_y;
  (void)buttons;
  App::instance().setStatusHint(L"Dock 缘条 — 单击折叠/展开内容区（demo）");
}

void DockButtonStrip::mousePressEvent(int client_x, int client_y, int button) {
  (void)client_x;
  (void)client_y;
  if (button != 1) {
    return;
  }
  if (auto* dock = dynamic_cast<DockArea*>(parent())) {
    dock->setContentExpanded(!dock->contentExpanded());
    App::instance().relayoutFromLastClientSize();
    App::instance().invalidateAll();
    App::instance().setStatusHint(dock->contentExpanded() ? L"Dock 已展开" : L"Dock 已折叠");
  }
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
  Win32DrawTextLeft(ctx, L"图层列表 Dock（演示）", RGB(55, 65, 85));
#else
  (void)ctx;
#endif
}

void PropsDockPanel::paintEvent(PaintContext& ctx) {
#if defined(_WIN32)
  Win32Fill(ctx, RGB(244, 245, 247));
  Win32Edge(ctx, RGB(160, 170, 185));
  Win32DrawTextLeft(ctx, L"图层属性 Dock（演示）", RGB(55, 65, 85));
#else
  (void)ctx;
#endif
}

void MapCanvas2D::paintEvent(PaintContext& ctx) {
#if defined(_WIN32)
  HDC hdc = static_cast<HDC>(ctx.nativeDevice);
  if (!hdc || ctx.clip.w <= 0 || ctx.clip.h <= 0) {
    return;
  }
  const bool hot = App::instance().hoverWidget() == this;
  Win32Fill(ctx, hot ? RGB(232, 238, 250) : RGB(245, 248, 255));
  Win32Edge(ctx, RGB(160, 175, 200));

  if (show_grid_) {
    const int step = std::max(8, static_cast<int>(40.0 * scale_));
    const double fx = std::fmod(pan_x_, static_cast<double>(step));
    const double fy = std::fmod(pan_y_, static_cast<double>(step));
    HPEN pen = CreatePen(PS_SOLID, 1, RGB(200, 210, 225));
    const HGDIOBJ old = SelectObject(hdc, pen);
    for (int x = static_cast<int>(fx); x < ctx.clip.w; x += step) {
      MoveToEx(hdc, ctx.clip.x + x, ctx.clip.y, nullptr);
      LineTo(hdc, ctx.clip.x + x, ctx.clip.y + ctx.clip.h);
    }
    for (int y = static_cast<int>(fy); y < ctx.clip.h; y += step) {
      MoveToEx(hdc, ctx.clip.x, ctx.clip.y + y, nullptr);
      LineTo(hdc, ctx.clip.x + ctx.clip.w, ctx.clip.y + y);
    }
    SelectObject(hdc, old);
    DeleteObject(pen);
  }

  wchar_t buf[192]{};
  const int pct = static_cast<int>(std::lround(scale_ * 100.0));
  _snwprintf_s(buf, _TRUNCATE, L"2D canvas — %d%% — 中键平移 · 滚轮缩放（指针锚点）", pct);
  Win32DrawTextLeft(ctx, buf, RGB(45, 55, 75));
#else
  (void)ctx;
#endif
}

void MapCanvas2D::wheelAt(int client_x, int client_y, int wheel_delta) {
#if defined(_WIN32)
  int ox = 0;
  int oy = 0;
  WidgetRootClientOrigin(this, &ox, &oy);
  const double lx = static_cast<double>(client_x - ox);
  const double ly = static_cast<double>(client_y - oy);
  const double factor = wheel_delta > 0 ? 1.1 : 1.0 / 1.1;
  const double old_s = scale_;
  scale_ = std::clamp(scale_ * factor, 0.25, 4.0);
  const double ratio = scale_ / old_s;
  pan_x_ = lx - (lx - pan_x_) * ratio;
  pan_y_ = ly - (ly - pan_y_) * ratio;
#else
  (void)client_x;
  (void)client_y;
  (void)wheel_delta;
#endif
}

void MapCanvas2D::middleDown(int client_x, int client_y) {
  mdrag_ = true;
  mlast_x_ = client_x;
  mlast_y_ = client_y;
}

void MapCanvas2D::middleUp() { mdrag_ = false; }

void MapCanvas2D::zoomAtCenter(double factor) {
  const int cw = geometry().w;
  const int ch = geometry().h;
  if (cw <= 0 || ch <= 0) {
    return;
  }
  const double lx = static_cast<double>(cw) * 0.5;
  const double ly = static_cast<double>(ch) * 0.5;
  const double old_s = scale_;
  scale_ = std::clamp(scale_ * factor, 0.25, 4.0);
  const double ratio = scale_ / old_s;
  pan_x_ = lx - (lx - pan_x_) * ratio;
  pan_y_ = ly - (ly - pan_y_) * ratio;
}

void MapCanvas2D::fitViewDemo() {
  pan_x_ = 0.0;
  pan_y_ = 0.0;
  scale_ = 1.0;
}

void MapCanvas2D::originDemo() {
  const int cw = geometry().w;
  const int ch = geometry().h;
  if (cw <= 0 || ch <= 0) {
    return;
  }
  pan_x_ = static_cast<double>(cw) * 0.5;
  pan_y_ = static_cast<double>(ch) * 0.5;
}

void MapCanvas2D::resetZoom100() {
  const int cw = geometry().w;
  const int ch = geometry().h;
  if (cw <= 0 || ch <= 0) {
    return;
  }
  const double lx = static_cast<double>(cw) * 0.5;
  const double ly = static_cast<double>(ch) * 0.5;
  const double old_s = scale_;
  scale_ = 1.0;
  const double ratio = scale_ / old_s;
  pan_x_ = lx - (lx - pan_x_) * ratio;
  pan_y_ = ly - (ly - pan_y_) * ratio;
}

void MapCanvas2D::mouseMoveEvent(int client_x, int client_y, unsigned buttons) {
#if defined(_WIN32)
  int ox = 0;
  int oy = 0;
  WidgetClientOrigin(this, &ox, &oy);
  const int lx = client_x - ox;
  const int ly = client_y - oy;

  if (mdrag_ && (buttons & 4u) != 0) {
    const int dx = client_x - mlast_x_;
    const int dy = client_y - mlast_y_;
    mlast_x_ = client_x;
    mlast_y_ = client_y;
    pan_x_ += static_cast<double>(dx);
    pan_y_ += static_cast<double>(dy);
  }

  wchar_t buf[200]{};
  const int pct = static_cast<int>(std::lround(scale_ * 100.0));
  _snwprintf_s(buf, _TRUNCATE, L"Canvas (%d, %d) — %d%% — 网格 %s", lx, ly, pct,
               show_grid_ ? L"开" : L"关");
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
  HDC hdc = static_cast<HDC>(ctx.nativeDevice);
  SetBkMode(hdc, TRANSPARENT);
  SetTextColor(hdc, RGB(35, 40, 50));
  RECT title{ctx.clip.x + 6, ctx.clip.y + 2, ctx.clip.x + ctx.clip.w - 6, ctx.clip.y + 22};
  const wchar_t* t = expanded_ ? L"快捷键 ▲" : L"快捷键 ▼";
  DrawTextW(hdc, t, -1, &title, DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX);
  if (expanded_ && ctx.clip.h > 26) {
    RECT body{ctx.clip.x + 8, ctx.clip.y + 24, ctx.clip.x + ctx.clip.w - 8, ctx.clip.y + ctx.clip.h - 4};
    const wchar_t* txt =
        L"中键拖拽：平移\r\n"
        L"滚轮：缩放（指针锚点）\r\n"
        L"左下条：− / + / 适应 / 原点 / 还原\r\n"
        L"右上：切换背景网格";
    DrawTextW(hdc, txt, -1, &body, DT_LEFT | DT_WORDBREAK | DT_NOPREFIX);
  }
#else
  (void)ctx;
#endif
}

void ShortcutHelpPanel::mouseMoveEvent(int client_x, int client_y, unsigned buttons) {
  (void)client_x;
  (void)client_y;
  (void)buttons;
  App::instance().setStatusHint(L"快捷键说明 — 单击标题折叠/展开（默认折叠）");
}

void ShortcutHelpPanel::mousePressEvent(int client_x, int client_y, int button) {
  if (button != 1) {
    return;
  }
  int ox = 0;
  int oy = 0;
  WidgetRootClientOrigin(this, &ox, &oy);
  const int lx = client_x - ox;
  const int ly = client_y - oy;
  if (lx >= 0 && lx < geometry().w && ly >= 0 && ly < 24) {
    expanded_ = !expanded_;
    App::instance().relayoutFromLastClientSize();
    App::instance().invalidateAll();
  }
}

void LayerVisibilityPanel::paintEvent(PaintContext& ctx) {
#if defined(_WIN32)
  Win32Fill(ctx, RGB(255, 255, 255));
  Win32Edge(ctx, RGB(180, 188, 200));
  HDC hdc = static_cast<HDC>(ctx.nativeDevice);
  SetBkMode(hdc, TRANSPARENT);
  SetTextColor(hdc, RGB(40, 45, 55));
  bool grid = true;
  if (App::instance().demoMapCanvas()) {
    grid = App::instance().demoMapCanvas()->showGrid();
  }
  RECT rchk{ctx.clip.x + 8, ctx.clip.y + 10, ctx.clip.x + 28, ctx.clip.y + 30};
  const wchar_t* box = grid ? L"☑" : L"☐";
  DrawTextW(hdc, box, -1, &rchk, DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX);
  RECT rt{ctx.clip.x + 32, ctx.clip.y + 8, ctx.clip.x + ctx.clip.w - 8, ctx.clip.y + 34};
  DrawTextW(hdc, L"显示背景网格", -1, &rt, DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX);
  RECT rd{ctx.clip.x + 8, ctx.clip.y + 38, ctx.clip.x + ctx.clip.w - 8, ctx.clip.y + 66};
  DrawTextW(hdc, L"要素可见性（演示）", -1, &rd, DT_LEFT | DT_WORDBREAK | DT_NOPREFIX);
#else
  (void)ctx;
#endif
}

void LayerVisibilityPanel::mouseMoveEvent(int client_x, int client_y, unsigned buttons) {
  (void)client_x;
  (void)client_y;
  (void)buttons;
  App::instance().setStatusHint(L"可见性 — 单击切换背景网格");
}

void LayerVisibilityPanel::mousePressEvent(int client_x, int client_y, int button) {
  (void)client_x;
  (void)client_y;
  if (button != 1) {
    return;
  }
  if (auto* map = App::instance().demoMapCanvas()) {
    map->setShowGrid(!map->showGrid());
    App::instance().setStatusHint(map->showGrid() ? L"背景网格：开" : L"背景网格：关");
    App::instance().invalidateAll();
  }
}

void MapZoomBar::paintEvent(PaintContext& ctx) {
#if defined(_WIN32)
  Win32Fill(ctx, RGB(248, 249, 251));
  Win32Edge(ctx, RGB(190, 195, 205));
  const int w = ctx.clip.w;
  const int n = 5;
  const int seg = std::max(1, w / n);
  HPEN pen = CreatePen(PS_SOLID, 1, RGB(210, 215, 222));
  HDC hdc = static_cast<HDC>(ctx.nativeDevice);
  const HGDIOBJ old = SelectObject(hdc, pen);
  for (int i = 1; i < n; ++i) {
    const int x = ctx.clip.x + i * seg;
    MoveToEx(hdc, x, ctx.clip.y + 4, nullptr);
    LineTo(hdc, x, ctx.clip.y + ctx.clip.h - 4);
  }
  SelectObject(hdc, old);
  DeleteObject(pen);
  const wchar_t* parts[] = {L"−", L"+", L"适应", L"原点", L"还原"};
  for (int i = 0; i < n; ++i) {
    RECT rc{ctx.clip.x + i * seg, ctx.clip.y, ctx.clip.x + (i + 1) * seg, ctx.clip.y + ctx.clip.h};
    SetBkMode(hdc, TRANSPARENT);
    SetTextColor(hdc, RGB(35, 40, 50));
    DrawTextW(hdc, parts[i], -1, &rc, DT_CENTER | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX);
  }
#else
  (void)ctx;
#endif
}

void MapZoomBar::mouseMoveEvent(int client_x, int client_y, unsigned buttons) {
  (void)client_x;
  (void)client_y;
  (void)buttons;
  App::instance().setStatusHint(L"缩放条 — −／+／适应／原点／还原（无全局快捷键）");
}

void MapZoomBar::mousePressEvent(int client_x, int client_y, int button) {
  if (button != 1) {
    return;
  }
  int ox = 0;
  int oy = 0;
  WidgetRootClientOrigin(this, &ox, &oy);
  const int lx = client_x - ox;
  auto* canvas = dynamic_cast<MapCanvas2D*>(parent());
  if (!canvas) {
    return;
  }
  const int w = geometry().w;
  const int n = 5;
  const int seg = std::max(1, w / n);
  int idx = lx / seg;
  if (idx < 0) {
    idx = 0;
  }
  if (idx > n - 1) {
    idx = n - 1;
  }
  switch (idx) {
    case 0:
      canvas->zoomAtCenter(1.0 / 1.1);
      App::instance().setStatusHint(L"缩放：缩小");
      break;
    case 1:
      canvas->zoomAtCenter(1.1);
      App::instance().setStatusHint(L"缩放：放大");
      break;
    case 2:
      canvas->fitViewDemo();
      App::instance().setStatusHint(L"适应：平移与比例已重置（演示）");
      break;
    case 3:
      canvas->originDemo();
      App::instance().setStatusHint(L"原点：视口中心对齐（演示）");
      break;
    case 4:
      canvas->resetZoom100();
      App::instance().setStatusHint(L"还原：100% 以视口中心为锚点");
      break;
    default:
      break;
  }
  App::instance().invalidateAll();
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
