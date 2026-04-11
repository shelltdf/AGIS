#include <fstream>
#include <sstream>
#include <string>

#include <windows.h>

#include "common/app_core/resource.h"
#include "common/app_core/main_app.h"
#include "common/app_core/main_globals.h"
#include "core/app_log.h"
#include "main_gis_xml.h"
#include "map_engine/map_engine.h"
#include "map_engine/map_projection.h"

#include <cstdlib>

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

void GisOpenFromPath(HWND owner, const std::wstring& path) {
  if (path.empty()) {
    return;
  }
  std::wstring err;
  if (!LoadGisXmlFrom(path, &err)) {
    if (owner) {
      MessageBoxW(owner, err.c_str(), L"打开 .gis 失败", MB_OK | MB_ICONWARNING);
    }
    return;
  }
  g_currentGisPath = path;
  RefreshUiAfterDocumentReload();
  SyncMainTitle();
  AppLogLine(std::wstring(L"[GIS] 打开文件：") + path);
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
