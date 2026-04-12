#pragma once

#include <windows.h>
#include <commctrl.h>

#include <string>

#include "core/export.h"

constexpr int kSplitterW = 6;
constexpr int kDockStripW = 20;
constexpr int kDockStripBtnH = 48;
constexpr int kLayerMin = 160;
constexpr int kLayerMax = 520;
constexpr int kPropsMin = 180;
constexpr int kPropsMax = 520;
constexpr int kMapMin = 100;
constexpr UINT WM_APP_SHOW_LOG = WM_APP + 1;
constexpr UINT WM_APP_LAYER_SEL = WM_APP + 2;

AGIS_COMMON_API extern HWND g_hwndMain;
AGIS_COMMON_API extern HWND g_hwndToolbar;
AGIS_COMMON_API extern HWND g_hwndLayerStrip;
AGIS_COMMON_API extern HWND g_hwndLayer;
AGIS_COMMON_API extern HWND g_hwndMap;
/** 地图区外壳：主窗 `WS_CHILD`，无菜单；地图相关菜单在「视图 → 地图界面」。内嵌 `g_hwndMap`（AGISMapHost）。 */
AGIS_COMMON_API extern HWND g_hwndMapShell;
AGIS_COMMON_API extern HWND g_hwndProps;
AGIS_COMMON_API extern HWND g_hwndPropsStrip;
AGIS_COMMON_API extern HWND g_hwndStatus;
AGIS_COMMON_API extern HWND g_hwndLogDlg;
AGIS_COMMON_API extern HWND g_hwndConvertDlg;
/** 「数据驱动说明」模式对话框（存在则参与主题/DWM 刷新）。 */
AGIS_COMMON_API extern HWND g_hwndHelpDataDriversDlg;
AGIS_COMMON_API extern std::wstring g_currentGisPath;
AGIS_COMMON_API extern int g_layerContentW;
AGIS_COMMON_API extern int g_propsContentW;
AGIS_COMMON_API extern int g_layerSelIndex;
AGIS_COMMON_API extern int g_splitterDrag;
AGIS_COMMON_API extern bool g_view3d;
AGIS_COMMON_API extern bool g_showLayerDock;
AGIS_COMMON_API extern bool g_showPropsDock;
AGIS_COMMON_API extern bool g_layerDockExpanded;
AGIS_COMMON_API extern bool g_propsDockExpanded;
AGIS_COMMON_API extern int g_toolbarHeight;
AGIS_COMMON_API extern HIMAGELIST g_toolbarImageList;
AGIS_COMMON_API extern HMENU g_hmenuProjSub;
/** 主菜单「视图 → 2D 渲染」子菜单（`SyncViewMenu` 内 `CheckMenuRadioItem`）。 */
AGIS_COMMON_API extern HMENU g_hmenuRenderSub;
/** 主菜单「视图 → 地图界面」弹出子菜单句柄（`WM_INITMENUPOPUP` 同步勾选）。 */
AGIS_COMMON_API extern HMENU g_hmenuMapUiSub;
AGIS_COMMON_API extern std::wstring g_pendingPreviewModelPath;
/// 为 true 时 `g_pendingPreviewModelPath` 视为 tileset 目录或 tileset.json，走 3D Tiles→glTF 加载。
AGIS_COMMON_API extern bool g_pendingPreviewLoadAs3DTiles;

AGIS_COMMON_API extern const wchar_t kMainClass[];
AGIS_COMMON_API extern const wchar_t kLayerClass[];
AGIS_COMMON_API extern const wchar_t kMapClass[];
AGIS_COMMON_API extern const wchar_t kMapShellClass[];
AGIS_COMMON_API extern const wchar_t kPropsClass[];
AGIS_COMMON_API extern const wchar_t kLogClass[];
AGIS_COMMON_API extern const wchar_t kConvertClass[];
AGIS_COMMON_API extern const wchar_t kModelPreviewClass[];
AGIS_COMMON_API extern const wchar_t kTilePreviewClass[];

/// 将窗口移到 `refForMonitor` 所在显示器工作区内居中（ref 可为 nullptr，则用 hwnd 定显示器）。
AGIS_COMMON_API void AgisCenterWindowInMonitorWorkArea(HWND hwnd, HWND refForMonitor);
