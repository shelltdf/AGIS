#include "map_engine/map_engine.h"

#include "core/main_globals.h"

#include "ui_engine/gdiplus_ui.h"

#include <windows.h>

namespace {

const wchar_t kMapEngineDemoClass[] = L"AGISMapEngineDemoHost";

/** 顶层宿主：在 MapHostProc 处理完 WM_DESTROY 后结束消息循环并做引擎收尾。 */
LRESULT CALLBACK MapEngineDemoHostProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) {
  const LRESULT r = MapHostProc(hwnd, msg, wParam, lParam);
  if (msg == WM_DESTROY) {
    MapEngine::Instance().Shutdown();
    PostQuitMessage(0);
    return 0;
  }
  return r;
}

bool RegisterDemoMapClass(HINSTANCE inst) {
  WNDCLASSW wc{};
  wc.lpfnWndProc = MapEngineDemoHostProc;
  wc.hInstance = inst;
  wc.lpszClassName = kMapEngineDemoClass;
  wc.hCursor = LoadCursorW(nullptr, IDC_ARROW);
  wc.hbrBackground = reinterpret_cast<HBRUSH>(COLOR_WINDOW + 1);
  wc.style = CS_OWNDC;
  return RegisterClassW(&wc) != 0;
}

}  // namespace

int APIENTRY wWinMain(HINSTANCE hInst, HINSTANCE, LPWSTR, int cmdShow) {
  UiGdiplusInit();
  MapEngine::Instance().Init();
  if (!RegisterDemoMapClass(hInst)) {
    MapEngine::Instance().Shutdown();
    UiGdiplusShutdown();
    return 1;
  }
  HWND hwnd = CreateWindowExW(0, kMapEngineDemoClass, L"AGIS — map_engine 演示",
                              WS_OVERLAPPEDWINDOW, CW_USEDEFAULT, CW_USEDEFAULT, 960, 640, nullptr, nullptr, hInst,
                              nullptr);
  if (!hwnd) {
    MapEngine::Instance().Shutdown();
    UiGdiplusShutdown();
    return 1;
  }
  AgisCenterWindowInMonitorWorkArea(hwnd, nullptr);
  ShowWindow(hwnd, cmdShow);
  UpdateWindow(hwnd);

  MSG msg{};
  while (GetMessageW(&msg, nullptr, 0, 0) > 0) {
    TranslateMessage(&msg);
    DispatchMessageW(&msg);
  }
  UiGdiplusShutdown();
  return static_cast<int>(msg.wParam);
}
