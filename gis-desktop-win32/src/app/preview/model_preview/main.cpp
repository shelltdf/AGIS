#include <windows.h>
#include <commctrl.h>
#include <shellapi.h>

#include <string>

#include "common/app_core/main_app.h"
#include "common/app_core/main_globals.h"
#include "common/ui/ui_font.h"
#include "ui_engine/gdiplus_ui.h"

static bool RegisterModelPreviewClass(HINSTANCE inst) {
  WNDCLASSW wc{};
  wc.lpfnWndProc = ModelPreviewWndProc;
  wc.hInstance = inst;
  wc.lpszClassName = kModelPreviewClass;
  wc.hCursor = LoadCursor(nullptr, IDC_ARROW);
  wc.hbrBackground = reinterpret_cast<HBRUSH>(COLOR_WINDOW + 1);
  return RegisterClassW(&wc) != 0;
}

int APIENTRY wWinMain(HINSTANCE hInst, HINSTANCE, LPWSTR, int) {
  INITCOMMONCONTROLSEX icc{sizeof(icc), ICC_STANDARD_CLASSES | ICC_WIN95_CLASSES | ICC_BAR_CLASSES};
  InitCommonControlsEx(&icc);
  UiGdiplusInit();
  UiFontInit();
  if (!RegisterModelPreviewClass(hInst)) {
    UiFontShutdown();
    UiGdiplusShutdown();
    return 1;
  }

  int argc = 0;
  LPWSTR* argv = CommandLineToArgvW(GetCommandLineW(), &argc);
  bool as3dTiles = false;
  std::wstring path;
  if (argv && argc >= 2) {
    int idx = 1;
    if (argv[idx] && _wcsicmp(argv[idx], L"--3dtiles") == 0) {
      as3dTiles = true;
      ++idx;
    }
    if (idx < argc && argv[idx]) {
      path = argv[idx];
    }
  }
  if (argv) LocalFree(argv);

  if (as3dTiles) {
    OpenModelPreviewWindow3DTiles(nullptr, path);
  } else {
    OpenModelPreviewWindow(nullptr, path);
  }
  const HWND previewWnd = FindWindowW(kModelPreviewClass, nullptr);
  MSG msg{};
  while (previewWnd && IsWindow(previewWnd) && GetMessageW(&msg, nullptr, 0, 0)) {
    TranslateMessage(&msg);
    DispatchMessageW(&msg);
  }
  UiFontShutdown();
  UiGdiplusShutdown();
  return static_cast<int>(msg.wParam);
}
