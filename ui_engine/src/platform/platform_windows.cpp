#include "platform_windows.h"

#if defined(AGIS_BUILD_UI_ENGINE_DEMO)
#include "ui_engine_demo.h"
#endif
#include "ui_engine/app.h"
#include "ui_engine/gdiplus_ui.h"
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

void DemoPaintClient(HDC hdc, const RECT& client, Widget* root) {
  const int w = client.right - client.left;
  const int h = client.bottom - client.top;
  HBRUSH bg = CreateSolidBrush(RGB(240, 242, 245));
  FillRect(hdc, &client, bg);
  DeleteObject(bg);

  RECT rcLayer{client.left + 8, client.top + 8, client.left + 8 + 200, client.top + 8 + 180};
  UiPaintLayerPanel(hdc, rcLayer);

  RECT rcProps{client.right - 280, client.top + 8, client.right - 8, client.top + 200};
  RECT driverCard{rcProps.left + 12, rcProps.top + 40, rcProps.right - 12, rcProps.top + 100};
  RECT sourceCard{rcProps.left + 12, rcProps.top + 108, rcProps.right - 12, rcProps.top + 180};
  UiPaintLayerPropsDockFrame(hdc, rcProps, &driverCard, &sourceCard, L"ui_engine_demo");

  RECT rcCenter{client.left + w / 4, client.top + h / 4, client.right - w / 4, client.bottom - h / 4};
  UiPaintMapCenterHint(hdc, rcCenter, L"MapCenterHint / GDI+");

  RECT rcSmall{client.left + 230, client.top + 200, client.left + 430, client.top + 320};
  UiPaintLayerPropsPanel(hdc, rcSmall, L"PropsPanel", L"body line");

  RECT rcHint{client.right - 320, client.bottom - 48, client.right - 12, client.bottom - 12};
  UiPaintMapHintOverlay(hdc, rcHint, L"UiPaintMapHintOverlay — ESC 退出");

  SetBkMode(hdc, TRANSPARENT);
  SetTextColor(hdc, RGB(20, 24, 32));
#if defined(AGIS_BUILD_UI_ENGINE_DEMO)
  const std::wstring line = FormatUiEngineDemoStatusLine(App::instance(), root);
#else
  const std::wstring line = L"";
#endif
  RECT rcText{client.left + 12, client.bottom - 72, client.right - 12, client.bottom - 36};
  DrawTextW(hdc, line.c_str(), static_cast<int>(line.size()), &rcText, DT_LEFT | DT_WORDBREAK);
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
