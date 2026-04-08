#pragma once

#include <windows.h>

enum class AgisThemeMenu : int {
  kFollowSystem = 0,
  kLight = 1,
  kDark = 2,
};

/** 当前菜单选择（由设置菜单与注册表同步）。 */
extern AgisThemeMenu g_themeMenu;

bool AgisWindowsPrefersDarkApps();

/** 综合「用户选择 + 系统」后的实际暗色标志。 */
bool AgisEffectiveUiDark();

/** 主窗：DWM、工具栏/状态栏/分割条视觉、侧栏 GDI+、`InvalidateRect`、菜单单选同步。 */
void AgisApplyTheme(HWND mainHwnd);

/** 仅同步 DWM 沉浸式暗色标题栏（弹出窗在 `WM_CREATE` 末尾调用）。 */
void AgisApplyDwmDark(HWND hwnd, bool dark);

void AgisLoadThemePreference();
void AgisSaveThemePreference();

/** 主客户区擦除用画刷（随 `AgisApplyTheme` 更新）。可能为 nullptr，则回退系统颜色。 */
HBRUSH AgisMainClientBackgroundBrush();
