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

LRESULT CALLBACK DemoWndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) {
  (void)lParam;
  switch (msg) {
    case WM_DESTROY: {
      if (auto* plat = dynamic_cast<PlatformWindows*>(App::instance().platform())) {
        plat->NotifyRootWindowDestroyed(hwnd);
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
      HDC hdc = BeginPaint(hwnd, &ps);
      RECT client{};
      GetClientRect(hwnd, &client);
      auto* root = reinterpret_cast<Widget*>(GetWindowLongPtr(hwnd, GWLP_USERDATA));
      DemoPaintClient(hdc, client, root);
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
  wc.style = CS_HREDRAW | CS_VREDRAW;
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
      HWND hwnd = CreateWindowExW(0, kDemoClassName, title.c_str(), WS_OVERLAPPEDWINDOW, CW_USEDEFAULT, CW_USEDEFAULT,
                                  ww, hh, nullptr, nullptr, hinst, nullptr);
      if (!hwnd) {
        continue;
      }
      SetWindowLongPtr(hwnd, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(rw));
      root_windows_.push_back(hwnd);
      const int show = first ? show_window_command_ : SW_SHOW;
      ShowWindow(hwnd, show);
      UpdateWindow(hwnd);
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
