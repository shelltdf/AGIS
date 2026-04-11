#include <algorithm>
#include <windows.h>
#include <windowsx.h>
#include <commctrl.h>
#include <shellapi.h>

#include "core/resource.h"
#include "utils/ui_font.h"
#include "common/app_core/main_app.h"
#include "core/main_globals.h"
#include "map_engine/map_engine.h"
#include "map_engine/map_gpu.h"
#include "map_engine/map_projection.h"

#include <cstring>

/** WM_MOUSEWHEEL 发往焦点窗口；焦点在工具栏/侧栏时转发到地图，且 lParam 为屏幕坐标。 */
bool ForwardWheelToMapIfOver(WPARAM wParam, LPARAM lParam) {
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
  if (rb == MapRenderBackend::kGdiPlus) {
    rid = ID_VIEW_RENDER_GDIPLUS;
  } else if (rb == MapRenderBackend::kD2d) {
    rid = ID_VIEW_RENDER_D2D;
  } else if (rb == MapRenderBackend::kBgfxD3d11) {
    rid = ID_VIEW_RENDER_BGFX_D3D11;
  } else if (rb == MapRenderBackend::kBgfxOpenGL) {
    rid = ID_VIEW_RENDER_BGFX_OPENGL;
  } else if (rb == MapRenderBackend::kBgfxAuto) {
    rid = ID_VIEW_RENDER_BGFX_AUTO;
  }
  CheckMenuRadioItem(view, ID_VIEW_RENDER_FIRST, ID_VIEW_RENDER_LAST, rid, MF_BYCOMMAND);
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
  if (g_hwndMapShell) {
    if (mapW <= 0 || innerH <= 0) {
      ShowWindow(g_hwndMapShell, SW_HIDE);
    } else {
      auto menuBarH = [](HWND ref) -> int {
        using GetSmfw = int(WINAPI*)(HWND, int);
        static GetSmfw pfn = reinterpret_cast<GetSmfw>(GetProcAddress(GetModuleHandleW(L"user32.dll"), "GetSystemMetricsForWindow"));
        if (pfn && ref) {
          return pfn(ref, SM_CYMENU);
        }
        return GetSystemMetrics(SM_CYMENU);
      };
      const int mb = menuBarH(g_hwndMain);
      const int mapClientH = (std::max)(1, innerH - mb);
      RECT rc{0, 0, mapW, mapClientH};
      const DWORD popStyle = WS_POPUP | WS_CLIPCHILDREN | WS_CLIPSIBLINGS;
      AdjustWindowRectEx(&rc, popStyle, TRUE, WS_EX_TOOLWINDOW);
      const int ow = rc.right - rc.left;
      const int oh = rc.bottom - rc.top;
      POINT pt{mapLeft, top};
      ClientToScreen(g_hwndMain, &pt);
      SetWindowPos(g_hwndMapShell, nullptr, pt.x, pt.y, ow, oh, SWP_NOZORDER | SWP_NOACTIVATE);
      ShowWindow(g_hwndMapShell, SW_SHOW);
      DrawMenuBar(g_hwndMapShell);
    }
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
  SendMessageW(tb, TB_SETBUTTONSIZE, 0, MAKELPARAM(30, 28));

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

HMENU BuildMapHostMenu() {
  HMENU bar = CreateMenu();
  if (!bar) {
    return nullptr;
  }
  HMENU render = CreateMenu();
  if (!render) {
    DestroyMenu(bar);
    return nullptr;
  }
  AppendMenuW(render, MF_STRING, ID_VIEW_RENDER_GDI, L"GDI");
  AppendMenuW(render, MF_STRING, ID_VIEW_RENDER_GDIPLUS, L"GDI+");
  AppendMenuW(render, MF_STRING, ID_VIEW_RENDER_D2D, L"Direct2D");
  AppendMenuW(render, MF_SEPARATOR, 0, nullptr);
  AppendMenuW(render, MF_STRING, ID_VIEW_RENDER_BGFX_D3D11, L"Bgfx + D3D11");
  AppendMenuW(render, MF_STRING, ID_VIEW_RENDER_BGFX_OPENGL, L"Bgfx + OpenGL");
  AppendMenuW(render, MF_STRING, ID_VIEW_RENDER_BGFX_AUTO, L"Bgfx 自动");
  AppendMenuW(bar, MF_POPUP, reinterpret_cast<UINT_PTR>(render), L"渲染(&R)");

  HMENU view = CreateMenu();
  if (!view) {
    DestroyMenu(bar);
    return nullptr;
  }
  AppendMenuW(view, MF_STRING, IDC_MAP_UI_SHOW_SHORTCUT, L"快捷键区");
  AppendMenuW(view, MF_STRING, IDC_MAP_UI_SHOW_VIS, L"可见性与经纬网");
  AppendMenuW(view, MF_STRING, IDC_MAP_UI_SHOW_BOTTOM, L"底部缩放与适应栏");
  AppendMenuW(view, MF_STRING, IDC_MAP_UI_SHOW_HINT, L"操作提示文字");
  AppendMenuW(view, MF_SEPARATOR, 0, nullptr);
  AppendMenuW(view, MF_STRING, IDC_MAP_UI_GRID, L"绘制经纬网");
  AppendMenuW(bar, MF_POPUP, reinterpret_cast<UINT_PTR>(view), L"界面(&V)");
  return bar;
}

void SyncMapHostMenuPopup(HWND mapShell, HMENU popup) {
  HMENU bar = GetMenu(mapShell);
  if (!bar || !popup) {
    return;
  }
  HMENU sm0 = GetSubMenu(bar, 0);
  HMENU sm1 = GetSubMenu(bar, 1);
  MapEngine& eg = MapEngine::Instance();
  if (popup == sm0) {
    const MapRenderBackend cur = MapGpu_GetActiveBackend();
    UINT rid = ID_VIEW_RENDER_GDI;
    if (cur == MapRenderBackend::kGdiPlus) {
      rid = ID_VIEW_RENDER_GDIPLUS;
    } else if (cur == MapRenderBackend::kD2d) {
      rid = ID_VIEW_RENDER_D2D;
    } else if (cur == MapRenderBackend::kBgfxD3d11) {
      rid = ID_VIEW_RENDER_BGFX_D3D11;
    } else if (cur == MapRenderBackend::kBgfxOpenGL) {
      rid = ID_VIEW_RENDER_BGFX_OPENGL;
    } else if (cur == MapRenderBackend::kBgfxAuto) {
      rid = ID_VIEW_RENDER_BGFX_AUTO;
    }
    CheckMenuRadioItem(popup, ID_VIEW_RENDER_FIRST, ID_VIEW_RENDER_LAST, rid, MF_BYCOMMAND);
  } else if (popup == sm1) {
    CheckMenuItem(popup, IDC_MAP_UI_SHOW_SHORTCUT,
                  MF_BYCOMMAND | (eg.IsMapUiShowShortcutChrome() ? MF_CHECKED : MF_UNCHECKED));
    CheckMenuItem(popup, IDC_MAP_UI_SHOW_VIS,
                  MF_BYCOMMAND | (eg.IsMapUiShowVisChrome() ? MF_CHECKED : MF_UNCHECKED));
    CheckMenuItem(popup, IDC_MAP_UI_SHOW_BOTTOM,
                  MF_BYCOMMAND | (eg.IsMapUiShowBottomChrome() ? MF_CHECKED : MF_UNCHECKED));
    CheckMenuItem(popup, IDC_MAP_UI_SHOW_HINT,
                  MF_BYCOMMAND | (eg.IsMapUiShowHintOverlay() ? MF_CHECKED : MF_UNCHECKED));
    CheckMenuItem(popup, IDC_MAP_UI_GRID,
                  MF_BYCOMMAND | (eg.Document().GetShowLatLonGrid() ? MF_CHECKED : MF_UNCHECKED));
  }
}

void LayoutMapShellClient(HWND mapShell) {
  if (!g_hwndMap || !IsWindow(g_hwndMap)) {
    return;
  }
  RECT cr{};
  GetClientRect(mapShell, &cr);
  const int cw = std::max(0, static_cast<int>(cr.right));
  const int ch = std::max(0, static_cast<int>(cr.bottom));
  MoveWindow(g_hwndMap, 0, 0, cw, ch, TRUE);
}

LRESULT CALLBACK MapShellProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) {
  switch (msg) {
    case WM_CREATE: {
      auto* cs = reinterpret_cast<LPCREATESTRUCT>(lParam);
      HINSTANCE inst = cs->hInstance;
      HMENU menu = BuildMapHostMenu();
      if (menu) {
        SetMenu(hwnd, menu);
      }
      g_hwndMap = CreateWindowExW(WS_EX_CLIENTEDGE | WS_EX_COMPOSITED, kMapClass, L"",
                                  WS_CHILD | WS_VISIBLE | WS_CLIPSIBLINGS, 0, 0, 200, 200, hwnd, nullptr, inst,
                                  nullptr);
      if (!g_hwndMap) {
        return -1;
      }
      LayoutMapShellClient(hwnd);
      return 0;
    }
    case WM_DESTROY: {
      HMENU m = GetMenu(hwnd);
      SetMenu(hwnd, nullptr);
      if (m) {
        DestroyMenu(m);
      }
      g_hwndMap = nullptr;
      g_hwndMapShell = nullptr;
      return 0;
    }
    case WM_SIZE:
      LayoutMapShellClient(hwnd);
      return 0;
    case WM_INITMENUPOPUP:
      SyncMapHostMenuPopup(hwnd, reinterpret_cast<HMENU>(wParam));
      break;
    case WM_COMMAND:
      if (HIWORD(wParam) == 0 && lParam == 0 && g_hwndMap && IsWindow(g_hwndMap)) {
        SendMessageW(g_hwndMap, WM_COMMAND, wParam, 0);
        return 0;
      }
      break;
    default:
      break;
  }
  return DefWindowProcW(hwnd, msg, wParam, lParam);
}
