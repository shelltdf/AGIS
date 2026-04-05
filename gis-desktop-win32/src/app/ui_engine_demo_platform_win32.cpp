/**
 * ui_engine 演示专用 Win32 壳：GDI+、演示窗口、`BuildUiEngineDemoWidgetTree` 与 `PlatformWindows` 子类。
 * 仅由 `ui_engine_demo` 可执行文件链接；主程序 `agis_desktop` 只链接无演示依赖的 `platform_windows.cpp`。
 */

#include "ui_engine/platform_windows.h"

#include "app/ui_engine_demo.h"
#include "ui_engine/app.h"
#include "ui_engine/gdiplus_ui.h"

#include <memory>
#include <string>

#include <windows.h>

namespace agis::ui {

namespace {

App* g_uiApp = nullptr;
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
  const std::wstring line =
      g_uiApp ? FormatUiEngineDemoStatusLine(*g_uiApp, g_demoRoot.get()) : std::wstring{};
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
      if (wParam == VK_ESCAPE && g_uiApp) {
        g_uiApp->requestQuit();
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

class PlatformWindowsUiEngineDemo final : public PlatformWindows {
 public:
  PlatformWindowsUiEngineDemo(HINSTANCE hinst, int nShowCmd);
  ~PlatformWindowsUiEngineDemo() override;

  bool ok() const { return hwnd_ != nullptr; }

  const char* backendId() const override;

 private:
  HINSTANCE hinst_{nullptr};
  HWND hwnd_{nullptr};
  bool class_registered_{false};
};

PlatformWindowsUiEngineDemo::PlatformWindowsUiEngineDemo(HINSTANCE hinst, int nShowCmd) : hinst_(hinst) {
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

PlatformWindowsUiEngineDemo::~PlatformWindowsUiEngineDemo() {
  if (hwnd_ && IsWindow(hwnd_)) {
    DestroyWindow(hwnd_);
  }
  hwnd_ = nullptr;
  if (class_registered_ && hinst_) {
    UnregisterClassW(kDemoClassName, hinst_);
    class_registered_ = false;
  }
  g_demoRoot.reset();
  UiGdiplusShutdown();
}

const char* PlatformWindowsUiEngineDemo::backendId() const { return "win32"; }

}  // namespace

int RunUiEngineDemoWin32(HINSTANCE hInst, int nShowCmd) {
  App app;
  g_uiApp = &app;

  auto platform = std::make_unique<PlatformWindowsUiEngineDemo>(hInst, nShowCmd);
  if (!platform->ok()) {
    g_uiApp = nullptr;
    return 1;
  }

  app.setPlatform(std::move(platform));
  const int code = app.exec();
  g_uiApp = nullptr;
  return code;
}

}  // namespace agis::ui
