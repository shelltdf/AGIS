#include <algorithm>
#include <string>
#include <windows.h>
#include <windowsx.h>
#include <commctrl.h>

#ifndef GIS_DESKTOP_HAVE_GDAL
#define GIS_DESKTOP_HAVE_GDAL 0
#endif

#include "core/resource.h"
#include "utils/agis_ui_l10n.h"
#include "utils/ui_font.h"
#include "utils/ui_theme.h"
#include "core/app_log.h"
#include "common/app_core/main_app.h"
#include "core/main_globals.h"
#include "map_engine/map_engine.h"
#include "ui_engine/gdiplus_ui.h"

static std::wstring g_propsLayerSubtitleForPaint;
static RECT g_propsDriverCardRc{};
static RECT g_propsSourceCardRc{};

static UiDockChromeText MakeWorkbenchDockChrome() {
  return {AgisTr(AgisUiStr::DockLayerTitle),
          AgisTr(AgisUiStr::DockLayerSubtitle),
          AgisTr(AgisUiStr::DockPropsTitle),
          AgisTr(AgisUiStr::DockPropsSubtitleDefault),
          AgisTr(AgisUiStr::DockChipRight),
          AgisTr(AgisUiStr::CardDriverTitle),
          AgisTr(AgisUiStr::CardDriverSubtitle),
          AgisTr(AgisUiStr::CardSourceTitle),
          AgisTr(AgisUiStr::CardSourceSubtitle)};
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
    AppendMenuW(pop, MF_STRING | (GIS_DESKTOP_HAVE_GDAL ? MF_ENABLED : MF_GRAYED), ID_LAYER_CTX_ADD,
                AgisTr(AgisUiStr::CtxAddLayer));
    AppendMenuW(pop, MF_SEPARATOR, 0, nullptr);
    AppendMenuW(pop, MF_STRING | (onLayer ? MF_ENABLED : MF_GRAYED), ID_LAYER_CTX_DELETE, AgisTr(AgisUiStr::CtxDelete));
    AppendMenuW(pop, MF_STRING | (canUp ? MF_ENABLED : MF_GRAYED), ID_LAYER_CTX_UP, AgisTr(AgisUiStr::CtxUp));
    AppendMenuW(pop, MF_STRING | (canDown ? MF_ENABLED : MF_GRAYED), ID_LAYER_CTX_DOWN, AgisTr(AgisUiStr::CtxDown));

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
        AppLogLine(AgisTr(AgisUiStr::LogLayerCtxAdd));
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
        AppLogLine(AgisTr(AgisUiStr::LogLayerDeleted));
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
        AppLogLine(AgisTr(AgisUiStr::LogLayerMovedUp));
        LayerListSyncUiAfterOp(hwnd, mainFr, hit - 1);
        return 0;
      }
      case ID_LAYER_CTX_DOWN: {
        if (!canDown) {
          return 0;
        }
        MapEngine::Instance().Document().MoveLayerDown(static_cast<size_t>(hit));
        AppLogLine(AgisTr(AgisUiStr::LogLayerMovedDown));
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
      UiDockChromeText chrome = MakeWorkbenchDockChrome();
      UiPaintLayerPanel(hdc, r, &chrome);
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

namespace {

static HBRUSH g_propsStaticBgLight = nullptr;
static HBRUSH g_propsStaticBgDark = nullptr;
static HBRUSH g_propsEditBgLight = nullptr;
static HBRUSH g_propsEditBgDark = nullptr;
static HBRUSH g_propsBtnBgLight = nullptr;
static HBRUSH g_propsBtnBgDark = nullptr;

}  // namespace

LRESULT CALLBACK PropsPaneProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) {
  switch (msg) {
    case WM_MOUSEWHEEL:
      if (ForwardWheelToMapIfOver(wParam, lParam)) {
        return 0;
      }
      break;
    case WM_CREATE: {
      HINSTANCE inst = GetModuleHandleW(nullptr);
      CreateWindowW(L"STATIC", AgisTr(AgisUiStr::CardDriverTitle), WS_CHILD | SS_LEFT, 12, 56, 100, 18, hwnd,
                    reinterpret_cast<HMENU>(static_cast<INT_PTR>(IDC_PROPS_LBL_DRIVER)), inst, nullptr);
      CreateWindowExW(
          WS_EX_CLIENTEDGE, L"EDIT", L"",
          WS_CHILD | WS_VISIBLE | ES_MULTILINE | ES_READONLY | ES_AUTOVSCROLL | WS_VSCROLL | WS_TABSTOP, 12, 78, 100,
          80, hwnd, reinterpret_cast<HMENU>(static_cast<INT_PTR>(IDC_PROPS_DRIVER_EDIT)), inst, nullptr);
      CreateWindowW(L"STATIC", AgisTr(AgisUiStr::CardSourceTitle), WS_CHILD | SS_LEFT, 12, 164, 100, 18, hwnd,
                    reinterpret_cast<HMENU>(static_cast<INT_PTR>(IDC_PROPS_LBL_SOURCE)), inst, nullptr);
      CreateWindowExW(
          WS_EX_CLIENTEDGE, L"EDIT", L"",
          WS_CHILD | WS_VISIBLE | ES_MULTILINE | ES_READONLY | ES_AUTOVSCROLL | WS_VSCROLL | WS_TABSTOP, 12, 186, 100,
          80, hwnd, reinterpret_cast<HMENU>(static_cast<INT_PTR>(IDC_PROPS_SOURCE_EDIT)), inst, nullptr);
      CreateWindowW(L"BUTTON", AgisTr(AgisUiStr::BtnBuildPyramid), WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON | WS_TABSTOP,
                    12, 0, 90, 28, hwnd, reinterpret_cast<HMENU>(static_cast<INT_PTR>(IDC_PROPS_BUILD_OV)), inst,
                    nullptr);
      CreateWindowW(L"BUTTON", AgisTr(AgisUiStr::BtnClearPyramid), WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON | WS_TABSTOP,
                    108, 0, 90, 28, hwnd, reinterpret_cast<HMENU>(static_cast<INT_PTR>(IDC_PROPS_CLEAR_OV)), inst,
                    nullptr);
      CreateWindowW(L"BUTTON", AgisTr(AgisUiStr::BtnChangeSource), WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON | WS_TABSTOP,
                    204, 0, 120, 28, hwnd, reinterpret_cast<HMENU>(static_cast<INT_PTR>(IDC_PROPS_CHANGE_SRC)), inst,
                    nullptr);
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
          AppLogLine(AgisTr(AgisUiStr::LogOvrBuilt));
          if (g_hwndMap) {
            InvalidateRect(g_hwndMap, nullptr, FALSE);
          }
          RefreshPropsPanel(hwnd);
        } else {
          AppLogLine(std::wstring(AgisTr(AgisUiStr::LogErrOvrBuildPrefix)) + err);
          MessageBoxW(owner, err.c_str(), L"AGIS", MB_OK | MB_ICONERROR);
        }
        return 0;
      }
      if (id == IDC_PROPS_CLEAR_OV) {
        std::wstring err;
        if (MapEngine::Instance().ClearOverviewsForLayer(g_layerSelIndex, err)) {
          AppLogLine(AgisTr(AgisUiStr::LogOvrCleared));
          if (g_hwndMap) {
            InvalidateRect(g_hwndMap, nullptr, FALSE);
          }
          RefreshPropsPanel(hwnd);
        } else {
          AppLogLine(std::wstring(AgisTr(AgisUiStr::LogErrOvrClearPrefix)) + err);
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
    case WM_CTLCOLORSTATIC: {
      HDC hdc = reinterpret_cast<HDC>(wParam);
      const bool dark = AgisEffectiveUiDark();
      SetBkMode(hdc, OPAQUE);
      if (dark) {
        if (!g_propsStaticBgDark) {
          g_propsStaticBgDark = CreateSolidBrush(RGB(44, 46, 52));
        }
        SetBkColor(hdc, RGB(44, 46, 52));
        SetTextColor(hdc, RGB(220, 222, 228));
        return reinterpret_cast<LRESULT>(g_propsStaticBgDark);
      }
      if (!g_propsStaticBgLight) {
        g_propsStaticBgLight = CreateSolidBrush(GetSysColor(COLOR_3DFACE));
      }
      SetBkColor(hdc, GetSysColor(COLOR_3DFACE));
      SetTextColor(hdc, RGB(30, 30, 30));
      return reinterpret_cast<LRESULT>(g_propsStaticBgLight);
    }
    case WM_CTLCOLOREDIT: {
      HDC hdc = reinterpret_cast<HDC>(wParam);
      const bool dark = AgisEffectiveUiDark();
      SetBkMode(hdc, OPAQUE);
      if (dark) {
        if (!g_propsEditBgDark) {
          g_propsEditBgDark = CreateSolidBrush(RGB(52, 54, 60));
        }
        SetBkColor(hdc, RGB(52, 54, 60));
        SetTextColor(hdc, RGB(230, 230, 235));
        return reinterpret_cast<LRESULT>(g_propsEditBgDark);
      }
      if (!g_propsEditBgLight) {
        g_propsEditBgLight = CreateSolidBrush(RGB(255, 255, 255));
      }
      SetBkColor(hdc, RGB(255, 255, 255));
      SetTextColor(hdc, RGB(28, 36, 52));
      return reinterpret_cast<LRESULT>(g_propsEditBgLight);
    }
    case WM_CTLCOLORBTN: {
      HDC hdc = reinterpret_cast<HDC>(wParam);
      const bool dark = AgisEffectiveUiDark();
      SetBkMode(hdc, OPAQUE);
      if (dark) {
        if (!g_propsBtnBgDark) {
          g_propsBtnBgDark = CreateSolidBrush(RGB(56, 58, 64));
        }
        SetBkColor(hdc, RGB(56, 58, 64));
        SetTextColor(hdc, RGB(230, 230, 235));
        return reinterpret_cast<LRESULT>(g_propsBtnBgDark);
      }
      if (!g_propsBtnBgLight) {
        g_propsBtnBgLight = CreateSolidBrush(GetSysColor(COLOR_BTNFACE));
      }
      SetBkColor(hdc, GetSysColor(COLOR_BTNFACE));
      SetTextColor(hdc, GetSysColor(COLOR_BTNTEXT));
      return reinterpret_cast<LRESULT>(g_propsBtnBgLight);
    }
    case WM_PAINT: {
      PAINTSTRUCT ps{};
      HDC hdc = BeginPaint(hwnd, &ps);
      RECT r{};
      GetClientRect(hwnd, &r);
      UiDockChromeText chrome = MakeWorkbenchDockChrome();
      UiPaintLayerPropsDockFrame(hdc, r, &g_propsDriverCardRc, &g_propsSourceCardRc,
                                 g_propsLayerSubtitleForPaint.c_str(), &chrome);
      EndPaint(hwnd, &ps);
      return 0;
    }
    default:
      return DefWindowProcW(hwnd, msg, wParam, lParam);
  }
  return DefWindowProcW(hwnd, msg, wParam, lParam);
}

namespace {

static HBRUSH g_logDlgBgLight = nullptr;
static HBRUSH g_logDlgBgDark = nullptr;
static HBRUSH g_logEditBgLight = nullptr;
static HBRUSH g_logEditBgDark = nullptr;
static HBRUSH g_logBtnBgLight = nullptr;
static HBRUSH g_logBtnBgDark = nullptr;

}  // namespace

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
      CreateWindowW(L"BUTTON", AgisTr(AgisUiStr::BtnCopyAll), WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON, bx, by, 120, 28,
                    hwnd, reinterpret_cast<HMENU>(static_cast<INT_PTR>(IDC_LOG_COPY)), GetModuleHandleW(nullptr),
                    nullptr);
      AppLogSetEdit(ed);
      AppLogFlushToEdit();
      if (ed) {
        SendMessageW(ed, WM_SETFONT, reinterpret_cast<WPARAM>(UiGetLogFont()), TRUE);
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
    case WM_CTLCOLORDLG: {
      if (AgisEffectiveUiDark()) {
        if (!g_logDlgBgDark) {
          g_logDlgBgDark = CreateSolidBrush(RGB(40, 42, 48));
        }
        return reinterpret_cast<LRESULT>(g_logDlgBgDark);
      }
      if (!g_logDlgBgLight) {
        g_logDlgBgLight = CreateSolidBrush(GetSysColor(COLOR_3DFACE));
      }
      return reinterpret_cast<LRESULT>(g_logDlgBgLight);
    }
    case WM_CTLCOLOREDIT: {
      HDC hdc = reinterpret_cast<HDC>(wParam);
      SetBkMode(hdc, OPAQUE);
      if (AgisEffectiveUiDark()) {
        if (!g_logEditBgDark) {
          g_logEditBgDark = CreateSolidBrush(RGB(52, 54, 60));
        }
        SetBkColor(hdc, RGB(52, 54, 60));
        SetTextColor(hdc, RGB(230, 230, 235));
        return reinterpret_cast<LRESULT>(g_logEditBgDark);
      }
      if (!g_logEditBgLight) {
        g_logEditBgLight = CreateSolidBrush(RGB(255, 255, 255));
      }
      SetBkColor(hdc, RGB(255, 255, 255));
      SetTextColor(hdc, RGB(28, 36, 52));
      return reinterpret_cast<LRESULT>(g_logEditBgLight);
    }
    case WM_CTLCOLORBTN: {
      HDC hdc = reinterpret_cast<HDC>(wParam);
      SetBkMode(hdc, OPAQUE);
      if (AgisEffectiveUiDark()) {
        if (!g_logBtnBgDark) {
          g_logBtnBgDark = CreateSolidBrush(RGB(56, 58, 64));
        }
        SetBkColor(hdc, RGB(56, 58, 64));
        SetTextColor(hdc, RGB(230, 230, 235));
        return reinterpret_cast<LRESULT>(g_logBtnBgDark);
      }
      if (!g_logBtnBgLight) {
        g_logBtnBgLight = CreateSolidBrush(GetSysColor(COLOR_BTNFACE));
      }
      SetBkColor(hdc, GetSysColor(COLOR_BTNFACE));
      SetTextColor(hdc, GetSysColor(COLOR_BTNTEXT));
      return reinterpret_cast<LRESULT>(g_logBtnBgLight);
    }
    case WM_COMMAND:
      if (LOWORD(wParam) == IDC_LOG_COPY) {
        HWND edit = GetDlgItem(hwnd, IDC_LOG_EDIT);
        const int n = GetWindowTextLengthW(edit);
        std::wstring buf(static_cast<size_t>(n) + 1, L'\0');
        GetWindowTextW(edit, buf.data(), n + 1);
        buf.resize(static_cast<size_t>(n));
        CopyTextToClipboard(hwnd, buf);
        AppLogLine(AgisTr(AgisUiStr::LogClipboardOk));
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
    SetWindowTextW(g_hwndLogDlg, AgisTr(AgisUiStr::WinLogTitle));
    if (HWND bt = GetDlgItem(g_hwndLogDlg, IDC_LOG_COPY)) {
      SetWindowTextW(bt, AgisTr(AgisUiStr::BtnCopyAll));
    }
    HWND ed = GetDlgItem(g_hwndLogDlg, IDC_LOG_EDIT);
    if (ed) {
      SetWindowTextW(ed, AppLogGetText().c_str());
    }
    return;
  }
  g_hwndLogDlg =
      CreateWindowExW(WS_EX_DLGMODALFRAME, kLogClass, AgisTr(AgisUiStr::WinLogTitle),
                      WS_POPUP | WS_CAPTION | WS_SYSMENU | WS_SIZEBOX | WS_MINIMIZEBOX, CW_USEDEFAULT,
                      CW_USEDEFAULT, 640, 420, owner, nullptr, GetModuleHandleW(nullptr), nullptr);
  if (g_hwndLogDlg) {
    AgisCenterWindowInMonitorWorkArea(g_hwndLogDlg, owner ? owner : g_hwndMain);
    ShowWindow(g_hwndLogDlg, SW_SHOW);
  }
}

void ShowAbout(HWND owner) {
  (void)owner;
  AppLogLine(AgisTr(AgisUiStr::AboutLine1));
  AppLogLine(AgisTr(AgisUiStr::AboutLine2));
  AppLogLine(AgisTr(AgisUiStr::AboutLine3));
}

void ApplyWorkbenchPanelsL10n() {
  if (g_hwndProps && IsWindow(g_hwndProps)) {
    if (HWND b = GetDlgItem(g_hwndProps, IDC_PROPS_BUILD_OV)) {
      SetWindowTextW(b, AgisTr(AgisUiStr::BtnBuildPyramid));
    }
    if (HWND b = GetDlgItem(g_hwndProps, IDC_PROPS_CLEAR_OV)) {
      SetWindowTextW(b, AgisTr(AgisUiStr::BtnClearPyramid));
    }
    if (HWND b = GetDlgItem(g_hwndProps, IDC_PROPS_CHANGE_SRC)) {
      SetWindowTextW(b, AgisTr(AgisUiStr::BtnChangeSource));
    }
    if (HWND s = GetDlgItem(g_hwndProps, IDC_PROPS_LBL_DRIVER)) {
      SetWindowTextW(s, AgisTr(AgisUiStr::CardDriverTitle));
    }
    if (HWND s = GetDlgItem(g_hwndProps, IDC_PROPS_LBL_SOURCE)) {
      SetWindowTextW(s, AgisTr(AgisUiStr::CardSourceTitle));
    }
    // 驱动/数据源说明在 EDIT 与 GDI+ 副标题中；须按当前语言从 MapEngine 重取（否则切换语言后仍显示旧文案）。
    RefreshPropsPanel(g_hwndProps);
  }
  if (g_hwndLogDlg && IsWindow(g_hwndLogDlg)) {
    SetWindowTextW(g_hwndLogDlg, AgisTr(AgisUiStr::WinLogTitle));
    if (HWND b = GetDlgItem(g_hwndLogDlg, IDC_LOG_COPY)) {
      SetWindowTextW(b, AgisTr(AgisUiStr::BtnCopyAll));
    }
  }
  if (g_hwndLayer && IsWindow(g_hwndLayer)) {
    InvalidateRect(g_hwndLayer, nullptr, FALSE);
  }
}
