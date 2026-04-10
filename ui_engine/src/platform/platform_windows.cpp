#include "platform_windows.h"

#include "ui_engine/app.h"
#include "ui_engine/gdiplus_ui.h"
#include "ui_engine/widget.h"
#include "ui_engine/widget_core.h"
#include "ui_engine/widgets_mainframe.h"

#include "common/ui/ui_private.h"

#include <algorithm>
#include <string>

#include <windows.h>
#include <windowsx.h>

namespace agis::ui {

PlatformWindows::PlatformWindows() = default;

namespace {

constexpr wchar_t kDemoClassName[] = L"AGIS_UIEngineDemo";

std::wstring RootWindowTitle(const Widget* w) {
  if (const auto* win = dynamic_cast<const Window*>(w)) {
    if (!win->title().empty()) {
      return win->title();
    }
  }
  return L"AGIS";
}

/** 按父子 `geometry()` 叠加为窗口客户区坐标，深度优先调用各 `Widget::paintEvent`（Win32 / `HDC`）。 */
void PaintWidgetSubtreeWin32(HDC hdc, Widget* w, int parent_x, int parent_y) {
  if (!w || !w->visible()) {
    return;
  }
  const Rect g = w->geometry();
  const int x = parent_x + g.x;
  const int y = parent_y + g.y;

  PaintContext ctx;
  ctx.nativeDevice = hdc;
  ctx.clip = {x, y, g.w, g.h};
  w->paintEvent(ctx);

  /** `MainFrame`：菜单栏最后绘制，使下拉叠在 Dock / 画布 / 状态栏等之上（下拉为叠层，几何上落在下方控件区域内）。 */
  if (dynamic_cast<MainFrame*>(w)) {
    MenuBarWidget* menu_bar = nullptr;
    for (const auto& ch : w->children()) {
      if (auto* mb = dynamic_cast<MenuBarWidget*>(ch.get())) {
        menu_bar = mb;
        break;
      }
    }
    for (const auto& ch : w->children()) {
      if (ch.get() != menu_bar) {
        PaintWidgetSubtreeWin32(hdc, ch.get(), x, y);
      }
    }
    if (menu_bar) {
      PaintWidgetSubtreeWin32(hdc, menu_bar, x, y);
    }
    return;
  }

  for (const auto& ch : w->children()) {
    PaintWidgetSubtreeWin32(hdc, ch.get(), x, y);
  }
}

void DemoPaintClient(HDC hdc, const RECT& client, Widget* root) {
  if (!root) {
    HBRUSH bg = CreateSolidBrush(RGB(240, 242, 245));
    FillRect(hdc, &client, bg);
    DeleteObject(bg);
    return;
  }
  PaintWidgetSubtreeWin32(hdc, root, 0, 0);
}

bool WidgetIsDescendantOf(Widget* w, Widget* ancestor) {
  if (!w || !ancestor) {
    return false;
  }
  for (Widget* p = w; p; p = p->parent()) {
    if (p == ancestor) {
      return true;
    }
  }
  return false;
}

/** 控件左上角在顶层客户区中的坐标（与命中测试一致）。 */
void WidgetScreenOrigin(Widget* w, int* ox, int* oy) {
  *ox = 0;
  *oy = 0;
  for (Widget* p = w; p; p = p->parent()) {
    const Rect g = p->geometry();
    *ox += g.x;
    *oy += g.y;
  }
}

bool ClientPointInWidget(Widget* w, int client_x, int client_y) {
  if (!w || !w->visible()) {
    return false;
  }
  int ox = 0;
  int oy = 0;
  WidgetScreenOrigin(w, &ox, &oy);
  const Rect g = w->geometry();
  return client_x >= ox && client_y >= oy && client_x < ox + g.w && client_y < oy + g.h;
}

void TrySetDemoWindowIcon(HWND hwnd, HINSTANCE hinst) {
  wchar_t path[MAX_PATH]{};
  if (GetModuleFileNameW(hinst, path, MAX_PATH) == 0) {
    return;
  }
  wchar_t* slash = wcsrchr(path, L'\\');
  if (!slash) {
    return;
  }
  *(slash + 1) = L'\0';
  wcscat_s(path, L"ui_engine_demo.ico");
  HICON hi = static_cast<HICON>(LoadImageW(nullptr, path, IMAGE_ICON, 0, 0, LR_LOADFROMFILE));
  if (!hi) {
    return;
  }
  SendMessageW(hwnd, WM_SETICON, ICON_SMALL, reinterpret_cast<LPARAM>(hi));
  SendMessageW(hwnd, WM_SETICON, ICON_BIG, reinterpret_cast<LPARAM>(hi));
}

Widget* HitTestWidget(Widget* w, int client_x, int client_y, int origin_x, int origin_y) {
  if (!w || !w->visible()) {
    return nullptr;
  }
  const Rect g = w->geometry();
  const int ax = origin_x + g.x;
  const int ay = origin_y + g.y;
  bool inside = client_x >= ax && client_y >= ay && client_x < ax + g.w && client_y < ay + g.h;
  if (!inside) {
    if (auto* mb = dynamic_cast<MenuBarWidget*>(w)) {
      inside = mb->containsPointForHitTest(client_x, client_y, origin_x, origin_y);
    }
  }
  if (!inside) {
    return nullptr;
  }
  const auto& ch = w->children();

  /**
   * `MainFrame`：子控件在 vector 里菜单栏在前、画布等在后；逆序命中时画布先于菜单栏，
   * 下拉与画布重叠时误命中画布，`MenuItem` 无悬停。须与绘制顺序一致：**先**测菜单栏子树，
   * 再对其余子控件逆序命中。
   */
  if (dynamic_cast<MainFrame*>(w)) {
    MenuBarWidget* menu_bar = nullptr;
    for (const auto& c : ch) {
      if (auto* mb = dynamic_cast<MenuBarWidget*>(c.get())) {
        menu_bar = mb;
        break;
      }
    }
    if (menu_bar) {
      Widget* hit = HitTestWidget(menu_bar, client_x, client_y, ax, ay);
      if (hit) {
        return hit;
      }
    }
    for (auto it = ch.rbegin(); it != ch.rend(); ++it) {
      if (it->get() == menu_bar) {
        continue;
      }
      Widget* hit = HitTestWidget(it->get(), client_x, client_y, ax, ay);
      if (hit) {
        return hit;
      }
    }
    return w;
  }

  for (auto it = ch.rbegin(); it != ch.rend(); ++it) {
    Widget* hit = HitTestWidget(it->get(), client_x, client_y, ax, ay);
    if (hit) {
      return hit;
    }
  }
  return w;
}

LRESULT CALLBACK DemoWndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) {
  auto* root = reinterpret_cast<Widget*>(GetWindowLongPtr(hwnd, GWLP_USERDATA));

  switch (msg) {
    case WM_NCCREATE: {
      auto* cs = reinterpret_cast<CREATESTRUCTW*>(lParam);
      if (cs && cs->lpCreateParams) {
        SetWindowLongPtr(hwnd, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(cs->lpCreateParams));
      }
      return DefWindowProcW(hwnd, msg, wParam, lParam);
    }
    case WM_ERASEBKGND:
      /** 我们在 `WM_PAINT` 中整客户区自绘；禁止默认擦除以免与 `WM_PAINT` 叠画导致闪烁。 */
      return 1;
    case WM_DESTROY: {
      if (auto* plat = dynamic_cast<PlatformWindows*>(App::instance().platform())) {
        plat->NotifyRootWindowDestroyed(hwnd);
      }
      return 0;
    }
    case WM_SIZE: {
      if (wParam == SIZE_MINIMIZED || !root) {
        return DefWindowProcW(hwnd, msg, wParam, lParam);
      }
      RECT rc{};
      GetClientRect(hwnd, &rc);
      const int cw = rc.right - rc.left;
      const int ch = rc.bottom - rc.top;
      App::instance().notifyClientResize(root, cw, ch);
      InvalidateRect(hwnd, nullptr, FALSE);
      return 0;
    }
    case WM_MOUSEMOVE: {
      const int x = static_cast<short>(LOWORD(lParam));
      const int y = static_cast<short>(HIWORD(lParam));
      App& app = App::instance();
      app.setPointerClient(x, y);
      const unsigned buttons = static_cast<unsigned>(wParam) & 0xFFFFu;
      Widget* hit = nullptr;
      if (app.middleDragCanvas()) {
        hit = app.middleDragCanvas();
      } else {
        hit = root ? HitTestWidget(root, x, y, 0, 0) : nullptr;
      }
      Widget* prev = app.hoverWidget();
      const bool hover_changed = (hit != prev);
      if (hover_changed) {
        app.setHoverWidget(hit);
      }
      if (hit) {
        hit->mouseMoveEvent(x, y, buttons);
      }
      /**
       * 必须整客户区失效：`DemoPaintClient` 绘制整棵树；若仅失效状态栏条带，`WM_PAINT` 裁剪到条带后
       * 其它区域不会重绘，界面会错乱，命中区域与像素不一致，表现为「点不到菜单/工具栏」。
       * 双缓冲 `WM_PAINT` 后整窗重画不再明显闪烁。
       */
      InvalidateRect(hwnd, nullptr, FALSE);
      return 0;
    }
    case WM_LBUTTONDOWN: {
      SetFocus(hwnd);
      const int x = static_cast<short>(LOWORD(lParam));
      const int y = static_cast<short>(HIWORD(lParam));
      App::instance().setPointerClient(x, y);
      if (root) {
        Widget* hit = HitTestWidget(root, x, y, 0, 0);
        Menu* openm = App::instance().openDropDownMenu();
        if (openm && (!hit || !WidgetIsDescendantOf(hit, openm))) {
          App::instance().setOpenDropDownMenu(nullptr);
          hit = HitTestWidget(root, x, y, 0, 0);
        }
        App::instance().setHoverWidget(hit);
        if (hit) {
          hit->mousePressEvent(x, y, 1);
        }
        InvalidateRect(hwnd, nullptr, FALSE);
      }
      return 0;
    }
    case WM_LBUTTONUP: {
      const int x = static_cast<short>(LOWORD(lParam));
      const int y = static_cast<short>(HIWORD(lParam));
      App::instance().setPointerClient(x, y);
      if (root) {
        Widget* hit = HitTestWidget(root, x, y, 0, 0);
        if (hit) {
          hit->mouseReleaseEvent(x, y, 1);
        }
        InvalidateRect(hwnd, nullptr, FALSE);
      }
      return 0;
    }
    case WM_RBUTTONDOWN: {
      SetFocus(hwnd);
      const int x = static_cast<short>(LOWORD(lParam));
      const int y = static_cast<short>(HIWORD(lParam));
      App::instance().setPointerClient(x, y);
      if (!root) {
        return 0;
      }
      Widget* hit = HitTestWidget(root, x, y, 0, 0);
      if (auto* list = dynamic_cast<DemoTestListPanel*>(hit)) {
        int row = 0;
        if (list->computeRowAt(x, y, &row)) {
          list->setSelectedRow(row);
        }
        HMENU menu = CreatePopupMenu();
        const bool zh = App::instance().uiLanguage() == UiLanguage::kZhCN;
        AppendMenuW(menu, MF_STRING, 1, zh ? L"运行" : L"Run");
        AppendMenuW(menu, MF_STRING, 2, zh ? L"详情" : L"Details");
        POINT pt{x, y};
        ClientToScreen(hwnd, &pt);
        const UINT cmd =
            TrackPopupMenu(menu, TPM_LEFTALIGN | TPM_TOPALIGN | TPM_RETURNCMD, pt.x, pt.y, 0, hwnd, nullptr);
        DestroyMenu(menu);
        if (cmd == 1 || cmd == 2) {
          App::instance().runDemoTestAction(cmd == 2 ? 1 : 0, row);
        }
        InvalidateRect(hwnd, nullptr, FALSE);
        return 0;
      }
      return DefWindowProcW(hwnd, msg, wParam, lParam);
    }
    case WM_LBUTTONDBLCLK: {
      const int x = static_cast<short>(LOWORD(lParam));
      const int y = static_cast<short>(HIWORD(lParam));
      App::instance().setPointerClient(x, y);
      if (root) {
        Widget* hit = HitTestWidget(root, x, y, 0, 0);
        if (auto* list = dynamic_cast<DemoTestListPanel*>(hit)) {
          list->handleDoubleClickAt(x, y);
        } else if (dynamic_cast<StatusBarWidget*>(hit)) {
          App::instance().setStatusHint(
              L"[Log 演示] GIS 主程序中双击状态栏打开纯文本日志窗；本演示仅更新状态栏文案。");
        }
        InvalidateRect(hwnd, nullptr, FALSE);
      }
      return 0;
    }
    case WM_MBUTTONDOWN: {
      SetFocus(hwnd);
      const int x = static_cast<short>(LOWORD(lParam));
      const int y = static_cast<short>(HIWORD(lParam));
      App::instance().setPointerClient(x, y);
      MapCanvas2D* demo_map = App::instance().demoMapCanvas();
      if (demo_map && ClientPointInWidget(demo_map, x, y)) {
        App::instance().setMiddleDragCanvas(demo_map);
        demo_map->middleDown(x, y);
        SetCapture(hwnd);
        InvalidateRect(hwnd, nullptr, FALSE);
        return 0;
      }
      if (root) {
        Widget* hit = HitTestWidget(root, x, y, 0, 0);
        if (auto* map = dynamic_cast<MapCanvas2D*>(hit)) {
          App::instance().setMiddleDragCanvas(map);
          map->middleDown(x, y);
          SetCapture(hwnd);
        }
        InvalidateRect(hwnd, nullptr, FALSE);
      }
      return 0;
    }
    case WM_MBUTTONUP: {
      const int x = static_cast<short>(LOWORD(lParam));
      const int y = static_cast<short>(HIWORD(lParam));
      App::instance().setPointerClient(x, y);
      if (App::instance().middleDragCanvas()) {
        App::instance().middleDragCanvas()->middleUp();
        App::instance().setMiddleDragCanvas(nullptr);
        ReleaseCapture();
        InvalidateRect(hwnd, nullptr, FALSE);
      }
      return 0;
    }
    case WM_MOUSEWHEEL: {
      const int delta = static_cast<short>(HIWORD(wParam));
      POINT pt{GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam)};
      ScreenToClient(hwnd, &pt);
      App::instance().setPointerClient(pt.x, pt.y);
      if (root) {
        Widget* hit = HitTestWidget(root, pt.x, pt.y, 0, 0);
        if (auto* list = dynamic_cast<DemoTestListPanel*>(hit)) {
          list->wheelEvent(pt.x, pt.y, delta);
          InvalidateRect(hwnd, nullptr, FALSE);
          return 0;
        }
        MapCanvas2D* demo_map = App::instance().demoMapCanvas();
        if (demo_map && ClientPointInWidget(demo_map, pt.x, pt.y)) {
          demo_map->wheelAt(pt.x, pt.y, delta);
          InvalidateRect(hwnd, nullptr, FALSE);
          return 0;
        }
        if (auto* map = dynamic_cast<MapCanvas2D*>(hit)) {
          map->wheelAt(pt.x, pt.y, delta);
          InvalidateRect(hwnd, nullptr, FALSE);
        }
      }
      return 0;
    }
    case WM_KEYDOWN:
      if (wParam == VK_ESCAPE) {
        if (App::instance().openDropDownMenu()) {
          App::instance().setOpenDropDownMenu(nullptr);
          InvalidateRect(hwnd, nullptr, FALSE);
        }
      }
      return 0;
    case WM_PAINT: {
      PAINTSTRUCT ps{};
      HDC hdc_screen = BeginPaint(hwnd, &ps);
      RECT client{};
      GetClientRect(hwnd, &client);
      const int cw = client.right - client.left;
      const int ch = client.bottom - client.top;
      if (cw > 0 && ch > 0) {
        HDC hdc_mem = CreateCompatibleDC(hdc_screen);
        HBITMAP bmp = CreateCompatibleBitmap(hdc_screen, cw, ch);
        HBITMAP old_bmp = static_cast<HBITMAP>(SelectObject(hdc_mem, bmp));
        DemoPaintClient(hdc_mem, client, root);
        BitBlt(hdc_screen, 0, 0, cw, ch, hdc_mem, 0, 0, SRCCOPY);
        SelectObject(hdc_mem, old_bmp);
        DeleteObject(bmp);
        DeleteDC(hdc_mem);
      }
      EndPaint(hwnd, &ps);
      return 0;
    }
    default:
      return DefWindowProcW(hwnd, msg, wParam, lParam);
  }
}

}  // namespace

PlatformWindows::PlatformWindows(const AppLaunchParams& launch)
    : mode_(PlatformWindows::Mode::UiEngineDemo), native_instance_(launch.native_app_instance) {
  show_window_command_ = launch.show_window_command != 0 ? launch.show_window_command : SW_SHOWNORMAL;
  UiGdiplusInit();

  const HINSTANCE hinst = static_cast<HINSTANCE>(native_instance_);

  WNDCLASSEXW wc{};
  wc.cbSize = sizeof(wc);
  /** 自绘整客户区；勿用 CS_HREDRAW|CS_VREDRAW，否则易触发整窗重画加剧闪烁。 */
  wc.style = 0;
  wc.lpfnWndProc = DemoWndProc;
  wc.hInstance = hinst;
  wc.hCursor = LoadCursor(nullptr, IDC_ARROW);
  wc.hbrBackground = reinterpret_cast<HBRUSH>(COLOR_WINDOW + 1);
  wc.lpszClassName = kDemoClassName;
  if (RegisterClassExW(&wc)) {
    class_registered_ = true;
  }
}

void PlatformWindows::NotifyRootWindowDestroyed(void* hwnd) {
  auto it = std::find(root_windows_.begin(), root_windows_.end(), hwnd);
  if (it != root_windows_.end()) {
    root_windows_.erase(it);
  }
  if (root_windows_.empty() && !teardown_without_quit_) {
    PostQuitMessage(0);
  }
}

PlatformWindows::~PlatformWindows() {
  if (!this || this->mode_ != PlatformWindows::Mode::UiEngineDemo) {
    return;
  }
  teardown_without_quit_ = true;
  const std::vector<void*> copy = root_windows_;
  for (void* p : copy) {
    const HWND hwnd = static_cast<HWND>(p);
    if (IsWindow(hwnd)) {
      DestroyWindow(hwnd);
    }
  }
  root_windows_.clear();
  if (this->class_registered_ && this->native_instance_) {
    UnregisterClassW(kDemoClassName, static_cast<HINSTANCE>(this->native_instance_));
    this->class_registered_ = false;
  }
  App::instance().clearRootWidgets();
  UiGdiplusShutdown();
  this->mode_ = PlatformWindows::Mode::Basic;
}

#if defined(_WIN32)
bool PlatformWindows::ok() const {
  if (mode_ == PlatformWindows::Mode::UiEngineDemo) {
    return class_registered_ && native_instance_ != nullptr;
  }
  return true;
}
#endif

int PlatformWindows::runEventLoop(App& app) {
  if (mode_ == Mode::UiEngineDemo) {
    const HINSTANCE hinst = static_cast<HINSTANCE>(native_instance_);
    const auto& roots = app.rootWidgets();
    bool first = true;
    for (const auto& up : roots) {
      Widget* rw = up.get();
      if (!rw) {
        continue;
      }
      const Rect r = rw->geometry();
      const int ww = (r.w > 0) ? r.w : 960;
      const int hh = (r.h > 0) ? r.h : 640;
      const std::wstring title = RootWindowTitle(rw);
      HWND hwnd =
          CreateWindowExW(0, kDemoClassName, title.c_str(), WS_OVERLAPPEDWINDOW, CW_USEDEFAULT, CW_USEDEFAULT, ww, hh,
                          nullptr, nullptr, hinst, static_cast<LPVOID>(rw));
      if (!hwnd) {
        continue;
      }
      App::instance().setDemoHostWindow(hwnd);
      TrySetDemoWindowIcon(hwnd, hinst);
      SetWindowLongPtr(hwnd, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(rw));
      RECT crc{};
      GetClientRect(hwnd, &crc);
      const int cw0 = crc.right - crc.left;
      const int ch0 = crc.bottom - crc.top;
      if (cw0 > 0 && ch0 > 0) {
        app.notifyClientResize(rw, cw0, ch0);
      }
      root_windows_.push_back(hwnd);
      app.setInvalidateAllHandler([this]() {
        for (void* p : root_windows_) {
          if (p) {
            InvalidateRect(static_cast<HWND>(p), nullptr, FALSE);
          }
        }
      });
      const int show = first ? show_window_command_ : SW_SHOW;
      ShowWindow(hwnd, show);
      UpdateWindow(hwnd);
      RECT crc2{};
      GetClientRect(hwnd, &crc2);
      const int cw1 = crc2.right - crc2.left;
      const int ch1 = crc2.bottom - crc2.top;
      if (cw1 > 0 && ch1 > 0) {
        app.notifyClientResize(rw, cw1, ch1);
      }
      first = false;
    }
    if (root_windows_.empty()) {
      PostQuitMessage(0);
    }
  }

  MSG msg{};
  while (GetMessageW(&msg, nullptr, 0, 0) > 0) {
    TranslateMessage(&msg);
    DispatchMessageW(&msg);
  }
  return static_cast<int>(msg.wParam);
}

void PlatformWindows::requestExit() { PostQuitMessage(0); }

const char* PlatformWindows::backendId() const { return "win32"; }

}  // namespace agis::ui
