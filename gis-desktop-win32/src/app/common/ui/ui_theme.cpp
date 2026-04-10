#include "ui_theme.h"

#include "common/app_core/main_globals.h"
#include "common/app_core/resource.h"
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

AgisThemeMenu g_themeMenu = AgisThemeMenu::kFollowSystem;

bool AgisWindowsPrefersDarkApps() {
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

bool AgisEffectiveUiDark() {
  switch (g_themeMenu) {
    case AgisThemeMenu::kDark:
      return true;
    case AgisThemeMenu::kLight:
      return false;
    default:
      return AgisWindowsPrefersDarkApps();
  }
}

void AgisApplyDwmDark(HWND hwnd, bool dark) {
  if (!hwnd || !IsWindow(hwnd)) {
    return;
  }
  BOOL use = dark ? TRUE : FALSE;
  const HRESULT hr = DwmSetWindowAttribute(hwnd, DWMWA_USE_IMMERSIVE_DARK_MODE, &use, sizeof(use));
  (void)hr;
}

void AgisApplyTheme(HWND mainHwnd) {
  const bool dark = AgisEffectiveUiDark();
  UiSetPanelThemeDark(dark);
  RecreateMainClientBrush(dark);

  if (mainHwnd && IsWindow(mainHwnd)) {
    AgisApplyDwmDark(mainHwnd, dark);

    if (g_hwndToolbar) {
      ApplySetWindowTheme(g_hwndToolbar, dark);
    }
    if (g_hwndStatus) {
      ApplySetWindowTheme(g_hwndStatus, dark);
    }
    if (g_hwndLayerStrip) {
      ApplySetWindowTheme(g_hwndLayerStrip, dark);
    }
    if (g_hwndPropsStrip) {
      ApplySetWindowTheme(g_hwndPropsStrip, dark);
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

HBRUSH AgisMainClientBackgroundBrush() {
  return g_mainClientBrush;
}

void AgisLoadThemePreference() {
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

void AgisSaveThemePreference() {
  HKEY hkey = nullptr;
  DWORD disp = 0;
  if (RegCreateKeyExW(HKEY_CURRENT_USER, kRegAgis, 0, nullptr, 0, KEY_WRITE, nullptr, &hkey, &disp) != ERROR_SUCCESS) {
    return;
  }
  const DWORD v = static_cast<DWORD>(g_themeMenu);
  RegSetValueExW(hkey, kRegThemeVal, 0, REG_DWORD, reinterpret_cast<const BYTE*>(&v), sizeof(v));
  RegCloseKey(hkey);
}
