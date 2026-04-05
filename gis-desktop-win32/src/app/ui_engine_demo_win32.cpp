/**
 * ui_engine 演示 — Windows 壳层：窗口、消息循环、GDI+ 与 agis::ui::App / IGuiPlatform 对接。
 * 可移植逻辑见 `ui_engine_demo.cpp`。
 */

#include <windows.h>

#include <memory>
#include <string>

#include "app/ui_engine_demo.h"
#include "ui_engine/app.h"
#include "ui_engine/gdiplus_ui.h"
#include "ui_engine/platform_windows.h"

namespace {

constexpr wchar_t kClassName[] = L"AGIS_UIEngineDemo";
constexpr wchar_t kTitle[] = L"ui_engine demo";

/** WndProc / GDI+ 与当前栈上 `App` 绑定（演示期有效）。 */
agis::ui::App* g_uiApp = nullptr;

std::unique_ptr<agis::ui::MainFrame> g_demoRoot;

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
      g_uiApp ? agis::ui::FormatUiEngineDemoStatusLine(*g_uiApp, g_demoRoot.get()) : std::wstring{};
  RECT rcText{client.left + 12, client.bottom - 72, client.right - 12, client.bottom - 36};
  DrawTextW(hdc, line.c_str(), static_cast<int>(line.size()), &rcText, DT_LEFT | DT_WORDBREAK);
}

LRESULT CALLBACK WndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) {
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

}  // namespace

int APIENTRY wWinMain(HINSTANCE hInst, HINSTANCE, LPWSTR, int show) {
  agis::ui::App app;
  g_uiApp = &app;

  UiGdiplusInit();
  g_demoRoot = agis::ui::BuildUiEngineDemoWidgetTree();

  WNDCLASSEXW wc{};
  wc.cbSize = sizeof(wc);
  wc.style = CS_HREDRAW | CS_VREDRAW;
  wc.lpfnWndProc = WndProc;
  wc.hInstance = hInst;
  wc.hCursor = LoadCursor(nullptr, IDC_ARROW);
  wc.hbrBackground = reinterpret_cast<HBRUSH>(COLOR_WINDOW + 1);
  wc.lpszClassName = kClassName;
  RegisterClassExW(&wc);

  HWND hwnd = CreateWindowExW(0, kClassName, kTitle, WS_OVERLAPPEDWINDOW, CW_USEDEFAULT, CW_USEDEFAULT,
                              1000, 720, nullptr, nullptr, hInst, nullptr);
  if (!hwnd) {
    g_uiApp = nullptr;
    UiGdiplusShutdown();
    return 1;
  }

  app.setPlatform(std::make_unique<agis::ui::PlatformWindows>());
  ShowWindow(hwnd, show);
  UpdateWindow(hwnd);

  const int code = app.exec();
  g_uiApp = nullptr;
  UiGdiplusShutdown();
  return code;
}
