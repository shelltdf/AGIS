#include <windows.h>
#include <commctrl.h>
#include <shellapi.h>

#include <string>

#include "common/app_core/main_app.h"
#include "common/app_core/main_globals.h"
#include "common/ui/ui_font.h"
#include "common/ui/ui_debug_pick.h"
#include "ui_engine/gdiplus_ui.h"

// 独立进程入口：本地瓦片/栅格采样预览（`OpenTileRasterPreviewWindow`）；无网络拉流。

static bool RegisterTilePreviewClass(HINSTANCE inst) {
  WNDCLASSW wc{};
  wc.lpfnWndProc = TilePreviewWndProc;
  wc.hInstance = inst;
  wc.lpszClassName = kTilePreviewClass;
  wc.hCursor = LoadCursor(nullptr, IDC_ARROW);
  wc.hbrBackground = reinterpret_cast<HBRUSH>(COLOR_WINDOW + 1);
  return RegisterClassW(&wc) != 0;
}

int APIENTRY wWinMain(HINSTANCE hInst, HINSTANCE, LPWSTR, int) {
  INITCOMMONCONTROLSEX icc{sizeof(icc), ICC_STANDARD_CLASSES | ICC_WIN95_CLASSES | ICC_BAR_CLASSES};
  InitCommonControlsEx(&icc);
  UiGdiplusInit();
  UiFontInit();
  if (!RegisterTilePreviewClass(hInst)) {
    UiFontShutdown();
    UiGdiplusShutdown();
    return 1;
  }

  int argc = 0;
  LPWSTR* argv = CommandLineToArgvW(GetCommandLineW(), &argc);
  const std::wstring path = (argv && argc >= 2 && argv[1]) ? std::wstring(argv[1]) : L"";
  if (argv) LocalFree(argv);

  OpenTileRasterPreviewWindow(nullptr, path);
  AgisUiDebugPickInit(hInst);
  const HWND previewWnd = FindWindowW(kTilePreviewClass, nullptr);
  MSG msg{};
  while (previewWnd && IsWindow(previewWnd) && GetMessageW(&msg, nullptr, 0, 0)) {
    if (AgisUiDebugPickHandleMessage(&msg)) {
      continue;
    }
    TranslateMessage(&msg);
    DispatchMessageW(&msg);
  }
  AgisUiDebugPickShutdown();
  UiFontShutdown();
  UiGdiplusShutdown();
  return static_cast<int>(msg.wParam);
}
