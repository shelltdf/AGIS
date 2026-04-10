#include <windows.h>
#include <commctrl.h>
#include <shellapi.h>

#include <string>

#include "common/app_core/main_app.h"
#include "common/app_core/main_globals.h"
#include "common/ui/ui_font.h"

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
  UiFontInit();
  if (!RegisterTilePreviewClass(hInst)) {
    UiFontShutdown();
    return 1;
  }

  int argc = 0;
  LPWSTR* argv = CommandLineToArgvW(GetCommandLineW(), &argc);
  const std::wstring path = (argv && argc >= 2 && argv[1]) ? std::wstring(argv[1]) : L"";
  if (argv) LocalFree(argv);

  OpenTileRasterPreviewWindow(nullptr, path);
  MSG msg{};
  while (GetMessageW(&msg, nullptr, 0, 0)) {
    TranslateMessage(&msg);
    DispatchMessageW(&msg);
  }
  UiFontShutdown();
  return static_cast<int>(msg.wParam);
}
