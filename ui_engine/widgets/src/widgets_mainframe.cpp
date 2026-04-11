#include "ui_engine/widgets_mainframe.h"
#include "ui_engine/ui_private.h"

#include "ui_engine/app.h"

#include <algorithm>
#include <cmath>
#include <utility>
#include <vector>

#if defined(_WIN32)
#include <stdio.h>
#include <windows.h>
#include <objidl.h>
#include <gdiplus.h>
#pragma comment(lib, "gdiplus.lib")
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
constexpr int kMenuDropItemRowHeight = 26;

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
  Win32Fill(ctx, App::instance().themeColorSurface());
#else
  (void)ctx;
#endif
}

void MenuBarWidget::paintEvent(PaintContext& ctx) {
#if defined(_WIN32)
  const App& app = App::instance();
  const bool hot = app.hoverWidget() == this;
  Win32Fill(ctx, hot ? app.themeColorSurfaceAlt() : app.themeColorSurface());
  Win32Edge(ctx, app.themeColorMuted());
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
  const App& app = App::instance();
  const bool hot = app.hoverWidget() == this;
  const int strip = std::min(bar_strip_h_, ctx.clip.h);
  RECT rc_full{ctx.clip.x, ctx.clip.y, ctx.clip.x + ctx.clip.w, ctx.clip.y + ctx.clip.h};
  HDC hdc = static_cast<HDC>(ctx.nativeDevice);
  if (strip > 0 && strip <= ctx.clip.h) {
    RECT rc_top{ctx.clip.x, ctx.clip.y, ctx.clip.x + ctx.clip.w, ctx.clip.y + strip};
    HBRUSH bt = CreateSolidBrush(hot ? app.themeColorSurfaceAlt() : app.themeColorSurface());
    FillRect(hdc, &rc_top, bt);
    DeleteObject(bt);
    SetBkMode(hdc, TRANSPARENT);
    SetTextColor(hdc, app.themeColorText());
    RECT rc_title = rc_top;
    rc_title.left += 8;
    DrawTextW(hdc, title_.empty() ? L"Menu" : title_.c_str(), -1, &rc_title,
              DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX);
  }
  if (ctx.clip.h > strip) {
    RECT rc_drop{ctx.clip.x, ctx.clip.y + strip, ctx.clip.x + ctx.clip.w, ctx.clip.y + ctx.clip.h};
    HBRUSH bd = CreateSolidBrush(app.themeColorSurfaceAlt());
    FillRect(hdc, &rc_drop, bd);
    DeleteObject(bd);
    HBRUSH be = CreateSolidBrush(app.themeColorMuted());
    FrameRect(hdc, &rc_full, be);
    DeleteObject(be);
  } else {
    HBRUSH be = CreateSolidBrush(app.themeColorMuted());
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
  const App& app = App::instance();
  const bool hot = app.hoverWidget() == this;
  Win32Fill(ctx, hot ? app.themeColorSurfaceAlt() : app.themeColorSurface());
  Win32DrawTextLeft(ctx, text_.empty() ? L"Item" : text_.c_str(),
                    enabled_ ? app.themeColorText() : app.themeColorMuted());
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
  if (!enabled() || button != 1) {
    return;
  }
  if (on_activate_) {
    on_activate_();
    App::instance().setOpenDropDownMenu(nullptr);
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
  const App& app = App::instance();
  const bool hot = app.hoverWidget() == this;
  Win32Fill(ctx, hot ? app.themeColorSurfaceAlt() : app.themeColorSurface());
  Win32Edge(ctx, app.themeColorMuted());
  Win32DrawTextCenter(ctx, text_.empty() ? L"?" : text_.c_str(), app.themeColorText());
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
  if (on_activate_) {
    on_activate_();
    return;
  }
  std::wstring h = L"Toolbar 点击: ";
  h += text_.empty() ? L"(empty)" : text_;
  App::instance().setStatusHint(std::move(h));
}

void ToolBarWidget::paintEvent(PaintContext& ctx) {
#if defined(_WIN32)
  const App& app = App::instance();
  const bool hot = app.hoverWidget() == this;
  Win32Fill(ctx, hot ? app.themeColorSurfaceAlt() : app.themeColorSurface());
  Win32Edge(ctx, app.themeColorMuted());
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
  const App& app = App::instance();
  Win32Fill(ctx, app.themeColorSurfaceAlt());
  Win32Edge(ctx, app.themeColorMuted());
  wchar_t buf[512]{};
  const std::wstring& hint = app.statusHint();
  _snwprintf_s(buf, _TRUNCATE, L"Pointer: (%d, %d)   %ls", app.pointerClientX(), app.pointerClientY(), hint.c_str());
  Win32DrawTextLeft(ctx, buf, app.themeColorText());
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

MapCanvas2D::~MapCanvas2D() { releaseBitmap(); }

void MapCanvas2D::releaseBitmap() {
#if defined(_WIN32)
  if (gdi_bitmap_) {
    delete static_cast<Gdiplus::Bitmap*>(gdi_bitmap_);
    gdi_bitmap_ = nullptr;
  }
#endif
  bitmap_px_ = 0;
  bitmap_py_ = 0;
}

void MapCanvas2D::clearImage() {
  releaseBitmap();
  doc_path_.clear();
}

bool MapCanvas2D::loadImageFile(const wchar_t* path) {
#if defined(_WIN32)
  if (!path || !path[0]) {
    return false;
  }
  clearImage();
  auto* b = Gdiplus::Bitmap::FromFile(path);
  if (!b || b->GetLastStatus() != Gdiplus::Ok) {
    delete b;
    return false;
  }
  gdi_bitmap_ = b;
  bitmap_px_ = static_cast<int>(b->GetWidth());
  bitmap_py_ = static_cast<int>(b->GetHeight());
  doc_path_ = path;
  fitImageToView();
  return true;
#else
  (void)path;
  return false;
#endif
}

#if defined(_WIN32)
int GetImageEncoderClsid(const wchar_t* mime, CLSID* clsid) {
  UINT n = 0;
  UINT sz = 0;
  Gdiplus::GetImageEncodersSize(&n, &sz);
  if (sz == 0) {
    return -1;
  }
  std::vector<BYTE> buf(sz);
  auto* info = reinterpret_cast<Gdiplus::ImageCodecInfo*>(buf.data());
  Gdiplus::GetImageEncoders(n, sz, info);
  for (UINT i = 0; i < n; ++i) {
    if (wcscmp(info[i].MimeType, mime) == 0) {
      *clsid = info[i].Clsid;
      return static_cast<int>(i);
    }
  }
  return -1;
}
#endif

bool MapCanvas2D::saveImageToFile(const wchar_t* path) const {
#if defined(_WIN32)
  if (!path || !path[0] || !gdi_bitmap_) {
    return false;
  }
  const wchar_t* dot = wcsrchr(path, L'.');
  CLSID clsid{};
  if (dot && (_wcsicmp(dot, L".png") == 0)) {
    if (GetImageEncoderClsid(L"image/png", &clsid) < 0) {
      return false;
    }
  } else {
    if (GetImageEncoderClsid(L"image/bmp", &clsid) < 0) {
      return false;
    }
  }
  auto* bmp = static_cast<Gdiplus::Bitmap*>(gdi_bitmap_);
  return bmp->Save(path, &clsid, nullptr) == Gdiplus::Ok;
#else
  (void)path;
  return false;
#endif
}

void MapCanvas2D::fitImageToView() {
  const int cw = geometry().w;
  const int ch = geometry().h;
  if (cw <= 0 || ch <= 0 || bitmap_px_ <= 0 || bitmap_py_ <= 0) {
    return;
  }
  const double sx = static_cast<double>(cw) * 0.92 / static_cast<double>(bitmap_px_);
  const double sy = static_cast<double>(ch) * 0.92 / static_cast<double>(bitmap_py_);
  scale_ = std::clamp(std::min(sx, sy), 0.05, 8.0);
  pan_x_ = (static_cast<double>(cw) - static_cast<double>(bitmap_px_) * scale_) * 0.5;
  pan_y_ = (static_cast<double>(ch) - static_cast<double>(bitmap_py_) * scale_) * 0.5;
}

void MapCanvas2D::paintEvent(PaintContext& ctx) {
#if defined(_WIN32)
  HDC hdc = static_cast<HDC>(ctx.nativeDevice);
  if (!hdc || ctx.clip.w <= 0 || ctx.clip.h <= 0) {
    return;
  }
  const App& app = App::instance();
  const bool hot = app.hoverWidget() == this;
  Win32Fill(ctx, hot ? app.themeColorSurfaceAlt() : app.themeColorSurface());
  Win32Edge(ctx, RGB(130, 150, 190));

  if (gdi_bitmap_) {
    Gdiplus::Graphics g(hdc);
    g.SetInterpolationMode(Gdiplus::InterpolationModeHighQualityBicubic);
    g.SetPixelOffsetMode(Gdiplus::PixelOffsetModeHalf);
    auto* bmp = static_cast<Gdiplus::Bitmap*>(gdi_bitmap_);
    Gdiplus::Matrix m(
        static_cast<Gdiplus::REAL>(scale_), 0.f, 0.f, static_cast<Gdiplus::REAL>(scale_),
        static_cast<Gdiplus::REAL>(ctx.clip.x + pan_x_), static_cast<Gdiplus::REAL>(ctx.clip.y + pan_y_));
    g.SetTransform(&m);
    g.DrawImage(bmp, 0, 0, static_cast<INT>(bitmap_px_), static_cast<INT>(bitmap_py_));
    g.ResetTransform();
  }

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
  _snwprintf_s(buf, _TRUNCATE, L"2D · %d%% · 中键拖拽平移 · 滚轮缩放", pct);
  Win32DrawTextLeft(ctx, buf, app.themeColorText());
#else
  (void)ctx;
#endif
}

void MapCanvas2D::wheelEvent(int client_x, int client_y, int delta) { wheelAt(client_x, client_y, delta); }

void MapCanvas2D::wheelAt(int client_x, int client_y, int wheel_delta) {
#if defined(_WIN32)
  int ox = 0;
  int oy = 0;
  WidgetRootClientOrigin(this, &ox, &oy);
  const double lx = static_cast<double>(client_x - ox);
  const double ly = static_cast<double>(client_y - oy);
  const double factor = wheel_delta > 0 ? 1.1 : 1.0 / 1.1;
  const double old_s = scale_;
  scale_ = std::clamp(scale_ * factor, 0.05, 8.0);
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
  scale_ = std::clamp(scale_ * factor, 0.05, 8.0);
  const double ratio = scale_ / old_s;
  pan_x_ = lx - (lx - pan_x_) * ratio;
  pan_y_ = ly - (ly - pan_y_) * ratio;
}

void MapCanvas2D::fitViewDemo() {
  if (hasImage()) {
    fitImageToView();
  } else {
    pan_x_ = 0.0;
    pan_y_ = 0.0;
    scale_ = 1.0;
  }
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

int DemoTestListPanel::rowAtLocalY(int ly) const {
  const int y = ly + scroll_y_;
  if (y < 0) {
    return -1;
  }
  return y / kRowH;
}

bool DemoTestListPanel::computeRowAt(int client_x, int client_y, int* out_row) const {
  int ox = 0;
  int oy = 0;
  WidgetRootClientOrigin(this, &ox, &oy);
  const int lx = client_x - ox;
  const int ly = client_y - oy;
  const Rect g = geometry();
  if (lx < 0 || ly < 0 || lx >= g.w || ly >= g.h) {
    return false;
  }
  const int r = rowAtLocalY(ly);
  if (r < 0 || r >= static_cast<int>(specs_.size())) {
    return false;
  }
  if (out_row) {
    *out_row = r;
  }
  return true;
}

void DemoTestListPanel::activateRow(int row, bool details_only) {
  if (row < 0 || row >= static_cast<int>(specs_.size())) {
    return;
  }
  const auto& s = specs_[static_cast<size_t>(row)];
  if (s.is_header) {
    return;
  }
  App::instance().runDemoTestAction(details_only ? 1 : 0, row);
}

void DemoTestListPanel::paintEvent(PaintContext& ctx) {
#if defined(_WIN32)
  HDC hdc = static_cast<HDC>(ctx.nativeDevice);
  const App& app = App::instance();
  Win32Fill(ctx, app.themeColorSurfaceAlt());
  Win32Edge(ctx, RGB(180, 188, 200));

  const int total_h = static_cast<int>(specs_.size()) * kRowH;
  const int view_h = ctx.clip.h;
  const int max_scroll = std::max(0, total_h - view_h);
  if (scroll_y_ > max_scroll) {
    scroll_y_ = max_scroll;
  }

  SetBkMode(hdc, TRANSPARENT);
  HFONT font = CreateFontW(-15, 0, 0, 0, FW_NORMAL, 0, 0, 0, DEFAULT_CHARSET, OUT_DEFAULT_PRECIS,
                           CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY, DEFAULT_PITCH | FF_DONTCARE, L"Segoe UI");
  const HGDIOBJ oldf = SelectObject(hdc, font);

  for (int i = 0; i < static_cast<int>(specs_.size()); ++i) {
    const int y = i * kRowH - scroll_y_;
    if (y + kRowH < 0 || y > view_h) {
      continue;
    }
    const auto& s = specs_[static_cast<size_t>(i)];
    RECT row_rc{ctx.clip.x, ctx.clip.y + y, ctx.clip.x + ctx.clip.w, ctx.clip.y + y + kRowH};
    if (i == selected_row_) {
      HBRUSH hi = CreateSolidBrush(RGB(88, 120, 220));
      FillRect(hdc, &row_rc, hi);
      DeleteObject(hi);
      SetTextColor(hdc, RGB(255, 255, 255));
    } else if (i == hover_row_) {
      HBRUSH hi = CreateSolidBrush(app.themeColorSurface());
      FillRect(hdc, &row_rc, hi);
      SetTextColor(hdc, app.themeColorText());
    } else if (s.is_header) {
      HBRUSH hi = CreateSolidBrush(RGB(230, 232, 238));
      FillRect(hdc, &row_rc, hi);
      SetTextColor(hdc, app.themeColorMuted());
    } else {
      SetTextColor(hdc, app.themeColorText());
    }

    RECT text_rc{row_rc.left + 10, row_rc.top + 2, row_rc.right - 6, row_rc.bottom - 2};
    const wchar_t* prefix = s.is_header ? L"" : (s.no_root_test ? L"◆ " : L"  ");
    std::wstring line = prefix;
    line += s.label;
    DrawTextW(hdc, line.c_str(), -1, &text_rc, DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX);
  }

  SelectObject(hdc, oldf);
  DeleteObject(font);
#else
  (void)ctx;
#endif
}

void DemoTestListPanel::mouseMoveEvent(int client_x, int client_y, unsigned buttons) {
  (void)buttons;
  int row = -1;
  if (computeRowAt(client_x, client_y, &row)) {
    hover_row_ = row;
  } else {
    hover_row_ = -1;
  }
  App::instance().setStatusHint(L"测试列表 — 双击运行；右键运行/详情");
}

void DemoTestListPanel::mousePressEvent(int client_x, int client_y, int button) {
  if (button != 1) {
    return;
  }
  int row = -1;
  if (computeRowAt(client_x, client_y, &row)) {
    selected_row_ = row;
  }
  App::instance().invalidateAll();
}

void DemoTestListPanel::wheelEvent(int client_x, int client_y, int delta) {
  (void)client_x;
  (void)client_y;
  const int step = kRowH * 3;
  scroll_y_ -= (delta / 120) * step;
  if (scroll_y_ < 0) {
    scroll_y_ = 0;
  }
  App::instance().invalidateAll();
}

void DemoTestListPanel::handleDoubleClickAt(int client_x, int client_y) {
  int row = -1;
  if (!computeRowAt(client_x, client_y, &row)) {
    return;
  }
  const auto& s = specs_[static_cast<size_t>(row)];
  if (s.is_header) {
    return;
  }
#if defined(_WIN32)
  if (s.no_root_test) {
    MessageBoxW(static_cast<HWND>(App::instance().demoHostWindow()),
                L"该项无法作为根控件单独测试。\n请通过主框架组合场景或其它集成测试查看。",
                L"ui_engine 演示", MB_OK | MB_ICONINFORMATION);
    return;
  }
#endif
  activateRow(row, false);
}

void DemoRightSlotPanel::paintEvent(PaintContext& ctx) {
#if defined(_WIN32)
  const App& app = App::instance();
  Win32Fill(ctx, app.themeColorSurfaceAlt());
  Win32Edge(ctx, RGB(170, 178, 192));
  wchar_t buf[128]{};
  _snwprintf_s(buf, _TRUNCATE, L"Dock #%d — %s", slot_ + 1, title_.empty() ? L"（空）" : title_.c_str());
  HDC hdc = static_cast<HDC>(ctx.nativeDevice);
  SetBkMode(hdc, TRANSPARENT);
  SetTextColor(hdc, app.themeColorMuted());
  RECT rc{ctx.clip.x + 10, ctx.clip.y + 8, ctx.clip.x + ctx.clip.w - 8, ctx.clip.y + ctx.clip.h - 6};
  DrawTextW(hdc, buf, -1, &rc, DT_LEFT | DT_WORDBREAK | DT_NOPREFIX);
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
