#include "utils/ui_theme.h"

#include "core/main_globals.h"
#include "core/resource.h"
#include "ui_engine/gdiplus_ui.h"

#include <dwmapi.h>
#include <uxtheme.h>

#pragma comment(lib, "dwmapi.lib")
#pragma comment(lib, "uxtheme.lib")

#ifndef DWMWA_USE_IMMERSIVE_DARK_MODE
#define DWMWA_USE_IMMERSIVE_DARK_MODE 20
#endif

namespace {

constexpr wchar_t kRegAgis[] = L"Software\\AGIS\\GIS-Desktop";
constexpr wchar_t kRegThemeVal[] = L"ThemeMenu";

static HBRUSH g_mainClientBrush = nullptr;

void RecreateMainClientBrush(bool dark) {
  if (g_mainClientBrush) {
    DeleteObject(g_mainClientBrush);
    g_mainClientBrush = nullptr;
  }
  const COLORREF c = dark ? RGB(40, 42, 48) : GetSysColor(COLOR_3DFACE);
  g_mainClientBrush = CreateSolidBrush(c);
}

void ApplySetWindowTheme(HWND w, bool dark) {
  if (!w || !IsWindow(w)) {
    return;
  }
  if (dark) {
    SetWindowTheme(w, L"DarkMode_Explorer", nullptr);
  } else {
    SetWindowTheme(w, L"Explorer", nullptr);
  }
}

}  // namespace

AGIS_COMMON_API AgisThemeMenu g_themeMenu = AgisThemeMenu::kFollowSystem;

AGIS_COMMON_API bool AgisWindowsPrefersDarkApps() {
  DWORD val = 1;
  DWORD cb = sizeof(val);
  HKEY hkey = nullptr;
  if (RegOpenKeyExW(HKEY_CURRENT_USER, L"Software\\Microsoft\\Windows\\CurrentVersion\\Themes\\Personalize", 0,
                    KEY_READ, &hkey) != ERROR_SUCCESS) {
    return false;
  }
  const LSTATUS st =
      RegQueryValueExW(hkey, L"AppsUseLightTheme", nullptr, nullptr, reinterpret_cast<BYTE*>(&val), &cb);
  RegCloseKey(hkey);
  if (st != ERROR_SUCCESS) {
    return false;
  }
  return val == 0;
}

AGIS_COMMON_API bool AgisEffectiveUiDark() {
  switch (g_themeMenu) {
  case AgisThemeMenu::kDark:
    return true;
  case AgisThemeMenu::kLight:
    return false;
  default:
    return AgisWindowsPrefersDarkApps();
  }
}

/// uxtheme 未文档化导出：让菜单栏等与进程首选模式一致（Win10 1903+ / Win11 常见组合）。
static void AgisSyncPreferredAppModeForProcess(bool dark) {
  HMODULE ux = GetModuleHandleW(L"uxtheme.dll");
  if (!ux) {
    return;
  }
  using SetPreferredAppModeFn = int(WINAPI*)(int);
  using FlushMenuThemesFn = void(WINAPI*)();
  auto setPm = reinterpret_cast<SetPreferredAppModeFn>(GetProcAddress(ux, MAKEINTRESOURCEA(135)));
  auto flush = reinterpret_cast<FlushMenuThemesFn>(GetProcAddress(ux, MAKEINTRESOURCEA(141)));
  if (!setPm) {
    return;
  }
  // 0=Default, 1=AllowDark — 暗色时允许系统绘制暗色菜单/部分壳控件
  setPm(dark ? 1 : 0);
  if (flush) {
    flush();
  }
}

namespace {

BOOL CALLBACK AgisThemeEnumChildProc(HWND hwnd, LPARAM lpDark) {
  ApplySetWindowTheme(hwnd, lpDark != 0);
  EnumChildWindows(hwnd, AgisThemeEnumChildProc, lpDark);
  return TRUE;
}

}  // namespace

AGIS_COMMON_API void AgisApplyExplorerDarkThemeToTree(HWND root) {
  if (!root || !IsWindow(root)) {
    return;
  }
  const bool dark = AgisEffectiveUiDark();
  const LPARAM lp = dark ? 1 : 0;
  ApplySetWindowTheme(root, dark);
  EnumChildWindows(root, AgisThemeEnumChildProc, lp);
}

AGIS_COMMON_API void AgisApplyDwmDark(HWND hwnd, bool dark) {
  if (!hwnd || !IsWindow(hwnd)) {
    return;
  }
  BOOL use = dark ? TRUE : FALSE;
  const HRESULT hr = DwmSetWindowAttribute(hwnd, DWMWA_USE_IMMERSIVE_DARK_MODE, &use, sizeof(use));
  (void)hr;
}

AGIS_COMMON_API void AgisApplyTheme(HWND mainHwnd) {
  const bool dark = AgisEffectiveUiDark();
  AgisSyncPreferredAppModeForProcess(dark);
  UiSetPanelThemeDark(dark);
  RecreateMainClientBrush(dark);

  if (mainHwnd && IsWindow(mainHwnd)) {
    AgisApplyDwmDark(mainHwnd, dark);

    // 主框架 / 转换 / 预览等：对根窗口及全部子控件应用 Explorer 暗色主题（原仅对白名单窗口调用，SDI 主窗漏掉）。
    AgisApplyExplorerDarkThemeToTree(mainHwnd);

    if (g_hwndMapShell && IsWindow(g_hwndMapShell)) {
      AgisApplyDwmDark(g_hwndMapShell, dark);
    }

    HMENU bar = GetMenu(mainHwnd);
    if (bar) {
      HMENU th = GetSubMenu(bar, 6);
      if (th) {
        UINT sel = ID_THEME_SYSTEM;
        if (g_themeMenu == AgisThemeMenu::kLight) {
          sel = ID_THEME_LIGHT;
        } else if (g_themeMenu == AgisThemeMenu::kDark) {
          sel = ID_THEME_DARK;
        }
        CheckMenuRadioItem(th, ID_THEME_SYSTEM, ID_THEME_DARK, sel, MF_BYCOMMAND);
      }
    }
  }

  if (g_hwndLogDlg && IsWindow(g_hwndLogDlg)) {
    AgisApplyDwmDark(g_hwndLogDlg, dark);
    RedrawWindow(g_hwndLogDlg, nullptr, nullptr, RDW_INVALIDATE | RDW_ERASE | RDW_ALLCHILDREN);
  }
  if (g_hwndConvertDlg && IsWindow(g_hwndConvertDlg)) {
    AgisApplyDwmDark(g_hwndConvertDlg, dark);
    AgisApplyExplorerDarkThemeToTree(g_hwndConvertDlg);
    RedrawWindow(g_hwndConvertDlg, nullptr, nullptr, RDW_INVALIDATE | RDW_ERASE | RDW_ALLCHILDREN);
  }
  if (g_hwndHelpDataDriversDlg && IsWindow(g_hwndHelpDataDriversDlg)) {
    AgisApplyDwmDark(g_hwndHelpDataDriversDlg, dark);
    RedrawWindow(g_hwndHelpDataDriversDlg, nullptr, nullptr, RDW_INVALIDATE | RDW_ERASE | RDW_ALLCHILDREN);
  }

  if (g_hwndLayer) {
    InvalidateRect(g_hwndLayer, nullptr, FALSE);
  }
  if (g_hwndProps) {
    InvalidateRect(g_hwndProps, nullptr, FALSE);
  }
  if (mainHwnd && IsWindow(mainHwnd)) {
    RedrawWindow(mainHwnd, nullptr, nullptr, RDW_INVALIDATE | RDW_ERASE | RDW_ALLCHILDREN);
  }
}

AGIS_COMMON_API HBRUSH AgisMainClientBackgroundBrush() {
  return g_mainClientBrush;
}

AGIS_COMMON_API void AgisLoadThemePreference() {
  HKEY hkey = nullptr;
  if (RegOpenKeyExW(HKEY_CURRENT_USER, kRegAgis, 0, KEY_READ, &hkey) != ERROR_SUCCESS) {
    return;
  }
  DWORD v = 0;
  DWORD cb = sizeof(v);
  if (RegQueryValueExW(hkey, kRegThemeVal, nullptr, nullptr, reinterpret_cast<BYTE*>(&v), &cb) == ERROR_SUCCESS) {
    if (v <= 2) {
      g_themeMenu = static_cast<AgisThemeMenu>(v);
    }
  }
  RegCloseKey(hkey);
}

AGIS_COMMON_API void AgisSaveThemePreference() {
  HKEY hkey = nullptr;
  DWORD disp = 0;
  if (RegCreateKeyExW(HKEY_CURRENT_USER, kRegAgis, 0, nullptr, 0, KEY_WRITE, nullptr, &hkey, &disp) != ERROR_SUCCESS) {
    return;
  }
  const DWORD v = static_cast<DWORD>(g_themeMenu);
  RegSetValueExW(hkey, kRegThemeVal, 0, REG_DWORD, reinterpret_cast<const BYTE*>(&v), sizeof(v));
  RegCloseKey(hkey);
}
