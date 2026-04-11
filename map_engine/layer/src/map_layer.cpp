#include "map_engine/map_layer.h"

#include <string>
#include <utility>

MapLayer::MapLayer(std::unique_ptr<MapLayerDriver> driver) : driver_(std::move(driver)) {}

MapLayerDriverKind MapLayer::DriverKind() const {
  return driver_ ? driver_->kind() : MapLayerDriverKind::kGdalFile;
}

void MapLayer::AppendDriverProperties(std::wstring* out) const {
  if (!out || !driver_) {
    return;
  }
  driver_->appendDriverProperties(gdalDatasetForDriver(), sourcePathForDriver(), out);
}

void MapLayer::AppendSourceProperties(std::wstring* out) const {
  if (!out || !driver_) {
    return;
  }
  driver_->appendSourceProperties(gdalDatasetForDriver(), sourcePathForDriver(), out);
}

GDALDataset* MapLayer::gdalDatasetForDriver() const {
  return nullptr;
}

std::wstring MapLayer::sourcePathForDriver() const {
  return L"";
}

const wchar_t* MapLayerKindLabel(MapLayerKind k) {
  switch (k) {
    case MapLayerKind::kRasterGdal:
      return L"栅格（GDAL）";
    case MapLayerKind::kVectorGdal:
      return L"矢量（GDAL）";
    case MapLayerKind::kOther:
    default:
      return L"其他";
  }
}

const wchar_t* MapLayerDriverKindLabel(MapLayerDriverKind k) {
  switch (k) {
    case MapLayerDriverKind::kGdalFile:
      return L"GDAL 本地/虚拟文件";
    case MapLayerDriverKind::kTmsXyz:
      return L"TMS / XYZ";
    case MapLayerDriverKind::kWmts:
      return L"WMTS（OGC Web Map Tile Service）";
    case MapLayerDriverKind::kArcGisRestJson:
      return L"ArcGIS REST JSON（Services Directory，GDAL WMS）";
    case MapLayerDriverKind::kSoapPlaceholder:
      return L"OGC SOAP（占位）";
    case MapLayerDriverKind::kWmsPlaceholder:
      return L"WMS KVP（占位）";
    default:
      return L"未知";
  }
}

bool MapLayer::BuildOverviews(std::wstring& err) {
  if (!driver_) {
    err = L"此图层类型不支持金字塔。";
    return false;
  }
  return driver_->buildOverviews(gdalDatasetForDriver(), err);
}

bool MapLayer::ClearOverviews(std::wstring& err) {
  if (!driver_) {
    err = L"此图层类型不支持金字塔。";
    return false;
  }
  return driver_->clearOverviews(gdalDatasetForDriver(), err);
}
