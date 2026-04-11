#pragma once

#include <windows.h>

#include "core/export.h"

enum class AgisThemeMenu : int {
  kFollowSystem = 0,
  kLight = 1,
  kDark = 2,
};

/** 当前菜单选择（由设置菜单与注册表同步）。 */
AGIS_COMMON_API extern AgisThemeMenu g_themeMenu;

AGIS_COMMON_API bool AgisWindowsPrefersDarkApps();

/** 综合「用户选择 + 系统」后的实际暗色标志。 */
AGIS_COMMON_API bool AgisEffectiveUiDark();

/** 主窗：DWM、工具栏/状态栏/分割条视觉、侧栏 GDI+、`InvalidateRect`、菜单单选同步。 */
AGIS_COMMON_API void AgisApplyTheme(HWND mainHwnd);

/** 仅同步 DWM 沉浸式暗色标题栏（弹出窗在 `WM_CREATE` 末尾调用）。 */
AGIS_COMMON_API void AgisApplyDwmDark(HWND hwnd, bool dark);

AGIS_COMMON_API void AgisLoadThemePreference();
AGIS_COMMON_API void AgisSaveThemePreference();

/** 主客户区擦除用画刷（随 `AgisApplyTheme` 更新）。可能为 nullptr，则回退系统颜色。 */
AGIS_COMMON_API HBRUSH AgisMainClientBackgroundBrush();
