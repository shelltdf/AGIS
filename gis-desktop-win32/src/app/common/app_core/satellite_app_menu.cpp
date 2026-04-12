#include "common/app_core/satellite_app_menu.h"

#include "core/resource.h"
#include "utils/agis_ui_l10n.h"
#include "utils/ui_theme.h"

HMENU AgisBuildSatelliteLangThemeMenuBar() {
  HMENU bar = CreateMenu();
  if (!bar) {
    return nullptr;
  }
  HMENU lang = CreateMenu();
  if (!lang) {
    DestroyMenu(bar);
    return nullptr;
  }
  AppendMenuW(lang, MF_STRING | (AgisGetUiLanguage() == AgisUiLanguage::kZh ? MF_CHECKED : MF_UNCHECKED), ID_LANG_ZH,
                AgisTr(AgisUiStr::MenuLangZh));
  AppendMenuW(lang, MF_STRING | (AgisGetUiLanguage() == AgisUiLanguage::kEn ? MF_CHECKED : MF_UNCHECKED), ID_LANG_EN,
                AgisTr(AgisUiStr::MenuLangEn));
  AppendMenuW(bar, MF_POPUP, reinterpret_cast<UINT_PTR>(lang), AgisTr(AgisUiStr::MenuLang));

  HMENU theme = CreateMenu();
  if (!theme) {
    DestroyMenu(bar);
    return nullptr;
  }
  const UINT chkSys = (g_themeMenu == AgisThemeMenu::kFollowSystem) ? MF_CHECKED : MF_UNCHECKED;
  const UINT chkLt = (g_themeMenu == AgisThemeMenu::kLight) ? MF_CHECKED : MF_UNCHECKED;
  const UINT chkDk = (g_themeMenu == AgisThemeMenu::kDark) ? MF_CHECKED : MF_UNCHECKED;
  AppendMenuW(theme, MF_STRING | chkSys, ID_THEME_SYSTEM, AgisTr(AgisUiStr::MenuThemeSystem));
  AppendMenuW(theme, MF_STRING | chkLt, ID_THEME_LIGHT, AgisTr(AgisUiStr::MenuThemeLight));
  AppendMenuW(theme, MF_STRING | chkDk, ID_THEME_DARK, AgisTr(AgisUiStr::MenuThemeDark));
  AppendMenuW(bar, MF_POPUP, reinterpret_cast<UINT_PTR>(theme), AgisTr(AgisUiStr::MenuTheme));
  return bar;
}

void AgisSetSatelliteLangThemeMenu(HWND hwnd) {
  if (!hwnd || !IsWindow(hwnd)) {
    return;
  }
  HMENU neu = AgisBuildSatelliteLangThemeMenuBar();
  if (!neu) {
    return;
  }
  HMENU old = GetMenu(hwnd);
  SetMenu(hwnd, neu);
  if (old) {
    DestroyMenu(old);
  }
  DrawMenuBar(hwnd);
}

void AgisOnSatelliteLangThemeMenuPopup(HWND hwnd, HMENU popup) {
  (void)hwnd;
  if (!popup) {
    return;
  }
  const int n = GetMenuItemCount(popup);
  if (n <= 0) {
    return;
  }
  const UINT first = GetMenuItemID(popup, 0);
  if (first == ID_LANG_ZH) {
    const UINT lid = AgisGetUiLanguage() == AgisUiLanguage::kEn ? ID_LANG_EN : ID_LANG_ZH;
    CheckMenuRadioItem(popup, ID_LANG_ZH, ID_LANG_EN, lid, MF_BYCOMMAND);
  } else if (first == ID_THEME_SYSTEM) {
    UINT sel = ID_THEME_SYSTEM;
    if (g_themeMenu == AgisThemeMenu::kLight) {
      sel = ID_THEME_LIGHT;
    } else if (g_themeMenu == AgisThemeMenu::kDark) {
      sel = ID_THEME_DARK;
    }
    CheckMenuRadioItem(popup, ID_THEME_SYSTEM, ID_THEME_DARK, sel, MF_BYCOMMAND);
  }
}

bool AgisTryHandleSatelliteLangThemeMenuCommand(HWND hwnd, UINT cmd, void (*onLanguageChanged)(HWND),
                                              void (*onThemeChanged)(HWND)) {
  if (cmd == ID_LANG_ZH || cmd == ID_LANG_EN) {
    AgisSetUiLanguage(cmd == ID_LANG_EN ? AgisUiLanguage::kEn : AgisUiLanguage::kZh);
    AgisSaveUiLanguagePreference();
    AgisSetSatelliteLangThemeMenu(hwnd);
    AgisApplyTheme(hwnd);
    if (onLanguageChanged) {
      onLanguageChanged(hwnd);
    }
    return true;
  }
  if (cmd == ID_THEME_SYSTEM || cmd == ID_THEME_LIGHT || cmd == ID_THEME_DARK) {
    if (cmd == ID_THEME_SYSTEM) {
      g_themeMenu = AgisThemeMenu::kFollowSystem;
    } else if (cmd == ID_THEME_LIGHT) {
      g_themeMenu = AgisThemeMenu::kLight;
    } else {
      g_themeMenu = AgisThemeMenu::kDark;
    }
    AgisSaveThemePreference();
    AgisApplyTheme(hwnd);
    if (onThemeChanged) {
      onThemeChanged(hwnd);
    }
    return true;
  }
  return false;
}
