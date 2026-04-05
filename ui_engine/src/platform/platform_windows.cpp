#include "platform_windows.h"

#include "ui_engine/app.h"
#include "ui_engine/gdiplus_ui.h"
#include "ui_engine/widget.h"
#include "ui_engine/widget_core.h"

#include <algorithm>
#include <string>

#include <windows.h>

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

Widget* HitTestWidget(Widget* w, int client_x, int client_y, int origin_x, int origin_y) {
  if (!w || !w->visible()) {
    return nullptr;
  }
  const Rect g = w->geometry();
  const int ax = origin_x + g.x;
  const int ay = origin_y + g.y;
  if (client_x < ax || client_y < ay || client_x >= ax + g.w || client_y >= ay + g.h) {
    return nullptr;
  }
  const auto& ch = w->children();
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
      Widget* hit = root ? HitTestWidget(root, x, y, 0, 0) : nullptr;
      Widget* prev = app.hoverWidget();
      const bool hover_changed = (hit != prev);
      if (hover_changed) {
        app.setHoverWidget(hit);
      }
      const unsigned buttons = static_cast<unsigned>(wParam) & 0xFFFFu;
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
    case WM_KEYDOWN:
      if (wParam == VK_ESCAPE) {
        App::instance().requestQuit();
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
      SetWindowLongPtr(hwnd, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(rw));
      RECT crc{};
      GetClientRect(hwnd, &crc);
      const int cw0 = crc.right - crc.left;
      const int ch0 = crc.bottom - crc.top;
      if (cw0 > 0 && ch0 > 0) {
        app.notifyClientResize(rw, cw0, ch0);
      }
      root_windows_.push_back(hwnd);
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
