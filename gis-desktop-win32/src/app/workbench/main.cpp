#include <algorithm>
#include <string>

#include <windows.h>
#include <windowsx.h>
#include <commctrl.h>

#ifndef GIS_DESKTOP_HAVE_GDAL
#define GIS_DESKTOP_HAVE_GDAL 0
#endif

#include "workbench/help_data_drivers.h"
#include "common/app_core/resource.h"
#include "common/ui/ui_debug_pick.h"
#include "common/ui/ui_font.h"
#include "common/ui/ui_theme.h"
#include "core/app_log.h"
#include "common/app_core/main_app.h"
#include "common/app_core/main_globals.h"
#include "map_engine/map_engine.h"
#include "map_engine/map_projection.h"
#include "ui_engine/gdiplus_ui.h"

#pragma comment(lib, "comctl32.lib")
#pragma comment(lib, "uxtheme.lib")

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
      UiFontInit();
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
      g_hwndProps =
          CreateWindowExW(0, kPropsClass, L"", WS_CHILD | WS_VISIBLE | WS_CLIPSIBLINGS | WS_CLIPCHILDREN, 0, 0, 100, 100,
                          hwnd, nullptr, GetModuleHandleW(nullptr), nullptr);
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
      AgisLoadThemePreference();
      AgisApplyTheme(hwnd);
      return 0;
    }
    case WM_ERASEBKGND: {
      HDC hdc = reinterpret_cast<HDC>(wParam);
      if (!hdc) {
        break;
      }
      RECT rc{};
      GetClientRect(hwnd, &rc);
      HBRUSH br = AgisMainClientBackgroundBrush();
      if (!br) {
        br = reinterpret_cast<HBRUSH>(GetStockObject(COLOR_3DFACE + 1));
      }
      FillRect(hdc, &rc, br);
      return 1;
    }
    case WM_SETTINGCHANGE:
      if (lParam && wcscmp(reinterpret_cast<const wchar_t*>(lParam), L"ImmersiveColorSet") == 0 &&
          g_themeMenu == AgisThemeMenu::kFollowSystem) {
        AgisApplyTheme(hwnd);
        AppLogLine(L"[主题] 已随系统颜色设置更新（跟随系统）。");
      }
      break;
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
        if (id == ID_THEME_SYSTEM) {
          g_themeMenu = AgisThemeMenu::kFollowSystem;
        } else if (id == ID_THEME_LIGHT) {
          g_themeMenu = AgisThemeMenu::kLight;
        } else {
          g_themeMenu = AgisThemeMenu::kDark;
        }
        AgisSaveThemePreference();
        AgisApplyTheme(hwnd);
        AppLogLine(AgisEffectiveUiDark() ? L"[主题] 已应用深色界面。" : L"[主题] 已应用浅色界面。");
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
          nw = (std::max)(kDockStripW + kLayerMin, (std::min)(nw, kDockStripW + kLayerMax));
          const int rightBlock =
              (g_showPropsDock && g_propsDockExpanded)
                  ? (g_propsContentW + kDockStripW + kSplitterW)
                  : (g_showPropsDock ? kDockStripW : 0);
          const int maxLayer = totalW - kSplitterW - kMapMin - rightBlock;
          nw = (std::min)(nw, (std::max)(kDockStripW + kLayerMin, maxLayer));
          g_layerContentW = nw - kDockStripW;
        } else if (g_splitterDrag == 2) {
          int np = totalW - pt.x - kSplitterW / 2;
          np = (std::max)(kDockStripW + kPropsMin, (std::min)(np, kDockStripW + kPropsMax));
          const int leftBlock = (g_showLayerDock && g_layerDockExpanded)
                                    ? (g_layerContentW + kDockStripW + kSplitterW)
                                    : (g_showLayerDock ? kDockStripW : 0);
          const int maxProps = totalW - leftBlock - kMapMin - kSplitterW;
          np = (std::min)(np, (std::max)(kDockStripW + kPropsMin, maxProps));
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
      UiFontShutdown();
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
  wc.lpfnWndProc = TilePreviewWndProc;
  wc.lpszClassName = kTilePreviewClass;
  wc.style = 0;
  // 避免 WM_ERASEBKGND 先擦除再 WM_PAINT 导致的 XYZ/瓦片预览闪烁；擦除在 WM_PAINT 内与内容一并绘制
  wc.hbrBackground = nullptr;
  if (!RegisterClassW(&wc)) {
    return false;
  }
  return true;
}

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
  AgisCenterWindowInMonitorWorkArea(hwnd, nullptr);
  ShowWindow(hwnd, cmdShow);
  UpdateWindow(hwnd);
  AgisUiDebugPickInit(hInst);
  MSG msg{};
  msg.wParam = 0;
  bool quit = false;
  while (!quit) {
    if (HWND pw = FindWindowW(kModelPreviewClass, nullptr); pw && IsWindow(pw)) {
      ModelPreviewPumpPriorityLoadMessages(pw);
    }
    while (PeekMessageW(&msg, nullptr, 0, 0, PM_REMOVE)) {
      if (msg.message == WM_QUIT) {
        quit = true;
        break;
      }
      if (AgisUiDebugPickHandleMessage(&msg)) {
        continue;
      }
      TranslateMessage(&msg);
      DispatchMessageW(&msg);
    }
    if (quit) {
      break;
    }
    HWND pw = FindWindowW(kModelPreviewClass, nullptr);
    if (pw && IsWindow(pw)) {
      ModelPreviewFrameStep(pw);
    }
    if (!PeekMessageW(&msg, nullptr, 0, 0, PM_NOREMOVE)) {
      (void)MsgWaitForMultipleObjectsEx(0, nullptr, 50, QS_ALLINPUT, 0);
    }
  }
  AgisUiDebugPickShutdown();
  UiGdiplusShutdown();
  return static_cast<int>(msg.wParam);
}
