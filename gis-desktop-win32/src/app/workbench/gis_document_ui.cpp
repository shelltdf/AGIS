#include <string>

#include <windows.h>

#include "core/resource.h"
#include "common/app_core/main_app.h"
#include "core/main_globals.h"
#include "core/app_log.h"
#include "utils/agis_ui_l10n.h"

#include "map_engine/gis_project_xml.h"
#include "map_engine/map_engine.h"
#include "map_engine/map_projection.h"

std::wstring CurrentWindowTitle() {
  if (g_currentGisPath.empty()) {
    return AgisTr(AgisUiStr::WinTitleNoDoc);
  }
  return std::wstring(L"AGIS — ") + g_currentGisPath;
}

void SyncMainTitle() {
  if (g_hwndMain) {
    SetWindowTextW(g_hwndMain, CurrentWindowTitle().c_str());
  }
}

void RefreshUiAfterDocumentReload() {
  MapEngine::Instance().SyncSceneGraphFromMap();
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
  return SaveGisProjectXml(MapEngine::Instance().Document(), path);
}

bool LoadGisXmlFrom(const std::wstring& path, std::wstring* err) {
  return LoadGisProjectXml(MapEngine::Instance().Document(), path, err);
}

std::wstring PromptOpenGisPath(HWND owner) {
  wchar_t path[MAX_PATH]{};
  OPENFILENAMEW ofn{};
  ofn.lStructSize = sizeof(ofn);
  ofn.hwndOwner = owner;
  ofn.lpstrFilter = AgisOpenGisFileFilterPtr();
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
  ofn.lpstrFilter = AgisSaveGisFileFilterPtr();
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
  AppLogLine(AgisTr(AgisUiStr::GisLogNewDoc));
  MessageBoxW(owner, AgisTr(AgisUiStr::GisMsgNewDocOk), L"AGIS", MB_OK | MB_ICONINFORMATION);
}

void GisOpenFromPath(HWND owner, const std::wstring& path) {
  if (path.empty()) {
    return;
  }
  std::wstring err;
  if (!LoadGisXmlFrom(path, &err)) {
    if (owner) {
      MessageBoxW(owner, err.c_str(), AgisTr(AgisUiStr::GisCapOpenFail), MB_OK | MB_ICONWARNING);
    }
    return;
  }
  g_currentGisPath = path;
  RefreshUiAfterDocumentReload();
  SyncMainTitle();
  AppLogLine(std::wstring(AgisTr(AgisUiStr::GisLogOpenedPrefix)) + path);
}

void GisOpen(HWND owner) {
  const std::wstring path = PromptOpenGisPath(owner);
  if (path.empty()) {
    return;
  }
  GisOpenFromPath(owner, path);
}

void GisSaveAs(HWND owner) {
  const std::wstring path = PromptSaveGisPath(owner, g_currentGisPath);
  if (path.empty()) {
    return;
  }
  if (!SaveGisXmlTo(path)) {
    MessageBoxW(owner, AgisTr(AgisUiStr::GisMsgSaveFail), AgisTr(AgisUiStr::GisCapSave), MB_OK | MB_ICONWARNING);
    return;
  }
  g_currentGisPath = path;
  SyncMainTitle();
  AppLogLine(std::wstring(AgisTr(AgisUiStr::GisLogSavedPrefix)) + path);
}

void GisSave(HWND owner) {
  if (g_currentGisPath.empty()) {
    GisSaveAs(owner);
    return;
  }
  if (!SaveGisXmlTo(g_currentGisPath)) {
    MessageBoxW(owner, AgisTr(AgisUiStr::GisMsgSaveFail), AgisTr(AgisUiStr::GisCapSave), MB_OK | MB_ICONWARNING);
    return;
  }
  AppLogLine(std::wstring(AgisTr(AgisUiStr::GisLogSavedPrefix)) + g_currentGisPath);
}
