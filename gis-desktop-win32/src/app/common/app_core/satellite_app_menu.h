#pragma once

#include <windows.h>

/// 构建仅含「语言」「主题」两级菜单（与主程序 AGIS 菜单文案一致）。
HMENU AgisBuildSatelliteLangThemeMenuBar();

/// 替换 `hwnd` 的菜单栏为新的语言/主题菜单（销毁旧菜单）。
void AgisSetSatelliteLangThemeMenu(HWND hwnd);

/// `WM_INITMENUPOPUP`：同步语言/主题子菜单的单选状态。
void AgisOnSatelliteLangThemeMenuPopup(HWND hwnd, HMENU popup);

/** `WM_COMMAND`：处理 `ID_LANG_*` / `ID_THEME_*`。
 *  `onLanguageChanged` / `onThemeChanged` 可为 nullptr；用于刷新子控件文案或重绘。 */
bool AgisTryHandleSatelliteLangThemeMenuCommand(HWND hwnd, UINT cmd, void (*onLanguageChanged)(HWND),
                                                void (*onThemeChanged)(HWND));
