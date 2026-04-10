#include <windows.h>
#include <commctrl.h>
#include <shellapi.h>

#include <string>

#include "app/ui_font.h"
#include "app/ui_debug_pick.h"
#include "app/ui_theme.h"
#include "main_app.h"
#include "main_globals.h"

// 独立转换程序不内置 3D 预览窗口：调用系统默认程序打开。
void OpenModelPreviewWindow(HWND owner, const std::wstring& path) {
  ShellExecuteW(owner, L"open", path.c_str(), nullptr, nullptr, SW_SHOWNORMAL);
}
void OpenTileRasterPreviewWindow(HWND owner, const std::wstring& path) {
  ShellExecuteW(owner, L"open", path.c_str(), nullptr, nullptr, SW_SHOWNORMAL);
}
void OpenModelPreviewWindow3DTiles(HWND owner, const std::wstring& tilesetRootOrFile) {
  ShellExecuteW(owner, L"open", tilesetRootOrFile.c_str(), nullptr, nullptr, SW_SHOWNORMAL);
}

void CopyTextToClipboard(HWND owner, const std::wstring& text) {
  if (!OpenClipboard(owner)) return;
  EmptyClipboard();
  const size_t bytes = (text.size() + 1) * sizeof(wchar_t);
  HGLOBAL h = GlobalAlloc(GMEM_MOVEABLE, bytes);
  if (h) {
    void* p = GlobalLock(h);
    if (p) {
      memcpy(p, text.c_str(), bytes);
      GlobalUnlock(h);
      SetClipboardData(CF_UNICODETEXT, h);
      h = nullptr;
    }
    if (h) GlobalFree(h);
  }
  CloseClipboard();
}

static bool RegisterConvertClass(HINSTANCE inst) {
  WNDCLASSW wc{};
  wc.lpfnWndProc = ConvertWndProc;
  wc.hInstance = inst;
  wc.lpszClassName = kConvertClass;
  wc.hCursor = LoadCursor(nullptr, IDC_ARROW);
  wc.hbrBackground = reinterpret_cast<HBRUSH>(COLOR_3DFACE + 1);
  return RegisterClassW(&wc) != 0;
}

int APIENTRY wWinMain(HINSTANCE hInst, HINSTANCE, LPWSTR, int) {
  INITCOMMONCONTROLSEX icc{sizeof(icc), ICC_BAR_CLASSES | ICC_STANDARD_CLASSES | ICC_WIN95_CLASSES | ICC_PROGRESS_CLASS};
  InitCommonControlsEx(&icc);
  UiFontInit();
  if (!RegisterConvertClass(hInst)) {
    UiFontShutdown();
    return 1;
  }
  ShowDataConvertWindow(nullptr);
  AgisUiDebugPickInit(hInst);
  MSG msg{};
  while (GetMessageW(&msg, nullptr, 0, 0)) {
    if (AgisUiDebugPickHandleMessage(&msg)) {
      continue;
    }
    TranslateMessage(&msg);
    DispatchMessageW(&msg);
  }
  AgisUiDebugPickShutdown();
  UiFontShutdown();
  return static_cast<int>(msg.wParam);
}

