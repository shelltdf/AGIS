#include <algorithm>
#include <string>

#include <windows.h>
#include <windowsx.h>
#include <commctrl.h>
#include <shellapi.h>

#ifndef GIS_DESKTOP_HAVE_GDAL
#define GIS_DESKTOP_HAVE_GDAL 0
#endif

#include "workbench/help_data_drivers.h"
#include "core/resource.h"
#include "debug/ui_debug_pick.h"
#include "utils/ui_font.h"
#include "utils/agis_ui_l10n.h"
#include "utils/ui_theme.h"
#include "core/app_log.h"
#include "common/app_core/main_app.h"
#include "core/main_globals.h"
#include "map_engine/map_engine.h"
#include "map_engine/map_projection.h"
#include "ui_engine/gdiplus_ui.h"

#pragma comment(lib, "comctl32.lib")
#pragma comment(lib, "uxtheme.lib")
#pragma comment(lib, "shell32.lib")

namespace {

const wchar_t* ProjMenuLabelForUi(MapDisplayProjection p) {
  return AgisGetUiLanguage() == AgisUiLanguage::kEn ? MapProj_MenuLabelEn(p) : MapProj_MenuLabel(p);
}

}  // namespace

HMENU BuildMenu() {
  HMENU bar = CreateMenu();
  HMENU file = CreateMenu();
  AppendMenuW(file, MF_STRING, ID_FILE_NEW_GIS, AgisTr(AgisUiStr::MenuFileNew));
  AppendMenuW(file, MF_STRING, ID_FILE_OPEN_GIS, AgisTr(AgisUiStr::MenuFileOpen));
  AppendMenuW(file, MF_STRING, ID_FILE_SAVE_GIS, AgisTr(AgisUiStr::MenuFileSave));
  AppendMenuW(file, MF_STRING, ID_FILE_SAVE_AS_GIS, AgisTr(AgisUiStr::MenuFileSaveAs));
  AppendMenuW(file, MF_SEPARATOR, 0, nullptr);
  AppendMenuW(file, MF_STRING, ID_FILE_SCREENSHOT, AgisTr(AgisUiStr::MenuFileScreenshot));
  AppendMenuW(file, MF_SEPARATOR, 0, nullptr);
  AppendMenuW(file, MF_STRING, ID_FILE_EXIT, AgisTr(AgisUiStr::MenuFileExit));
  AppendMenuW(bar, MF_POPUP, reinterpret_cast<UINT_PTR>(file), AgisTr(AgisUiStr::MenuFile));

  HMENU view = CreateMenu();
  AppendMenuW(view, MF_STRING | MF_CHECKED, ID_VIEW_MODE_2D, AgisTr(AgisUiStr::MenuView2d));
  AppendMenuW(view, MF_STRING, ID_VIEW_MODE_3D, AgisTr(AgisUiStr::MenuView3d));
  AppendMenuW(view, MF_SEPARATOR, 0, nullptr);
  {
    HMENU renderSub = CreateMenu();
    AppendMenuW(renderSub, MF_STRING | MF_CHECKED, ID_VIEW_RENDER_GDI, AgisTr(AgisUiStr::MenuRenderGdi));
    AppendMenuW(renderSub, MF_STRING, ID_VIEW_RENDER_GDIPLUS, AgisTr(AgisUiStr::MenuRenderGdiPlus));
    AppendMenuW(renderSub, MF_STRING, ID_VIEW_RENDER_D2D, AgisTr(AgisUiStr::MenuRenderD2d));
    AppendMenuW(renderSub, MF_SEPARATOR, 0, nullptr);
    AppendMenuW(renderSub, MF_STRING, ID_VIEW_RENDER_BGFX_D3D11, AgisTr(AgisUiStr::MenuRenderBgfxD3d11));
    AppendMenuW(renderSub, MF_STRING, ID_VIEW_RENDER_BGFX_OPENGL, AgisTr(AgisUiStr::MenuRenderBgfxGl));
    AppendMenuW(renderSub, MF_STRING, ID_VIEW_RENDER_BGFX_AUTO, AgisTr(AgisUiStr::MenuRenderBgfxAuto));
    AppendMenuW(view, MF_POPUP, reinterpret_cast<UINT_PTR>(renderSub), AgisTr(AgisUiStr::MenuViewRenderRoot));
    g_hmenuRenderSub = renderSub;
  }
  AppendMenuW(view, MF_SEPARATOR, 0, nullptr);
  {
    HMENU projSub = CreateMenu();
    for (int i = 0; i < static_cast<int>(MapDisplayProjection::kCount); ++i) {
      AppendMenuW(projSub, MF_STRING, ID_VIEW_PROJ_FIRST + i,
                  ProjMenuLabelForUi(static_cast<MapDisplayProjection>(i)));
    }
    AppendMenuW(view, MF_POPUP, reinterpret_cast<UINT_PTR>(projSub), AgisTr(AgisUiStr::MenuViewProjRoot));
    g_hmenuProjSub = projSub;
  }
  AppendMenuW(view, MF_SEPARATOR, 0, nullptr);
  {
    HMENU mapUiSub = CreateMenu();
    AppendMenuW(mapUiSub, MF_STRING, IDC_MAP_UI_SHOW_SHORTCUT, AgisTr(AgisUiStr::MenuMapShortcut));
    AppendMenuW(mapUiSub, MF_STRING, IDC_MAP_UI_SHOW_VIS, AgisTr(AgisUiStr::MenuMapVis));
    AppendMenuW(mapUiSub, MF_STRING, IDC_MAP_UI_SHOW_BOTTOM, AgisTr(AgisUiStr::MenuMapBottom));
    AppendMenuW(mapUiSub, MF_STRING, IDC_MAP_UI_SHOW_HINT, AgisTr(AgisUiStr::MenuMapHint));
    AppendMenuW(mapUiSub, MF_SEPARATOR, 0, nullptr);
    AppendMenuW(mapUiSub, MF_STRING, IDC_MAP_UI_GRID, AgisTr(AgisUiStr::MenuMapGrid));
    AppendMenuW(view, MF_POPUP, reinterpret_cast<UINT_PTR>(mapUiSub), AgisTr(AgisUiStr::MenuMapChromeRoot));
    g_hmenuMapUiSub = mapUiSub;
  }
  AppendMenuW(view, MF_SEPARATOR, 0, nullptr);
  AppendMenuW(view, MF_STRING, ID_VIEW_LOG, AgisTr(AgisUiStr::MenuViewLog));
  AppendMenuW(bar, MF_POPUP, reinterpret_cast<UINT_PTR>(view), AgisTr(AgisUiStr::MenuView));

  HMENU win = CreateMenu();
  AppendMenuW(win, MF_STRING | MF_CHECKED, ID_WINDOW_LAYER_DOCK, AgisTr(AgisUiStr::MenuWinLayerDock));
  AppendMenuW(win, MF_STRING | MF_CHECKED, ID_WINDOW_PROPS_DOCK, AgisTr(AgisUiStr::MenuWinPropsDock));
  AppendMenuW(bar, MF_POPUP, reinterpret_cast<UINT_PTR>(win), AgisTr(AgisUiStr::MenuWindow));

  HMENU layer = CreateMenu();
  AppendMenuW(layer, MF_STRING | (GIS_DESKTOP_HAVE_GDAL ? MF_ENABLED : MF_GRAYED), ID_LAYER_ADD,
              AgisTr(AgisUiStr::MenuLayerAdd));
  AppendMenuW(bar, MF_POPUP, reinterpret_cast<UINT_PTR>(layer), AgisTr(AgisUiStr::MenuLayer));

  HMENU tools = CreateMenu();
  AppendMenuW(tools, MF_STRING, ID_TOOL_DATA_CONVERT, AgisTr(AgisUiStr::MenuToolsConvert));
  AppendMenuW(bar, MF_POPUP, reinterpret_cast<UINT_PTR>(tools), AgisTr(AgisUiStr::MenuTools));

  HMENU lang = CreateMenu();
  AppendMenuW(lang, MF_STRING | (AgisGetUiLanguage() == AgisUiLanguage::kZh ? MF_CHECKED : MF_UNCHECKED), ID_LANG_ZH,
              AgisTr(AgisUiStr::MenuLangZh));
  AppendMenuW(lang, MF_STRING | (AgisGetUiLanguage() == AgisUiLanguage::kEn ? MF_CHECKED : MF_UNCHECKED), ID_LANG_EN,
              AgisTr(AgisUiStr::MenuLangEn));
  AppendMenuW(bar, MF_POPUP, reinterpret_cast<UINT_PTR>(lang), AgisTr(AgisUiStr::MenuLang));

  HMENU theme = CreateMenu();
  AppendMenuW(theme, MF_STRING | MF_CHECKED, ID_THEME_SYSTEM, AgisTr(AgisUiStr::MenuThemeSystem));
  AppendMenuW(theme, MF_STRING, ID_THEME_LIGHT, AgisTr(AgisUiStr::MenuThemeLight));
  AppendMenuW(theme, MF_STRING, ID_THEME_DARK, AgisTr(AgisUiStr::MenuThemeDark));
  AppendMenuW(bar, MF_POPUP, reinterpret_cast<UINT_PTR>(theme), AgisTr(AgisUiStr::MenuTheme));

  HMENU help = CreateMenu();
  AppendMenuW(help, MF_STRING, ID_HELP_DATA_DRIVERS, AgisTr(AgisUiStr::MenuHelpDrivers));
  AppendMenuW(help, MF_STRING, ID_HELP_ABOUT, AgisTr(AgisUiStr::MenuHelpAbout));
  AppendMenuW(bar, MF_POPUP, reinterpret_cast<UINT_PTR>(help), AgisTr(AgisUiStr::MenuHelp));

  HMENU debug = CreateMenu();
  AppendMenuW(debug, MF_STRING, ID_DEBUG_CLIPBOARD_SCREENSHOT, AgisTr(AgisUiStr::MenuDebugClip));
  AppendMenuW(debug, MF_STRING, ID_DEBUG_COPY_UI_JSON, AgisTr(AgisUiStr::MenuDebugJson));
  AppendMenuW(bar, MF_POPUP, reinterpret_cast<UINT_PTR>(debug), AgisTr(AgisUiStr::MenuDebug));

  return bar;
}

void AgisReapplyWorkbenchMenu(HWND hwnd) {
  HMENU neu = BuildMenu();
  HMENU old = GetMenu(hwnd);
  SetMenu(hwnd, neu);
  if (old) {
    DestroyMenu(old);
  }
  DrawMenuBar(hwnd);
  SyncViewMenu(hwnd);
  SyncWindowMenu(hwnd);
  SyncMainTitle();
  UpdateStatusParts();
  if (g_hwndToolbar) {
    InvalidateRect(g_hwndToolbar, nullptr, TRUE);
  }
  AgisApplyTheme(hwnd);
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
      AgisLoadUiLanguagePreference();
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
      g_hwndMapShell = CreateWindowExW(0, kMapShellClass, L"",
                                       WS_CHILD | WS_CLIPCHILDREN | WS_CLIPSIBLINGS | WS_VISIBLE, 0, 0, 120, 120, hwnd,
                                       nullptr, GetModuleHandleW(nullptr), nullptr);
      if (!g_hwndMapShell || !g_hwndMap) {
        return -1;
      }
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
      AppLogLine(AgisTr(AgisUiStr::LogStartup));
#if GIS_DESKTOP_HAVE_GDAL
      AppLogLine(AgisTr(AgisUiStr::LogGdalOn));
#else
      AppLogLine(AgisTr(AgisUiStr::LogGdalOff));
#endif
      SyncMainTitle();
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
        AppLogLine(AgisTr(AgisUiStr::LogThemeFollowSystem));
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
      if (g_hmenuRenderSub && sub == g_hmenuRenderSub) {
        SyncViewMenu(hwnd);
      }
      if (g_hmenuMapUiSub && sub == g_hmenuMapUiSub) {
        SyncMainFrameMapUiMenu(sub);
      }
      if (bar && sub == GetSubMenu(bar, 2)) {
        SyncWindowMenu(hwnd);
      }
      if (bar && sub == GetSubMenu(bar, 5)) {
        const UINT lid = AgisGetUiLanguage() == AgisUiLanguage::kEn ? ID_LANG_EN : ID_LANG_ZH;
        CheckMenuRadioItem(sub, ID_LANG_ZH, ID_LANG_EN, lid, MF_BYCOMMAND);
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
          case ID_FILE_NEW_GIS:
            s = AgisTr(AgisUiStr::TipFileNew);
            break;
          case ID_FILE_OPEN_GIS:
            s = AgisTr(AgisUiStr::TipFileOpen);
            break;
          case ID_FILE_SAVE_GIS:
            s = AgisTr(AgisUiStr::TipFileSave);
            break;
          case ID_FILE_SAVE_AS_GIS:
            s = AgisTr(AgisUiStr::TipFileSaveAs);
            break;
          case ID_LAYER_ADD:
#if GIS_DESKTOP_HAVE_GDAL
            s = AgisTr(AgisUiStr::TipLayerAdd);
#else
            s = AgisTr(AgisUiStr::TipLayerAddNoGdal);
#endif
            break;
          case ID_FILE_SCREENSHOT:
            s = AgisTr(AgisUiStr::TipScreenshot);
            break;
          case ID_VIEW_LOG:
            s = AgisTr(AgisUiStr::TipLog);
            break;
          case ID_VIEW_MODE_2D:
            s = AgisTr(AgisUiStr::Tip2d);
            break;
          case ID_VIEW_MODE_3D:
            s = AgisTr(AgisUiStr::Tip3d);
            break;
          case ID_HELP_DATA_DRIVERS:
            s = AgisTr(AgisUiStr::TipHelpDrivers);
            break;
          case ID_HELP_ABOUT:
            s = AgisTr(AgisUiStr::TipAbout);
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
        AppLogLine(AgisTr(g_layerDockExpanded ? AgisUiStr::LogDockLayerExpanded : AgisUiStr::LogDockLayerCollapsed));
        return 0;
      }
      if (id == IDC_PROPS_DOCK_STRIP_BTN && notify == BN_CLICKED) {
        g_propsDockExpanded = !g_propsDockExpanded;
        LayoutChildren();
        AppLogLine(AgisTr(g_propsDockExpanded ? AgisUiStr::LogDockPropsExpanded : AgisUiStr::LogDockPropsCollapsed));
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
        AppLogLine(AgisTr(g_showLayerDock ? AgisUiStr::LogWinLayerShown : AgisUiStr::LogWinLayerHidden));
        return 0;
      }
      if (id == ID_WINDOW_PROPS_DOCK) {
        g_showPropsDock = !g_showPropsDock;
        SyncWindowMenu(hwnd);
        LayoutChildren();
        AppLogLine(AgisTr(g_showPropsDock ? AgisUiStr::LogWinPropsShown : AgisUiStr::LogWinPropsHidden));
        return 0;
      }
      if (id == ID_VIEW_MODE_2D) {
        g_view3d = false;
        SyncViewMenu(hwnd);
        InvalidateRect(g_hwndMap, nullptr, TRUE);
        UpdateStatusParts();
        AppLogLine(AgisTr(AgisUiStr::LogView2dOn));
        return 0;
      }
      if (id == ID_VIEW_MODE_3D) {
        g_view3d = true;
        SyncViewMenu(hwnd);
        InvalidateRect(g_hwndMap, nullptr, TRUE);
        UpdateStatusParts();
        AppLogLine(AgisTr(AgisUiStr::LogView3dOn));
        return 0;
      }
      if (id >= ID_VIEW_RENDER_FIRST && id <= ID_VIEW_RENDER_LAST) {
        MapRenderBackend b = MapRenderBackend::kGdi;
        if (id == ID_VIEW_RENDER_GDIPLUS) {
          b = MapRenderBackend::kGdiPlus;
        } else if (id == ID_VIEW_RENDER_D2D) {
          b = MapRenderBackend::kD2d;
        } else if (id == ID_VIEW_RENDER_BGFX_D3D11) {
          b = MapRenderBackend::kBgfxD3d11;
        } else if (id == ID_VIEW_RENDER_BGFX_OPENGL) {
          b = MapRenderBackend::kBgfxOpenGL;
        } else if (id == ID_VIEW_RENDER_BGFX_AUTO) {
          b = MapRenderBackend::kBgfxAuto;
        }
        MapEngine::Instance().SetRenderBackend(b);
        SyncViewMenu(hwnd);
        return 0;
      }
      if (id >= ID_VIEW_PROJ_FIRST && id <= ID_VIEW_PROJ_LAST) {
        const int pi = id - ID_VIEW_PROJ_FIRST;
        if (pi >= 0 && pi < static_cast<int>(MapDisplayProjection::kCount)) {
          MapEngine::Instance().Document().SetDisplayProjection(static_cast<MapDisplayProjection>(pi));
          SyncViewMenu(hwnd);
          InvalidateRect(g_hwndMap, nullptr, FALSE);
          AppLogLine(std::wstring(AgisTr(AgisUiStr::LogViewProjPrefix)) +
                     ProjMenuLabelForUi(static_cast<MapDisplayProjection>(pi)));
          if (!MapEngine::Instance().Document().layers.empty()) {
            AppLogLine(AgisTr(AgisUiStr::LogProjLayersHint));
          }
        }
        return 0;
      }
      if (id == ID_VIEW_LOG) {
        ShowLogDialog(hwnd);
        return 0;
      }
      if (id >= IDC_MAP_UI_SHOW_SHORTCUT && id <= IDC_MAP_UI_GRID && notify == 0) {
        if (g_hwndMap && IsWindow(g_hwndMap)) {
          SendMessageW(g_hwndMap, WM_COMMAND, wParam, lParam);
        }
        return 0;
      }
      if (id == ID_LAYER_ADD) {
#if !GIS_DESKTOP_HAVE_GDAL
        MessageBoxW(hwnd, AgisTr(AgisUiStr::MsgGdalOffBody), AgisTr(AgisUiStr::MsgGdalOffTitle),
                    MB_OK | MB_ICONINFORMATION);
#else
        MapEngine::Instance().OnAddLayerFromDialog(hwnd, GetDlgItem(g_hwndLayer, IDC_LAYER_LIST));
#endif
        return 0;
      }
      if (id == ID_TOOL_DATA_CONVERT) {
        if (!AgisLaunchSiblingToolExe(hwnd, L"AGIS-Convert.exe", nullptr)) {
          MessageBoxW(hwnd,
                      AgisPickUiLang(L"无法启动 AGIS-Convert.exe。\n请确认其与 AGIS.exe 位于同一输出目录。",
                                     L"Could not start AGIS-Convert.exe.\nMake sure it sits next to AGIS.exe in the same output folder."),
                      AgisTr(AgisUiStr::MenuToolsConvert), MB_OK | MB_ICONWARNING);
        }
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
      if (id == ID_DEBUG_CLIPBOARD_SCREENSHOT) {
        if (AgisCopyMainWindowScreenshotToClipboard(hwnd)) {
          AppLogLine(AgisTr(AgisUiStr::DebugClipOk));
        } else {
          AppLogLine(AgisTr(AgisUiStr::DebugClipFail));
        }
        return 0;
      }
      if (id == ID_DEBUG_COPY_UI_JSON) {
        AgisCopyWorkbenchUiStateJsonToClipboard(hwnd);
        return 0;
      }
      if (id == ID_LANG_ZH || id == ID_LANG_EN) {
        AgisSetUiLanguage(id == ID_LANG_EN ? AgisUiLanguage::kEn : AgisUiLanguage::kZh);
        AgisSaveUiLanguagePreference();
        AgisReapplyWorkbenchMenu(hwnd);
        ApplyWorkbenchPanelsL10n();
        if (g_hwndMap && IsWindow(g_hwndMap)) {
          MapEngine::Instance().ApplyMapHostUiLanguage(g_hwndMap);
        }
        if (g_hwndLayer && IsWindow(g_hwndLayer)) {
          if (HWND lb = GetDlgItem(g_hwndLayer, IDC_LAYER_LIST)) {
            MapEngine::Instance().RefreshLayerList(lb);
          }
        }
        AppLogLine(AgisTr(id == ID_LANG_ZH ? AgisUiStr::LogLangZh : AgisUiStr::LogLangEn));
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
        AppLogLine(AgisTr(AgisEffectiveUiDark() ? AgisUiStr::ThemeAppliedDark : AgisUiStr::ThemeAppliedLight));
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
      if (g_hwndMapShell && IsWindow(g_hwndMapShell)) {
        DestroyWindow(g_hwndMapShell);
      }
      g_hwndMapShell = nullptr;
      g_hwndMap = nullptr;
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
  // 勿用 CS_OWNDC：与 D2D HWND RenderTarget / bgfx 子窗呈现叠用易导致 resize 闪烁或错一帧。
  wc.style = 0;
  wc.hbrBackground = reinterpret_cast<HBRUSH>(COLOR_WINDOW + 1);
  if (!RegisterClassW(&wc)) {
    return false;
  }
  wc.lpfnWndProc = MapShellProc;
  wc.lpszClassName = kMapShellClass;
  wc.style = 0;
  wc.hbrBackground = reinterpret_cast<HBRUSH>(COLOR_3DFACE + 1);
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

static std::wstring WorkbenchCliGisPathFromCommandLine() {
  int argc = 0;
  LPWSTR* argv = CommandLineToArgvW(GetCommandLineW(), &argc);
  if (!argv) {
    return {};
  }
  auto freeArgv = [&]() { LocalFree(argv); };
  std::wstring out;
  for (int i = 1; i < argc; ++i) {
    const wchar_t* a = argv[i];
    if (!a || a[0] == L'\0') {
      continue;
    }
    if (a[0] == L'/' || (a[0] == L'-' && a[1] == L'-')) {
      continue;
    }
    const std::wstring p(a);
    const size_t dot = p.find_last_of(L'.');
    if (dot == std::wstring::npos) {
      continue;
    }
    const wchar_t* ext = p.c_str() + dot;
    if (_wcsicmp(ext, L".gis") == 0 || _wcsicmp(ext, L".xml") == 0) {
      out = p;
      break;
    }
  }
  freeArgv();
  return out;
}

int APIENTRY wWinMain(HINSTANCE hInst, HINSTANCE, LPWSTR, int cmdShow) {
  const std::wstring cliGisPath = WorkbenchCliGisPathFromCommandLine();
  AgisLoadUiLanguagePreference();
  UiGdiplusInit();
  MapEngine::Instance().Init();
  if (!RegisterClasses(hInst)) {
    UiGdiplusShutdown();
    return 1;
  }
  HWND hwnd = CreateWindowExW(
      0, kMainClass, AgisTr(AgisUiStr::WinTitleNoDoc),
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
  if (!cliGisPath.empty()) {
    GisOpenFromPath(hwnd, cliGisPath);
  }
  AgisUiDebugPickInit(hInst);
  MSG msg{};
  while (GetMessageW(&msg, nullptr, 0, 0) > 0) {
    if (AgisUiDebugPickHandleMessage(&msg)) {
      continue;
    }
    TranslateMessage(&msg);
    DispatchMessageW(&msg);
  }
  AgisUiDebugPickShutdown();
  UiGdiplusShutdown();
  return static_cast<int>(msg.wParam);
}
