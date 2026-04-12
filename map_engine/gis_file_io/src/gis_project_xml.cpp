#include "map_engine/gis_project_xml.h"

#include "map_engine/gis_xml.h"
#include "map_engine/map.h"

#include "core/app_log.h"
#include "utils/agis_ui_l10n.h"

#include <fstream>
#include <sstream>
#include <string>

namespace {

std::wstring ParentDirOfPath(const std::wstring& path) {
  const size_t p = path.find_last_of(L"\\/");
  if (p == std::wstring::npos) {
    return L"";
  }
  return path.substr(0, p);
}

bool IsLikelyAbsoluteOrUrl(const std::wstring& s) {
  if (s.size() >= 2 && s[1] == L':') {
    return true;
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

}  // namespace

std::wstring GisDriverKindToTag(MapLayerDriverKind kind) {
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

MapLayerDriverKind GisDriverKindFromTag(const std::wstring& s) {
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

bool SaveGisProjectXml(const Map& doc, const std::wstring& path) {
  std::wofstream ofs(path);
  if (!ofs.is_open()) {
    return false;
  }
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
    ofs << L"    <layer driver=\"" << GisDriverKindToTag(lyr->DriverKind()) << L"\"";
    ofs << L" visible=\"" << (lyr->IsLayerVisible() ? 1 : 0) << L"\"";
    ofs << L" source=\"" << XmlEscape(src) << L"\"";
    ofs << L" name=\"" << XmlEscape(name) << L"\"/>\n";
  }
  ofs << L"  </layers>\n";
  ofs << L"</agis-gis>\n";
  return true;
}

bool LoadGisProjectXml(Map& doc, const std::wstring& path, std::wstring* err) {
  std::wifstream ifs(path);
  if (!ifs.is_open()) {
    if (err) {
      *err = AgisTr(AgisUiStr::GisErrOpenFile);
    }
    return false;
  }
  std::wstringstream ss;
  ss << ifs.rdbuf();
  const std::wstring xml = ss.str();
  if (xml.find(L"<agis-gis") == std::wstring::npos) {
    if (err) {
      *err = AgisTr(AgisUiStr::GisErrInvalidXml);
    }
    return false;
  }
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

    const MapLayerDriverKind kind = GisDriverKindFromTag(GetXmlAttr(line, L"driver"));
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
      AppLogLine(std::wstring(AgisTr(AgisUiStr::GisLogLayerRestoreFailPrefix)) + source +
                 AgisTr(AgisUiStr::GisLogLayerRestoreReasonSep) + loadErr);
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
  AppLogLine(AgisTr(AgisUiStr::GisLogFileReadOk));
  AppLogLine(std::wstring(AgisTr(AgisUiStr::GisLogRestoreStatsPrefix)) + std::to_wstring(loaded) + L"/" +
             std::to_wstring(failed));
  return true;
}
