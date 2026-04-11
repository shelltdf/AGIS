#include "common/app_core/main_app.h"
#include "core/main_globals.h"
#include "core/resource.h"
#include "core/app_log.h"
#include "map_engine/map_engine.h"
#include "map_engine/map_gpu.h"
#include "map_engine/map_projection.h"
#include "utils/ui_theme.h"

#include <array>
#include <cstdio>
#include <string>

#ifndef GIS_DESKTOP_HAVE_GDAL
#define GIS_DESKTOP_HAVE_GDAL 0
#endif

#ifndef PW_RENDERFULLCONTENT
#define PW_RENDERFULLCONTENT 2
#endif

namespace {

std::string WideToUtf8(const std::wstring& w) {
  if (w.empty()) {
    return {};
  }
  const int n = WideCharToMultiByte(CP_UTF8, 0, w.c_str(), static_cast<int>(w.size()), nullptr, 0, nullptr, nullptr);
  if (n <= 0) {
    return {};
  }
  std::string s(static_cast<size_t>(n), '\0');
  WideCharToMultiByte(CP_UTF8, 0, w.c_str(), static_cast<int>(w.size()), s.data(), n, nullptr, nullptr);
  return s;
}

void AppendEscapedJsonUtf8(std::string* out, const std::string& utf8) {
  if (!out) {
    return;
  }
  out->push_back('"');
  for (unsigned char c : utf8) {
    switch (c) {
      case '\\':
        *out += "\\\\";
        break;
      case '"':
        *out += "\\\"";
        break;
      case '\b':
        *out += "\\b";
        break;
      case '\f':
        *out += "\\f";
        break;
      case '\n':
        *out += "\\n";
        break;
      case '\r':
        *out += "\\r";
        break;
      case '\t':
        *out += "\\t";
        break;
      default:
        if (c < 0x20u) {
          char buf[8]{};
          std::snprintf(buf, sizeof(buf), "\\u%04x", c);
          *out += buf;
        } else {
          out->push_back(static_cast<char>(c));
        }
        break;
    }
  }
  out->push_back('"');
}

const char* ThemeMenuTag(AgisThemeMenu m) {
  switch (m) {
    case AgisThemeMenu::kFollowSystem:
      return "followSystem";
    case AgisThemeMenu::kLight:
      return "light";
    case AgisThemeMenu::kDark:
      return "dark";
    default:
      return "unknown";
  }
}

const char* RenderBackendTag(MapRenderBackend b) {
  switch (b) {
    case MapRenderBackend::kGdi:
      return "gdi";
    case MapRenderBackend::kGdiPlus:
      return "gdiPlus";
    case MapRenderBackend::kD2d:
      return "d2d";
    case MapRenderBackend::kBgfxD3d11:
      return "bgfxD3d11";
    case MapRenderBackend::kBgfxOpenGL:
      return "bgfxOpenGL";
    case MapRenderBackend::kBgfxAuto:
      return "bgfxAuto";
    default:
      return "unknown";
  }
}

}  // namespace

bool AgisCopyMainWindowScreenshotToClipboard(HWND mainHwnd) {
  if (!mainHwnd || !IsWindow(mainHwnd)) {
    return false;
  }
  RECT wr{};
  if (!GetWindowRect(mainHwnd, &wr)) {
    return false;
  }
  const int w = wr.right - wr.left;
  const int h = wr.bottom - wr.top;
  if (w <= 0 || h <= 0) {
    return false;
  }
  HDC hdcWin = GetWindowDC(mainHwnd);
  if (!hdcWin) {
    return false;
  }
  HDC hdcScr = GetDC(nullptr);
  if (!hdcScr) {
    ReleaseDC(mainHwnd, hdcWin);
    return false;
  }
  HDC hdcMem = CreateCompatibleDC(hdcScr);
  HBITMAP hbmp = CreateCompatibleBitmap(hdcScr, w, h);
  if (!hdcMem || !hbmp) {
    if (hbmp) {
      DeleteObject(hbmp);
    }
    if (hdcMem) {
      DeleteDC(hdcMem);
    }
    ReleaseDC(nullptr, hdcScr);
    ReleaseDC(mainHwnd, hdcWin);
    return false;
  }
  const HGDIOBJ oldBmp = SelectObject(hdcMem, hbmp);
  BOOL painted = PrintWindow(mainHwnd, hdcMem, PW_RENDERFULLCONTENT);
  if (!painted) {
    painted = BitBlt(hdcMem, 0, 0, w, h, hdcWin, 0, 0, SRCCOPY);
  }
  SelectObject(hdcMem, oldBmp);
  DeleteDC(hdcMem);
  ReleaseDC(nullptr, hdcScr);
  ReleaseDC(mainHwnd, hdcWin);
  if (!painted) {
    DeleteObject(hbmp);
    return false;
  }
  if (!OpenClipboard(mainHwnd)) {
    DeleteObject(hbmp);
    return false;
  }
  EmptyClipboard();
  const HANDLE setOk = SetClipboardData(CF_BITMAP, hbmp);
  CloseClipboard();
  if (!setOk) {
    DeleteObject(hbmp);
    return false;
  }
  return true;
}

void AgisCopyWorkbenchUiStateJsonToClipboard(HWND mainHwnd) {
  MapEngine& eng = MapEngine::Instance();
  const MapDocument& doc = eng.Document();
  std::string j;
  j.reserve(4096);
  j += "{\n";
  j += "  \"agisWorkbenchUi\": \"1.0\",\n";

  j += "  \"window\": {\n";
  if (mainHwnd && IsWindow(mainHwnd)) {
    std::array<wchar_t, 512> title{};
    GetWindowTextW(mainHwnd, title.data(), static_cast<int>(title.size()));
    j += "    \"title\": ";
    AppendEscapedJsonUtf8(&j, WideToUtf8(title.data()));
    j += ",\n";
    RECT cr{};
    GetClientRect(mainHwnd, &cr);
    j += "    \"clientWidth\": " + std::to_string(cr.right - cr.left) + ",\n";
    j += "    \"clientHeight\": " + std::to_string(cr.bottom - cr.top) + ",\n";
    RECT wr{};
    GetWindowRect(mainHwnd, &wr);
    j += "    \"windowLeft\": " + std::to_string(wr.left) + ",\n";
    j += "    \"windowTop\": " + std::to_string(wr.top) + ",\n";
    j += "    \"windowRight\": " + std::to_string(wr.right) + ",\n";
    j += "    \"windowBottom\": " + std::to_string(wr.bottom) + ",\n";
    WINDOWPLACEMENT wpl{};
    wpl.length = sizeof(wpl);
    if (GetWindowPlacement(mainHwnd, &wpl)) {
      j += "    \"showCmd\": " + std::to_string(static_cast<int>(wpl.showCmd)) + ",\n";
    }
  }
  j += "    \"toolbarHeight\": " + std::to_string(g_toolbarHeight) + "\n";
  j += "  },\n";

  j += "  \"docks\": {\n";
  j += "    \"view3dMode\": " + std::string(g_view3d ? "true" : "false") + ",\n";
  j += "    \"showLayerDock\": " + std::string(g_showLayerDock ? "true" : "false") + ",\n";
  j += "    \"showPropsDock\": " + std::string(g_showPropsDock ? "true" : "false") + ",\n";
  j += "    \"layerDockExpanded\": " + std::string(g_layerDockExpanded ? "true" : "false") + ",\n";
  j += "    \"propsDockExpanded\": " + std::string(g_propsDockExpanded ? "true" : "false") + ",\n";
  j += "    \"layerContentWidth\": " + std::to_string(g_layerContentW) + ",\n";
  j += "    \"propsContentWidth\": " + std::to_string(g_propsContentW) + ",\n";
  j += "    \"selectedLayerIndex\": " + std::to_string(g_layerSelIndex) + "\n";
  j += "  },\n";

  j += "  \"document\": {\n";
  j += "    \"currentGisPath\": ";
  AppendEscapedJsonUtf8(&j, WideToUtf8(g_currentGisPath));
  j += "\n  },\n";

  j += "  \"theme\": {\n";
  j += "    \"menuSelection\": \"";
  j += ThemeMenuTag(g_themeMenu);
  j += "\",\n";
  j += "    \"effectiveDarkUi\": " + std::string(AgisEffectiveUiDark() ? "true" : "false") + "\n";
  j += "  },\n";

  j += "  \"map\": {\n";
  j += "    \"renderBackend\": \"";
  j += RenderBackendTag(eng.GetRenderBackend());
  j += "\",\n";
  j += "    \"layerCount\": " + std::to_string(eng.GetLayerCount()) + ",\n";
  j += "    \"showLatLonGrid\": " + std::string(doc.GetShowLatLonGrid() ? "true" : "false") + ",\n";
  j += "    \"displayProjection\": ";
  AppendEscapedJsonUtf8(&j, WideToUtf8(MapProj_MenuLabel(doc.GetDisplayProjection())));
  j += ",\n";
  j += "    \"displayProjectionIndex\": " + std::to_string(static_cast<int>(doc.GetDisplayProjection())) + ",\n";
  j += "    \"scalePercentUi\": " + std::to_string(doc.ScalePercentForUi()) + ",\n";
  j += "    \"refViewWidthDeg\": " + std::to_string(doc.refViewWidthDeg) + ",\n";
  j += "    \"refViewHeightDeg\": " + std::to_string(doc.refViewHeightDeg) + ",\n";
  j += "    \"viewExtent\": {\n";
  j += "      \"minX\": " + std::to_string(doc.view.minX) + ",\n";
  j += "      \"minY\": " + std::to_string(doc.view.minY) + ",\n";
  j += "      \"maxX\": " + std::to_string(doc.view.maxX) + ",\n";
  j += "      \"maxY\": " + std::to_string(doc.view.maxY) + "\n";
  j += "    },\n";
  j += "    \"mapHostVisible\": " + std::string((g_hwndMap && IsWindow(g_hwndMap)) ? "true" : "false") + ",\n";
  j += "    \"mapShellVisible\": " + std::string((g_hwndMapShell && IsWindow(g_hwndMapShell)) ? "true" : "false") + ",\n";
  j += "    \"uiOverlay\": {\n";
  j += "      \"showShortcutChrome\": " + std::string(eng.IsMapUiShowShortcutChrome() ? "true" : "false") + ",\n";
  j += "      \"shortcutHelpExpanded\": " + std::string(eng.IsMapShortcutHelpExpanded() ? "true" : "false") + ",\n";
  j += "      \"showVisChrome\": " + std::string(eng.IsMapUiShowVisChrome() ? "true" : "false") + ",\n";
  j += "      \"visibilityPanelExpanded\": " + std::string(eng.IsMapVisibilityPanelExpanded() ? "true" : "false") + ",\n";
  j += "      \"showBottomChrome\": " + std::string(eng.IsMapUiShowBottomChrome() ? "true" : "false") + ",\n";
  j += "      \"showHintOverlay\": " + std::string(eng.IsMapUiShowHintOverlay() ? "true" : "false") + "\n";
  j += "    }\n";
  j += "  },\n";

  j += "  \"layers\": [\n";
  for (size_t i = 0; i < doc.layers.size(); ++i) {
    if (i > 0) {
      j += ",\n";
    }
    j += "    {\n";
    j += "      \"index\": " + std::to_string(i) + ",\n";
    j += "      \"displayName\": ";
    AppendEscapedJsonUtf8(&j, WideToUtf8(doc.layers[i]->DisplayName()));
    j += ",\n";
    j += "      \"driverKind\": ";
    AppendEscapedJsonUtf8(&j, WideToUtf8(MapLayerDriverKindLabel(doc.layers[i]->DriverKind())));
    j += ",\n";
    j += "      \"visible\": " + std::string(doc.layers[i]->IsLayerVisible() ? "true" : "false") + ",\n";
    j += "      \"sourcePathForSave\": ";
    AppendEscapedJsonUtf8(&j, WideToUtf8(doc.layers[i]->SourcePathForSave()));
    j += "\n";
    j += "    }";
  }
  j += "\n  ],\n";

  j += "  \"build\": {\n";
  j += "    \"gisDesktopHaveGdal\": " + std::string(GIS_DESKTOP_HAVE_GDAL ? "true" : "false") + "\n";
  j += "  }\n";
  j += "}\n";

  const std::wstring wjson = [&j]() -> std::wstring {
    const int nw = MultiByteToWideChar(CP_UTF8, 0, j.c_str(), static_cast<int>(j.size()), nullptr, 0);
    if (nw <= 0) {
      return {};
    }
    std::wstring w(static_cast<size_t>(nw), L'\0');
    MultiByteToWideChar(CP_UTF8, 0, j.c_str(), static_cast<int>(j.size()), w.data(), nw);
    return w;
  }();

  if (wjson.empty()) {
    AppLogLine(L"[调试] 界面信息 JSON 生成失败（编码）。");
    return;
  }
  CopyTextToClipboard(mainHwnd, wjson);
  AppLogLine(L"[调试] 已复制界面状态 JSON 到剪贴板（UTF-16 文本）。");
}
