#pragma once

#include <windows.h>
#include <commctrl.h>

#include <string>

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

extern HWND g_hwndMain;
extern HWND g_hwndToolbar;
extern HWND g_hwndLayerStrip;
extern HWND g_hwndLayer;
extern HWND g_hwndMap;
extern HWND g_hwndProps;
extern HWND g_hwndPropsStrip;
extern HWND g_hwndStatus;
extern HWND g_hwndLogDlg;
extern HWND g_hwndConvertDlg;
/** 「数据驱动说明」模式对话框（存在则参与主题/DWM 刷新）。 */
extern HWND g_hwndHelpDataDriversDlg;
extern std::wstring g_currentGisPath;
extern int g_layerContentW;
extern int g_propsContentW;
extern int g_layerSelIndex;
extern int g_splitterDrag;
extern bool g_view3d;
extern bool g_showLayerDock;
extern bool g_showPropsDock;
extern bool g_layerDockExpanded;
extern bool g_propsDockExpanded;
extern int g_toolbarHeight;
extern HIMAGELIST g_toolbarImageList;
extern HMENU g_hmenuProjSub;
extern std::wstring g_pendingPreviewModelPath;

extern const wchar_t kMainClass[];
extern const wchar_t kLayerClass[];
extern const wchar_t kMapClass[];
extern const wchar_t kPropsClass[];
extern const wchar_t kLogClass[];
extern const wchar_t kConvertClass[];
extern const wchar_t kModelPreviewClass[];
