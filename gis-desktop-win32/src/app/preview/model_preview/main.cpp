#include <windows.h>
#include <commctrl.h>
#include <shellapi.h>

#include <string>

#include "common/app_core/main_app.h"
#include "common/app_core/main_globals.h"
#include "common/ui/ui_font.h"
#include "common/ui/ui_debug_pick.h"
#include "ui_engine/gdiplus_ui.h"

static HBRUSH g_modelPreviewWinClassBgBrush;

static bool RegisterModelPreviewClass(HINSTANCE inst) {
  WNDCLASSW wc{};
  wc.lpfnWndProc = ModelPreviewWndProc;
  wc.hInstance = inst;
  wc.lpszClassName = kModelPreviewClass;
  wc.hCursor = LoadCursor(nullptr, IDC_ARROW);
  if (!g_modelPreviewWinClassBgBrush) {
    g_modelPreviewWinClassBgBrush = CreateSolidBrush(RGB(200, 212, 230));
  }
  wc.hbrBackground = g_modelPreviewWinClassBgBrush ? g_modelPreviewWinClassBgBrush
                                                 : reinterpret_cast<HBRUSH>(COLOR_WINDOW + 1);
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
  AgisUiDebugPickInit(hInst);
  const HWND previewWnd = FindWindowW(kModelPreviewClass, nullptr);
  MSG msg{};
  msg.wParam = 0;
  bool quit = false;
  while (!quit) {
    while (PeekMessageW(&msg, nullptr, 0, 0, PM_REMOVE)) {
      if (msg.message == WM_QUIT) {
        quit = true;
        break;
      }
      if (AgisUiDebugPickHandleMessage(&msg)) {
        continue;
      }
      TranslateMessage(&msg);
      DispatchMessageW(&msg);
    }
    if (quit) {
      break;
    }
    if (!previewWnd || !IsWindow(previewWnd)) {
      break;
    }
    ModelPreviewFrameStep(previewWnd);
    if (!PeekMessageW(&msg, nullptr, 0, 0, PM_NOREMOVE)) {
      WaitMessage();
    }
  }
  AgisUiDebugPickShutdown();
  UiFontShutdown();
  UiGdiplusShutdown();
  return static_cast<int>(msg.wParam);
}
