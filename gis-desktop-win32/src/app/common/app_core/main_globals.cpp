#include "common/app_core/main_globals.h"

void AgisCenterWindowInMonitorWorkArea(HWND hwnd, HWND refForMonitor) {
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

HWND g_hwndMain = nullptr;
HWND g_hwndToolbar = nullptr;
HWND g_hwndLayerStrip = nullptr;
HWND g_hwndLayer = nullptr;
HWND g_hwndMap = nullptr;
HWND g_hwndMapShell = nullptr;
HWND g_hwndProps = nullptr;
HWND g_hwndPropsStrip = nullptr;
HWND g_hwndStatus = nullptr;
HWND g_hwndLogDlg = nullptr;
HWND g_hwndConvertDlg = nullptr;
HWND g_hwndHelpDataDriversDlg = nullptr;
std::wstring g_currentGisPath;
int g_layerContentW = 236;
int g_propsContentW = 256;
int g_layerSelIndex = -1;
int g_splitterDrag = 0;
bool g_view3d = false;
bool g_showLayerDock = true;
bool g_showPropsDock = true;
bool g_layerDockExpanded = true;
bool g_propsDockExpanded = true;
int g_toolbarHeight = 0;
HIMAGELIST g_toolbarImageList = nullptr;
HMENU g_hmenuProjSub = nullptr;
std::wstring g_pendingPreviewModelPath;
bool g_pendingPreviewLoadAs3DTiles = false;

const wchar_t kMainClass[] = L"AGISMainFrame";
const wchar_t kLayerClass[] = L"AGISLayerPane";
const wchar_t kMapClass[] = L"AGISMapHost";
const wchar_t kMapShellClass[] = L"AGISMapShell";
const wchar_t kPropsClass[] = L"AGISPropsPane";
const wchar_t kLogClass[] = L"AGISLogWindow";
const wchar_t kConvertClass[] = L"AGISDataConvertWindow";
const wchar_t kModelPreviewClass[] = L"AGISModelPreviewWindow";
const wchar_t kTilePreviewClass[] = L"AGISTileRasterPreviewWindow";
