#include "common/app_core/main_globals.h"

AGIS_COMMON_API void AgisCenterWindowInMonitorWorkArea(HWND hwnd, HWND refForMonitor) {
  if (!hwnd || !IsWindow(hwnd)) {
    return;
  }
  RECT wr{};
  if (!GetWindowRect(hwnd, &wr)) {
    return;
  }
  const int ww = wr.right - wr.left;
  const int wh = wr.bottom - wr.top;
  HWND ref = (refForMonitor && IsWindow(refForMonitor)) ? refForMonitor : hwnd;
  const HMONITOR mon = MonitorFromWindow(ref, MONITOR_DEFAULTTONEAREST);
  MONITORINFO mi{};
  mi.cbSize = sizeof(mi);
  if (!GetMonitorInfoW(mon, &mi)) {
    return;
  }
  const RECT& wa = mi.rcWork;
  const int x = wa.left + (wa.right - wa.left - ww) / 2;
  const int y = wa.top + (wa.bottom - wa.top - wh) / 2;
  SetWindowPos(hwnd, nullptr, x, y, 0, 0, SWP_NOSIZE | SWP_NOZORDER | SWP_NOACTIVATE);
}

AGIS_COMMON_API HWND g_hwndMain = nullptr;
AGIS_COMMON_API HWND g_hwndToolbar = nullptr;
AGIS_COMMON_API HWND g_hwndLayerStrip = nullptr;
AGIS_COMMON_API HWND g_hwndLayer = nullptr;
AGIS_COMMON_API HWND g_hwndMap = nullptr;
AGIS_COMMON_API HWND g_hwndMapShell = nullptr;
AGIS_COMMON_API HWND g_hwndProps = nullptr;
AGIS_COMMON_API HWND g_hwndPropsStrip = nullptr;
AGIS_COMMON_API HWND g_hwndStatus = nullptr;
AGIS_COMMON_API HWND g_hwndLogDlg = nullptr;
AGIS_COMMON_API HWND g_hwndConvertDlg = nullptr;
AGIS_COMMON_API HWND g_hwndHelpDataDriversDlg = nullptr;
AGIS_COMMON_API std::wstring g_currentGisPath;
AGIS_COMMON_API int g_layerContentW = 236;
AGIS_COMMON_API int g_propsContentW = 256;
AGIS_COMMON_API int g_layerSelIndex = -1;
AGIS_COMMON_API int g_splitterDrag = 0;
AGIS_COMMON_API bool g_view3d = false;
AGIS_COMMON_API bool g_showLayerDock = true;
AGIS_COMMON_API bool g_showPropsDock = true;
AGIS_COMMON_API bool g_layerDockExpanded = true;
AGIS_COMMON_API bool g_propsDockExpanded = true;
AGIS_COMMON_API int g_toolbarHeight = 0;
AGIS_COMMON_API HIMAGELIST g_toolbarImageList = nullptr;
AGIS_COMMON_API HMENU g_hmenuProjSub = nullptr;
AGIS_COMMON_API std::wstring g_pendingPreviewModelPath;
AGIS_COMMON_API bool g_pendingPreviewLoadAs3DTiles = false;

AGIS_COMMON_API const wchar_t kMainClass[] = L"AGISMainFrame";
AGIS_COMMON_API const wchar_t kLayerClass[] = L"AGISLayerPane";
AGIS_COMMON_API const wchar_t kMapClass[] = L"AGISMapHost";
AGIS_COMMON_API const wchar_t kMapShellClass[] = L"AGISMapShell";
AGIS_COMMON_API const wchar_t kPropsClass[] = L"AGISPropsPane";
AGIS_COMMON_API const wchar_t kLogClass[] = L"AGISLogWindow";
AGIS_COMMON_API const wchar_t kConvertClass[] = L"AGISDataConvertWindow";
AGIS_COMMON_API const wchar_t kModelPreviewClass[] = L"AGISModelPreviewWindow";
AGIS_COMMON_API const wchar_t kTilePreviewClass[] = L"AGISTileRasterPreviewWindow";
