#include <windows.h>
#include <windowsx.h>
#include <commctrl.h>
#include <shellapi.h>
#include <algorithm>
#include <string>

#include "app/resource.h"
#include "app/ui_font.h"
#include "core/app_log.h"
#include "map/map_engine.h"
#include "map/map_projection.h"
#include "ui/gdiplus_ui.h"

#pragma comment(lib, "comctl32.lib")

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
  const MapRenderBackend rb = MapEngine_GetRenderBackend();
  UINT rid = ID_VIEW_RENDER_GDI;
  if (rb == MapRenderBackend::kD3d11) {
    rid = ID_VIEW_RENDER_D3D11;
  } else if (rb == MapRenderBackend::kOpenGL) {
    rid = ID_VIEW_RENDER_GL;
  }
  CheckMenuRadioItem(view, ID_VIEW_RENDER_GDI, ID_VIEW_RENDER_GL, rid, MF_BYCOMMAND);
  if (g_hmenuProjSub) {
    const int pi = static_cast<int>(MapEngine_Document().GetDisplayProjection());
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
  MapEngine_RefreshLayerList(listbox);
  const int n = MapEngine_GetLayerCount();
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
    if (MapEngine_OnLayerListClick(hwnd, x, y)) {
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
    const int n = MapEngine_GetLayerCount();
    const bool onLayer = (n > 0 && hit >= 0 && hit < n);
    const bool canUp = onLayer && hit > 0;
    const bool canDown = onLayer && hit < n - 1;

    HMENU pop = CreatePopupMenu();
    AppendMenuW(pop, MF_STRING, ID_LAYER_CTX_ADD, L"添加图层…");
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
        MapEngine_OnAddLayerFromDialog(mainFr, hwnd);
        {
          const int nn = MapEngine_GetLayerCount();
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
      case ID_LAYER_CTX_DELETE: {
        if (!onLayer) {
          return 0;
        }
        std::wstring err;
        if (!MapEngine_Document().RemoveLayerAt(static_cast<size_t>(hit), err)) {
          if (!err.empty()) {
            MessageBoxW(mainFr, err.c_str(), L"AGIS", MB_OK | MB_ICONWARNING);
          }
          return 0;
        }
        AppLogLine(L"[图层] 已删除所选图层。");
        {
          const int nn = MapEngine_GetLayerCount();
          const int newSel = (nn > 0) ? std::min(hit, nn - 1) : -1;
          LayerListSyncUiAfterOp(hwnd, mainFr, newSel);
        }
        return 0;
      }
      case ID_LAYER_CTX_UP: {
        if (!canUp) {
          return 0;
        }
        MapEngine_Document().MoveLayerUp(static_cast<size_t>(hit));
        AppLogLine(L"[图层] 已上移。");
        LayerListSyncUiAfterOp(hwnd, mainFr, hit - 1);
        return 0;
      }
      case ID_LAYER_CTX_DOWN: {
        if (!canDown) {
          return 0;
        }
        MapEngine_Document().MoveLayerDown(static_cast<size_t>(hit));
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
        MapEngine_MeasureLayerListItem(mis);
        return TRUE;
      }
      break;
    }
    case WM_DRAWITEM: {
      const DRAWITEMSTRUCT* dis = reinterpret_cast<LPDRAWITEMSTRUCT>(lParam);
      if (dis && dis->CtlID == IDC_LAYER_LIST) {
        MapEngine_PaintLayerListItem(dis);
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
  MapEngine_GetLayerInfoForUi(g_layerSelIndex, &title, &driverTxt, &sourceTxt);
  g_propsLayerSubtitleForPaint = title;
  if (edDriver) {
    SetWindowTextW(edDriver, driverTxt.c_str());
  }
  if (edSrc) {
    SetWindowTextW(edSrc, sourceTxt.c_str());
  }
  const bool raster = MapEngine_IsRasterGdalLayer(g_layerSelIndex);
  const bool layerOk = g_layerSelIndex >= 0 && g_layerSelIndex < MapEngine_GetLayerCount();
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
        if (MapEngine_BuildOverviewsForLayer(g_layerSelIndex, err)) {
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
        if (MapEngine_ClearOverviewsForLayer(g_layerSelIndex, err)) {
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
        if (MapEngine_ReplaceLayerSourceFromUi(owner, lb, g_layerSelIndex)) {
          MapEngine_RefreshLayerList(lb);
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

HMENU BuildMenu() {
  HMENU bar = CreateMenu();
  HMENU file = CreateMenu();
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
  AppendMenuW(layer, MF_STRING, ID_LAYER_ADD, L"添加数据图层(&A)...");
  AppendMenuW(bar, MF_POPUP, reinterpret_cast<UINT_PTR>(layer), L"图层(&Y)");

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

      MapEngine_RefreshLayerList(GetDlgItem(g_hwndLayer, IDC_LAYER_LIST));
      AppLogLine(L"AGIS 启动完成。");
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
            s = L"添加图层（先选择 GDAL / TMS / WMS） — 无全局快捷键";
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
      if (id == ID_FILE_SCREENSHOT) {
        MapEngine_PromptSaveMapScreenshot(hwnd, g_hwndMap);
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
        MapEngine_SetRenderBackend(b);
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
          MapEngine_Document().SetDisplayProjection(static_cast<MapDisplayProjection>(pi));
          SyncViewMenu(hwnd);
          InvalidateRect(g_hwndMap, nullptr, FALSE);
          AppLogLine(std::wstring(L"[视图] 投影：") + MapProj_MenuLabel(static_cast<MapDisplayProjection>(pi)));
          if (!MapEngine_Document().layers.empty()) {
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
        MapEngine_OnAddLayerFromDialog(hwnd, GetDlgItem(g_hwndLayer, IDC_LAYER_LIST));
        return 0;
      }
      if (id == ID_HELP_ABOUT) {
        ShowAbout(hwnd);
        return 0;
      }
      if (id == ID_LANG_ZH || id == ID_LANG_EN) {
        HMENU bar = GetMenu(hwnd);
        HMENU lang = GetSubMenu(bar, 4);
        CheckMenuRadioItem(lang, ID_LANG_ZH, ID_LANG_EN, id, MF_BYCOMMAND);
        AppLogLine(id == ID_LANG_ZH ? L"[语言] 已选择：中文（界面字符串后续资源包化）"
                                    : L"[语言] Selected: English (UI pending resource bundle)");
        return 0;
      }
      if (id == ID_THEME_SYSTEM || id == ID_THEME_LIGHT || id == ID_THEME_DARK) {
        HMENU bar = GetMenu(hwnd);
        HMENU th = GetSubMenu(bar, 5);
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
      MapEngine_Shutdown();
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
  return true;
}

}  // namespace

int APIENTRY wWinMain(HINSTANCE hInst, HINSTANCE, LPWSTR, int cmdShow) {
  UiGdiplusInit();
  MapEngine_Init();
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
