#include "ui_engine/platform_windows.h"

#include "ui_engine/app.h"

#include <windows.h>

#if defined(AGIS_BUILD_UI_ENGINE_DEMO)
#include "app/ui_engine_demo.h"
#include "ui_engine/gdiplus_ui.h"

#include <memory>
#include <string>
#endif

namespace agis::ui {

PlatformWindows::PlatformWindows() = default;

#if defined(AGIS_BUILD_UI_ENGINE_DEMO)

namespace {

std::unique_ptr<MainFrame> g_demoRoot;

constexpr wchar_t kDemoClassName[] = L"AGIS_UIEngineDemo";
constexpr wchar_t kDemoTitle[] = L"ui_engine demo";

void DemoPaintClient(HDC hdc, const RECT& client) {
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
  const std::wstring line = FormatUiEngineDemoStatusLine(App::instance(), g_demoRoot.get());
  RECT rcText{client.left + 12, client.bottom - 72, client.right - 12, client.bottom - 36};
  DrawTextW(hdc, line.c_str(), static_cast<int>(line.size()), &rcText, DT_LEFT | DT_WORDBREAK);
}

LRESULT CALLBACK DemoWndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) {
  (void)hwnd;
  switch (msg) {
    case WM_DESTROY:
      g_demoRoot.reset();
      PostQuitMessage(0);
      return 0;
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
      DemoPaintClient(hdc, client);
      EndPaint(hwnd, &ps);
      return 0;
    }
    default:
      return DefWindowProcW(hwnd, msg, wParam, lParam);
  }
}

}  // namespace

PlatformWindows::PlatformWindows(HINSTANCE hinst, int nShowCmd)
    : mode_(PlatformWindows::Mode::UiEngineDemo), hinst_(hinst) {
  UiGdiplusInit();
  g_demoRoot = BuildUiEngineDemoWidgetTree();

  WNDCLASSEXW wc{};
  wc.cbSize = sizeof(wc);
  wc.style = CS_HREDRAW | CS_VREDRAW;
  wc.lpfnWndProc = DemoWndProc;
  wc.hInstance = hinst_;
  wc.hCursor = LoadCursor(nullptr, IDC_ARROW);
  wc.hbrBackground = reinterpret_cast<HBRUSH>(COLOR_WINDOW + 1);
  wc.lpszClassName = kDemoClassName;
  if (RegisterClassExW(&wc)) {
    class_registered_ = true;
  }

  hwnd_ = CreateWindowExW(0, kDemoClassName, kDemoTitle, WS_OVERLAPPEDWINDOW, CW_USEDEFAULT, CW_USEDEFAULT,
                           1000, 720, nullptr, nullptr, hinst_, nullptr);
  if (hwnd_) {
    ShowWindow(hwnd_, nShowCmd);
    UpdateWindow(hwnd_);
  }
}

#endif  // AGIS_BUILD_UI_ENGINE_DEMO

PlatformWindows::~PlatformWindows() {
#if defined(_WIN32)
  detail::PlatformWindowsReleaseDemoResources(this);
#endif
}

#if defined(_WIN32)
bool PlatformWindows::ok() const {
  if (mode_ == PlatformWindows::Mode::UiEngineDemo) {
    return hwnd_ != nullptr;
  }
  return true;
}
#endif

int PlatformWindows::runEventLoop(App& app) {
  (void)app;
  MSG msg{};
  while (GetMessageW(&msg, nullptr, 0, 0) > 0) {
    TranslateMessage(&msg);
    DispatchMessageW(&msg);
  }
  return static_cast<int>(msg.wParam);
}

void PlatformWindows::requestExit() { PostQuitMessage(0); }

const char* PlatformWindows::backendId() const { return "win32"; }

namespace detail {

#if defined(AGIS_BUILD_UI_ENGINE_DEMO)

void PlatformWindowsReleaseDemoResources(PlatformWindows* p) {
  if (!p || p->mode_ != PlatformWindows::Mode::UiEngineDemo) {
    return;
  }
  if (p->hwnd_ && IsWindow(p->hwnd_)) {
    DestroyWindow(p->hwnd_);
  }
  p->hwnd_ = nullptr;
  if (p->class_registered_ && p->hinst_) {
    UnregisterClassW(kDemoClassName, p->hinst_);
    p->class_registered_ = false;
  }
  g_demoRoot.reset();
  UiGdiplusShutdown();
  p->mode_ = PlatformWindows::Mode::Basic;
}

#else

void PlatformWindowsReleaseDemoResources(PlatformWindows* p) {
  (void)p;
}

#endif

}  // namespace detail

}  // namespace agis::ui
