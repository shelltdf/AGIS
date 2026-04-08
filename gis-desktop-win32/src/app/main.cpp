#include <windows.h>
#include <windowsx.h>
#include <commctrl.h>
#include <commdlg.h>
#include <shellapi.h>
#include <shlobj.h>
#include <GL/gl.h>
#include <d3d11.h>
#include <d3dcompiler.h>
#include <dxgi.h>
#include <algorithm>
#include <array>
#include <cmath>
#include <filesystem>
#include <fstream>
#include <set>
#include <sstream>
#include <string>
#include <vector>

#include "app/help_data_drivers.h"
#include "app/resource.h"
#include "app/ui_font.h"
#include "core/app_log.h"
#include "map_engine/map_engine.h"
#include "map_engine/map_projection.h"
#include "ui_engine/gdiplus_ui.h"

#pragma comment(lib, "comctl32.lib")
#pragma comment(lib, "opengl32.lib")
#pragma comment(lib, "d3d11.lib")
#pragma comment(lib, "dxgi.lib")
#pragma comment(lib, "d3dcompiler.lib")

#ifndef GIS_DESKTOP_HAVE_GDAL
#define GIS_DESKTOP_HAVE_GDAL 0
#endif

static HFONT g_appUiFont = nullptr;
static bool g_appUiFontOwned = false;

static HFONT CreateAppUiFont() {
  HFONT f = CreateFontW(-12, 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE, DEFAULT_CHARSET, OUT_DEFAULT_PRECIS,
                        CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY, DEFAULT_PITCH | FF_DONTCARE, L"Microsoft YaHei UI");
  return f;
}

HFONT UiGetAppFont() {
  return g_appUiFont ? g_appUiFont : reinterpret_cast<HFONT>(GetStockObject(DEFAULT_GUI_FONT));
}

namespace {

constexpr int kSplitterW = 6;
/** Dock 缘条（折叠按钮区域）宽度，与 Dock View 兄弟并列 */
constexpr int kDockStripW = 20;
/** 缘条上折叠钮高度（纵向居中，避免占满整列显得过长） */
constexpr int kDockStripBtnH = 48;
constexpr int kLayerMin = 160;
constexpr int kLayerMax = 520;
constexpr int kPropsMin = 180;
constexpr int kPropsMax = 520;
constexpr int kMapMin = 100;
constexpr UINT WM_APP_SHOW_LOG = WM_APP + 1;
constexpr UINT WM_APP_LAYER_SEL = WM_APP + 2;

HWND g_hwndMain = nullptr;
HWND g_hwndToolbar = nullptr;
HWND g_hwndLayerStrip = nullptr;
HWND g_hwndLayer = nullptr;
HWND g_hwndMap = nullptr;
HWND g_hwndProps = nullptr;
HWND g_hwndPropsStrip = nullptr;
HWND g_hwndStatus = nullptr;
HWND g_hwndLogDlg = nullptr;
HWND g_hwndConvertDlg = nullptr;
std::wstring g_currentGisPath;

/** 左侧 Dock View 内列表区宽度（不含缘条） */
int g_layerContentW = 236;
/** 右侧 Dock View 内属性区宽度（不含缘条） */
int g_propsContentW = 256;
int g_layerSelIndex = -1;
static std::wstring g_propsLayerSubtitleForPaint;
static RECT g_propsDriverCardRc{};
static RECT g_propsSourceCardRc{};
int g_splitterDrag = 0;  // 0=none, 1=left, 2=right
bool g_view3d = false;
bool g_showLayerDock = true;
bool g_showPropsDock = true;
/** 左侧 Dock 内容区是否展开（缘条始终可见） */
bool g_layerDockExpanded = true;
/** 右侧 Dock 内容区是否展开 */
bool g_propsDockExpanded = true;
int g_toolbarHeight = 0;
HIMAGELIST g_toolbarImageList = nullptr;
/** 「视图 → 投影」弹出菜单句柄，用于 SyncViewMenu 单选标记 */
HMENU g_hmenuProjSub = nullptr;

const wchar_t kMainClass[] = L"AGISMainFrame";
const wchar_t kLayerClass[] = L"AGISLayerPane";
const wchar_t kMapClass[] = L"AGISMapHost";
const wchar_t kPropsClass[] = L"AGISPropsPane";
const wchar_t kLogClass[] = L"AGISLogWindow";
const wchar_t kConvertClass[] = L"AGISDataConvertWindow";
const wchar_t kModelPreviewClass[] = L"AGISModelPreviewWindow";
std::wstring g_pendingPreviewModelPath;

/** WM_MOUSEWHEEL 发往焦点窗口；焦点在工具栏/侧栏时转发到地图，且 lParam 为屏幕坐标。 */
static bool ForwardWheelToMapIfOver(WPARAM wParam, LPARAM lParam) {
  if (!g_hwndMap || g_view3d) {
    return false;
  }
  POINT pt{GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam)};
  RECT wr{};
  GetWindowRect(g_hwndMap, &wr);
  if (!PtInRect(&wr, pt)) {
    return false;
  }
  SendMessageW(g_hwndMap, WM_MOUSEWHEEL, wParam, lParam);
  return true;
}

LRESULT CALLBACK ToolbarWheelSubclass(HWND h, UINT m, WPARAM w, LPARAM l, UINT_PTR, DWORD_PTR) {
  if (m == WM_MOUSEWHEEL && ForwardWheelToMapIfOver(w, l)) {
    return 0;
  }
  return DefSubclassProc(h, m, w, l);
}

void CopyTextToClipboard(HWND owner, const std::wstring& text) {
  if (!OpenClipboard(owner)) {
    return;
  }
  EmptyClipboard();
  const SIZE_T bytes = (text.size() + 1) * sizeof(wchar_t);
  HGLOBAL hMem = GlobalAlloc(GMEM_MOVEABLE, bytes);
  if (!hMem) {
    CloseClipboard();
    return;
  }
  void* p = GlobalLock(hMem);
  if (p) {
    memcpy(p, text.c_str(), bytes);
    GlobalUnlock(hMem);
    SetClipboardData(CF_UNICODETEXT, hMem);
  } else {
    GlobalFree(hMem);
  }
  CloseClipboard();
}

void SyncViewMenu(HWND hwnd) {
  HMENU menu = GetMenu(hwnd);
  if (!menu) {
    return;
  }
  HMENU view = GetSubMenu(menu, 1);
  if (!view) {
    return;
  }
  CheckMenuRadioItem(view, ID_VIEW_MODE_2D, ID_VIEW_MODE_3D,
                       g_view3d ? ID_VIEW_MODE_3D : ID_VIEW_MODE_2D, MF_BYCOMMAND);
  const MapRenderBackend rb = MapEngine::Instance().GetRenderBackend();
  UINT rid = ID_VIEW_RENDER_GDI;
  if (rb == MapRenderBackend::kD3d11) {
    rid = ID_VIEW_RENDER_D3D11;
  } else if (rb == MapRenderBackend::kOpenGL) {
    rid = ID_VIEW_RENDER_GL;
  }
  CheckMenuRadioItem(view, ID_VIEW_RENDER_GDI, ID_VIEW_RENDER_GL, rid, MF_BYCOMMAND);
  if (g_hmenuProjSub) {
    const int pi = static_cast<int>(MapEngine::Instance().Document().GetDisplayProjection());
    if (pi >= 0 && pi < static_cast<int>(MapDisplayProjection::kCount)) {
      CheckMenuRadioItem(g_hmenuProjSub, ID_VIEW_PROJ_FIRST, ID_VIEW_PROJ_LAST, ID_VIEW_PROJ_FIRST + pi,
                         MF_BYCOMMAND);
    }
  }
}

void SyncWindowMenu(HWND hwnd) {
  HMENU menu = GetMenu(hwnd);
  if (!menu) {
    return;
  }
  HMENU win = GetSubMenu(menu, 2);
  if (!win) {
    return;
  }
  CheckMenuItem(win, ID_WINDOW_LAYER_DOCK, MF_BYCOMMAND | (g_showLayerDock ? MF_CHECKED : MF_UNCHECKED));
  CheckMenuItem(win, ID_WINDOW_PROPS_DOCK, MF_BYCOMMAND | (g_showPropsDock ? MF_CHECKED : MF_UNCHECKED));
}

void GetInnerClient(HWND hwnd, RECT* out) {
  GetClientRect(hwnd, out);
  if (g_hwndToolbar) {
    RECT tr{};
    GetWindowRect(g_hwndToolbar, &tr);
    MapWindowPoints(HWND_DESKTOP, hwnd, reinterpret_cast<LPPOINT>(&tr), 2);
    out->top = tr.bottom;
    g_toolbarHeight = tr.bottom - tr.top;
  } else {
    g_toolbarHeight = 0;
  }
  if (g_hwndStatus) {
    RECT sr{};
    GetWindowRect(g_hwndStatus, &sr);
    out->bottom -= (sr.bottom - sr.top);
  }
}

void UpdateStatusParts() {
  if (!g_hwndStatus) {
    return;
  }
  RECT rc{};
  GetClientRect(g_hwndMain, &rc);
  const int parts[] = {static_cast<int>(rc.right * 0.55), -1};
  SendMessageW(g_hwndStatus, SB_SETPARTS, 2, reinterpret_cast<LPARAM>(parts));
  const std::wstring mode = g_view3d ? L"3D" : L"2D";
  SendMessageW(g_hwndStatus, SB_SETTEXT, 0,
               reinterpret_cast<LPARAM>(L"就绪 — 双击此处打开日志"));
  SendMessageW(g_hwndStatus, SB_SETTEXT, 1, reinterpret_cast<LPARAM>(mode.c_str()));
}

void LayoutChildren() {
  if (!g_hwndMain) {
    return;
  }
  RECT client{};
  GetClientRect(g_hwndMain, &client);
  if (g_hwndToolbar) {
    const DWORD bsz = static_cast<DWORD>(SendMessageW(g_hwndToolbar, TB_GETBUTTONSIZE, 0, 0));
    const int tbH = std::max(HIWORD(bsz) + 8, 36);
    MoveWindow(g_hwndToolbar, 0, 0, client.right, tbH, TRUE);
    SendMessageW(g_hwndToolbar, TB_AUTOSIZE, 0, 0);
    RECT tr{};
    GetWindowRect(g_hwndToolbar, &tr);
    MapWindowPoints(HWND_DESKTOP, g_hwndMain, reinterpret_cast<LPPOINT>(&tr), 2);
    g_toolbarHeight = tr.bottom - tr.top;
  }
  if (g_hwndStatus) {
    SendMessageW(g_hwndStatus, WM_SIZE, 0, 0);
    RECT sr{};
    GetWindowRect(g_hwndStatus, &sr);
    client.bottom -= (sr.bottom - sr.top);
  }
  RECT inner{};
  GetInnerClient(g_hwndMain, &inner);
  const int top = inner.top;
  const int innerH = inner.bottom - inner.top;
  const int totalW = client.right - client.left;

  const int splitL =
      (g_showLayerDock && g_layerDockExpanded) ? kSplitterW : 0;
  const int splitR =
      (g_showPropsDock && g_propsDockExpanded) ? kSplitterW : 0;

  int lcw = g_showLayerDock && g_layerDockExpanded
                ? std::max(kLayerMin, std::min(g_layerContentW, kLayerMax))
                : 0;
  int pcw = g_showPropsDock && g_propsDockExpanded
                ? std::max(kPropsMin, std::min(g_propsContentW, kPropsMax))
                : 0;

  const int layerTotalW =
      g_showLayerDock ? (kDockStripW + (g_layerDockExpanded ? lcw : 0)) : 0;
  const int propsTotalW =
      g_showPropsDock ? ((g_propsDockExpanded ? pcw : 0) + kDockStripW) : 0;

  int mapW = totalW - layerTotalW - splitL - propsTotalW - splitR;
  if (mapW < kMapMin) {
    int need = kMapMin - mapW;
    if (g_showPropsDock && g_propsDockExpanded) {
      const int takeP = std::min(need, pcw - kPropsMin);
      pcw -= takeP;
      need -= takeP;
    }
    if (g_showLayerDock && g_layerDockExpanded && need > 0) {
      const int takeL = std::min(need, lcw - kLayerMin);
      lcw -= takeL;
      need -= takeL;
    }
    mapW = totalW - (g_showLayerDock ? (kDockStripW + (g_layerDockExpanded ? lcw : 0)) : 0) - splitL -
           (g_showPropsDock ? ((g_propsDockExpanded ? pcw : 0) + kDockStripW) : 0) - splitR;
    const int minNeed = (g_showLayerDock ? kDockStripW + kLayerMin + splitL : 0) + kMapMin +
                        (g_showPropsDock ? splitR + kDockStripW + kPropsMin : 0);
    if (mapW < kMapMin && totalW >= minNeed) {
      if (g_showLayerDock && g_layerDockExpanded) {
        lcw = kLayerMin;
      }
      if (g_showPropsDock && g_propsDockExpanded) {
        pcw = kPropsMin;
      }
    }
  }
  if (g_showLayerDock && g_layerDockExpanded) {
    g_layerContentW = lcw;
  }
  if (g_showPropsDock && g_propsDockExpanded) {
    g_propsContentW = pcw;
  }

  const int layerTotalW2 =
      g_showLayerDock ? (kDockStripW + (g_layerDockExpanded ? g_layerContentW : 0)) : 0;
  const int propsTotalW2 =
      g_showPropsDock ? ((g_propsDockExpanded ? g_propsContentW : 0) + kDockStripW) : 0;
  const int mapLeft = layerTotalW2 + splitL;
  mapW = totalW - layerTotalW2 - splitL - propsTotalW2 - splitR;

  const int stripY = top;
  if (g_hwndLayerStrip) {
    if (g_showLayerDock) {
      ShowWindow(g_hwndLayerStrip, SW_SHOW);
      MoveWindow(g_hwndLayerStrip, 0, stripY, kDockStripW, kDockStripBtnH, TRUE);
      SetWindowTextW(g_hwndLayerStrip, g_layerDockExpanded ? L"‹" : L"›");
    } else {
      ShowWindow(g_hwndLayerStrip, SW_HIDE);
    }
  }
  if (g_hwndLayer) {
    if (g_showLayerDock && g_layerDockExpanded) {
      ShowWindow(g_hwndLayer, SW_SHOW);
      MoveWindow(g_hwndLayer, kDockStripW, top, g_layerContentW, innerH, TRUE);
    } else {
      ShowWindow(g_hwndLayer, SW_HIDE);
    }
  }
  if (g_hwndMap) {
    ShowWindow(g_hwndMap, SW_SHOW);
    MoveWindow(g_hwndMap, mapLeft, top, mapW, innerH, TRUE);
  }
  if (g_hwndPropsStrip) {
    if (g_showPropsDock) {
      ShowWindow(g_hwndPropsStrip, SW_SHOW);
      MoveWindow(g_hwndPropsStrip, totalW - kDockStripW, stripY, kDockStripW, kDockStripBtnH, TRUE);
      SetWindowTextW(g_hwndPropsStrip, g_propsDockExpanded ? L"›" : L"‹");
    } else {
      ShowWindow(g_hwndPropsStrip, SW_HIDE);
    }
  }
  if (g_hwndProps) {
    if (g_showPropsDock && g_propsDockExpanded) {
      ShowWindow(g_hwndProps, SW_SHOW);
      const int px = totalW - kDockStripW - g_propsContentW;
      MoveWindow(g_hwndProps, px, top, g_propsContentW, innerH, TRUE);
    } else {
      ShowWindow(g_hwndProps, SW_HIDE);
    }
  }
  UpdateStatusParts();
}

bool HitLeftSplitter(int x, int y, int innerTop, int innerBottom) {
  if (!g_showLayerDock || !g_layerDockExpanded) {
    return false;
  }
  const int layerTotalW = kDockStripW + g_layerContentW;
  return x >= layerTotalW && x < layerTotalW + kSplitterW && y >= innerTop && y < innerBottom;
}

bool HitRightSplitter(int x, int y, int innerTop, int innerBottom) {
  if (!g_showPropsDock || !g_propsDockExpanded) {
    return false;
  }
  RECT client{};
  GetClientRect(g_hwndMain, &client);
  const int totalW = client.right;
  const int propsTotalW = g_propsContentW + kDockStripW;
  const int rx0 = totalW - propsTotalW - kSplitterW;
  const int rx1 = totalW - propsTotalW;
  return x >= rx0 && x < rx1 && y >= innerTop && y < innerBottom;
}

HIMAGELIST BuildToolbarImageList() {
  HIMAGELIST himl = ImageList_Create(16, 16, ILC_COLOR32 | ILC_MASK, 8, 4);
  if (!himl) {
    return nullptr;
  }
  // SHSTOCKICONID 数值（与 SDK 枚举一致，避免旧头文件缺少部分符号）
  static const int kStockIds[6] = {
      3,    // SIID_FOLDER — 添加图层
      45,   // SIID_PRINT — 截图
      79,   // SIID_INFO — 日志
      34,   // SIID_DESKTOPPC — 2D
      135,  // SIID_WORLDWEB — 3D（占位区分）
      23,   // SIID_HELP — 关于
  };
  for (int sid : kStockIds) {
    SHSTOCKICONINFO sii{};
    sii.cbSize = sizeof(sii);
    if (SUCCEEDED(SHGetStockIconInfo(static_cast<SHSTOCKICONID>(sid), SHGFI_ICON | SHGFI_SMALLICON, &sii))) {
      ImageList_AddIcon(himl, sii.hIcon);
      DestroyIcon(sii.hIcon);
    } else {
      HICON hi = LoadIconW(nullptr, IDI_APPLICATION);
      if (hi) {
        ImageList_AddIcon(himl, hi);
      }
    }
  }
  return himl;
}

HWND CreateMainToolbar(HWND parent, HINSTANCE inst) {
  g_toolbarImageList = BuildToolbarImageList();
  HWND tb =
      CreateWindowExW(0, TOOLBARCLASSNAMEW, nullptr,
                      WS_CHILD | WS_VISIBLE | TBSTYLE_FLAT | TBSTYLE_TOOLTIPS | CCS_NODIVIDER | CCS_TOP,
                      0, 0, 0, 0, parent, reinterpret_cast<HMENU>(static_cast<INT_PTR>(IDC_MAIN_TOOLBAR)),
                      inst, nullptr);
  if (!tb) {
    return nullptr;
  }
  SendMessageW(tb, TB_BUTTONSTRUCTSIZE, sizeof(TBBUTTON), 0);
  if (g_toolbarImageList) {
    SendMessageW(tb, TB_SETIMAGELIST, 0, reinterpret_cast<LPARAM>(g_toolbarImageList));
  }
  SendMessageW(tb, TB_SETBUTTONSIZE, 0, MAKELPARAM(28, 26));

  TBBUTTON bt[8]{};
  auto setBtn = [](TBBUTTON* b, int image, int cmd) {
    b->iBitmap = image;
    b->idCommand = cmd;
    b->fsState = TBSTATE_ENABLED;
    b->fsStyle = BTNS_BUTTON;
    b->iString = (INT_PTR)-1;
  };
  auto setSep = [](TBBUTTON* b) {
    b->iBitmap = 0;
    b->idCommand = 0;
    b->fsState = TBSTATE_ENABLED;
    b->fsStyle = BTNS_SEP;
    b->iString = 0;
  };
  setBtn(&bt[0], 0, ID_LAYER_ADD);
#if !GIS_DESKTOP_HAVE_GDAL
  bt[0].fsState = 0;
#endif
  setSep(&bt[1]);
  setBtn(&bt[2], 1, ID_FILE_SCREENSHOT);
  setSep(&bt[3]);
  setBtn(&bt[4], 2, ID_VIEW_LOG);
  setBtn(&bt[5], 3, ID_VIEW_MODE_2D);
  setBtn(&bt[6], 4, ID_VIEW_MODE_3D);
  setBtn(&bt[7], 5, ID_HELP_ABOUT);
  SendMessageW(tb, TB_ADDBUTTONS, 8, reinterpret_cast<LPARAM>(bt));
  SendMessageW(tb, TB_AUTOSIZE, 0, 0);
  return tb;
}

constexpr UINT_PTR kLayerListSubclassId = 5;

static void LayerListSyncUiAfterOp(HWND listbox, HWND mainFrame, int newSelIndex) {
  MapEngine::Instance().RefreshLayerList(listbox);
  const int n = MapEngine::Instance().GetLayerCount();
  if (n <= 0) {
    SendMessageW(listbox, LB_SETCURSEL, static_cast<WPARAM>(-1), 0);
    PostMessageW(mainFrame, WM_APP_LAYER_SEL, 0, static_cast<LPARAM>(-1));
  } else {
    const int s = std::max(0, std::min(newSelIndex, n - 1));
    SendMessageW(listbox, LB_SETCURSEL, s, 0);
    PostMessageW(mainFrame, WM_APP_LAYER_SEL, 0, static_cast<LPARAM>(s));
  }
  if (g_hwndMap) {
    InvalidateRect(g_hwndMap, nullptr, FALSE);
  }
}

LRESULT CALLBACK LayerListSubclassProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam, UINT_PTR,
                                       DWORD_PTR refData) {
  if (msg == WM_LBUTTONDOWN) {
    const int x = GET_X_LPARAM(lParam);
    const int y = GET_Y_LPARAM(lParam);
    if (MapEngine::Instance().OnLayerListClick(hwnd, x, y)) {
      HWND layerPane = reinterpret_cast<HWND>(refData);
      HWND mainFr = GetParent(layerPane);
      const LRESULT lr = SendMessageW(hwnd, LB_ITEMFROMPOINT, 0, MAKELPARAM(x, y));
      if (lr != static_cast<LRESULT>(-1)) {
        const int hit = static_cast<int>(LOWORD(lr));
        SendMessageW(hwnd, LB_SETCURSEL, static_cast<WPARAM>(hit), 0);
        PostMessageW(mainFr, WM_APP_LAYER_SEL, 0, static_cast<LPARAM>(hit));
      }
      return 0;
    }
  }
  if (msg == WM_CONTEXTMENU) {
    HWND layerPane = reinterpret_cast<HWND>(refData);
    HWND mainFr = GetParent(layerPane);
    POINT pt{GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam)};
    if (pt.x == -1 && pt.y == -1) {
      GetCursorPos(&pt);
    }
    POINT clientPt = pt;
    ScreenToClient(hwnd, &clientPt);
    const LRESULT lr = SendMessageW(hwnd, LB_ITEMFROMPOINT, 0, MAKELPARAM(clientPt.x, clientPt.y));
    int hit = -1;
    if (static_cast<int>(lr) != LB_ERR && HIWORD(lr) == 0) {
      hit = static_cast<int>(LOWORD(lr));
    }
    const int n = MapEngine::Instance().GetLayerCount();
    const bool onLayer = (n > 0 && hit >= 0 && hit < n);
    const bool canUp = onLayer && hit > 0;
    const bool canDown = onLayer && hit < n - 1;

    HMENU pop = CreatePopupMenu();
    AppendMenuW(pop,
                MF_STRING | (GIS_DESKTOP_HAVE_GDAL ? MF_ENABLED : MF_GRAYED),
                ID_LAYER_CTX_ADD, L"添加图层…");
    AppendMenuW(pop, MF_SEPARATOR, 0, nullptr);
    AppendMenuW(pop, MF_STRING | (onLayer ? MF_ENABLED : MF_GRAYED), ID_LAYER_CTX_DELETE, L"删除");
    AppendMenuW(pop, MF_STRING | (canUp ? MF_ENABLED : MF_GRAYED), ID_LAYER_CTX_UP, L"上移");
    AppendMenuW(pop, MF_STRING | (canDown ? MF_ENABLED : MF_GRAYED), ID_LAYER_CTX_DOWN, L"下移");

    SetForegroundWindow(mainFr);
    const UINT cmd =
        TrackPopupMenu(pop, TPM_RIGHTBUTTON | TPM_LEFTALIGN | TPM_RETURNCMD, pt.x, pt.y, 0, mainFr, nullptr);
    DestroyMenu(pop);
    if (cmd == 0) {
      return 0;
    }

    switch (cmd) {
      case ID_LAYER_CTX_ADD:
#if !GIS_DESKTOP_HAVE_GDAL
        return 0;
#else
        MapEngine::Instance().OnAddLayerFromDialog(mainFr, hwnd);
        {
          const int nn = MapEngine::Instance().GetLayerCount();
          if (nn > 0) {
            SendMessageW(hwnd, LB_SETCURSEL, static_cast<WPARAM>(nn - 1), 0);
            PostMessageW(mainFr, WM_APP_LAYER_SEL, 0, static_cast<LPARAM>(nn - 1));
          }
        }
        if (g_hwndMap) {
          InvalidateRect(g_hwndMap, nullptr, FALSE);
        }
        AppLogLine(L"[图层] 已通过右键菜单发起添加。");
        return 0;
#endif
      case ID_LAYER_CTX_DELETE: {
        if (!onLayer) {
          return 0;
        }
        std::wstring err;
        if (!MapEngine::Instance().Document().RemoveLayerAt(static_cast<size_t>(hit), err)) {
          if (!err.empty()) {
            MessageBoxW(mainFr, err.c_str(), L"AGIS", MB_OK | MB_ICONWARNING);
          }
          return 0;
        }
        AppLogLine(L"[图层] 已删除所选图层。");
        {
          const int nn = MapEngine::Instance().GetLayerCount();
          const int newSel = (nn > 0) ? std::min(hit, nn - 1) : -1;
          LayerListSyncUiAfterOp(hwnd, mainFr, newSel);
        }
        return 0;
      }
      case ID_LAYER_CTX_UP: {
        if (!canUp) {
          return 0;
        }
        MapEngine::Instance().Document().MoveLayerUp(static_cast<size_t>(hit));
        AppLogLine(L"[图层] 已上移。");
        LayerListSyncUiAfterOp(hwnd, mainFr, hit - 1);
        return 0;
      }
      case ID_LAYER_CTX_DOWN: {
        if (!canDown) {
          return 0;
        }
        MapEngine::Instance().Document().MoveLayerDown(static_cast<size_t>(hit));
        AppLogLine(L"[图层] 已下移。");
        LayerListSyncUiAfterOp(hwnd, mainFr, hit + 1);
        return 0;
      }
      default:
        break;
    }
    return 0;
  }
  return DefSubclassProc(hwnd, msg, wParam, lParam);
}

LRESULT CALLBACK LayerPaneProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) {
  switch (msg) {
    case WM_MOUSEWHEEL:
      if (ForwardWheelToMapIfOver(wParam, lParam)) {
        return 0;
      }
      break;
    case WM_CREATE:
      CreateWindowExW(WS_EX_CLIENTEDGE, WC_LISTBOXW, L"",
                      WS_CHILD | WS_VISIBLE | LBS_OWNERDRAWFIXED | LBS_NOINTEGRALHEIGHT | LBS_NOTIFY | WS_VSCROLL, 8, 52,
                      200, 200, hwnd, reinterpret_cast<HMENU>(static_cast<INT_PTR>(IDC_LAYER_LIST)),
                      GetModuleHandleW(nullptr), nullptr);
      if (HWND lb = GetDlgItem(hwnd, IDC_LAYER_LIST)) {
        SetWindowSubclass(lb, LayerListSubclassProc, kLayerListSubclassId, reinterpret_cast<DWORD_PTR>(hwnd));
        SendMessageW(lb, WM_SETFONT, reinterpret_cast<WPARAM>(UiGetAppFont()), TRUE);
      }
      return 0;
    case WM_DESTROY:
      if (HWND lb = GetDlgItem(hwnd, IDC_LAYER_LIST)) {
        RemoveWindowSubclass(lb, LayerListSubclassProc, kLayerListSubclassId);
      }
      return 0;
    case WM_COMMAND:
      if (LOWORD(wParam) == IDC_LAYER_LIST && HIWORD(wParam) == LBN_SELCHANGE) {
        HWND lb = GetDlgItem(hwnd, IDC_LAYER_LIST);
        const LRESULT sel = lb ? SendMessageW(lb, LB_GETCURSEL, 0, 0) : LB_ERR;
        PostMessageW(GetParent(hwnd), WM_APP_LAYER_SEL, 0, static_cast<LPARAM>(sel));
        return 0;
      }
      break;
    case WM_SIZE: {
      HWND lb = GetDlgItem(hwnd, IDC_LAYER_LIST);
      if (lb) {
        RECT r{};
        GetClientRect(hwnd, &r);
        const int rw = std::max(0, static_cast<int>(r.right) - 16);
        const int rh = std::max(0, static_cast<int>(r.bottom) - 60);
        MoveWindow(lb, 8, 52, rw, rh, TRUE);
      }
      return 0;
    }
    case WM_MEASUREITEM: {
      auto* mis = reinterpret_cast<LPMEASUREITEMSTRUCT>(lParam);
      if (mis && mis->CtlID == IDC_LAYER_LIST) {
        MapEngine::Instance().MeasureLayerListItem(mis);
        return TRUE;
      }
      break;
    }
    case WM_DRAWITEM: {
      const DRAWITEMSTRUCT* dis = reinterpret_cast<LPDRAWITEMSTRUCT>(lParam);
      if (dis && dis->CtlID == IDC_LAYER_LIST) {
        MapEngine::Instance().PaintLayerListItem(dis);
        return TRUE;
      }
      break;
    }
    case WM_ERASEBKGND:
      return 1;
    case WM_PAINT: {
      PAINTSTRUCT ps{};
      HDC hdc = BeginPaint(hwnd, &ps);
      RECT r{};
      GetClientRect(hwnd, &r);
      UiPaintLayerPanel(hdc, r);
      EndPaint(hwnd, &ps);
      return 0;
    }
    default:
      return DefWindowProcW(hwnd, msg, wParam, lParam);
  }
  return DefWindowProcW(hwnd, msg, wParam, lParam);
}

static void LayoutPropsPane(HWND hwnd) {
  RECT r{};
  GetClientRect(hwnd, &r);
  const int rw = r.right;
  const int rh = r.bottom;
  constexpr int kBtnRow = 36;
  constexpr int kContentTop = 56;
  constexpr int kPadBottom = 8;
  /** 与 gdiplus_ui DrawPropsCardHeader 的标题带高度一致。 */
  constexpr int kSectionHeader = 28;
  constexpr int kInnerPad = 6;
  constexpr int kCardBottom = 5;
  constexpr int g2 = 10;
  const int btnTop = std::max(0, rh - kBtnRow);
  const int avail = btnTop - kContentTop - kPadBottom;
  /** 两段：每段 (标题+内边距) + 编辑区 + 卡片底边；中间 g2。 */
  const int perSection = kSectionHeader + kInnerPad + kCardBottom;
  const int bothEd = avail - 2 * perSection - g2;
  const int edH = std::max(32, bothEd / 2);
  const int x = 12;
  const int w = std::max(40, rw - 24);
  const int yDriverEd = kContentTop + kSectionHeader + kInnerPad;
  const int ySrcEd = yDriverEd + edH + kCardBottom + g2 + kSectionHeader + kInnerPad;
  if (HWND h = GetDlgItem(hwnd, IDC_PROPS_LBL_DRIVER)) {
    MoveWindow(h, x, kContentTop, w, kSectionHeader, FALSE);
    ShowWindow(h, SW_HIDE);
  }
  if (HWND h = GetDlgItem(hwnd, IDC_PROPS_DRIVER_EDIT)) {
    MoveWindow(h, x, yDriverEd, w, edH, TRUE);
  }
  if (HWND h = GetDlgItem(hwnd, IDC_PROPS_LBL_SOURCE)) {
    MoveWindow(h, x, yDriverEd + edH + kCardBottom + g2, w, kSectionHeader, FALSE);
    ShowWindow(h, SW_HIDE);
  }
  if (HWND h = GetDlgItem(hwnd, IDC_PROPS_SOURCE_EDIT)) {
    MoveWindow(h, x, ySrcEd, w, edH, TRUE);
  }
  g_propsDriverCardRc.left = x - 6;
  g_propsDriverCardRc.top = kContentTop - 3;
  g_propsDriverCardRc.right = x + w + 6;
  g_propsDriverCardRc.bottom = yDriverEd + edH + kCardBottom;
  g_propsSourceCardRc.left = x - 6;
  g_propsSourceCardRc.top = yDriverEd + edH + kCardBottom + g2 - 3;
  g_propsSourceCardRc.right = x + w + 6;
  g_propsSourceCardRc.bottom = ySrcEd + edH + kCardBottom;
  const int bw = std::max(60, (rw - 32) / 3);
  if (HWND b1 = GetDlgItem(hwnd, IDC_PROPS_BUILD_OV)) {
    MoveWindow(b1, 12, btnTop, bw, 28, TRUE);
  }
  if (HWND b2 = GetDlgItem(hwnd, IDC_PROPS_CLEAR_OV)) {
    MoveWindow(b2, 16 + bw, btnTop, bw, 28, TRUE);
  }
  if (HWND b3 = GetDlgItem(hwnd, IDC_PROPS_CHANGE_SRC)) {
    MoveWindow(b3, 20 + 2 * bw, btnTop, std::max(60, rw - 32 - 2 * bw), 28, TRUE);
  }
}

void RefreshPropsPanel(HWND hwndProps) {
  if (!hwndProps) {
    return;
  }
  HWND edDriver = GetDlgItem(hwndProps, IDC_PROPS_DRIVER_EDIT);
  HWND edSrc = GetDlgItem(hwndProps, IDC_PROPS_SOURCE_EDIT);
  HWND b1 = GetDlgItem(hwndProps, IDC_PROPS_BUILD_OV);
  HWND b2 = GetDlgItem(hwndProps, IDC_PROPS_CLEAR_OV);
  HWND b3 = GetDlgItem(hwndProps, IDC_PROPS_CHANGE_SRC);
  std::wstring title;
  std::wstring driverTxt;
  std::wstring sourceTxt;
  MapEngine::Instance().GetLayerInfoForUi(g_layerSelIndex, &title, &driverTxt, &sourceTxt);
  g_propsLayerSubtitleForPaint = title;
  if (edDriver) {
    SetWindowTextW(edDriver, driverTxt.c_str());
  }
  if (edSrc) {
    SetWindowTextW(edSrc, sourceTxt.c_str());
  }
  const bool raster = MapEngine::Instance().IsRasterGdalLayer(g_layerSelIndex);
  const bool layerOk = g_layerSelIndex >= 0 && g_layerSelIndex < MapEngine::Instance().GetLayerCount();
  if (b1) {
    EnableWindow(b1, (raster && layerOk) ? TRUE : FALSE);
  }
  if (b2) {
    EnableWindow(b2, (raster && layerOk) ? TRUE : FALSE);
  }
  if (b3) {
    EnableWindow(b3, layerOk ? TRUE : FALSE);
  }
  InvalidateRect(hwndProps, nullptr, FALSE);
}

LRESULT CALLBACK PropsPaneProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) {
  switch (msg) {
    case WM_MOUSEWHEEL:
      if (ForwardWheelToMapIfOver(wParam, lParam)) {
        return 0;
      }
      break;
    case WM_CREATE: {
      HINSTANCE inst = GetModuleHandleW(nullptr);
      CreateWindowW(L"STATIC", L"驱动属性", WS_CHILD | SS_LEFT, 12, 56, 100, 18, hwnd,
                    reinterpret_cast<HMENU>(static_cast<INT_PTR>(IDC_PROPS_LBL_DRIVER)), inst, nullptr);
      CreateWindowExW(
          WS_EX_CLIENTEDGE, L"EDIT", L"",
          WS_CHILD | WS_VISIBLE | ES_MULTILINE | ES_READONLY | ES_AUTOVSCROLL | WS_VSCROLL | WS_TABSTOP, 12, 78, 100,
          80, hwnd, reinterpret_cast<HMENU>(static_cast<INT_PTR>(IDC_PROPS_DRIVER_EDIT)), inst, nullptr);
      CreateWindowW(L"STATIC", L"数据源属性", WS_CHILD | SS_LEFT, 12, 164, 100, 18, hwnd,
                    reinterpret_cast<HMENU>(static_cast<INT_PTR>(IDC_PROPS_LBL_SOURCE)), inst, nullptr);
      CreateWindowExW(
          WS_EX_CLIENTEDGE, L"EDIT", L"",
          WS_CHILD | WS_VISIBLE | ES_MULTILINE | ES_READONLY | ES_AUTOVSCROLL | WS_VSCROLL | WS_TABSTOP, 12, 186, 100,
          80, hwnd, reinterpret_cast<HMENU>(static_cast<INT_PTR>(IDC_PROPS_SOURCE_EDIT)), inst, nullptr);
      CreateWindowW(L"BUTTON", L"生成金字塔", WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON | WS_TABSTOP, 12, 0, 90, 28,
                    hwnd, reinterpret_cast<HMENU>(static_cast<INT_PTR>(IDC_PROPS_BUILD_OV)), inst, nullptr);
      CreateWindowW(L"BUTTON", L"删除金字塔", WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON | WS_TABSTOP, 108, 0, 90, 28,
                    hwnd, reinterpret_cast<HMENU>(static_cast<INT_PTR>(IDC_PROPS_CLEAR_OV)), inst, nullptr);
      CreateWindowW(L"BUTTON", L"更换数据源…", WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON | WS_TABSTOP, 204, 0, 120, 28,
                    hwnd, reinterpret_cast<HMENU>(static_cast<INT_PTR>(IDC_PROPS_CHANGE_SRC)), inst, nullptr);
      for (int cid :
           {IDC_PROPS_LBL_DRIVER, IDC_PROPS_LBL_SOURCE, IDC_PROPS_DRIVER_EDIT, IDC_PROPS_SOURCE_EDIT}) {
        if (HWND c = GetDlgItem(hwnd, cid)) {
          SendMessageW(c, WM_SETFONT, reinterpret_cast<WPARAM>(UiGetAppFont()), TRUE);
        }
      }
      if (HWND ed = GetDlgItem(hwnd, IDC_PROPS_DRIVER_EDIT)) {
        SendMessageW(ed, EM_SETMARGINS, static_cast<WPARAM>(0x0001u | 0x0002u),
                     static_cast<LPARAM>(MAKELPARAM(10, 10)));
      }
      if (HWND ed = GetDlgItem(hwnd, IDC_PROPS_SOURCE_EDIT)) {
        SendMessageW(ed, EM_SETMARGINS, static_cast<WPARAM>(0x0001u | 0x0002u),
                     static_cast<LPARAM>(MAKELPARAM(10, 10)));
      }
      for (int bid : {IDC_PROPS_BUILD_OV, IDC_PROPS_CLEAR_OV, IDC_PROPS_CHANGE_SRC}) {
        if (HWND b = GetDlgItem(hwnd, bid)) {
          SendMessageW(b, WM_SETFONT, reinterpret_cast<WPARAM>(UiGetAppFont()), TRUE);
        }
      }
      LayoutPropsPane(hwnd);
      RefreshPropsPanel(hwnd);
      return 0;
    }
    case WM_SIZE: {
      LayoutPropsPane(hwnd);
      return 0;
    }
    case WM_COMMAND: {
      const int id = LOWORD(wParam);
      const int code = HIWORD(wParam);
      if (code != BN_CLICKED) {
        break;
      }
      HWND owner = GetParent(hwnd);
      if (id == IDC_PROPS_BUILD_OV) {
        std::wstring err;
        if (MapEngine::Instance().BuildOverviewsForLayer(g_layerSelIndex, err)) {
          AppLogLine(L"[图层] 已生成金字塔。");
          if (g_hwndMap) {
            InvalidateRect(g_hwndMap, nullptr, FALSE);
          }
          RefreshPropsPanel(hwnd);
        } else {
          AppLogLine(std::wstring(L"[错误] 生成金字塔：") + err);
          MessageBoxW(owner, err.c_str(), L"AGIS", MB_OK | MB_ICONERROR);
        }
        return 0;
      }
      if (id == IDC_PROPS_CLEAR_OV) {
        std::wstring err;
        if (MapEngine::Instance().ClearOverviewsForLayer(g_layerSelIndex, err)) {
          AppLogLine(L"[图层] 已删除金字塔。");
          if (g_hwndMap) {
            InvalidateRect(g_hwndMap, nullptr, FALSE);
          }
          RefreshPropsPanel(hwnd);
        } else {
          AppLogLine(std::wstring(L"[错误] 删除金字塔：") + err);
          MessageBoxW(owner, err.c_str(), L"AGIS", MB_OK | MB_ICONERROR);
        }
        return 0;
      }
      if (id == IDC_PROPS_CHANGE_SRC) {
        HWND lb = g_hwndLayer ? GetDlgItem(g_hwndLayer, IDC_LAYER_LIST) : nullptr;
        if (MapEngine::Instance().ReplaceLayerSourceFromUi(owner, lb, g_layerSelIndex)) {
          MapEngine::Instance().RefreshLayerList(lb);
          RefreshPropsPanel(hwnd);
        }
        return 0;
      }
      break;
    }
    case WM_ERASEBKGND:
      return 1;
    case WM_PAINT: {
      PAINTSTRUCT ps{};
      HDC hdc = BeginPaint(hwnd, &ps);
      RECT r{};
      GetClientRect(hwnd, &r);
      UiPaintLayerPropsDockFrame(hdc, r, &g_propsDriverCardRc, &g_propsSourceCardRc,
                                 g_propsLayerSubtitleForPaint.c_str());
      EndPaint(hwnd, &ps);
      return 0;
    }
    default:
      return DefWindowProcW(hwnd, msg, wParam, lParam);
  }
  return DefWindowProcW(hwnd, msg, wParam, lParam);
}

LRESULT CALLBACK LogWndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) {
  switch (msg) {
    case WM_CREATE: {
      const auto* cs = reinterpret_cast<CREATESTRUCTW*>(lParam);
      const int cw = std::max(100, static_cast<int>(cs->cx));
      const int ch = std::max(80, static_cast<int>(cs->cy));
      const int ew = std::max(80, cw - 24);
      const int eh = std::max(60, ch - 52);
      const int bx = std::max(12, cw - 132);
      const int by = std::max(12, ch - 40);
      HWND ed = CreateWindowExW(
          WS_EX_CLIENTEDGE, L"EDIT", AppLogGetText().c_str(),
          WS_CHILD | WS_VISIBLE | ES_MULTILINE | ES_READONLY | ES_AUTOVSCROLL | WS_VSCROLL, 12, 12, ew, eh, hwnd,
          reinterpret_cast<HMENU>(static_cast<INT_PTR>(IDC_LOG_EDIT)), GetModuleHandleW(nullptr), nullptr);
      CreateWindowW(L"BUTTON", L"复制全部", WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON, bx, by, 120, 28, hwnd,
                    reinterpret_cast<HMENU>(static_cast<INT_PTR>(IDC_LOG_COPY)), GetModuleHandleW(nullptr),
                    nullptr);
      AppLogSetEdit(ed);
      AppLogFlushToEdit();
      if (ed) {
        SendMessageW(ed, WM_SETFONT, reinterpret_cast<WPARAM>(UiGetAppFont()), TRUE);
      }
      if (HWND bt = GetDlgItem(hwnd, IDC_LOG_COPY)) {
        SendMessageW(bt, WM_SETFONT, reinterpret_cast<WPARAM>(UiGetAppFont()), TRUE);
      }
      return 0;
    }
    case WM_SIZE: {
      RECT r{};
      GetClientRect(hwnd, &r);
      const int rw = static_cast<int>(r.right);
      const int rh = static_cast<int>(r.bottom);
      HWND ed = GetDlgItem(hwnd, IDC_LOG_EDIT);
      HWND bt = GetDlgItem(hwnd, IDC_LOG_COPY);
      if (ed) {
        const int ew = std::max(80, rw - 24);
        const int eh = std::max(60, rh - 52);
        MoveWindow(ed, 12, 12, ew, eh, TRUE);
      }
      if (bt) {
        const int bx = std::max(12, rw - 132);
        const int by = std::max(12, rh - 40);
        MoveWindow(bt, bx, by, 120, 28, TRUE);
      }
      return 0;
    }
    case WM_COMMAND:
      if (LOWORD(wParam) == IDC_LOG_COPY) {
        HWND edit = GetDlgItem(hwnd, IDC_LOG_EDIT);
        const int n = GetWindowTextLengthW(edit);
        std::wstring buf(static_cast<size_t>(n) + 1, L'\0');
        GetWindowTextW(edit, buf.data(), n + 1);
        buf.resize(static_cast<size_t>(n));
        CopyTextToClipboard(hwnd, buf);
        AppLogLine(L"[日志] 已复制到剪贴板。");
        return 0;
      }
      break;
    case WM_CLOSE:
      AppLogSetEdit(nullptr);
      DestroyWindow(hwnd);
      g_hwndLogDlg = nullptr;
      return 0;
    default:
      break;
  }
  return DefWindowProcW(hwnd, msg, wParam, lParam);
}

void ShowLogDialog(HWND owner) {
  if (g_hwndLogDlg && IsWindow(g_hwndLogDlg)) {
    SetForegroundWindow(g_hwndLogDlg);
    HWND ed = GetDlgItem(g_hwndLogDlg, IDC_LOG_EDIT);
    if (ed) {
      SetWindowTextW(ed, AppLogGetText().c_str());
    }
    return;
  }
  g_hwndLogDlg =
      CreateWindowExW(WS_EX_DLGMODALFRAME, kLogClass, L"日志",
                      WS_POPUP | WS_CAPTION | WS_SYSMENU | WS_SIZEBOX | WS_MINIMIZEBOX, CW_USEDEFAULT,
                      CW_USEDEFAULT, 640, 420, owner, nullptr, GetModuleHandleW(nullptr), nullptr);
  if (g_hwndLogDlg) {
    ShowWindow(g_hwndLogDlg, SW_SHOW);
  }
}

void ShowAbout(HWND owner) {
  (void)owner;
  AppLogLine(L"[关于] AGIS — GIS 数据编辑与处理（开发版）");
  AppLogLine(L"[关于] 版本 0.1.0-dev · Windows 本地 SDI · GDAL/GDI+ 集成中");
  AppLogLine(L"[提示] 关于信息见上文；可通过菜单「帮助 → 关于」再次写入日志。");
  (void)owner;
}

void WriteConvertLog(HWND hwnd, const wchar_t* line) {
  HWND hLog = GetDlgItem(hwnd, IDC_CONV_LOG);
  if (!hLog || !line) {
    return;
  }
  const int len = GetWindowTextLengthW(hLog);
  SendMessageW(hLog, EM_SETSEL, static_cast<WPARAM>(len), static_cast<LPARAM>(len));
  std::wstring s = line;
  s += L"\r\n";
  SendMessageW(hLog, EM_REPLACESEL, FALSE, reinterpret_cast<LPARAM>(s.c_str()));
}

std::wstring CurrentWindowTitle() {
  if (g_currentGisPath.empty()) {
    return L"AGIS — 地图视图（单文档 SDI）";
  }
  return std::wstring(L"AGIS — ") + g_currentGisPath;
}

void SyncMainTitle() {
  if (g_hwndMain) {
    SetWindowTextW(g_hwndMain, CurrentWindowTitle().c_str());
  }
}

std::wstring XmlEscape(const std::wstring& s) {
  std::wstring out;
  out.reserve(s.size() + 16);
  for (wchar_t ch : s) {
    switch (ch) {
      case L'&':
        out += L"&amp;";
        break;
      case L'<':
        out += L"&lt;";
        break;
      case L'>':
        out += L"&gt;";
        break;
      case L'"':
        out += L"&quot;";
        break;
      case L'\'':
        out += L"&apos;";
        break;
      default:
        out.push_back(ch);
        break;
    }
  }
  return out;
}

std::wstring XmlUnescape(const std::wstring& s) {
  std::wstring out = s;
  auto rep = [&out](const wchar_t* from, const wchar_t* to) {
    size_t pos = 0;
    const size_t fromLen = wcslen(from);
    const size_t toLen = wcslen(to);
    while ((pos = out.find(from, pos)) != std::wstring::npos) {
      out.replace(pos, fromLen, to);
      pos += toLen;
    }
  };
  rep(L"&lt;", L"<");
  rep(L"&gt;", L">");
  rep(L"&quot;", L"\"");
  rep(L"&apos;", L"'");
  rep(L"&amp;", L"&");
  return out;
}

std::wstring GetXmlAttr(const std::wstring& line, const wchar_t* key) {
  const std::wstring k = std::wstring(key) + L"=\"";
  const size_t p0 = line.find(k);
  if (p0 == std::wstring::npos) {
    return L"";
  }
  const size_t p1 = p0 + k.size();
  const size_t p2 = line.find(L"\"", p1);
  if (p2 == std::wstring::npos || p2 <= p1) {
    return L"";
  }
  return XmlUnescape(line.substr(p1, p2 - p1));
}

bool ParseBoolAttr(const std::wstring& line, const wchar_t* key, bool def) {
  const std::wstring s = GetXmlAttr(line, key);
  if (s.empty()) {
    return def;
  }
  return !(s == L"0" || s == L"false" || s == L"False" || s == L"FALSE");
}

int ParseIntAttr(const std::wstring& line, const wchar_t* key, int def) {
  const std::wstring s = GetXmlAttr(line, key);
  if (s.empty()) {
    return def;
  }
  return _wtoi(s.c_str());
}

double ParseDoubleAttr(const std::wstring& line, const wchar_t* key, double def) {
  const std::wstring s = GetXmlAttr(line, key);
  if (s.empty()) {
    return def;
  }
  return _wtof(s.c_str());
}

std::wstring DriverKindToTag(MapLayerDriverKind kind) {
  switch (kind) {
    case MapLayerDriverKind::kGdalFile:
      return L"gdal-file";
    case MapLayerDriverKind::kTmsXyz:
      return L"tms-xyz";
    case MapLayerDriverKind::kWmts:
      return L"wmts";
    case MapLayerDriverKind::kArcGisRestJson:
      return L"arcgis-rest-json";
    case MapLayerDriverKind::kSoapPlaceholder:
      return L"soap";
    case MapLayerDriverKind::kWmsPlaceholder:
      return L"wms";
    default:
      return L"unknown";
  }
}

MapLayerDriverKind DriverKindFromTag(const std::wstring& s) {
  if (s == L"tms-xyz") {
    return MapLayerDriverKind::kTmsXyz;
  }
  if (s == L"wmts") {
    return MapLayerDriverKind::kWmts;
  }
  if (s == L"arcgis-rest-json") {
    return MapLayerDriverKind::kArcGisRestJson;
  }
  if (s == L"soap") {
    return MapLayerDriverKind::kSoapPlaceholder;
  }
  if (s == L"wms") {
    return MapLayerDriverKind::kWmsPlaceholder;
  }
  return MapLayerDriverKind::kGdalFile;
}

void RefreshUiAfterDocumentReload() {
  if (g_hwndLayer) {
    if (HWND lb = GetDlgItem(g_hwndLayer, IDC_LAYER_LIST)) {
      MapEngine::Instance().RefreshLayerList(lb);
    }
  }
  g_layerSelIndex = -1;
  if (g_hwndProps) {
    RefreshPropsPanel(g_hwndProps);
  }
  if (g_hwndMap) {
    InvalidateRect(g_hwndMap, nullptr, FALSE);
  }
}

bool SaveGisXmlTo(const std::wstring& path) {
  std::wofstream ofs(path);
  if (!ofs.is_open()) {
    return false;
  }
  const auto& doc = MapEngine::Instance().Document();
  ofs << L"<?xml version=\"1.0\" encoding=\"utf-8\"?>\n";
  ofs << L"<agis-gis version=\"1.0\">\n";
  ofs << L"  <display projection=\"" << static_cast<int>(doc.GetDisplayProjection()) << L"\"";
  ofs << L" showGrid=\"" << (doc.GetShowLatLonGrid() ? 1 : 0) << L"\"";
  ofs << L" viewMinX=\"" << doc.view.minX << L"\" viewMinY=\"" << doc.view.minY << L"\"";
  ofs << L" viewMaxX=\"" << doc.view.maxX << L"\" viewMaxY=\"" << doc.view.maxY << L"\"/>\n";
  ofs << L"  <layers count=\"" << doc.layers.size() << L"\">\n";
  for (const auto& lyr : doc.layers) {
    if (!lyr) {
      continue;
    }
    const std::wstring src = lyr->SourcePathForSave();
    const std::wstring name = lyr->DisplayName();
    ofs << L"    <layer driver=\"" << DriverKindToTag(lyr->DriverKind()) << L"\"";
    ofs << L" visible=\"" << (lyr->IsLayerVisible() ? 1 : 0) << L"\"";
    ofs << L" source=\"" << XmlEscape(src) << L"\"";
    ofs << L" name=\"" << XmlEscape(name) << L"\"/>\n";
  }
  ofs << L"  </layers>\n";
  ofs << L"</agis-gis>\n";
  return true;
}

std::wstring ParentDirOfPath(const std::wstring& path) {
  const size_t p = path.find_last_of(L"\\/");
  if (p == std::wstring::npos) {
    return L"";
  }
  return path.substr(0, p);
}

bool IsLikelyAbsoluteOrUrl(const std::wstring& s) {
  if (s.size() >= 2 && s[1] == L':') {
    return true;  // C:\...
  }
  if (!s.empty() && (s[0] == L'\\' || s[0] == L'/')) {
    return true;
  }
  return s.find(L"://") != std::wstring::npos || s.find(L":\\\\") != std::wstring::npos;
}

std::wstring JoinPathSimple(const std::wstring& dir, const std::wstring& file) {
  if (dir.empty()) {
    return file;
  }
  if (dir.back() == L'\\' || dir.back() == L'/') {
    return dir + file;
  }
  return dir + L"\\" + file;
}

bool LoadGisXmlFrom(const std::wstring& path, std::wstring* err) {
  std::wifstream ifs(path);
  if (!ifs.is_open()) {
    if (err) {
      *err = L"无法打开文件。";
    }
    return false;
  }
  std::wstringstream ss;
  ss << ifs.rdbuf();
  const std::wstring xml = ss.str();
  if (xml.find(L"<agis-gis") == std::wstring::npos) {
    if (err) {
      *err = L"不是有效的 .gis(XML) 文件。";
    }
    return false;
  }
  auto& doc = MapEngine::Instance().Document();
  const std::wstring gisDir = ParentDirOfPath(path);
  doc.layers.clear();
  doc.SetDisplayProjection(static_cast<MapDisplayProjection>(ParseIntAttr(xml, L"projection", 0)));
  doc.SetShowLatLonGrid(ParseBoolAttr(xml, L"showGrid", true));

  doc.view.minX = ParseDoubleAttr(xml, L"viewMinX", doc.view.minX);
  doc.view.minY = ParseDoubleAttr(xml, L"viewMinY", doc.view.minY);
  doc.view.maxX = ParseDoubleAttr(xml, L"viewMaxX", doc.view.maxX);
  doc.view.maxY = ParseDoubleAttr(xml, L"viewMaxY", doc.view.maxY);

  size_t pos = 0;
  int loaded = 0;
  int failed = 0;
  while ((pos = xml.find(L"<layer ", pos)) != std::wstring::npos) {
    const size_t end = xml.find(L"/>", pos);
    if (end == std::wstring::npos) {
      break;
    }
    const std::wstring line = xml.substr(pos, end - pos + 2);
    pos = end + 2;

    const MapLayerDriverKind kind = DriverKindFromTag(GetXmlAttr(line, L"driver"));
    const bool visible = ParseBoolAttr(line, L"visible", true);
    std::wstring source = GetXmlAttr(line, L"source");
    if (!source.empty() && !IsLikelyAbsoluteOrUrl(source)) {
      source = JoinPathSimple(gisDir, source);
    }

    std::wstring loadErr;
    bool ok = false;
    switch (kind) {
      case MapLayerDriverKind::kTmsXyz:
        ok = doc.AddLayerFromTmsUrl(source, loadErr);
        break;
      case MapLayerDriverKind::kWmts:
        ok = doc.AddLayerFromWmtsUrl(source, loadErr);
        break;
      case MapLayerDriverKind::kArcGisRestJson:
        ok = doc.AddLayerFromArcGisRestJsonUrl(source, loadErr);
        break;
      default:
        ok = doc.AddLayerFromFile(source, loadErr);
        break;
    }
    if (!ok) {
      ++failed;
      AppLogLine(std::wstring(L"[GIS] 图层恢复失败：") + source + L"，原因：" + loadErr);
      continue;
    }
    ++loaded;
    if (!doc.layers.empty()) {
      doc.layers.back()->SetLayerVisible(visible);
    }
  }
  if (!doc.view.valid()) {
    doc.FitViewToLayers();
  }
  AppLogLine(L"[GIS] 已读取 .gis 文件：图层与显示状态已恢复。");
  AppLogLine(std::wstring(L"[GIS] 恢复图层成功/失败：") + std::to_wstring(loaded) + L"/" + std::to_wstring(failed));
  return true;
}

std::wstring PromptOpenGisPath(HWND owner) {
  wchar_t path[MAX_PATH]{};
  OPENFILENAMEW ofn{};
  ofn.lStructSize = sizeof(ofn);
  ofn.hwndOwner = owner;
  ofn.lpstrFilter = L"AGIS GIS 文件 (*.gis)\0*.gis\0XML 文件 (*.xml)\0*.xml\0所有文件 (*.*)\0*.*\0";
  ofn.lpstrFile = path;
  ofn.nMaxFile = MAX_PATH;
  ofn.Flags = OFN_FILEMUSTEXIST | OFN_PATHMUSTEXIST;
  if (GetOpenFileNameW(&ofn) == 0) {
    return L"";
  }
  return path;
}

std::wstring PromptSaveGisPath(HWND owner, const std::wstring& seed) {
  wchar_t path[MAX_PATH]{};
  if (!seed.empty()) {
    wcsncpy_s(path, seed.c_str(), _TRUNCATE);
  }
  OPENFILENAMEW ofn{};
  ofn.lStructSize = sizeof(ofn);
  ofn.hwndOwner = owner;
  ofn.lpstrFilter = L"AGIS GIS 文件 (*.gis)\0*.gis\0XML 文件 (*.xml)\0*.xml\0";
  ofn.lpstrFile = path;
  ofn.nMaxFile = MAX_PATH;
  ofn.lpstrDefExt = L"gis";
  ofn.Flags = OFN_OVERWRITEPROMPT | OFN_PATHMUSTEXIST;
  if (GetSaveFileNameW(&ofn) == 0) {
    return L"";
  }
  return path;
}

void GisNew(HWND owner) {
  auto& doc = MapEngine::Instance().Document();
  doc.layers.clear();
  doc.SetDisplayProjection(MapDisplayProjection::kGeographicWgs84);
  doc.SetShowLatLonGrid(true);
  doc.view = DefaultGeographicView();
  RefreshUiAfterDocumentReload();
  g_currentGisPath.clear();
  SyncMainTitle();
  AppLogLine(L"[GIS] 新建 .gis 文档。");
  MessageBoxW(owner, L"已新建空白 .gis 文档（XML）。", L"AGIS", MB_OK | MB_ICONINFORMATION);
}

void GisOpen(HWND owner) {
  const std::wstring path = PromptOpenGisPath(owner);
  if (path.empty()) {
    return;
  }
  std::wstring err;
  if (!LoadGisXmlFrom(path, &err)) {
    MessageBoxW(owner, err.c_str(), L"打开 .gis 失败", MB_OK | MB_ICONWARNING);
    return;
  }
  g_currentGisPath = path;
  RefreshUiAfterDocumentReload();
  SyncMainTitle();
  AppLogLine(std::wstring(L"[GIS] 打开文件：") + path);
}

void GisSaveAs(HWND owner) {
  const std::wstring path = PromptSaveGisPath(owner, g_currentGisPath);
  if (path.empty()) {
    return;
  }
  if (!SaveGisXmlTo(path)) {
    MessageBoxW(owner, L"保存失败。", L"保存 .gis", MB_OK | MB_ICONWARNING);
    return;
  }
  g_currentGisPath = path;
  SyncMainTitle();
  AppLogLine(std::wstring(L"[GIS] 已保存：") + path);
}

void GisSave(HWND owner) {
  if (g_currentGisPath.empty()) {
    GisSaveAs(owner);
    return;
  }
  if (!SaveGisXmlTo(g_currentGisPath)) {
    MessageBoxW(owner, L"保存失败。", L"保存 .gis", MB_OK | MB_ICONWARNING);
    return;
  }
  AppLogLine(std::wstring(L"[GIS] 已保存：") + g_currentGisPath);
}

void LayoutConvertWindow(HWND hwnd) {
  RECT rc{};
  GetClientRect(hwnd, &rc);
  const int w = rc.right - rc.left;
  const int h = rc.bottom - rc.top;
  const int m = 10;
  const int topH = std::max(180, h / 2);
  const int colW = (w - m * 4) / 3;

  MoveWindow(GetDlgItem(hwnd, IDC_CONV_INPUT_TYPE), m, m + 20, colW - 28, 220, TRUE);
  MoveWindow(GetDlgItem(hwnd, IDC_CONV_INPUT_TYPE_HELP), m + colW - 24, m + 20, 24, 24, TRUE);
  MoveWindow(GetDlgItem(hwnd, IDC_CONV_INPUT_SUBTYPE), m, m + 48, colW - 28, 220, TRUE);
  MoveWindow(GetDlgItem(hwnd, IDC_CONV_INPUT_SUBTYPE_HELP), m + colW - 24, m + 48, 24, 24, TRUE);
  MoveWindow(GetDlgItem(hwnd, IDC_CONV_INPUT_PATH), m, m + 76, colW - 140, 24, TRUE);
  MoveWindow(GetDlgItem(hwnd, IDC_CONV_INPUT_BROWSE), m + colW - 136, m + 76, 66, 24, TRUE);
  MoveWindow(GetDlgItem(hwnd, IDC_CONV_INPUT_PREVIEW), m + colW - 66, m + 76, 66, 24, TRUE);
  MoveWindow(GetDlgItem(hwnd, IDC_CONV_INPUT_INFO), m, m + 106, colW, topH - 110, TRUE);

  MoveWindow(GetDlgItem(hwnd, IDC_CONV_SETTING), m * 2 + colW, m + 20, colW, topH - 24, TRUE);

  MoveWindow(GetDlgItem(hwnd, IDC_CONV_OUTPUT_TYPE), m * 3 + colW * 2, m + 20, colW - 28, 220, TRUE);
  MoveWindow(GetDlgItem(hwnd, IDC_CONV_OUTPUT_TYPE_HELP), m * 3 + colW * 2 + colW - 24, m + 20, 24, 24, TRUE);
  MoveWindow(GetDlgItem(hwnd, IDC_CONV_OUTPUT_SUBTYPE), m * 3 + colW * 2, m + 48, colW - 28, 220, TRUE);
  MoveWindow(GetDlgItem(hwnd, IDC_CONV_OUTPUT_SUBTYPE_HELP), m * 3 + colW * 2 + colW - 24, m + 48, 24, 24, TRUE);
  MoveWindow(GetDlgItem(hwnd, IDC_CONV_OUTPUT_PATH), m * 3 + colW * 2, m + 76, colW - 140, 24, TRUE);
  MoveWindow(GetDlgItem(hwnd, IDC_CONV_OUTPUT_BROWSE), m * 3 + colW * 2 + colW - 136, m + 76, 66, 24, TRUE);
  MoveWindow(GetDlgItem(hwnd, IDC_CONV_OUTPUT_PREVIEW), m * 3 + colW * 2 + colW - 66, m + 76, 66, 24, TRUE);
  MoveWindow(GetDlgItem(hwnd, IDC_CONV_OUTPUT_INFO), m * 3 + colW * 2, m + 106, colW, topH - 110, TRUE);

  const int y1 = m + topH + 8;
  MoveWindow(GetDlgItem(hwnd, IDC_CONV_CMDLINE), m, y1, w - m * 2, 56, TRUE);
  MoveWindow(GetDlgItem(hwnd, IDC_CONV_PROGRESS), m, y1 + 60, w - m * 2 - 90, 22, TRUE);
  MoveWindow(GetDlgItem(hwnd, IDC_CONV_RUN), w - m - 100, y1 + 59, 100, 26, TRUE);
  MoveWindow(GetDlgItem(hwnd, IDC_CONV_MSG), m, y1 + 88, w - m * 2, 40, TRUE);
  MoveWindow(GetDlgItem(hwnd, IDC_CONV_LOG), m, y1 + 132, w - m * 2, h - (y1 + 132) - m, TRUE);
}

void FillConvertTypeCombo(HWND combo) {
  SendMessageW(combo, CB_RESETCONTENT, 0, 0);
  SendMessageW(combo, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(L"GIS数据（矢量/栅格）"));
  SendMessageW(combo, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(L"模型数据（TIN/DEM/3DMesh）"));
  SendMessageW(combo, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(L"瓦片数据（XYZ/TMS/WMTS）"));
  SendMessageW(combo, CB_SETCURSEL, 0, 0);
}

void FillConvertSubtypeCombo(HWND combo, int majorType) {
  SendMessageW(combo, CB_RESETCONTENT, 0, 0);
  switch (majorType) {
    case 0:
      SendMessageW(combo, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(L"全部（自动识别）"));
      SendMessageW(combo, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(L"矢量（Shapefile/GeoJSON）"));
      SendMessageW(combo, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(L"栅格（GeoTIFF）"));
      SendMessageW(combo, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(L"空间数据库（GPKG）"));
      break;
    case 1:
      SendMessageW(combo, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(L"TIN（三角网）"));
      SendMessageW(combo, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(L"DEM（高程栅格）"));
      SendMessageW(combo, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(L"3DMesh（网格模型）"));
      break;
    default:
      SendMessageW(combo, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(L"XYZ（金字塔瓦片）"));
      SendMessageW(combo, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(L"TMS（倒序行号）"));
      SendMessageW(combo, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(L"WMTS（服务化瓦片）"));
      break;
  }
  SendMessageW(combo, CB_SETCURSEL, 0, 0);
}

const wchar_t* ConvertTypeTooltipByMajor(int major) {
  switch (major) {
    case 0:
      return L"GIS数据：面向地图生产与分析。\n"
             L"- 矢量：点/线/面要素，属性字段丰富，适合查询与编辑。\n"
             L"- 栅格：像元网格（影像/高程等），适合连续场分析。\n"
             L"- 空间数据库：统一封装多图层与索引（如 GPKG）。";
    case 1:
      return L"模型数据：面向三维/地形表达。\n"
             L"- TIN：不规则三角网，地形边界表达精细。\n"
             L"- DEM：规则高程栅格，适合分析与重采样。\n"
             L"- 3DMesh：三维网格，适合渲染与发布。";
    default:
      return L"瓦片数据：面向快速显示与分发。\n"
             L"- XYZ：常用 Web 瓦片行列规则。\n"
             L"- TMS：与 XYZ 行号方向不同（Y 轴翻转）。\n"
             L"- WMTS：标准化服务接口，便于平台互操作。";
  }
}

const wchar_t* ConvertSubtypeTooltipByMajorSubtype(int major, int sub) {
  if (major == 0) {
    switch (sub) {
      case 0:
        return L"全部（自动识别）\n"
               L"策略：按输入路径与数据内容自动识别矢量/栅格/容器数据。\n"
               L"适用：来源不确定或批处理场景。";
      case 1:
        return L"矢量（Shapefile/GeoJSON）\n"
               L"文件格式：.shp/.dbf/.shx 或 .geojson\n"
               L"细节：保留要素与属性；注意编码、坐标系与字段长度。";
      case 2:
        return L"栅格（GeoTIFF）\n"
               L"文件格式：.tif/.tiff\n"
               L"细节：支持分辨率、NoData、压缩与金字塔。";
      default:
        return L"空间数据库（GPKG）\n"
               L"文件格式：.gpkg（SQLite 容器）\n"
               L"细节：可存多图层、索引与元数据，便于工程归档。";
    }
  }
  if (major == 1) {
    switch (sub) {
      case 0:
        return L"TIN（三角网）\n"
               L"常见格式：.tin/.obj/.ply（实现可扩展）\n"
               L"细节：保地形突变更好，适合地表重建。";
      case 1:
        return L"DEM（高程栅格）\n"
               L"常见格式：GeoTIFF/ASCII Grid\n"
               L"细节：规则网格，适合坡度/流域等分析。";
      default:
        return L"3DMesh（网格模型）\n"
               L"常见格式：.obj/.fbx/.gltf（实现可扩展）\n"
               L"细节：偏渲染表达，关注 LOD、贴图与压缩。";
    }
  }
  switch (sub) {
    case 0:
      return L"XYZ（金字塔瓦片）\n"
             L"组织：/{z}/{x}/{y}\n"
             L"细节：与绝大多数 Web 地图兼容。";
    case 1:
      return L"TMS（倒序行号）\n"
             L"组织：/{z}/{x}/{y}，但 Y 轴方向与 XYZ 相反\n"
             L"细节：与部分历史服务兼容时常用。";
    default:
      return L"WMTS（服务化瓦片）\n"
             L"协议：OGC WMTS（KVP/REST）\n"
             L"细节：可声明 TileMatrixSet、样式和能力文档。";
  }
}

const wchar_t* GetConvertTooltipText(HWND dlg, UINT_PTR ctrlId) {
  const int inMajor = static_cast<int>(SendMessageW(GetDlgItem(dlg, IDC_CONV_INPUT_TYPE), CB_GETCURSEL, 0, 0));
  const int outMajor = static_cast<int>(SendMessageW(GetDlgItem(dlg, IDC_CONV_OUTPUT_TYPE), CB_GETCURSEL, 0, 0));
  const int inSub = static_cast<int>(SendMessageW(GetDlgItem(dlg, IDC_CONV_INPUT_SUBTYPE), CB_GETCURSEL, 0, 0));
  const int outSub = static_cast<int>(SendMessageW(GetDlgItem(dlg, IDC_CONV_OUTPUT_SUBTYPE), CB_GETCURSEL, 0, 0));
  switch (ctrlId) {
    case IDC_CONV_INPUT_TYPE:
      return ConvertTypeTooltipByMajor(inMajor < 0 ? 0 : inMajor);
    case IDC_CONV_OUTPUT_TYPE:
      return ConvertTypeTooltipByMajor(outMajor < 0 ? 0 : outMajor);
    case IDC_CONV_INPUT_SUBTYPE:
      return ConvertSubtypeTooltipByMajorSubtype(inMajor < 0 ? 0 : inMajor, inSub < 0 ? 0 : inSub);
    case IDC_CONV_OUTPUT_SUBTYPE:
      return ConvertSubtypeTooltipByMajorSubtype(outMajor < 0 ? 0 : outMajor, outSub < 0 ? 0 : outSub);
    default:
      return L"";
  }
}

void AttachConvertTooltip(HWND dlg, HWND tip, int ctrlId) {
  HWND h = GetDlgItem(dlg, ctrlId);
  if (!h || !tip) {
    return;
  }
  TOOLINFOW ti{};
  ti.cbSize = sizeof(ti);
  ti.uFlags = TTF_IDISHWND | TTF_SUBCLASS;
  ti.hwnd = dlg;
  ti.uId = reinterpret_cast<UINT_PTR>(h);
  ti.lpszText = LPSTR_TEXTCALLBACKW;
  SendMessageW(tip, TTM_ADDTOOLW, 0, reinterpret_cast<LPARAM>(&ti));
}

void ShowConvertHelpDialog(HWND hwnd, bool inputSide, bool typeHelp) {
  const int major = static_cast<int>(SendMessageW(GetDlgItem(hwnd, inputSide ? IDC_CONV_INPUT_TYPE : IDC_CONV_OUTPUT_TYPE),
                                                  CB_GETCURSEL, 0, 0));
  const int sub = static_cast<int>(
      SendMessageW(GetDlgItem(hwnd, inputSide ? IDC_CONV_INPUT_SUBTYPE : IDC_CONV_OUTPUT_SUBTYPE), CB_GETCURSEL, 0, 0));
  const wchar_t* body = typeHelp ? ConvertTypeTooltipByMajor(major < 0 ? 0 : major)
                                 : ConvertSubtypeTooltipByMajorSubtype(major < 0 ? 0 : major, sub < 0 ? 0 : sub);
  const wchar_t* title = nullptr;
  if (inputSide) {
    title = typeHelp ? L"输入类型说明" : L"输入子类型说明";
  } else {
    title = typeHelp ? L"输出类型说明" : L"输出子类型说明";
  }
  MessageBoxW(hwnd, body, title, MB_OK | MB_ICONINFORMATION);
}

void SyncConvertInfoByType(HWND hwnd, bool inputSide) {
  const int typeId = inputSide ? IDC_CONV_INPUT_TYPE : IDC_CONV_OUTPUT_TYPE;
  const int subtypeId = inputSide ? IDC_CONV_INPUT_SUBTYPE : IDC_CONV_OUTPUT_SUBTYPE;
  const int infoId = inputSide ? IDC_CONV_INPUT_INFO : IDC_CONV_OUTPUT_INFO;
  HWND hType = GetDlgItem(hwnd, typeId);
  HWND hSubtype = GetDlgItem(hwnd, subtypeId);
  HWND hInfo = GetDlgItem(hwnd, infoId);
  if (!hType || !hSubtype || !hInfo) {
    return;
  }
  const int sel = static_cast<int>(SendMessageW(hType, CB_GETCURSEL, 0, 0));
  const int t = sel < 0 ? 0 : sel;
  FillConvertSubtypeCombo(hSubtype, t);
  const wchar_t* preset = L"";
  if (t == 0) {
    preset = inputSide ? L"输入路径：\r\n编码：UTF-8\r\n几何类型：自动检测\r\nCRS：自动识别"
                       : L"输出路径：\r\n目标CRS：WebMercator\r\n输出格式：GPKG\r\n精度：中";
  } else if (t == 1) {
    preset = inputSide ? L"输入路径：\r\n高程基准：\r\n网格密度：\r\n缺失值策略：插值"
                       : L"输出路径：\r\n模型格式：3DMesh\r\nLOD：2\r\n压缩：开启";
  } else {
    preset = inputSide ? L"输入源：\r\n级别范围：0-14\r\n切片规则：XYZ/TMS\r\n并发读取：4"
                       : L"输出目录：\r\n切片方案：XYZ\r\n压缩：PNG/JPEG\r\n并发写入：4";
  }
  SetWindowTextW(hInfo, preset);
}

std::wstring GetComboSelectedText(HWND combo) {
  if (!combo) {
    return L"";
  }
  const int sel = static_cast<int>(SendMessageW(combo, CB_GETCURSEL, 0, 0));
  if (sel < 0) {
    return L"";
  }
  wchar_t buf[256]{};
  SendMessageW(combo, CB_GETLBTEXT, static_cast<WPARAM>(sel), reinterpret_cast<LPARAM>(buf));
  return buf;
}

std::wstring PromptOpenInputPath(HWND owner) {
  wchar_t path[MAX_PATH]{};
  OPENFILENAMEW ofn{};
  ofn.lStructSize = sizeof(ofn);
  ofn.hwndOwner = owner;
  ofn.lpstrFilter = L"所有文件 (*.*)\0*.*\0";
  ofn.lpstrFile = path;
  ofn.nMaxFile = MAX_PATH;
  ofn.Flags = OFN_FILEMUSTEXIST | OFN_PATHMUSTEXIST;
  return GetOpenFileNameW(&ofn) ? std::wstring(path) : L"";
}

std::wstring PromptSaveOutputPath(HWND owner) {
  wchar_t path[MAX_PATH]{};
  OPENFILENAMEW ofn{};
  ofn.lStructSize = sizeof(ofn);
  ofn.hwndOwner = owner;
  ofn.lpstrFilter = L"所有文件 (*.*)\0*.*\0";
  ofn.lpstrFile = path;
  ofn.nMaxFile = MAX_PATH;
  ofn.Flags = OFN_OVERWRITEPROMPT | OFN_PATHMUSTEXIST;
  return GetSaveFileNameW(&ofn) ? std::wstring(path) : L"";
}

std::wstring PromptSelectOutputFolder(HWND owner) {
  BROWSEINFOW bi{};
  bi.hwndOwner = owner;
  bi.lpszTitle = L"选择输出目录";
  bi.ulFlags = BIF_RETURNONLYFSDIRS | BIF_NEWDIALOGSTYLE;
  PIDLIST_ABSOLUTE pidl = SHBrowseForFolderW(&bi);
  if (!pidl) {
    return L"";
  }
  wchar_t folder[MAX_PATH]{};
  const BOOL ok = SHGetPathFromIDListW(pidl, folder);
  CoTaskMemFree(pidl);
  return ok ? std::wstring(folder) : L"";
}

const wchar_t* ConvertToolExeName(int inMajor, int outMajor) {
  if (inMajor == 0 && outMajor == 1) return L"agis_convert_gis_to_model.exe";
  if (inMajor == 0 && outMajor == 2) return L"agis_convert_gis_to_tile.exe";
  if (inMajor == 1 && outMajor == 0) return L"agis_convert_model_to_gis.exe";
  if (inMajor == 1 && outMajor == 2) return L"agis_convert_model_to_tile.exe";
  if (inMajor == 2 && outMajor == 0) return L"agis_convert_tile_to_gis.exe";
  if (inMajor == 2 && outMajor == 1) return L"agis_convert_tile_to_model.exe";
  return nullptr;
}

std::wstring QuoteArg(const std::wstring& s) {
  std::wstring out = L"\"";
  for (wchar_t ch : s) {
    if (ch == L'"') {
      out += L"\\\"";
    } else {
      out.push_back(ch);
    }
  }
  out += L"\"";
  return out;
}

std::wstring BuildConvertCommandLine(HWND hwnd) {
  const int inMajor = static_cast<int>(SendMessageW(GetDlgItem(hwnd, IDC_CONV_INPUT_TYPE), CB_GETCURSEL, 0, 0));
  const int outMajor = static_cast<int>(SendMessageW(GetDlgItem(hwnd, IDC_CONV_OUTPUT_TYPE), CB_GETCURSEL, 0, 0));
  const wchar_t* exeName = ConvertToolExeName(inMajor, outMajor);
  wchar_t modulePath[MAX_PATH]{};
  GetModuleFileNameW(nullptr, modulePath, MAX_PATH);
  std::wstring exeDir = modulePath;
  const size_t slash = exeDir.find_last_of(L"\\/");
  if (slash != std::wstring::npos) {
    exeDir.resize(slash + 1);
  }
  const std::wstring exePath = exeName ? (exeDir + exeName) : L"<请选择不同输入/输出类型>";
  wchar_t inPath[1024]{};
  wchar_t outPath[1024]{};
  GetWindowTextW(GetDlgItem(hwnd, IDC_CONV_INPUT_PATH), inPath, 1024);
  GetWindowTextW(GetDlgItem(hwnd, IDC_CONV_OUTPUT_PATH), outPath, 1024);
  const std::wstring inType = GetComboSelectedText(GetDlgItem(hwnd, IDC_CONV_INPUT_TYPE));
  const std::wstring inSub = GetComboSelectedText(GetDlgItem(hwnd, IDC_CONV_INPUT_SUBTYPE));
  const std::wstring outType = GetComboSelectedText(GetDlgItem(hwnd, IDC_CONV_OUTPUT_TYPE));
  const std::wstring outSub = GetComboSelectedText(GetDlgItem(hwnd, IDC_CONV_OUTPUT_SUBTYPE));
  return L"命令行:\r\n" + QuoteArg(exePath) + L"\r\n"
         L"  --input " + QuoteArg(inPath) + L"\r\n"
         L"  --output " + QuoteArg(outPath) + L"\r\n"
         L"  --input-type " + QuoteArg(inType) + L"\r\n"
         L"  --input-subtype " + QuoteArg(inSub) + L"\r\n"
         L"  --output-type " + QuoteArg(outType) + L"\r\n"
         L"  --output-subtype " + QuoteArg(outSub);
}

void UpdateConvertCmdlinePreview(HWND hwnd) {
  SetWindowTextW(GetDlgItem(hwnd, IDC_CONV_CMDLINE), BuildConvertCommandLine(hwnd).c_str());
}

struct ObjPreviewStats {
  uint64_t vertices = 0;
  uint64_t texcoords = 0;
  uint64_t normals = 0;
  uint64_t faces = 0;
  uint64_t materials = 0;
  uint64_t textures = 0;
  uint64_t fileBytes = 0;
};

struct PreviewVec3 {
  float x = 0.0f;
  float y = 0.0f;
  float z = 0.0f;
};

struct ObjPreviewModel {
  struct MaterialInfo {
    std::wstring name;
    float kdR = 0.30f;
    float kdG = 0.62f;
    float kdB = 0.92f;
    std::wstring mapKdPath;
  };
  std::vector<PreviewVec3> vertices;
  std::vector<std::array<int, 3>> faces;
  std::vector<int> faceMaterial;  // 与 faces 同步，记录材质索引
  std::vector<MaterialInfo> materials;
  PreviewVec3 center{};
  float extent = 1.0f;
  float kdR = 0.30f;
  float kdG = 0.62f;
  float kdB = 0.92f;
  std::wstring primaryMapKdPath;
};

enum class PreviewRenderBackend { kOpenGL, kDx11 };

struct ModelPreviewState {
  ObjPreviewModel model;
  std::wstring path;
  float rotX = 0.5f;
  float rotY = -0.8f;
  float zoom = 1.0f;
  bool dragging = false;
  POINT lastPt{};
  bool solid = true;
  PreviewRenderBackend backend = PreviewRenderBackend::kOpenGL;
  HDC glHdc = nullptr;
  HGLRC glRc = nullptr;
  ID3D11Device* d3dDev = nullptr;
  ID3D11DeviceContext* d3dCtx = nullptr;
  IDXGISwapChain* d3dSwap = nullptr;
  ID3D11RenderTargetView* d3dRtv = nullptr;
  ID3D11VertexShader* d3dVs = nullptr;
  ID3D11PixelShader* d3dPs = nullptr;
  ID3D11InputLayout* d3dLayout = nullptr;
  ID3D11Buffer* d3dVb = nullptr;
  ID3D11RasterizerState* d3dRsSolid = nullptr;
  ID3D11RasterizerState* d3dRsWire = nullptr;
  ID3D11DepthStencilState* d3dDsState = nullptr;
  ID3D11Texture2D* d3dDsTex = nullptr;
  ID3D11DepthStencilView* d3dDsv = nullptr;
  UINT d3dVbCap = 0;
  DWORD lastFpsTick = 0;
  int frameCounter = 0;
  float fps = 0.0f;
};

template <typename T>
void SafeRelease(T*& p) {
  if (p) {
    p->Release();
    p = nullptr;
  }
}

bool InitPreviewGl(HWND hwnd, ModelPreviewState* st) {
  if (!st || st->glRc) {
    return st && st->glRc;
  }
  st->glHdc = GetDC(hwnd);
  if (!st->glHdc) {
    return false;
  }
  PIXELFORMATDESCRIPTOR pfd{};
  pfd.nSize = sizeof(pfd);
  pfd.nVersion = 1;
  pfd.dwFlags = PFD_DRAW_TO_WINDOW | PFD_SUPPORT_OPENGL | PFD_DOUBLEBUFFER;
  pfd.iPixelType = PFD_TYPE_RGBA;
  pfd.cColorBits = 32;
  pfd.cDepthBits = 24;
  const int pf = ChoosePixelFormat(st->glHdc, &pfd);
  if (!pf || !SetPixelFormat(st->glHdc, pf, &pfd)) {
    ReleaseDC(hwnd, st->glHdc);
    st->glHdc = nullptr;
    return false;
  }
  st->glRc = wglCreateContext(st->glHdc);
  if (!st->glRc) {
    ReleaseDC(hwnd, st->glHdc);
    st->glHdc = nullptr;
    return false;
  }
  return true;
}

void ReleasePreviewGl(HWND hwnd, ModelPreviewState* st) {
  if (!st) {
    return;
  }
  if (st->glRc) {
    wglMakeCurrent(nullptr, nullptr);
    wglDeleteContext(st->glRc);
    st->glRc = nullptr;
  }
  if (st->glHdc) {
    ReleaseDC(hwnd, st->glHdc);
    st->glHdc = nullptr;
  }
}

bool RecreatePreviewDxRtv(ModelPreviewState* st) {
  if (!st || !st->d3dSwap || !st->d3dDev) {
    return false;
  }
  SafeRelease(st->d3dRtv);
  ID3D11Texture2D* bb = nullptr;
  HRESULT hr = st->d3dSwap->GetBuffer(0, IID_PPV_ARGS(&bb));
  if (FAILED(hr) || !bb) {
    return false;
  }
  hr = st->d3dDev->CreateRenderTargetView(bb, nullptr, &st->d3dRtv);
  bb->Release();
  return SUCCEEDED(hr) && st->d3dRtv;
}

void ReleasePreviewDx(ModelPreviewState* st) {
  if (!st) return;
  SafeRelease(st->d3dDsv);
  SafeRelease(st->d3dDsTex);
  SafeRelease(st->d3dDsState);
  SafeRelease(st->d3dRsWire);
  SafeRelease(st->d3dRsSolid);
  SafeRelease(st->d3dVb);
  st->d3dVbCap = 0;
  SafeRelease(st->d3dLayout);
  SafeRelease(st->d3dVs);
  SafeRelease(st->d3dPs);
  SafeRelease(st->d3dRtv);
  SafeRelease(st->d3dSwap);
  SafeRelease(st->d3dCtx);
  SafeRelease(st->d3dDev);
}

bool InitPreviewDx(HWND hwnd, ModelPreviewState* st) {
  if (!st) return false;
  if (st->d3dDev && st->d3dCtx && st->d3dSwap && st->d3dRtv) return true;
  ReleasePreviewDx(st);
  RECT rc{};
  GetClientRect(hwnd, &rc);
  DXGI_SWAP_CHAIN_DESC sd{};
  sd.BufferCount = 1;
  sd.BufferDesc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
  sd.BufferDesc.Width = static_cast<UINT>((std::max)(1L, rc.right - rc.left));
  sd.BufferDesc.Height = static_cast<UINT>((std::max)(1L, rc.bottom - rc.top));
  sd.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
  sd.OutputWindow = hwnd;
  sd.SampleDesc.Count = 1;
  sd.Windowed = TRUE;
  sd.SwapEffect = DXGI_SWAP_EFFECT_DISCARD;
  D3D_FEATURE_LEVEL fls[] = {D3D_FEATURE_LEVEL_11_0, D3D_FEATURE_LEVEL_10_0};
  D3D_FEATURE_LEVEL outFl{};
  HRESULT hr = D3D11CreateDeviceAndSwapChain(nullptr, D3D_DRIVER_TYPE_HARDWARE, nullptr, 0, fls, ARRAYSIZE(fls),
                                             D3D11_SDK_VERSION, &sd, &st->d3dSwap, &st->d3dDev, &outFl, &st->d3dCtx);
  if (FAILED(hr)) return false;
  if (!RecreatePreviewDxRtv(st)) return false;
  D3D11_TEXTURE2D_DESC dtd{};
  dtd.Width = sd.BufferDesc.Width;
  dtd.Height = sd.BufferDesc.Height;
  dtd.MipLevels = 1;
  dtd.ArraySize = 1;
  dtd.Format = DXGI_FORMAT_D24_UNORM_S8_UINT;
  dtd.SampleDesc.Count = 1;
  dtd.BindFlags = D3D11_BIND_DEPTH_STENCIL;
  if (FAILED(st->d3dDev->CreateTexture2D(&dtd, nullptr, &st->d3dDsTex))) return false;
  if (FAILED(st->d3dDev->CreateDepthStencilView(st->d3dDsTex, nullptr, &st->d3dDsv))) return false;
  D3D11_DEPTH_STENCIL_DESC dsd{};
  dsd.DepthEnable = TRUE;
  dsd.DepthWriteMask = D3D11_DEPTH_WRITE_MASK_ALL;
  dsd.DepthFunc = D3D11_COMPARISON_LESS_EQUAL;
  if (FAILED(st->d3dDev->CreateDepthStencilState(&dsd, &st->d3dDsState))) return false;
  D3D11_RASTERIZER_DESC rsSolid{};
  rsSolid.FillMode = D3D11_FILL_SOLID;
  rsSolid.CullMode = D3D11_CULL_BACK;
  rsSolid.FrontCounterClockwise = FALSE;
  rsSolid.DepthClipEnable = TRUE;
  if (FAILED(st->d3dDev->CreateRasterizerState(&rsSolid, &st->d3dRsSolid))) return false;
  D3D11_RASTERIZER_DESC rsWire = rsSolid;
  rsWire.FillMode = D3D11_FILL_WIREFRAME;
  rsWire.CullMode = D3D11_CULL_NONE;
  if (FAILED(st->d3dDev->CreateRasterizerState(&rsWire, &st->d3dRsWire))) return false;
  static const char* kVs = R"(
struct VS_IN { float2 pos : POSITION; float4 col : COLOR; };
struct VS_OUT { float4 pos : SV_POSITION; float4 col : COLOR; };
VS_OUT main(VS_IN i){ VS_OUT o; o.pos=float4(i.pos,0,1); o.col=i.col; return o; })";
  static const char* kPs = R"(
float4 main(float4 pos:SV_POSITION, float4 col:COLOR) : SV_Target { return col; })";
  ID3DBlob* vsBlob = nullptr;
  ID3DBlob* psBlob = nullptr;
  ID3DBlob* errBlob = nullptr;
  hr = D3DCompile(kVs, strlen(kVs), nullptr, nullptr, nullptr, "main", "vs_4_0", 0, 0, &vsBlob, &errBlob);
  if (FAILED(hr) || !vsBlob) {
    SafeRelease(errBlob);
    return false;
  }
  hr = D3DCompile(kPs, strlen(kPs), nullptr, nullptr, nullptr, "main", "ps_4_0", 0, 0, &psBlob, &errBlob);
  if (FAILED(hr) || !psBlob) {
    SafeRelease(vsBlob);
    SafeRelease(errBlob);
    return false;
  }
  D3D11_INPUT_ELEMENT_DESC ied[] = {
      {"POSITION", 0, DXGI_FORMAT_R32G32_FLOAT, 0, 0, D3D11_INPUT_PER_VERTEX_DATA, 0},
      {"COLOR", 0, DXGI_FORMAT_R32G32B32A32_FLOAT, 0, 8, D3D11_INPUT_PER_VERTEX_DATA, 0},
  };
  hr = st->d3dDev->CreateInputLayout(ied, 2, vsBlob->GetBufferPointer(), vsBlob->GetBufferSize(), &st->d3dLayout);
  if (FAILED(hr)) {
    SafeRelease(vsBlob);
    SafeRelease(psBlob);
    return false;
  }
  hr = st->d3dDev->CreateVertexShader(vsBlob->GetBufferPointer(), vsBlob->GetBufferSize(), nullptr, &st->d3dVs);
  if (FAILED(hr)) {
    SafeRelease(vsBlob);
    SafeRelease(psBlob);
    return false;
  }
  hr = st->d3dDev->CreatePixelShader(psBlob->GetBufferPointer(), psBlob->GetBufferSize(), nullptr, &st->d3dPs);
  SafeRelease(vsBlob);
  SafeRelease(psBlob);
  return SUCCEEDED(hr);
}

std::wstring TrimLeft(const std::wstring& s) {
  size_t i = 0;
  while (i < s.size() && iswspace(s[i])) ++i;
  return s.substr(i);
}

bool ParseMtlKdColor(const std::filesystem::path& mtlPath, float* r, float* g, float* b) {
  if (!r || !g || !b) return false;
  std::wifstream ifs(mtlPath);
  if (!ifs.is_open()) return false;
  std::wstring line;
  while (std::getline(ifs, line)) {
    line = TrimLeft(line);
    if (line.rfind(L"Kd ", 0) == 0) {
      std::wistringstream ss(line.substr(3));
      float rr = 0.0f, gg = 0.0f, bb = 0.0f;
      if (ss >> rr >> gg >> bb) {
        *r = std::clamp(rr, 0.0f, 1.0f);
        *g = std::clamp(gg, 0.0f, 1.0f);
        *b = std::clamp(bb, 0.0f, 1.0f);
        return true;
      }
    }
  }
  return false;
}

bool ParseMtlMaterials(const std::filesystem::path& mtlPath, std::vector<ObjPreviewModel::MaterialInfo>* out) {
  if (!out) return false;
  out->clear();
  std::wifstream ifs(mtlPath);
  if (!ifs.is_open()) return false;
  std::wstring line;
  int cur = -1;
  while (std::getline(ifs, line)) {
    line = TrimLeft(line);
    if (line.rfind(L"newmtl ", 0) == 0) {
      ObjPreviewModel::MaterialInfo m{};
      m.name = line.substr(7);
      out->push_back(m);
      cur = static_cast<int>(out->size()) - 1;
    } else if (cur >= 0 && line.rfind(L"Kd ", 0) == 0) {
      std::wistringstream ss(line.substr(3));
      float r = 0, g = 0, b = 0;
      if (ss >> r >> g >> b) {
        (*out)[cur].kdR = std::clamp(r, 0.0f, 1.0f);
        (*out)[cur].kdG = std::clamp(g, 0.0f, 1.0f);
        (*out)[cur].kdB = std::clamp(b, 0.0f, 1.0f);
      }
    } else if (cur >= 0 && line.rfind(L"map_Kd ", 0) == 0) {
      std::filesystem::path p = mtlPath.parent_path() / line.substr(7);
      (*out)[cur].mapKdPath = p.wstring();
    }
  }
  return !out->empty();
}

std::wstring ToHumanBytes(uint64_t bytes) {
  const wchar_t* units[] = {L"B", L"KB", L"MB", L"GB"};
  double v = static_cast<double>(bytes);
  int idx = 0;
  while (v >= 1024.0 && idx < 3) {
    v /= 1024.0;
    ++idx;
  }
  wchar_t buf[64]{};
  swprintf_s(buf, L"%.2f %s", v, units[idx]);
  return buf;
}

ObjPreviewStats ScanObjStats(const std::wstring& path) {
  ObjPreviewStats s{};
  std::error_code ec;
  s.fileBytes = std::filesystem::file_size(path, ec);
  std::wifstream ifs(path);
  if (!ifs.is_open()) {
    return s;
  }
  std::set<std::wstring> mats;
  std::set<std::wstring> texs;
  std::wstring line;
  while (std::getline(ifs, line)) {
    line = TrimLeft(line);
    if (line.rfind(L"v ", 0) == 0) ++s.vertices;
    else if (line.rfind(L"vt ", 0) == 0) ++s.texcoords;
    else if (line.rfind(L"vn ", 0) == 0) ++s.normals;
    else if (line.rfind(L"f ", 0) == 0) ++s.faces;
    else if (line.rfind(L"usemtl ", 0) == 0) mats.insert(line.substr(7));
    else if (line.rfind(L"map_Kd ", 0) == 0) texs.insert(line.substr(7));
  }
  s.materials = mats.size();
  s.textures = texs.size();
  return s;
}

bool ParseObjModel(const std::wstring& path, ObjPreviewModel* out) {
  if (!out) {
    return false;
  }
  out->vertices.clear();
  out->faces.clear();
  out->faceMaterial.clear();
  out->materials.clear();
  out->center = {};
  out->extent = 1.0f;
  out->kdR = 0.30f;
  out->kdG = 0.62f;
  out->kdB = 0.92f;
  out->primaryMapKdPath.clear();
  std::wifstream ifs(path);
  if (!ifs.is_open()) {
    return false;
  }
  std::wstring line;
  std::filesystem::path objPath(path);
  int curMtlIndex = -1;
  PreviewVec3 vmin{1e9f, 1e9f, 1e9f};
  PreviewVec3 vmax{-1e9f, -1e9f, -1e9f};
  while (std::getline(ifs, line)) {
    line = TrimLeft(line);
    if (line.rfind(L"v ", 0) == 0) {
      std::wistringstream ss(line.substr(2));
      PreviewVec3 v{};
      ss >> v.x >> v.y >> v.z;
      out->vertices.push_back(v);
      vmin.x = (std::min)(vmin.x, v.x);
      vmin.y = (std::min)(vmin.y, v.y);
      vmin.z = (std::min)(vmin.z, v.z);
      vmax.x = (std::max)(vmax.x, v.x);
      vmax.y = (std::max)(vmax.y, v.y);
      vmax.z = (std::max)(vmax.z, v.z);
    } else if (line.rfind(L"mtllib ", 0) == 0) {
      std::filesystem::path mtl = objPath.parent_path() / line.substr(7);
      ParseMtlKdColor(mtl, &out->kdR, &out->kdG, &out->kdB);
      ParseMtlMaterials(mtl, &out->materials);
      if (!out->materials.empty() && !out->materials[0].mapKdPath.empty()) {
        out->primaryMapKdPath = out->materials[0].mapKdPath;
      }
    } else if (line.rfind(L"usemtl ", 0) == 0) {
      const std::wstring name = line.substr(7);
      curMtlIndex = -1;
      for (size_t i = 0; i < out->materials.size(); ++i) {
        if (out->materials[i].name == name) {
          curMtlIndex = static_cast<int>(i);
          if (!out->materials[i].mapKdPath.empty()) {
            out->primaryMapKdPath = out->materials[i].mapKdPath;
          }
          break;
        }
      }
    } else if (line.rfind(L"f ", 0) == 0) {
      std::wistringstream ss(line.substr(2));
      std::vector<int> idx;
      std::wstring tok;
      while (ss >> tok) {
        const size_t slash = tok.find(L'/');
        const std::wstring n = slash == std::wstring::npos ? tok : tok.substr(0, slash);
        if (n.empty()) {
          continue;
        }
        int vi = _wtoi(n.c_str());
        if (vi < 0) {
          vi = static_cast<int>(out->vertices.size()) + vi + 1;
        }
        if (vi > 0) {
          idx.push_back(vi - 1);
        }
      }
      if (idx.size() >= 3) {
        for (size_t i = 1; i + 1 < idx.size(); ++i) {
          out->faces.push_back({idx[0], idx[i], idx[i + 1]});
          out->faceMaterial.push_back(curMtlIndex);
        }
      }
    }
  }
  if (!out->vertices.empty()) {
    out->center = {(vmin.x + vmax.x) * 0.5f, (vmin.y + vmax.y) * 0.5f, (vmin.z + vmax.z) * 0.5f};
    const float ex = (std::max)(1e-6f, vmax.x - vmin.x);
    const float ey = (std::max)(1e-6f, vmax.y - vmin.y);
    const float ez = (std::max)(1e-6f, vmax.z - vmin.z);
    out->extent = (std::max)(ex, (std::max)(ey, ez));
  }
  return !out->vertices.empty() && !out->faces.empty();
}

POINT ProjectPoint(const PreviewVec3& v, float rotX, float rotY, float zoom, const RECT& rc) {
  const float cx = std::cos(rotX), sx = std::sin(rotX);
  const float cy = std::cos(rotY), sy = std::sin(rotY);
  float x1 = v.x;
  float y1 = v.y * cx - v.z * sx;
  float z1 = v.y * sx + v.z * cx;
  float x2 = x1 * cy + z1 * sy;
  float z2 = -x1 * sy + z1 * cy;
  const float halfW = static_cast<float>((std::max)(1, static_cast<int>((rc.right - rc.left) / 2)));
  const float halfH = static_cast<float>((std::max)(1, static_cast<int>((rc.bottom - rc.top) / 2)));
  const float persp = 1.8f / (z2 + 3.5f);
  POINT p{};
  p.x = rc.left + static_cast<int>(halfW + x2 * halfW * zoom * persp);
  p.y = rc.top + static_cast<int>(halfH - y1 * halfH * zoom * persp);
  return p;
}

void DrawModelPreview(HDC hdc, const RECT& rc, const ModelPreviewState& st) {
  // OpenGL 风格：偏清亮底色 + 蓝色实体；DX11 风格：偏深底色 + 青蓝线框。
  const bool glStyle = st.backend == PreviewRenderBackend::kOpenGL;
  const COLORREF bgColor = glStyle ? RGB(245, 248, 252) : RGB(31, 35, 42);
  const COLORREF edgeColor = glStyle ? RGB(36, 82, 156) : RGB(112, 196, 255);
  const COLORREF fillColor =
      RGB(static_cast<int>(st.model.kdR * 255.0f), static_cast<int>(st.model.kdG * 255.0f), static_cast<int>(st.model.kdB * 255.0f));
  HBRUSH bg = CreateSolidBrush(RGB(245, 248, 252));
  DeleteObject(bg);
  bg = CreateSolidBrush(bgColor);
  FillRect(hdc, &rc, bg);
  DeleteObject(bg);
  HPEN pen = CreatePen(PS_SOLID, glStyle ? 1 : 2, edgeColor);
  HPEN oldPen = reinterpret_cast<HPEN>(SelectObject(hdc, pen));
  HBRUSH solidBrush = CreateSolidBrush(fillColor);
  const bool doFill = st.solid && glStyle;
  HBRUSH oldBrush = reinterpret_cast<HBRUSH>(SelectObject(hdc, doFill ? solidBrush : GetStockObject(NULL_BRUSH)));
  SetBkMode(hdc, TRANSPARENT);
  if (!glStyle) {
    SetPolyFillMode(hdc, WINDING);
  }
  for (const auto& f : st.model.faces) {
    if (f[0] < 0 || f[1] < 0 || f[2] < 0 || f[0] >= static_cast<int>(st.model.vertices.size()) ||
        f[1] >= static_cast<int>(st.model.vertices.size()) || f[2] >= static_cast<int>(st.model.vertices.size())) {
      continue;
    }
    POINT pts[3] = {ProjectPoint({st.model.vertices[f[0]].x - st.model.center.x, st.model.vertices[f[0]].y - st.model.center.y,
                                  st.model.vertices[f[0]].z - st.model.center.z},
                                 st.rotX, st.rotY, st.zoom, rc),
                    ProjectPoint({st.model.vertices[f[1]].x - st.model.center.x, st.model.vertices[f[1]].y - st.model.center.y,
                                  st.model.vertices[f[1]].z - st.model.center.z},
                                 st.rotX, st.rotY, st.zoom, rc),
                    ProjectPoint({st.model.vertices[f[2]].x - st.model.center.x, st.model.vertices[f[2]].y - st.model.center.y,
                                  st.model.vertices[f[2]].z - st.model.center.z},
                                 st.rotX, st.rotY, st.zoom, rc)};
    if (glStyle) {
      Polygon(hdc, pts, 3);
    } else {
      MoveToEx(hdc, pts[0].x, pts[0].y, nullptr);
      LineTo(hdc, pts[1].x, pts[1].y);
      LineTo(hdc, pts[2].x, pts[2].y);
      LineTo(hdc, pts[0].x, pts[0].y);
    }
  }
  SelectObject(hdc, oldBrush);
  SelectObject(hdc, oldPen);
  DeleteObject(solidBrush);
  DeleteObject(pen);
}

void DrawModelPreviewOpenGL(HWND hwnd, const RECT& rc, const ModelPreviewState& st) {
  if (!st.glHdc || !st.glRc) {
    return;
  }
  if (!wglMakeCurrent(st.glHdc, st.glRc)) {
    return;
  }
  RECT cr{};
  GetClientRect(hwnd, &cr);
  int vpW = static_cast<int>(cr.right - cr.left);
  int vpH = static_cast<int>(cr.bottom - cr.top);
  if (vpW < 1) vpW = 1;
  if (vpH < 1) vpH = 1;
  glViewport(0, 0, vpW, vpH);
  glClearColor(0.10f, 0.12f, 0.16f, 1.0f);
  glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
  glEnable(GL_DEPTH_TEST);
  glMatrixMode(GL_PROJECTION);
  glLoadIdentity();
  int rw = static_cast<int>(rc.right - rc.left);
  int rh = static_cast<int>(rc.bottom - rc.top);
  if (rw < 1) rw = 1;
  if (rh < 1) rh = 1;
  const double w = static_cast<double>(rw);
  const double h = static_cast<double>(rh);
  const double ar = w / h;
  glFrustum(-ar * 0.3, ar * 0.3, -0.3, 0.3, 0.7, 100.0);
  glMatrixMode(GL_MODELVIEW);
  glLoadIdentity();
  glTranslated(0.0, 0.0, -3.5);
  glRotated(st.rotX * 57.29578, 1.0, 0.0, 0.0);
  glRotated(st.rotY * 57.29578, 0.0, 1.0, 0.0);
  glScaled(st.zoom, st.zoom, st.zoom);
  glTranslated(-st.model.center.x, -st.model.center.y, -st.model.center.z);

  if (st.solid) {
    glPolygonMode(GL_FRONT_AND_BACK, GL_FILL);
    glBegin(GL_TRIANGLES);
    for (size_t fi = 0; fi < st.model.faces.size(); ++fi) {
      const auto& f = st.model.faces[fi];
      if (f[0] < 0 || f[1] < 0 || f[2] < 0 || f[0] >= static_cast<int>(st.model.vertices.size()) ||
          f[1] >= static_cast<int>(st.model.vertices.size()) || f[2] >= static_cast<int>(st.model.vertices.size())) {
        continue;
      }
      float cr = st.model.kdR, cg = st.model.kdG, cb = st.model.kdB;
      if (fi < st.model.faceMaterial.size()) {
        const int mi = st.model.faceMaterial[fi];
        if (mi >= 0 && mi < static_cast<int>(st.model.materials.size())) {
          cr = st.model.materials[mi].kdR;
          cg = st.model.materials[mi].kdG;
          cb = st.model.materials[mi].kdB;
        }
      }
      glColor3f(cr, cg, cb);
      const auto& va = st.model.vertices[f[0]];
      const auto& vb = st.model.vertices[f[1]];
      const auto& vc = st.model.vertices[f[2]];
      glVertex3f(va.x, va.y, va.z);
      glVertex3f(vb.x, vb.y, vb.z);
      glVertex3f(vc.x, vc.y, vc.z);
    }
    glEnd();
  }
  glPolygonMode(GL_FRONT_AND_BACK, GL_LINE);
  glColor3f((std::min)(1.0f, st.model.kdR + 0.20f), (std::min)(1.0f, st.model.kdG + 0.20f),
            (std::min)(1.0f, st.model.kdB + 0.20f));
  glBegin(GL_TRIANGLES);
  for (const auto& f : st.model.faces) {
    if (f[0] < 0 || f[1] < 0 || f[2] < 0 || f[0] >= static_cast<int>(st.model.vertices.size()) ||
        f[1] >= static_cast<int>(st.model.vertices.size()) || f[2] >= static_cast<int>(st.model.vertices.size())) {
      continue;
    }
    const auto& a = st.model.vertices[f[0]];
    const auto& b = st.model.vertices[f[1]];
    const auto& c = st.model.vertices[f[2]];
    glVertex3f(a.x, a.y, a.z);
    glVertex3f(b.x, b.y, b.z);
    glVertex3f(c.x, c.y, c.z);
  }
  glEnd();
  SwapBuffers(st.glHdc);
  wglMakeCurrent(nullptr, nullptr);
}

struct DxLineVertex {
  float x, y;
  float r, g, b, a;
};

void DrawModelPreviewDx11(HWND hwnd, const RECT& rc, ModelPreviewState* st) {
  if (!st || !InitPreviewDx(hwnd, st) || !st->d3dCtx || !st->d3dSwap || !st->d3dRtv) return;
  RECT cr{};
  GetClientRect(hwnd, &cr);
  UINT w = static_cast<UINT>((std::max)(1L, cr.right - cr.left));
  UINT h = static_cast<UINT>((std::max)(1L, cr.bottom - cr.top));
  DXGI_SWAP_CHAIN_DESC sd{};
  st->d3dSwap->GetDesc(&sd);
  if (sd.BufferDesc.Width != w || sd.BufferDesc.Height != h) {
    st->d3dCtx->OMSetRenderTargets(0, nullptr, nullptr);
    SafeRelease(st->d3dRtv);
    SafeRelease(st->d3dDsv);
    SafeRelease(st->d3dDsTex);
    if (SUCCEEDED(st->d3dSwap->ResizeBuffers(0, w, h, DXGI_FORMAT_UNKNOWN, 0))) {
      RecreatePreviewDxRtv(st);
      D3D11_TEXTURE2D_DESC dtd{};
      dtd.Width = w;
      dtd.Height = h;
      dtd.MipLevels = 1;
      dtd.ArraySize = 1;
      dtd.Format = DXGI_FORMAT_D24_UNORM_S8_UINT;
      dtd.SampleDesc.Count = 1;
      dtd.BindFlags = D3D11_BIND_DEPTH_STENCIL;
      st->d3dDev->CreateTexture2D(&dtd, nullptr, &st->d3dDsTex);
      if (st->d3dDsTex) st->d3dDev->CreateDepthStencilView(st->d3dDsTex, nullptr, &st->d3dDsv);
    }
  }
  std::vector<DxLineVertex> triVerts;
  std::vector<DxLineVertex> lineVerts;
  if (st->solid) {
    triVerts.reserve(st->model.faces.size() * 3);
  }
  lineVerts.reserve(st->model.faces.size() * 6);
  const float rw = static_cast<float>((std::max)(1L, rc.right - rc.left));
  const float rh = static_cast<float>((std::max)(1L, rc.bottom - rc.top));
  const float cx = std::cos(st->rotX), sx = std::sin(st->rotX);
  const float cy = std::cos(st->rotY), sy = std::sin(st->rotY);
  auto toNdc = [&](POINT p) {
    float x = ((p.x - rc.left) / rw) * 2.0f - 1.0f;
    float y = 1.0f - ((p.y - rc.top) / rh) * 2.0f;
    return std::pair<float, float>(x, y);
  };
  auto rotate = [&](const PreviewVec3& v) {
    PreviewVec3 o{};
    float x1 = v.x;
    float y1 = v.y * cx - v.z * sx;
    float z1 = v.y * sx + v.z * cx;
    o.x = x1 * cy + z1 * sy;
    o.y = y1;
    o.z = -x1 * sy + z1 * cy;
    return o;
  };
  for (size_t fidx = 0; fidx < st->model.faces.size(); ++fidx) {
    const auto& f = st->model.faces[fidx];
    if (f[0] < 0 || f[1] < 0 || f[2] < 0 || f[0] >= static_cast<int>(st->model.vertices.size()) ||
        f[1] >= static_cast<int>(st->model.vertices.size()) || f[2] >= static_cast<int>(st->model.vertices.size())) {
      continue;
    }
    POINT p0 = ProjectPoint({st->model.vertices[f[0]].x - st->model.center.x, st->model.vertices[f[0]].y - st->model.center.y,
                             st->model.vertices[f[0]].z - st->model.center.z},
                            st->rotX, st->rotY, st->zoom, rc);
    POINT p1 = ProjectPoint({st->model.vertices[f[1]].x - st->model.center.x, st->model.vertices[f[1]].y - st->model.center.y,
                             st->model.vertices[f[1]].z - st->model.center.z},
                            st->rotX, st->rotY, st->zoom, rc);
    POINT p2 = ProjectPoint({st->model.vertices[f[2]].x - st->model.center.x, st->model.vertices[f[2]].y - st->model.center.y,
                             st->model.vertices[f[2]].z - st->model.center.z},
                            st->rotX, st->rotY, st->zoom, rc);
    auto n0 = toNdc(p0), n1 = toNdc(p1), n2 = toNdc(p2);
    const float lr = 0.45f, lg = 0.82f, lb = 1.0f, la = 1.0f;
    if (st->solid) {
      PreviewVec3 va = st->model.vertices[f[0]];
      PreviewVec3 vb = st->model.vertices[f[1]];
      PreviewVec3 vc = st->model.vertices[f[2]];
      va.x -= st->model.center.x; va.y -= st->model.center.y; va.z -= st->model.center.z;
      vb.x -= st->model.center.x; vb.y -= st->model.center.y; vb.z -= st->model.center.z;
      vc.x -= st->model.center.x; vc.y -= st->model.center.y; vc.z -= st->model.center.z;
      const PreviewVec3 a3 = rotate(va);
      const PreviewVec3 b3 = rotate(vb);
      const PreviewVec3 c3 = rotate(vc);
      const float ux = b3.x - a3.x, uy = b3.y - a3.y, uz = b3.z - a3.z;
      const float vx = c3.x - a3.x, vy = c3.y - a3.y, vz = c3.z - a3.z;
      float nx = uy * vz - uz * vy;
      float ny = uz * vx - ux * vz;
      float nz = ux * vy - uy * vx;
      float nl = std::sqrt(nx * nx + ny * ny + nz * nz);
      if (nl < 1e-6f) nl = 1.0f;
      nx /= nl; ny /= nl; nz /= nl;
      const float lx = -0.35f, ly = 0.5f, lz = 0.78f;
      const float diffuse = (std::max)(0.0f, nx * lx + ny * ly + nz * lz);
      const float shade = 0.20f + 0.80f * diffuse;
      float mr = st->model.kdR, mg = st->model.kdG, mb = st->model.kdB;
      if (fidx < st->model.faceMaterial.size()) {
        const int mi = st->model.faceMaterial[fidx];
        if (mi >= 0 && mi < static_cast<int>(st->model.materials.size())) {
          mr = st->model.materials[mi].kdR;
          mg = st->model.materials[mi].kdG;
          mb = st->model.materials[mi].kdB;
        }
      }
      const float fr = mr * shade, fg = mg * shade, fb = mb * shade, fa = 1.0f;
      triVerts.push_back({n0.first, n0.second, fr, fg, fb, fa});
      triVerts.push_back({n1.first, n1.second, fr, fg, fb, fa});
      triVerts.push_back({n2.first, n2.second, fr, fg, fb, fa});
    }
    lineVerts.push_back({n0.first, n0.second, lr, lg, lb, la});
    lineVerts.push_back({n1.first, n1.second, lr, lg, lb, la});
    lineVerts.push_back({n1.first, n1.second, lr, lg, lb, la});
    lineVerts.push_back({n2.first, n2.second, lr, lg, lb, la});
    lineVerts.push_back({n2.first, n2.second, lr, lg, lb, la});
    lineVerts.push_back({n0.first, n0.second, lr, lg, lb, la});
  }
  std::vector<DxLineVertex> allVerts;
  allVerts.reserve(triVerts.size() + lineVerts.size());
  allVerts.insert(allVerts.end(), triVerts.begin(), triVerts.end());
  const UINT triCount = static_cast<UINT>(triVerts.size());
  allVerts.insert(allVerts.end(), lineVerts.begin(), lineVerts.end());
  const UINT needBytes = static_cast<UINT>(allVerts.size() * sizeof(DxLineVertex));
  if (needBytes > 0 && needBytes > st->d3dVbCap) {
    SafeRelease(st->d3dVb);
    D3D11_BUFFER_DESC bd{};
    bd.Usage = D3D11_USAGE_DYNAMIC;
    bd.BindFlags = D3D11_BIND_VERTEX_BUFFER;
    bd.CPUAccessFlags = D3D11_CPU_ACCESS_WRITE;
    bd.ByteWidth = needBytes + 4096;
    if (SUCCEEDED(st->d3dDev->CreateBuffer(&bd, nullptr, &st->d3dVb))) {
      st->d3dVbCap = bd.ByteWidth;
    }
  }
  if (needBytes > 0 && st->d3dVb) {
    D3D11_MAPPED_SUBRESOURCE ms{};
    if (SUCCEEDED(st->d3dCtx->Map(st->d3dVb, 0, D3D11_MAP_WRITE_DISCARD, 0, &ms))) {
      memcpy(ms.pData, allVerts.data(), needBytes);
      st->d3dCtx->Unmap(st->d3dVb, 0);
    }
  }
  FLOAT clear[4] = {0.10f, 0.12f, 0.16f, 1.0f};
  st->d3dCtx->OMSetRenderTargets(1, &st->d3dRtv, st->d3dDsv);
  if (st->d3dDsState) st->d3dCtx->OMSetDepthStencilState(st->d3dDsState, 0);
  D3D11_VIEWPORT vp{};
  vp.Width = static_cast<float>(w);
  vp.Height = static_cast<float>(h);
  vp.MinDepth = 0;
  vp.MaxDepth = 1;
  st->d3dCtx->RSSetViewports(1, &vp);
  st->d3dCtx->ClearRenderTargetView(st->d3dRtv, clear);
  if (st->d3dDsv) st->d3dCtx->ClearDepthStencilView(st->d3dDsv, D3D11_CLEAR_DEPTH | D3D11_CLEAR_STENCIL, 1.0f, 0);
  if (st->d3dVb && !allVerts.empty()) {
    UINT stride = sizeof(DxLineVertex), off = 0;
    st->d3dCtx->IASetInputLayout(st->d3dLayout);
    st->d3dCtx->IASetVertexBuffers(0, 1, &st->d3dVb, &stride, &off);
    st->d3dCtx->VSSetShader(st->d3dVs, nullptr, 0);
    st->d3dCtx->PSSetShader(st->d3dPs, nullptr, 0);
    if (triCount > 0) {
      if (st->d3dRsSolid) st->d3dCtx->RSSetState(st->d3dRsSolid);
      st->d3dCtx->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
      st->d3dCtx->Draw(triCount, 0);
    }
    if (st->d3dRsWire) st->d3dCtx->RSSetState(st->d3dRsWire);
    st->d3dCtx->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_LINELIST);
    st->d3dCtx->Draw(static_cast<UINT>(lineVerts.size()), triCount);
  }
  st->d3dSwap->Present(1, 0);
}

RECT GetPreviewViewportRect(HWND hwnd) {
  RECT rc{};
  GetClientRect(hwnd, &rc);
  const int clientRight = static_cast<int>(rc.right);
  const int clientBottom = static_cast<int>(rc.bottom);
  const int margin = 12;
  const int viewTop = 42;
  const int runtimeH = 20;
  const int infoH = (std::clamp<int>)((clientBottom - viewTop) / 3, 120, 240);
  const int runtimeTop = (std::max)(viewTop + 80, clientBottom - margin - runtimeH);
  const int infoTop = (std::max)(viewTop + 60, runtimeTop - 8 - infoH);
  rc.left = margin;
  rc.top = viewTop;
  const int left = static_cast<int>(rc.left);
  const int right = (std::max)(left + 40, clientRight - margin);
  rc.right = right;
  const int top = static_cast<int>(rc.top);
  const int bottom = (std::max)(top + 80, infoTop - 8);
  rc.bottom = bottom;
  return rc;
}

std::wstring BuildObjInfoText(const std::wstring& path) {
  const ObjPreviewStats st = ScanObjStats(path);
  const uint64_t estMem = st.vertices * 32 + st.texcoords * 8 + st.normals * 12 + st.faces * 16;
  const uint64_t estVram = estMem * 3 / 2;
  std::wstringstream ss;
  ss << L"文件: " << path << L"\r\n";
  ss << L"文件体积: " << ToHumanBytes(st.fileBytes) << L"\r\n\r\n";
  ss << L"[Mesh 信息]\r\n";
  ss << L"顶点数: " << st.vertices << L"\r\n";
  ss << L"纹理坐标数: " << st.texcoords << L"\r\n";
  ss << L"法线数: " << st.normals << L"\r\n";
  ss << L"面片数: " << st.faces << L"\r\n\r\n";
  ss << L"[材质/贴图信息]\r\n";
  ss << L"材质数量(usemtl): " << st.materials << L"\r\n";
  ss << L"贴图数量(map_Kd): " << st.textures << L"\r\n\r\n";
  ss << L"[资源占用估算]\r\n";
  ss << L"内存占用(估算): " << ToHumanBytes(estMem) << L"\r\n";
  ss << L"显存占用(估算): " << ToHumanBytes(estVram) << L"\r\n";
  return ss.str();
}

void FitPreviewCamera(ModelPreviewState* st) {
  if (!st) return;
  st->rotX = 0.5f;
  st->rotY = -0.8f;
  const float ex = (std::max)(0.001f, st->model.extent);
  st->zoom = std::clamp(1.35f / ex, 0.2f, 5.0f);
}

LRESULT CALLBACK ModelPreviewWndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) {
  constexpr int kPreviewModeComboId = 1;
  constexpr int kPreviewInfoEditId = 2;
  constexpr int kPreviewModeTextId = 3;
  constexpr int kPreviewSolidCheckId = 4;
  constexpr int kPreviewRuntimeTextId = 5;
  constexpr int kPreviewResetBtnId = 6;
  constexpr int kPreviewFitBtnId = 7;
  constexpr int kPreviewTexBtnId = 8;
  constexpr UINT_PTR kPreviewTimerId = 101;
  switch (msg) {
    case WM_CREATE: {
      auto* st = new ModelPreviewState();
      st->path = g_pendingPreviewModelPath;
      ParseObjModel(st->path, &st->model);
      st->lastFpsTick = GetTickCount();
      SetWindowLongPtrW(hwnd, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(st));
      CreateWindowW(L"STATIC", L"渲染模式：", WS_CHILD | WS_VISIBLE, 12, 12, 72, 20, hwnd, nullptr,
                    GetModuleHandleW(nullptr), nullptr);
      HWND mode = CreateWindowW(L"COMBOBOX", L"", WS_CHILD | WS_VISIBLE | CBS_DROPDOWNLIST | WS_VSCROLL, 88, 10, 180, 140,
                                hwnd, reinterpret_cast<HMENU>(static_cast<INT_PTR>(kPreviewModeComboId)),
                                GetModuleHandleW(nullptr), nullptr);
      SendMessageW(mode, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(L"内置渲染 - OpenGL"));
      SendMessageW(mode, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(L"内置渲染 - DirectX11"));
      SendMessageW(mode, CB_SETCURSEL, 0, 0);
      CreateWindowW(L"BUTTON", L"实体填充", WS_CHILD | WS_VISIBLE | BS_AUTOCHECKBOX, 280, 10, 88, 20, hwnd,
                    reinterpret_cast<HMENU>(static_cast<INT_PTR>(kPreviewSolidCheckId)), GetModuleHandleW(nullptr), nullptr);
      SendMessageW(GetDlgItem(hwnd, kPreviewSolidCheckId), BM_SETCHECK, BST_CHECKED, 0);
      CreateWindowW(L"BUTTON", L"重置视角", WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON, 372, 10, 76, 20, hwnd,
                    reinterpret_cast<HMENU>(static_cast<INT_PTR>(kPreviewResetBtnId)), GetModuleHandleW(nullptr), nullptr);
      CreateWindowW(L"BUTTON", L"适配模型", WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON, 372, 34, 76, 20, hwnd,
                    reinterpret_cast<HMENU>(static_cast<INT_PTR>(kPreviewFitBtnId)), GetModuleHandleW(nullptr), nullptr);
      CreateWindowW(L"BUTTON", L"预览贴图", WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON, 280, 34, 88, 20, hwnd,
                    reinterpret_cast<HMENU>(static_cast<INT_PTR>(kPreviewTexBtnId)), GetModuleHandleW(nullptr), nullptr);
      CreateWindowW(L"STATIC", L"当前渲染器: OpenGL（内置）", WS_CHILD | WS_VISIBLE, 280, 12, 170, 20, hwnd,
                    reinterpret_cast<HMENU>(static_cast<INT_PTR>(kPreviewModeTextId)), GetModuleHandleW(nullptr),
                    nullptr);
      HWND info = CreateWindowW(L"EDIT", L"", WS_CHILD | WS_VISIBLE | WS_BORDER | ES_MULTILINE | ES_AUTOVSCROLL |
                                               WS_VSCROLL | ES_READONLY,
                                12, 270, 436, 220, hwnd, reinterpret_cast<HMENU>(static_cast<INT_PTR>(kPreviewInfoEditId)),
                                GetModuleHandleW(nullptr), nullptr);
      SendMessageW(mode, WM_SETFONT, reinterpret_cast<WPARAM>(UiGetAppFont()), TRUE);
      SendMessageW(info, WM_SETFONT, reinterpret_cast<WPARAM>(UiGetAppFont()), TRUE);
      SetWindowTextW(info, BuildObjInfoText(g_pendingPreviewModelPath).c_str());
      FitPreviewCamera(st);
      CreateWindowW(L"STATIC", L"FPS: -- | 内存(估): -- | 显存(估): --", WS_CHILD | WS_VISIBLE, 12, 494, 436, 20, hwnd,
                    reinterpret_cast<HMENU>(static_cast<INT_PTR>(kPreviewRuntimeTextId)),
                    GetModuleHandleW(nullptr), nullptr);
      if (HWND rt = GetDlgItem(hwnd, kPreviewRuntimeTextId)) {
        SendMessageW(rt, WM_SETFONT, reinterpret_cast<WPARAM>(UiGetAppFont()), TRUE);
      }
      SetTimer(hwnd, kPreviewTimerId, 16, nullptr);
      return 0;
    }
    case WM_COMMAND:
      if (HIWORD(wParam) == CBN_SELCHANGE && LOWORD(wParam) == kPreviewModeComboId) {
        HWND mode = GetDlgItem(hwnd, kPreviewModeComboId);
        const int sel = static_cast<int>(SendMessageW(mode, CB_GETCURSEL, 0, 0));
        if (auto* st = reinterpret_cast<ModelPreviewState*>(GetWindowLongPtrW(hwnd, GWLP_USERDATA))) {
          st->backend = (sel == 1) ? PreviewRenderBackend::kDx11 : PreviewRenderBackend::kOpenGL;
          if (st->backend == PreviewRenderBackend::kOpenGL) {
            ReleasePreviewDx(st);
            InitPreviewGl(hwnd, st);
          } else {
            ReleasePreviewGl(hwnd, st);
            InitPreviewDx(hwnd, st);
          }
        }
        SetWindowTextW(GetDlgItem(hwnd, kPreviewModeTextId),
                       sel == 1 ? L"当前渲染器: DirectX11（内置）" : L"当前渲染器: OpenGL（内置）");
        RECT vrc = GetPreviewViewportRect(hwnd);
        InvalidateRect(hwnd, &vrc, FALSE);
        return 0;
      }
      if (LOWORD(wParam) == kPreviewSolidCheckId && HIWORD(wParam) == BN_CLICKED) {
        if (auto* st = reinterpret_cast<ModelPreviewState*>(GetWindowLongPtrW(hwnd, GWLP_USERDATA))) {
          st->solid = (SendMessageW(GetDlgItem(hwnd, kPreviewSolidCheckId), BM_GETCHECK, 0, 0) == BST_CHECKED);
          RECT vrc = GetPreviewViewportRect(hwnd);
          InvalidateRect(hwnd, &vrc, FALSE);
        }
        return 0;
      }
      if (LOWORD(wParam) == kPreviewResetBtnId && HIWORD(wParam) == BN_CLICKED) {
        if (auto* st = reinterpret_cast<ModelPreviewState*>(GetWindowLongPtrW(hwnd, GWLP_USERDATA))) {
          st->rotX = 0.5f;
          st->rotY = -0.8f;
          st->zoom = 1.0f;
          RECT vrc = GetPreviewViewportRect(hwnd);
          InvalidateRect(hwnd, &vrc, FALSE);
        }
        return 0;
      }
      if (LOWORD(wParam) == kPreviewFitBtnId && HIWORD(wParam) == BN_CLICKED) {
        if (auto* st = reinterpret_cast<ModelPreviewState*>(GetWindowLongPtrW(hwnd, GWLP_USERDATA))) {
          FitPreviewCamera(st);
          RECT vrc = GetPreviewViewportRect(hwnd);
          InvalidateRect(hwnd, &vrc, FALSE);
        }
        return 0;
      }
      if (LOWORD(wParam) == kPreviewTexBtnId && HIWORD(wParam) == BN_CLICKED) {
        if (auto* st = reinterpret_cast<ModelPreviewState*>(GetWindowLongPtrW(hwnd, GWLP_USERDATA))) {
          if (!st->model.primaryMapKdPath.empty()) {
            ShellExecuteW(hwnd, L"open", st->model.primaryMapKdPath.c_str(), nullptr, nullptr, SW_SHOWNORMAL);
          } else {
            MessageBoxW(hwnd, L"当前模型未检测到 map_Kd 贴图路径。", L"预览贴图", MB_OK | MB_ICONINFORMATION);
          }
        }
        return 0;
      }
      break;
    case WM_LBUTTONDOWN: {
      RECT vrc = GetPreviewViewportRect(hwnd);
      const POINT pt{GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam)};
      if (PtInRect(&vrc, pt)) {
        if (auto* st = reinterpret_cast<ModelPreviewState*>(GetWindowLongPtrW(hwnd, GWLP_USERDATA))) {
          st->dragging = true;
          st->lastPt = pt;
          SetCapture(hwnd);
        }
      }
      return 0;
    }
    case WM_MOUSEMOVE: {
      auto* st = reinterpret_cast<ModelPreviewState*>(GetWindowLongPtrW(hwnd, GWLP_USERDATA));
      if (st && st->dragging) {
        const POINT pt{GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam)};
        const int dx = pt.x - st->lastPt.x;
        const int dy = pt.y - st->lastPt.y;
        st->rotY += dx * 0.01f;
        st->rotX += dy * 0.01f;
        st->lastPt = pt;
        RECT vrc = GetPreviewViewportRect(hwnd);
        InvalidateRect(hwnd, &vrc, FALSE);
      }
      return 0;
    }
    case WM_LBUTTONUP:
      if (auto* st = reinterpret_cast<ModelPreviewState*>(GetWindowLongPtrW(hwnd, GWLP_USERDATA))) {
        st->dragging = false;
      }
      ReleaseCapture();
      return 0;
    case WM_MOUSEWHEEL:
      if (auto* st = reinterpret_cast<ModelPreviewState*>(GetWindowLongPtrW(hwnd, GWLP_USERDATA))) {
        const int delta = GET_WHEEL_DELTA_WPARAM(wParam);
        st->zoom *= (delta > 0) ? 1.1f : 0.9f;
        st->zoom = std::clamp(st->zoom, 0.2f, 5.0f);
        RECT vrc = GetPreviewViewportRect(hwnd);
        InvalidateRect(hwnd, &vrc, FALSE);
      }
      return 0;
    case WM_ERASEBKGND:
      // 预览视口由 OpenGL / DX11 自行清屏与交换，不做默认底色擦除可减少闪烁。
      return 1;
    case WM_TIMER:
      if (wParam == kPreviewTimerId) {
        RECT vrc = GetPreviewViewportRect(hwnd);
        InvalidateRect(hwnd, &vrc, FALSE);
        return 0;
      }
      break;
    case WM_SIZE: {
      RECT rc{};
      GetClientRect(hwnd, &rc);
      const int clientRight = static_cast<int>(rc.right);
      const int clientBottom = static_cast<int>(rc.bottom);
      const int margin = 12;
      const int runtimeH = 20;
      const int infoH = (std::clamp<int>)((clientBottom - 42) / 3, 120, 240);
      const int runtimeTop = (std::max)(140, clientBottom - margin - runtimeH);
      const int infoTop = (std::max)(120, runtimeTop - 8 - infoH);
      const int infoW = (std::max)(120, clientRight - margin * 2);
      if (HWND info = GetDlgItem(hwnd, kPreviewInfoEditId)) {
        MoveWindow(info, margin, infoTop, infoW, infoH, TRUE);
      }
      if (HWND rt = GetDlgItem(hwnd, kPreviewRuntimeTextId)) {
        MoveWindow(rt, margin, runtimeTop, infoW, runtimeH, TRUE);
      }
      RECT vrc = GetPreviewViewportRect(hwnd);
      InvalidateRect(hwnd, &vrc, FALSE);
      return 0;
    }
    case WM_PAINT: {
      PAINTSTRUCT ps{};
      HDC hdc = BeginPaint(hwnd, &ps);
      RECT vrc = GetPreviewViewportRect(hwnd);
      if (auto* st = reinterpret_cast<ModelPreviewState*>(GetWindowLongPtrW(hwnd, GWLP_USERDATA))) {
        if (st->backend == PreviewRenderBackend::kOpenGL) {
          InitPreviewGl(hwnd, st);
          DrawModelPreviewOpenGL(hwnd, vrc, *st);
        } else {
          DrawModelPreviewDx11(hwnd, vrc, st);
        }
        st->frameCounter += 1;
        const DWORD now = GetTickCount();
        const DWORD dt = now - st->lastFpsTick;
        if (dt >= 500) {
          st->fps = (st->frameCounter * 1000.0f) / static_cast<float>(dt);
          st->frameCounter = 0;
          st->lastFpsTick = now;
          const ObjPreviewStats ost = ScanObjStats(st->path);
          const uint64_t estMem = ost.vertices * 32 + ost.texcoords * 8 + ost.normals * 12 + ost.faces * 16;
          const uint64_t estVram = estMem * 3 / 2;
          wchar_t line[256]{};
          swprintf_s(line, L"FPS: %.1f | 内存(估): %s | 显存(估): %s", st->fps, ToHumanBytes(estMem).c_str(),
                     ToHumanBytes(estVram).c_str());
          SetWindowTextW(GetDlgItem(hwnd, kPreviewRuntimeTextId), line);
        }
      }
      FrameRect(hdc, &vrc, reinterpret_cast<HBRUSH>(GetStockObject(GRAY_BRUSH)));
      EndPaint(hwnd, &ps);
      return 0;
    }
    case WM_CLOSE:
      DestroyWindow(hwnd);
      return 0;
    case WM_DESTROY:
      KillTimer(hwnd, kPreviewTimerId);
      if (auto* st = reinterpret_cast<ModelPreviewState*>(GetWindowLongPtrW(hwnd, GWLP_USERDATA))) {
        ReleasePreviewGl(hwnd, st);
        ReleasePreviewDx(st);
        delete st;
        SetWindowLongPtrW(hwnd, GWLP_USERDATA, 0);
      }
      return 0;
    default:
      break;
  }
  return DefWindowProcW(hwnd, msg, wParam, lParam);
}

void OpenModelPreviewWindow(HWND owner, const std::wstring& path) {
  g_pendingPreviewModelPath = path;
  CreateWindowExW(WS_EX_TOOLWINDOW, kModelPreviewClass, L"模型数据预览",
                  WS_OVERLAPPEDWINDOW | WS_VISIBLE | WS_CLIPCHILDREN,
                  CW_USEDEFAULT, CW_USEDEFAULT, 480, 560, owner, nullptr, GetModuleHandleW(nullptr), nullptr);
}

bool RunConvertBackend(HWND hwnd) {
  const int inMajor = static_cast<int>(SendMessageW(GetDlgItem(hwnd, IDC_CONV_INPUT_TYPE), CB_GETCURSEL, 0, 0));
  const int outMajor = static_cast<int>(SendMessageW(GetDlgItem(hwnd, IDC_CONV_OUTPUT_TYPE), CB_GETCURSEL, 0, 0));
  if (inMajor < 0 || outMajor < 0 || inMajor == outMajor) {
    MessageBoxW(hwnd, L"请选择不同的输入类型与输出类型。", L"数据转换", MB_OK | MB_ICONWARNING);
    return false;
  }
  wchar_t inPath[1024]{};
  wchar_t outPath[1024]{};
  GetWindowTextW(GetDlgItem(hwnd, IDC_CONV_INPUT_PATH), inPath, 1024);
  GetWindowTextW(GetDlgItem(hwnd, IDC_CONV_OUTPUT_PATH), outPath, 1024);
  if (inPath[0] == L'\0' || outPath[0] == L'\0') {
    MessageBoxW(hwnd, L"请先设置输入和输出路径。", L"数据转换", MB_OK | MB_ICONWARNING);
    return false;
  }
  const wchar_t* exeName = ConvertToolExeName(inMajor, outMajor);
  if (!exeName) {
    return false;
  }
  wchar_t modulePath[MAX_PATH]{};
  GetModuleFileNameW(nullptr, modulePath, MAX_PATH);
  std::wstring exeDir = modulePath;
  const size_t slash = exeDir.find_last_of(L"\\/");
  if (slash != std::wstring::npos) {
    exeDir.resize(slash + 1);
  }
  const std::wstring exePath = exeDir + exeName;
  const std::wstring inType = GetComboSelectedText(GetDlgItem(hwnd, IDC_CONV_INPUT_TYPE));
  const std::wstring inSub = GetComboSelectedText(GetDlgItem(hwnd, IDC_CONV_INPUT_SUBTYPE));
  const std::wstring outType = GetComboSelectedText(GetDlgItem(hwnd, IDC_CONV_OUTPUT_TYPE));
  const std::wstring outSub = GetComboSelectedText(GetDlgItem(hwnd, IDC_CONV_OUTPUT_SUBTYPE));
  std::wstring cmd = QuoteArg(exePath) + L" --input " + QuoteArg(inPath) + L" --output " + QuoteArg(outPath) +
                     L" --input-type " + QuoteArg(inType) + L" --input-subtype " + QuoteArg(inSub) +
                     L" --output-type " + QuoteArg(outType) + L" --output-subtype " + QuoteArg(outSub);
  UpdateConvertCmdlinePreview(hwnd);
  WriteConvertLog(hwnd, (std::wstring(L"[命令] ") + cmd).c_str());
  std::vector<wchar_t> cmdBuf(cmd.begin(), cmd.end());
  cmdBuf.push_back(L'\0');
  STARTUPINFOW si{};
  si.cb = sizeof(si);
  PROCESS_INFORMATION pi{};
  if (!CreateProcessW(nullptr, cmdBuf.data(), nullptr, nullptr, FALSE, CREATE_NO_WINDOW, nullptr, nullptr, &si, &pi)) {
    WriteConvertLog(hwnd, (std::wstring(L"[错误] 无法启动：") + exeName).c_str());
    return false;
  }
  WaitForSingleObject(pi.hProcess, INFINITE);
  DWORD code = 1;
  GetExitCodeProcess(pi.hProcess, &code);
  CloseHandle(pi.hThread);
  CloseHandle(pi.hProcess);
  WriteConvertLog(hwnd, (std::wstring(L"[后端] 退出码：") + std::to_wstring(code)).c_str());
  return code == 0;
}

void PreviewPath(HWND hwnd, bool inputSide) {
  const int ctrl = inputSide ? IDC_CONV_INPUT_PATH : IDC_CONV_OUTPUT_PATH;
  const int majorCtrl = inputSide ? IDC_CONV_INPUT_TYPE : IDC_CONV_OUTPUT_TYPE;
  wchar_t pathBuf[1024]{};
  GetWindowTextW(GetDlgItem(hwnd, ctrl), pathBuf, 1024);
  std::wstring path = pathBuf;
  if (path.empty()) {
    MessageBoxW(hwnd, L"请先设置路径。", L"预览", MB_OK | MB_ICONINFORMATION);
    return;
  }
  DWORD attr = GetFileAttributesW(path.c_str());
  if (attr == INVALID_FILE_ATTRIBUTES) {
    const std::wstring msg = std::wstring(L"路径不存在：\n") + path;
    MessageBoxW(hwnd, msg.c_str(), L"预览", MB_OK | MB_ICONWARNING);
    return;
  }
  const int major = static_cast<int>(SendMessageW(GetDlgItem(hwnd, majorCtrl), CB_GETCURSEL, 0, 0));
  HMENU menu = CreatePopupMenu();
  AppendMenuW(menu, MF_STRING, 1, L"内置预览（默认）");
  AppendMenuW(menu, MF_STRING, 2, L"系统默认打开");
  AppendMenuW(menu, MF_STRING, 3, L"选择其他应用...");
  SetMenuDefaultItem(menu, 1, FALSE);
  POINT pt{};
  GetCursorPos(&pt);
  const UINT chosen = TrackPopupMenu(menu, TPM_RETURNCMD | TPM_RIGHTBUTTON, pt.x, pt.y, 0, hwnd, nullptr);
  DestroyMenu(menu);
  if (chosen == 0) {
    return;
  }
  if (chosen == 1) {
    if (major == 1 && (attr & FILE_ATTRIBUTE_DIRECTORY) == 0) {
      OpenModelPreviewWindow(hwnd, path);
      WriteConvertLog(hwnd, (std::wstring(inputSide ? L"[预览] 输入(内置3D)：" : L"[预览] 输出(内置3D)：") + path).c_str());
      return;
    }
    HINSTANCE h = ShellExecuteW(hwnd, L"open", path.c_str(), nullptr, nullptr, SW_SHOWNORMAL);
    if (reinterpret_cast<INT_PTR>(h) <= 32) {
      MessageBoxW(hwnd, L"无法打开预览目标。", L"预览", MB_OK | MB_ICONWARNING);
      return;
    }
  } else if (chosen == 2) {
    HINSTANCE h = ShellExecuteW(hwnd, L"open", path.c_str(), nullptr, nullptr, SW_SHOWNORMAL);
    if (reinterpret_cast<INT_PTR>(h) <= 32) {
      MessageBoxW(hwnd, L"无法用系统默认方式打开。", L"预览", MB_OK | MB_ICONWARNING);
      return;
    }
  } else {
    const std::wstring params = L"shell32.dll,OpenAs_RunDLL " + QuoteArg(path);
    HINSTANCE h = ShellExecuteW(hwnd, L"open", L"rundll32.exe", params.c_str(), nullptr, SW_SHOWNORMAL);
    if (reinterpret_cast<INT_PTR>(h) <= 32) {
      MessageBoxW(hwnd, L"无法打开“选择其他应用”。", L"预览", MB_OK | MB_ICONWARNING);
      return;
    }
  }
  WriteConvertLog(hwnd, (std::wstring(inputSide ? L"[预览] 输入：" : L"[预览] 输出：") + path).c_str());
}

void ShowDataConvertWindow(HWND owner) {
  if (g_hwndConvertDlg && IsWindow(g_hwndConvertDlg)) {
    ShowWindow(g_hwndConvertDlg, SW_SHOW);
    SetForegroundWindow(g_hwndConvertDlg);
    return;
  }
  g_hwndConvertDlg = CreateWindowExW(WS_EX_DLGMODALFRAME, kConvertClass, L"数据转换",
                                     WS_OVERLAPPEDWINDOW | WS_VISIBLE, CW_USEDEFAULT, CW_USEDEFAULT, 920, 620, owner,
                                     nullptr, GetModuleHandleW(nullptr), nullptr);
}

LRESULT CALLBACK ConvertWndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) {
  static HBRUSH s_bg = nullptr;
  static HBRUSH s_edit_bg = nullptr;
  static HWND s_tip = nullptr;
  switch (msg) {
    case WM_CREATE: {
      SendMessageW(hwnd, WM_SETFONT, reinterpret_cast<WPARAM>(UiGetAppFont()), TRUE);
      CreateWindowW(L"STATIC", L"输入类型与信息", WS_CHILD | WS_VISIBLE, 10, 8, 120, 16, hwnd, nullptr,
                    GetModuleHandleW(nullptr), nullptr);
      HWND inType = CreateWindowW(L"COMBOBOX", L"", WS_CHILD | WS_VISIBLE | CBS_DROPDOWNLIST | WS_VSCROLL, 10, 30, 240,
                                  240, hwnd, reinterpret_cast<HMENU>(static_cast<INT_PTR>(IDC_CONV_INPUT_TYPE)),
                                  GetModuleHandleW(nullptr), nullptr);
      CreateWindowW(L"BUTTON", L"?", WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON, 226, 30, 24, 24, hwnd,
                    reinterpret_cast<HMENU>(static_cast<INT_PTR>(IDC_CONV_INPUT_TYPE_HELP)), GetModuleHandleW(nullptr),
                    nullptr);
      CreateWindowW(L"COMBOBOX", L"", WS_CHILD | WS_VISIBLE | CBS_DROPDOWNLIST | WS_VSCROLL, 10, 58, 240, 220, hwnd,
                    reinterpret_cast<HMENU>(static_cast<INT_PTR>(IDC_CONV_INPUT_SUBTYPE)),
                    GetModuleHandleW(nullptr), nullptr);
      CreateWindowW(L"BUTTON", L"?", WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON, 226, 58, 24, 24, hwnd,
                    reinterpret_cast<HMENU>(static_cast<INT_PTR>(IDC_CONV_INPUT_SUBTYPE_HELP)),
                    GetModuleHandleW(nullptr), nullptr);
      CreateWindowW(L"EDIT", L"", WS_CHILD | WS_VISIBLE | WS_BORDER | ES_AUTOHSCROLL, 10, 86, 172, 24, hwnd,
                    reinterpret_cast<HMENU>(static_cast<INT_PTR>(IDC_CONV_INPUT_PATH)), GetModuleHandleW(nullptr),
                    nullptr);
      CreateWindowW(L"BUTTON", L"浏览...", WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON, 186, 86, 64, 24, hwnd,
                    reinterpret_cast<HMENU>(static_cast<INT_PTR>(IDC_CONV_INPUT_BROWSE)), GetModuleHandleW(nullptr),
                    nullptr);
      CreateWindowW(L"BUTTON", L"预览", WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON, 186, 86, 64, 24, hwnd,
                    reinterpret_cast<HMENU>(static_cast<INT_PTR>(IDC_CONV_INPUT_PREVIEW)), GetModuleHandleW(nullptr),
                    nullptr);
      CreateWindowW(L"EDIT", L"", WS_CHILD | WS_VISIBLE | WS_BORDER | ES_MULTILINE | ES_AUTOVSCROLL | WS_VSCROLL, 10,
                    116, 240, 200, hwnd, reinterpret_cast<HMENU>(static_cast<INT_PTR>(IDC_CONV_INPUT_INFO)),
                    GetModuleHandleW(nullptr), nullptr);

      CreateWindowW(L"STATIC", L"转换设置", WS_CHILD | WS_VISIBLE, 280, 8, 90, 16, hwnd, nullptr,
                    GetModuleHandleW(nullptr), nullptr);
      CreateWindowW(L"EDIT",
                    L"统一坐标系=WebMercator\r\n重采样=双线性\r\n精度=中\r\n（不同类型可扩展子类型参数）",
                    WS_CHILD | WS_VISIBLE | WS_BORDER | ES_MULTILINE | ES_AUTOVSCROLL | WS_VSCROLL, 280, 30, 240, 230,
                    hwnd, reinterpret_cast<HMENU>(static_cast<INT_PTR>(IDC_CONV_SETTING)), GetModuleHandleW(nullptr),
                    nullptr);

      CreateWindowW(L"STATIC", L"输出类型与信息", WS_CHILD | WS_VISIBLE, 550, 8, 120, 16, hwnd, nullptr,
                    GetModuleHandleW(nullptr), nullptr);
      HWND outType =
          CreateWindowW(L"COMBOBOX", L"", WS_CHILD | WS_VISIBLE | CBS_DROPDOWNLIST | WS_VSCROLL, 550, 30, 240, 240,
                        hwnd, reinterpret_cast<HMENU>(static_cast<INT_PTR>(IDC_CONV_OUTPUT_TYPE)),
                        GetModuleHandleW(nullptr), nullptr);
      CreateWindowW(L"BUTTON", L"?", WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON, 766, 30, 24, 24, hwnd,
                    reinterpret_cast<HMENU>(static_cast<INT_PTR>(IDC_CONV_OUTPUT_TYPE_HELP)),
                    GetModuleHandleW(nullptr), nullptr);
      CreateWindowW(L"COMBOBOX", L"", WS_CHILD | WS_VISIBLE | CBS_DROPDOWNLIST | WS_VSCROLL, 550, 58, 240, 220, hwnd,
                    reinterpret_cast<HMENU>(static_cast<INT_PTR>(IDC_CONV_OUTPUT_SUBTYPE)),
                    GetModuleHandleW(nullptr), nullptr);
      CreateWindowW(L"BUTTON", L"?", WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON, 766, 58, 24, 24, hwnd,
                    reinterpret_cast<HMENU>(static_cast<INT_PTR>(IDC_CONV_OUTPUT_SUBTYPE_HELP)),
                    GetModuleHandleW(nullptr), nullptr);
      CreateWindowW(L"EDIT", L"", WS_CHILD | WS_VISIBLE | WS_BORDER | ES_AUTOHSCROLL, 550, 86, 172, 24, hwnd,
                    reinterpret_cast<HMENU>(static_cast<INT_PTR>(IDC_CONV_OUTPUT_PATH)), GetModuleHandleW(nullptr),
                    nullptr);
      CreateWindowW(L"BUTTON", L"浏览...", WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON, 726, 86, 64, 24, hwnd,
                    reinterpret_cast<HMENU>(static_cast<INT_PTR>(IDC_CONV_OUTPUT_BROWSE)), GetModuleHandleW(nullptr),
                    nullptr);
      CreateWindowW(L"BUTTON", L"预览", WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON, 726, 86, 64, 24, hwnd,
                    reinterpret_cast<HMENU>(static_cast<INT_PTR>(IDC_CONV_OUTPUT_PREVIEW)),
                    GetModuleHandleW(nullptr), nullptr);
      CreateWindowW(L"EDIT", L"", WS_CHILD | WS_VISIBLE | WS_BORDER | ES_MULTILINE | ES_AUTOVSCROLL | WS_VSCROLL, 550,
                    116, 240, 200, hwnd, reinterpret_cast<HMENU>(static_cast<INT_PTR>(IDC_CONV_OUTPUT_INFO)),
                    GetModuleHandleW(nullptr), nullptr);

      CreateWindowW(PROGRESS_CLASSW, L"", WS_CHILD | WS_VISIBLE, 10, 280, 740, 22, hwnd,
                    reinterpret_cast<HMENU>(static_cast<INT_PTR>(IDC_CONV_PROGRESS)), GetModuleHandleW(nullptr),
                    nullptr);
      CreateWindowW(L"BUTTON", L"执行转换", WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON, 760, 278, 100, 26, hwnd,
                    reinterpret_cast<HMENU>(static_cast<INT_PTR>(IDC_CONV_RUN)), GetModuleHandleW(nullptr), nullptr);
      CreateWindowW(L"EDIT", L"命令行:\r\n(未执行)", WS_CHILD | WS_VISIBLE | WS_BORDER | ES_MULTILINE | ES_AUTOVSCROLL |
                                              WS_VSCROLL | ES_READONLY,
                    10, 280, 860, 56, hwnd, reinterpret_cast<HMENU>(static_cast<INT_PTR>(IDC_CONV_CMDLINE)),
                    GetModuleHandleW(nullptr), nullptr);
      CreateWindowW(L"STATIC", L"准备就绪。", WS_CHILD | WS_VISIBLE, 10, 308, 860, 20, hwnd,
                    reinterpret_cast<HMENU>(static_cast<INT_PTR>(IDC_CONV_MSG)), GetModuleHandleW(nullptr), nullptr);
      CreateWindowW(L"EDIT", L"", WS_CHILD | WS_VISIBLE | WS_BORDER | ES_MULTILINE | ES_AUTOVSCROLL | WS_VSCROLL, 10,
                    336, 860, 220, hwnd, reinterpret_cast<HMENU>(static_cast<INT_PTR>(IDC_CONV_LOG)),
                    GetModuleHandleW(nullptr), nullptr);

      FillConvertTypeCombo(inType);
      FillConvertTypeCombo(outType);
      SyncConvertInfoByType(hwnd, true);
      SyncConvertInfoByType(hwnd, false);
      for (int cid : {IDC_CONV_INPUT_TYPE, IDC_CONV_INPUT_TYPE_HELP, IDC_CONV_INPUT_SUBTYPE, IDC_CONV_INPUT_SUBTYPE_HELP,
                      IDC_CONV_INPUT_PATH, IDC_CONV_INPUT_BROWSE, IDC_CONV_INPUT_PREVIEW, IDC_CONV_INPUT_INFO, IDC_CONV_SETTING,
                      IDC_CONV_OUTPUT_TYPE, IDC_CONV_OUTPUT_TYPE_HELP, IDC_CONV_OUTPUT_SUBTYPE,
                      IDC_CONV_OUTPUT_SUBTYPE_HELP, IDC_CONV_OUTPUT_PATH, IDC_CONV_OUTPUT_BROWSE, IDC_CONV_OUTPUT_PREVIEW,
                      IDC_CONV_OUTPUT_INFO, IDC_CONV_CMDLINE, IDC_CONV_PROGRESS, IDC_CONV_RUN, IDC_CONV_MSG, IDC_CONV_LOG}) {
        if (HWND c = GetDlgItem(hwnd, cid)) {
          SendMessageW(c, WM_SETFONT, reinterpret_cast<WPARAM>(UiGetAppFont()), TRUE);
        }
      }
      SendMessageW(GetDlgItem(hwnd, IDC_CONV_PROGRESS), PBM_SETRANGE, 0, MAKELPARAM(0, 100));
      SendMessageW(GetDlgItem(hwnd, IDC_CONV_PROGRESS), PBM_SETPOS, 0, 0);
      s_tip = CreateWindowExW(WS_EX_TOPMOST, TOOLTIPS_CLASSW, nullptr,
                              WS_POPUP | TTS_ALWAYSTIP | TTS_NOPREFIX, CW_USEDEFAULT, CW_USEDEFAULT, CW_USEDEFAULT,
                              CW_USEDEFAULT, hwnd, nullptr, GetModuleHandleW(nullptr), nullptr);
      if (s_tip) {
        SendMessageW(s_tip, TTM_SETMAXTIPWIDTH, 0, 480);
        AttachConvertTooltip(hwnd, s_tip, IDC_CONV_INPUT_TYPE);
        AttachConvertTooltip(hwnd, s_tip, IDC_CONV_INPUT_SUBTYPE);
        AttachConvertTooltip(hwnd, s_tip, IDC_CONV_OUTPUT_TYPE);
        AttachConvertTooltip(hwnd, s_tip, IDC_CONV_OUTPUT_SUBTYPE);
      }
      WriteConvertLog(hwnd, L"[转换] 窗口已打开。");
      WriteConvertLog(hwnd, L"[转换] 支持：GIS数据 / 模型数据 / 瓦片数据（含子类型）。");
      UpdateConvertCmdlinePreview(hwnd);
      LayoutConvertWindow(hwnd);
      return 0;
    }
    case WM_CTLCOLORDLG: {
      if (!s_bg) {
        s_bg = CreateSolidBrush(RGB(245, 248, 252));
      }
      return reinterpret_cast<INT_PTR>(s_bg);
    }
    case WM_CTLCOLORSTATIC: {
      HDC hdc = reinterpret_cast<HDC>(wParam);
      SetBkMode(hdc, TRANSPARENT);
      SetTextColor(hdc, RGB(30, 42, 62));
      if (!s_bg) {
        s_bg = CreateSolidBrush(RGB(245, 248, 252));
      }
      return reinterpret_cast<INT_PTR>(s_bg);
    }
    case WM_CTLCOLOREDIT: {
      HDC hdc = reinterpret_cast<HDC>(wParam);
      SetBkMode(hdc, OPAQUE);
      SetBkColor(hdc, RGB(255, 255, 255));
      SetTextColor(hdc, RGB(28, 36, 52));
      if (!s_edit_bg) {
        s_edit_bg = CreateSolidBrush(RGB(255, 255, 255));
      }
      return reinterpret_cast<INT_PTR>(s_edit_bg);
    }
    case WM_SIZE:
      LayoutConvertWindow(hwnd);
      return 0;
    case WM_NOTIFY: {
      if (!lParam) {
        break;
      }
      const NMHDR* hdr = reinterpret_cast<const NMHDR*>(lParam);
      if (hdr->code == TTN_GETDISPINFOW) {
        auto* di = reinterpret_cast<NMTTDISPINFOW*>(lParam);
        di->lpszText = const_cast<LPWSTR>(GetConvertTooltipText(hwnd, hdr->idFrom));
        return 0;
      }
      break;
    }
    case WM_COMMAND:
      if (HIWORD(wParam) == CBN_SELCHANGE && LOWORD(wParam) == IDC_CONV_INPUT_TYPE) {
        SyncConvertInfoByType(hwnd, true);
        UpdateConvertCmdlinePreview(hwnd);
        return 0;
      }
      if (HIWORD(wParam) == CBN_SELCHANGE && LOWORD(wParam) == IDC_CONV_OUTPUT_TYPE) {
        SyncConvertInfoByType(hwnd, false);
        UpdateConvertCmdlinePreview(hwnd);
        return 0;
      }
      if (HIWORD(wParam) == CBN_SELCHANGE && (LOWORD(wParam) == IDC_CONV_INPUT_SUBTYPE || LOWORD(wParam) == IDC_CONV_OUTPUT_SUBTYPE)) {
        UpdateConvertCmdlinePreview(hwnd);
        return 0;
      }
      if (HIWORD(wParam) == EN_CHANGE && (LOWORD(wParam) == IDC_CONV_INPUT_PATH || LOWORD(wParam) == IDC_CONV_OUTPUT_PATH)) {
        UpdateConvertCmdlinePreview(hwnd);
        return 0;
      }
      if (LOWORD(wParam) == IDC_CONV_INPUT_BROWSE) {
        const std::wstring p = PromptOpenInputPath(hwnd);
        if (!p.empty()) {
          SetWindowTextW(GetDlgItem(hwnd, IDC_CONV_INPUT_PATH), p.c_str());
          WriteConvertLog(hwnd, (std::wstring(L"[路径] 输入：") + p).c_str());
          UpdateConvertCmdlinePreview(hwnd);
        }
        return 0;
      }
      if (LOWORD(wParam) == IDC_CONV_INPUT_PREVIEW) {
        PreviewPath(hwnd, true);
        return 0;
      }
      if (LOWORD(wParam) == IDC_CONV_OUTPUT_BROWSE) {
        const int outMajor = static_cast<int>(SendMessageW(GetDlgItem(hwnd, IDC_CONV_OUTPUT_TYPE), CB_GETCURSEL, 0, 0));
        std::wstring p;
        if (outMajor == 2) {
          p = PromptSelectOutputFolder(hwnd);
        } else {
          p = PromptSaveOutputPath(hwnd);
        }
        if (!p.empty()) {
          SetWindowTextW(GetDlgItem(hwnd, IDC_CONV_OUTPUT_PATH), p.c_str());
          WriteConvertLog(hwnd, (std::wstring(L"[路径] 输出：") + p).c_str());
          UpdateConvertCmdlinePreview(hwnd);
        }
        return 0;
      }
      if (LOWORD(wParam) == IDC_CONV_OUTPUT_PREVIEW) {
        PreviewPath(hwnd, false);
        return 0;
      }
      if (LOWORD(wParam) == IDC_CONV_INPUT_TYPE_HELP) {
        ShowConvertHelpDialog(hwnd, true, true);
        return 0;
      }
      if (LOWORD(wParam) == IDC_CONV_INPUT_SUBTYPE_HELP) {
        ShowConvertHelpDialog(hwnd, true, false);
        return 0;
      }
      if (LOWORD(wParam) == IDC_CONV_OUTPUT_TYPE_HELP) {
        ShowConvertHelpDialog(hwnd, false, true);
        return 0;
      }
      if (LOWORD(wParam) == IDC_CONV_OUTPUT_SUBTYPE_HELP) {
        ShowConvertHelpDialog(hwnd, false, false);
        return 0;
      }
      if (LOWORD(wParam) == IDC_CONV_RUN) {
        const std::wstring inType = GetComboSelectedText(GetDlgItem(hwnd, IDC_CONV_INPUT_TYPE));
        const std::wstring inSub = GetComboSelectedText(GetDlgItem(hwnd, IDC_CONV_INPUT_SUBTYPE));
        const std::wstring outType = GetComboSelectedText(GetDlgItem(hwnd, IDC_CONV_OUTPUT_TYPE));
        const std::wstring outSub = GetComboSelectedText(GetDlgItem(hwnd, IDC_CONV_OUTPUT_SUBTYPE));
        const std::wstring inLine = std::wstring(L"[任务] 输入：") + inType + L" / " + inSub;
        const std::wstring outLine = std::wstring(L"[任务] 输出：") + outType + L" / " + outSub;
        WriteConvertLog(hwnd, inLine.c_str());
        WriteConvertLog(hwnd, outLine.c_str());
        SendMessageW(GetDlgItem(hwnd, IDC_CONV_PROGRESS), PBM_SETPOS, 15, 0);
        SetWindowTextW(GetDlgItem(hwnd, IDC_CONV_MSG), L"处理中：正在启动后端命令行工具...");
        const bool ok = RunConvertBackend(hwnd);
        SendMessageW(GetDlgItem(hwnd, IDC_CONV_PROGRESS), PBM_SETPOS, ok ? 100 : 0, 0);
        SetWindowTextW(GetDlgItem(hwnd, IDC_CONV_MSG), ok ? L"完成：转换成功。" : L"失败：后端返回错误。");
        return 0;
      }
      break;
    case WM_CLOSE:
      DestroyWindow(hwnd);
      return 0;
    case WM_DESTROY:
      g_hwndConvertDlg = nullptr;
      s_tip = nullptr;
      if (s_bg) {
        DeleteObject(s_bg);
        s_bg = nullptr;
      }
      if (s_edit_bg) {
        DeleteObject(s_edit_bg);
        s_edit_bg = nullptr;
      }
      return 0;
    default:
      break;
  }
  return DefWindowProcW(hwnd, msg, wParam, lParam);
}

HMENU BuildMenu() {
  HMENU bar = CreateMenu();
  HMENU file = CreateMenu();
  AppendMenuW(file, MF_STRING, ID_FILE_NEW_GIS, L"新建(&N)");
  AppendMenuW(file, MF_STRING, ID_FILE_OPEN_GIS, L"打开(&O)...");
  AppendMenuW(file, MF_STRING, ID_FILE_SAVE_GIS, L"保存(&S)");
  AppendMenuW(file, MF_STRING, ID_FILE_SAVE_AS_GIS, L"另存(&A)...");
  AppendMenuW(file, MF_SEPARATOR, 0, nullptr);
  AppendMenuW(file, MF_STRING, ID_FILE_SCREENSHOT, L"保存地图截图(&S)...");
  AppendMenuW(file, MF_SEPARATOR, 0, nullptr);
  AppendMenuW(file, MF_STRING, ID_FILE_EXIT, L"退出(&X)");
  AppendMenuW(bar, MF_POPUP, reinterpret_cast<UINT_PTR>(file), L"文件(&F)");

  HMENU view = CreateMenu();
  AppendMenuW(view, MF_STRING | MF_CHECKED, ID_VIEW_MODE_2D, L"2D 模式(&2)");
  AppendMenuW(view, MF_STRING, ID_VIEW_MODE_3D, L"3D 模式(&3)");
  AppendMenuW(view, MF_SEPARATOR, 0, nullptr);
  AppendMenuW(view, MF_STRING | MF_CHECKED, ID_VIEW_RENDER_GDI, L"2D 渲染：GDI(&G)");
  AppendMenuW(view, MF_STRING, ID_VIEW_RENDER_D3D11, L"2D 渲染：Direct3D 11(&D)");
  AppendMenuW(view, MF_STRING, ID_VIEW_RENDER_GL, L"2D 渲染：OpenGL(&O)");
  AppendMenuW(view, MF_SEPARATOR, 0, nullptr);
  {
    HMENU projSub = CreateMenu();
    for (int i = 0; i < static_cast<int>(MapDisplayProjection::kCount); ++i) {
      AppendMenuW(projSub, MF_STRING, ID_VIEW_PROJ_FIRST + i,
                  MapProj_MenuLabel(static_cast<MapDisplayProjection>(i)));
    }
    AppendMenuW(view, MF_POPUP, reinterpret_cast<UINT_PTR>(projSub), L"投影(&J)");
    g_hmenuProjSub = projSub;
  }
  AppendMenuW(view, MF_SEPARATOR, 0, nullptr);
  AppendMenuW(view, MF_STRING, ID_VIEW_LOG, L"日志(&L)...");
  AppendMenuW(bar, MF_POPUP, reinterpret_cast<UINT_PTR>(view), L"视图(&V)");

  HMENU win = CreateMenu();
  AppendMenuW(win, MF_STRING | MF_CHECKED, ID_WINDOW_LAYER_DOCK, L"图层 Dock(&L)");
  AppendMenuW(win, MF_STRING | MF_CHECKED, ID_WINDOW_PROPS_DOCK, L"图层属性 Dock(&P)");
  AppendMenuW(bar, MF_POPUP, reinterpret_cast<UINT_PTR>(win), L"窗口(&W)");

  HMENU layer = CreateMenu();
  AppendMenuW(layer, MF_STRING | (GIS_DESKTOP_HAVE_GDAL ? MF_ENABLED : MF_GRAYED), ID_LAYER_ADD,
              L"添加数据图层(&A)...");
  AppendMenuW(bar, MF_POPUP, reinterpret_cast<UINT_PTR>(layer), L"图层(&Y)");

  HMENU tools = CreateMenu();
  AppendMenuW(tools, MF_STRING, ID_TOOL_DATA_CONVERT, L"数据转换(&C)...");
  AppendMenuW(bar, MF_POPUP, reinterpret_cast<UINT_PTR>(tools), L"工具(&T)");

  HMENU lang = CreateMenu();
  AppendMenuW(lang, MF_STRING | MF_CHECKED, ID_LANG_ZH, L"中文(&Z)");
  AppendMenuW(lang, MF_STRING, ID_LANG_EN, L"English(&E)");
  AppendMenuW(bar, MF_POPUP, reinterpret_cast<UINT_PTR>(lang), L"语言(&L)");

  HMENU theme = CreateMenu();
  AppendMenuW(theme, MF_STRING | MF_CHECKED, ID_THEME_SYSTEM, L"跟随系统(&S)");
  AppendMenuW(theme, MF_STRING, ID_THEME_LIGHT, L"浅色(&I)");
  AppendMenuW(theme, MF_STRING, ID_THEME_DARK, L"深色(&D)");
  AppendMenuW(bar, MF_POPUP, reinterpret_cast<UINT_PTR>(theme), L"主题(&T)");

  HMENU help = CreateMenu();
  AppendMenuW(help, MF_STRING, ID_HELP_DATA_DRIVERS, L"数据驱动说明(&D)...");
  AppendMenuW(help, MF_STRING, ID_HELP_ABOUT, L"关于(&A)...");
  AppendMenuW(bar, MF_POPUP, reinterpret_cast<UINT_PTR>(help), L"帮助(&H)");

  return bar;
}

LRESULT CALLBACK StatusSubclass(HWND h, UINT m, WPARAM w, LPARAM l, UINT_PTR, DWORD_PTR) {
  if (m == WM_LBUTTONDBLCLK) {
    PostMessageW(g_hwndMain, WM_APP_SHOW_LOG, 0, 0);
    return 0;
  }
  return DefSubclassProc(h, m, w, l);
}

LRESULT CALLBACK MainProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) {
  switch (msg) {
    case WM_CREATE: {
      g_hwndMain = hwnd;
      INITCOMMONCONTROLSEX icc{sizeof(icc), ICC_BAR_CLASSES | ICC_STANDARD_CLASSES | ICC_WIN95_CLASSES};
      InitCommonControlsEx(&icc);
      g_appUiFont = CreateAppUiFont();
      if (g_appUiFont) {
        g_appUiFontOwned = true;
      } else {
        g_appUiFont = reinterpret_cast<HFONT>(GetStockObject(DEFAULT_GUI_FONT));
        g_appUiFontOwned = false;
      }
      HMENU menu = BuildMenu();
      SetMenu(hwnd, menu);

      g_hwndToolbar = CreateMainToolbar(hwnd, GetModuleHandleW(nullptr));
      if (g_hwndToolbar) {
        SetWindowSubclass(g_hwndToolbar, ToolbarWheelSubclass, 3, 0);
        SendMessageW(g_hwndToolbar, WM_SETFONT, reinterpret_cast<WPARAM>(UiGetAppFont()), TRUE);
      }

      g_hwndStatus = CreateWindowExW(0, STATUSCLASSNAMEW, L"",
                                     WS_CHILD | WS_VISIBLE | SBARS_SIZEGRIP,
                                     0, 0, 0, 0, hwnd, nullptr, GetModuleHandleW(nullptr), nullptr);
      if (g_hwndStatus) {
        SetWindowSubclass(g_hwndStatus, StatusSubclass, 1, 0);
        SendMessageW(g_hwndStatus, WM_SETFONT, reinterpret_cast<WPARAM>(UiGetAppFont()), TRUE);
      }

      {
        HINSTANCE hi = GetModuleHandleW(nullptr);
        g_hwndLayerStrip =
            CreateWindowW(L"BUTTON", L"‹", WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON | WS_TABSTOP, 0, 0, kDockStripW,
                          kDockStripBtnH, hwnd, reinterpret_cast<HMENU>(static_cast<INT_PTR>(IDC_LAYER_DOCK_STRIP_BTN)),
                          hi, nullptr);
        SendMessageW(g_hwndLayerStrip, WM_SETFONT, reinterpret_cast<WPARAM>(UiGetAppFont()), TRUE);
      }
      g_hwndLayer = CreateWindowExW(0, kLayerClass, L"",
                                    WS_CHILD | WS_VISIBLE | WS_CLIPSIBLINGS,
                                    0, 0, 100, 100, hwnd, nullptr, GetModuleHandleW(nullptr), nullptr);
      g_hwndMap = CreateWindowExW(WS_EX_CLIENTEDGE | WS_EX_COMPOSITED, kMapClass, L"",
                                  WS_CHILD | WS_VISIBLE | WS_CLIPSIBLINGS,
                                  0, 0, 100, 100, hwnd, nullptr, GetModuleHandleW(nullptr), nullptr);
      g_hwndProps = CreateWindowExW(0, kPropsClass, L"",
                                   WS_CHILD | WS_VISIBLE | WS_CLIPSIBLINGS | WS_CLIPCHILDREN,
                                   0, 0, 100, 100, hwnd, nullptr, GetModuleHandleW(nullptr), nullptr);
      {
        HINSTANCE hi = GetModuleHandleW(nullptr);
        g_hwndPropsStrip =
            CreateWindowW(L"BUTTON", L"›", WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON | WS_TABSTOP, 0, 0, kDockStripW,
                          kDockStripBtnH, hwnd, reinterpret_cast<HMENU>(static_cast<INT_PTR>(IDC_PROPS_DOCK_STRIP_BTN)),
                          hi, nullptr);
        SendMessageW(g_hwndPropsStrip, WM_SETFONT, reinterpret_cast<WPARAM>(UiGetAppFont()), TRUE);
      }

      MapEngine::Instance().RefreshLayerList(GetDlgItem(g_hwndLayer, IDC_LAYER_LIST));
      AppLogLine(L"AGIS 启动完成。");
#if GIS_DESKTOP_HAVE_GDAL
      AppLogLine(L"[GIS] GDAL 已启用：可添加数据图层。");
#else
      AppLogLine(L"[GIS] 本构建未启用 GDAL（GIS_DESKTOP_HAVE_GDAL=0）；「添加数据图层」入口已禁用。默认构建应已启用 "
                 L"GDAL（仓库含 3rdparty 源码）；若曾使用 AGIS_USE_GDAL=off，请去掉该设置后重新运行 python "
                 L"build.py，并确保 CMake 能完成 GDAL/PROJ（见 3rdparty/README-GDAL-BUILD.md）。");
#endif
      SetWindowTextW(hwnd, L"AGIS — 地图视图（单文档 SDI）");
      SyncViewMenu(hwnd);
      SyncWindowMenu(hwnd);
      LayoutChildren();
      return 0;
    }
    case WM_APP_SHOW_LOG:
      ShowLogDialog(hwnd);
      return 0;
    case WM_APP_LAYER_SEL:
      g_layerSelIndex = static_cast<int>(lParam);
      if (g_hwndProps) {
        RefreshPropsPanel(g_hwndProps);
        InvalidateRect(g_hwndProps, nullptr, FALSE);
      }
      return 0;
    case WM_SIZE:
      LayoutChildren();
      return 0;
    case WM_INITMENUPOPUP: {
      HMENU sub = reinterpret_cast<HMENU>(wParam);
      HMENU bar = GetMenu(hwnd);
      if (bar && sub == GetSubMenu(bar, 1)) {
        SyncViewMenu(hwnd);
      }
      if (bar && sub == GetSubMenu(bar, 2)) {
        SyncWindowMenu(hwnd);
      }
      break;
    }
    case WM_KEYDOWN:
      if (wParam == VK_F1) {
        ShowDataDriversHelp(hwnd);
        return 0;
      }
      break;
    case WM_MOUSEWHEEL:
      if (ForwardWheelToMapIfOver(wParam, lParam)) {
        return 0;
      }
      break;
    case WM_NOTIFY: {
      const auto* nm = reinterpret_cast<LPNMHDR>(lParam);
      if (g_hwndToolbar && nm->hwndFrom == g_hwndToolbar && nm->code == TBN_GETINFOTIP) {
        auto* tip = reinterpret_cast<NMTBGETINFOTIP*>(lParam);
        if (!tip->pszText || tip->cchTextMax < 4) {
          break;
        }
        const wchar_t* s = L"";
        switch (tip->iItem) {
          case ID_LAYER_ADD:
#if GIS_DESKTOP_HAVE_GDAL
            s = L"添加图层（先选择 GDAL / TMS / WMS） — 无全局快捷键";
#else
            s = L"添加图层（本构建未启用 GDAL，已禁用） — 无全局快捷键";
#endif
            break;
          case ID_FILE_SCREENSHOT:
            s = L"将当前地图视图保存为 PNG — 无全局快捷键";
            break;
          case ID_VIEW_LOG:
            s = L"打开日志窗口 — 无全局快捷键";
            break;
          case ID_VIEW_MODE_2D:
            s = L"2D 地图 — 无全局快捷键";
            break;
          case ID_VIEW_MODE_3D:
            s = L"3D 模式（占位） — 无全局快捷键";
            break;
          case ID_HELP_DATA_DRIVERS:
            s = L"数据驱动说明（图层类型、GDAL 格式、输入方式） — F1";
            break;
          case ID_HELP_ABOUT:
            s = L"关于 AGIS — 无全局快捷键";
            break;
          default:
            break;
        }
        if (s[0]) {
          wcsncpy_s(tip->pszText, tip->cchTextMax, s, _TRUNCATE);
        }
      }
      break;
    }
    case WM_COMMAND: {
      const int id = LOWORD(wParam);
      const int notify = HIWORD(wParam);
      if (id == IDC_LAYER_DOCK_STRIP_BTN && notify == BN_CLICKED) {
        g_layerDockExpanded = !g_layerDockExpanded;
        LayoutChildren();
        AppLogLine(g_layerDockExpanded ? L"[窗口] 已展开图层 Dock 内容区。" : L"[窗口] 已折叠图层 Dock 内容区。");
        return 0;
      }
      if (id == IDC_PROPS_DOCK_STRIP_BTN && notify == BN_CLICKED) {
        g_propsDockExpanded = !g_propsDockExpanded;
        LayoutChildren();
        AppLogLine(g_propsDockExpanded ? L"[窗口] 已展开图层属性 Dock 内容区。"
                                       : L"[窗口] 已折叠图层属性 Dock 内容区。");
        return 0;
      }
      if (id == ID_FILE_EXIT) {
        PostQuitMessage(0);
        return 0;
      }
      if (id == ID_FILE_NEW_GIS) {
        GisNew(hwnd);
        return 0;
      }
      if (id == ID_FILE_OPEN_GIS) {
        GisOpen(hwnd);
        return 0;
      }
      if (id == ID_FILE_SAVE_GIS) {
        GisSave(hwnd);
        return 0;
      }
      if (id == ID_FILE_SAVE_AS_GIS) {
        GisSaveAs(hwnd);
        return 0;
      }
      if (id == ID_FILE_SCREENSHOT) {
        MapEngine::Instance().PromptSaveMapScreenshot(hwnd, g_hwndMap);
        return 0;
      }
      if (id == ID_WINDOW_LAYER_DOCK) {
        g_showLayerDock = !g_showLayerDock;
        SyncWindowMenu(hwnd);
        LayoutChildren();
        AppLogLine(g_showLayerDock ? L"[窗口] 已显示图层 Dock。" : L"[窗口] 已隐藏图层 Dock。");
        return 0;
      }
      if (id == ID_WINDOW_PROPS_DOCK) {
        g_showPropsDock = !g_showPropsDock;
        SyncWindowMenu(hwnd);
        LayoutChildren();
        AppLogLine(g_showPropsDock ? L"[窗口] 已显示图层属性 Dock。" : L"[窗口] 已隐藏图层属性 Dock。");
        return 0;
      }
      if (id == ID_VIEW_MODE_2D) {
        g_view3d = false;
        SyncViewMenu(hwnd);
        InvalidateRect(g_hwndMap, nullptr, TRUE);
        UpdateStatusParts();
        AppLogLine(L"[视图] 已切换到 2D 模式。");
        return 0;
      }
      if (id == ID_VIEW_MODE_3D) {
        g_view3d = true;
        SyncViewMenu(hwnd);
        InvalidateRect(g_hwndMap, nullptr, TRUE);
        UpdateStatusParts();
        AppLogLine(L"[视图] 已切换到 3D 模式（占位）。");
        return 0;
      }
      if (id == ID_VIEW_RENDER_GDI || id == ID_VIEW_RENDER_D3D11 || id == ID_VIEW_RENDER_GL) {
        MapRenderBackend b = MapRenderBackend::kGdi;
        if (id == ID_VIEW_RENDER_D3D11) {
          b = MapRenderBackend::kD3d11;
        } else if (id == ID_VIEW_RENDER_GL) {
          b = MapRenderBackend::kOpenGL;
        }
        MapEngine::Instance().SetRenderBackend(b);
        SyncViewMenu(hwnd);
        AppLogLine(b == MapRenderBackend::kGdi
                       ? L"[视图] 2D 呈现：GDI。"
                       : b == MapRenderBackend::kD3d11 ? L"[视图] 2D 呈现：Direct3D 11。"
                                                       : L"[视图] 2D 呈现：OpenGL。");
        return 0;
      }
      if (id >= ID_VIEW_PROJ_FIRST && id <= ID_VIEW_PROJ_LAST) {
        const int pi = id - ID_VIEW_PROJ_FIRST;
        if (pi >= 0 && pi < static_cast<int>(MapDisplayProjection::kCount)) {
          MapEngine::Instance().Document().SetDisplayProjection(static_cast<MapDisplayProjection>(pi));
          SyncViewMenu(hwnd);
          InvalidateRect(g_hwndMap, nullptr, FALSE);
          AppLogLine(std::wstring(L"[视图] 投影：") + MapProj_MenuLabel(static_cast<MapDisplayProjection>(pi)));
          if (!MapEngine::Instance().Document().layers.empty()) {
            AppLogLine(L"[提示] 当前已有图层，视图仍以数据坐标系绘制；投影切换主要作用于无图层时的经纬网显示。");
          }
        }
        return 0;
      }
      if (id == ID_VIEW_LOG) {
        ShowLogDialog(hwnd);
        return 0;
      }
      if (id == ID_LAYER_ADD) {
#if !GIS_DESKTOP_HAVE_GDAL
        MessageBoxW(hwnd,
                    L"本程序未启用 GDAL（GIS_DESKTOP_HAVE_GDAL=0）。\n\n默认构建应已启用 GDAL（仓库含 3rdparty "
                    L"源码）。若曾设置 AGIS_USE_GDAL=off，请去掉后重新运行 python build.py；否则请检查 CMake 是否找到 "
                    L"GDAL/PROJ（见 3rdparty/README-GDAL-BUILD.md）。",
                    L"AGIS", MB_OK | MB_ICONINFORMATION);
#else
        MapEngine::Instance().OnAddLayerFromDialog(hwnd, GetDlgItem(g_hwndLayer, IDC_LAYER_LIST));
#endif
        return 0;
      }
      if (id == ID_TOOL_DATA_CONVERT) {
        ShowDataConvertWindow(hwnd);
        return 0;
      }
      if (id == ID_HELP_DATA_DRIVERS) {
        ShowDataDriversHelp(hwnd);
        return 0;
      }
      if (id == ID_HELP_ABOUT) {
        ShowAbout(hwnd);
        return 0;
      }
      if (id == ID_LANG_ZH || id == ID_LANG_EN) {
        HMENU bar = GetMenu(hwnd);
        HMENU lang = GetSubMenu(bar, 5);
        CheckMenuRadioItem(lang, ID_LANG_ZH, ID_LANG_EN, id, MF_BYCOMMAND);
        AppLogLine(id == ID_LANG_ZH ? L"[语言] 已选择：中文（界面字符串后续资源包化）"
                                    : L"[语言] Selected: English (UI pending resource bundle)");
        return 0;
      }
      if (id == ID_THEME_SYSTEM || id == ID_THEME_LIGHT || id == ID_THEME_DARK) {
        HMENU bar = GetMenu(hwnd);
        HMENU th = GetSubMenu(bar, 6);
        CheckMenuRadioItem(th, ID_THEME_SYSTEM, ID_THEME_DARK, id, MF_BYCOMMAND);
        AppLogLine(L"[主题] 切换请求已记录（完整暗色/浅色后续对接）。");
        return 0;
      }
      return 0;
    }
    case WM_SETCURSOR:
      if (LOWORD(lParam) == HTCLIENT) {
        POINT pt{};
        GetCursorPos(&pt);
        ScreenToClient(hwnd, &pt);
        RECT inner{};
        GetInnerClient(hwnd, &inner);
        if (HitLeftSplitter(pt.x, pt.y, inner.top, inner.bottom) ||
            HitRightSplitter(pt.x, pt.y, inner.top, inner.bottom)) {
          SetCursor(LoadCursor(nullptr, IDC_SIZEWE));
          return TRUE;
        }
      }
      break;
    case WM_LBUTTONDOWN: {
      POINT pt{GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam)};
      RECT inner{};
      GetInnerClient(hwnd, &inner);
      if (HitLeftSplitter(pt.x, pt.y, inner.top, inner.bottom)) {
        g_splitterDrag = 1;
        SetCapture(hwnd);
        return 0;
      }
      if (HitRightSplitter(pt.x, pt.y, inner.top, inner.bottom)) {
        g_splitterDrag = 2;
        SetCapture(hwnd);
        return 0;
      }
      break;
    }
    case WM_MOUSEMOVE:
      if (g_splitterDrag != 0) {
        POINT pt{GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam)};
        RECT client{};
        GetClientRect(hwnd, &client);
        if (g_hwndStatus) {
          RECT sr{};
          GetWindowRect(g_hwndStatus, &sr);
          client.bottom -= (sr.bottom - sr.top);
        }
        const int totalW = client.right;
        if (g_splitterDrag == 1) {
          int nw = pt.x - kSplitterW / 2;
          nw = std::max(kDockStripW + kLayerMin, std::min(nw, kDockStripW + kLayerMax));
          const int rightBlock =
              (g_showPropsDock && g_propsDockExpanded)
                  ? (g_propsContentW + kDockStripW + kSplitterW)
                  : (g_showPropsDock ? kDockStripW : 0);
          const int maxLayer = totalW - kSplitterW - kMapMin - rightBlock;
          nw = std::min(nw, std::max(kDockStripW + kLayerMin, maxLayer));
          g_layerContentW = nw - kDockStripW;
        } else if (g_splitterDrag == 2) {
          int np = totalW - pt.x - kSplitterW / 2;
          np = std::max(kDockStripW + kPropsMin, std::min(np, kDockStripW + kPropsMax));
          const int leftBlock = (g_showLayerDock && g_layerDockExpanded)
                                    ? (g_layerContentW + kDockStripW + kSplitterW)
                                    : (g_showLayerDock ? kDockStripW : 0);
          const int maxProps = totalW - leftBlock - kMapMin - kSplitterW;
          np = std::min(np, std::max(kDockStripW + kPropsMin, maxProps));
          g_propsContentW = np - kDockStripW;
        }
        LayoutChildren();
        return 0;
      }
      break;
    case WM_LBUTTONUP:
      if (g_splitterDrag != 0) {
        g_splitterDrag = 0;
        ReleaseCapture();
        return 0;
      }
      break;
    case WM_DESTROY:
      if (g_hwndToolbar) {
        RemoveWindowSubclass(g_hwndToolbar, ToolbarWheelSubclass, 3);
      }
      if (g_toolbarImageList) {
        ImageList_Destroy(g_toolbarImageList);
        g_toolbarImageList = nullptr;
      }
      if (g_appUiFontOwned && g_appUiFont) {
        DeleteObject(g_appUiFont);
        g_appUiFont = nullptr;
        g_appUiFontOwned = false;
      }
      MapEngine::Instance().Shutdown();
      PostQuitMessage(0);
      return 0;
    default:
      break;
  }
  return DefWindowProcW(hwnd, msg, wParam, lParam);
}

bool RegisterClasses(HINSTANCE inst) {
  WNDCLASSW wc{};
  wc.lpfnWndProc = MainProc;
  wc.hInstance = inst;
  wc.lpszClassName = kMainClass;
  wc.hCursor = LoadCursor(nullptr, IDC_ARROW);
  wc.hbrBackground = reinterpret_cast<HBRUSH>(COLOR_3DFACE + 1);
  if (!RegisterClassW(&wc)) {
    return false;
  }
  wc.lpfnWndProc = LayerPaneProc;
  wc.lpszClassName = kLayerClass;
  wc.hbrBackground = reinterpret_cast<HBRUSH>(COLOR_WINDOW + 1);
  if (!RegisterClassW(&wc)) {
    return false;
  }
  wc.lpfnWndProc = PropsPaneProc;
  wc.lpszClassName = kPropsClass;
  if (!RegisterClassW(&wc)) {
    return false;
  }
  wc.lpfnWndProc = MapHostProc;
  wc.lpszClassName = kMapClass;
  wc.style = CS_OWNDC;
  wc.hbrBackground = reinterpret_cast<HBRUSH>(COLOR_WINDOW + 1);
  if (!RegisterClassW(&wc)) {
    return false;
  }
  wc.lpfnWndProc = LogWndProc;
  wc.lpszClassName = kLogClass;
  wc.style = 0;
  if (!RegisterClassW(&wc)) {
    return false;
  }
  wc.lpfnWndProc = ConvertWndProc;
  wc.lpszClassName = kConvertClass;
  wc.style = 0;
  if (!RegisterClassW(&wc)) {
    return false;
  }
  wc.lpfnWndProc = ModelPreviewWndProc;
  wc.lpszClassName = kModelPreviewClass;
  wc.style = CS_OWNDC;
  wc.hbrBackground = nullptr;
  if (!RegisterClassW(&wc)) {
    return false;
  }
  return true;
}

}  // namespace

int APIENTRY wWinMain(HINSTANCE hInst, HINSTANCE, LPWSTR, int cmdShow) {
  UiGdiplusInit();
  MapEngine::Instance().Init();
  if (!RegisterClasses(hInst)) {
    UiGdiplusShutdown();
    return 1;
  }
  HWND hwnd = CreateWindowExW(
      0, kMainClass, L"AGIS — 地图视图（单文档 SDI）",
      WS_OVERLAPPEDWINDOW,
      CW_USEDEFAULT, CW_USEDEFAULT, 1280, 800,
      nullptr, nullptr, hInst, nullptr);
  if (!hwnd) {
    UiGdiplusShutdown();
    return 1;
  }
  ShowWindow(hwnd, cmdShow);
  UpdateWindow(hwnd);
  MSG msg{};
  while (GetMessageW(&msg, nullptr, 0, 0)) {
    TranslateMessage(&msg);
    DispatchMessageW(&msg);
  }
  UiGdiplusShutdown();
  return static_cast<int>(msg.wParam);
}
