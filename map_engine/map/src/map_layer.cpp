#include "map_engine/map_layer.h"

#include "utils/agis_ui_l10n.h"

#include <string>
#include <utility>

MapLayer::MapLayer(std::unique_ptr<MapDataSource> dataSource) : dataSource_(std::move(dataSource)) {}

MapDataSourceKind MapLayer::DataSourceKind() const {
  return dataSource_ ? dataSource_->kind() : MapDataSourceKind::kGdalFile;
}

void MapLayer::AppendDriverProperties(std::wstring* out) const {
  if (!out || !dataSource_) {
    return;
  }
  dataSource_->appendDriverProperties(gdalDatasetForDataSource(), sourcePathForDataSource(), out);
}

void MapLayer::AppendSourceProperties(std::wstring* out) const {
  if (!out || !dataSource_) {
    return;
  }
  dataSource_->appendSourceProperties(gdalDatasetForDataSource(), sourcePathForDataSource(), out);
}

GDALDataset* MapLayer::gdalDatasetForDataSource() const {
  return nullptr;
}

std::wstring MapLayer::sourcePathForDataSource() const {
  return L"";
}

const wchar_t* MapLayerKindLabel(MapLayerKind k) {
  switch (k) {
    case MapLayerKind::kRasterGdal:
      return AgisTr(AgisUiStr::LayerKindRasterGdal);
    case MapLayerKind::kVectorGdal:
      return AgisTr(AgisUiStr::LayerKindVectorGdal);
    case MapLayerKind::kOther:
    default:
      return AgisTr(AgisUiStr::LayerKindOther);
  }
}

const wchar_t* MapDataSourceKindLabel(MapDataSourceKind k) {
  switch (k) {
    case MapDataSourceKind::kGdalFile:
      return AgisTr(AgisUiStr::LayerDriverGdalFile);
    case MapDataSourceKind::kTmsXyz:
      return AgisTr(AgisUiStr::LayerDriverTmsXyz);
    case MapDataSourceKind::kWmts:
      return AgisTr(AgisUiStr::LayerDriverWmts);
    case MapDataSourceKind::kArcGisRestJson:
      return AgisTr(AgisUiStr::LayerDriverArcGisJson);
    case MapDataSourceKind::kSoapPlaceholder:
      return AgisTr(AgisUiStr::LayerDriverSoapPh);
    case MapDataSourceKind::kWmsPlaceholder:
      return AgisTr(AgisUiStr::LayerDriverWmsPh);
    default:
      return AgisTr(AgisUiStr::LayerUnknown);
  }
}

bool MapLayer::BuildOverviews(std::wstring& err) {
  if (!dataSource_) {
    err = AgisTr(AgisUiStr::LayerErrOvrUnsupported);
    return false;
  }
  return dataSource_->buildOverviews(gdalDatasetForDataSource(), err);
}

bool MapLayer::ClearOverviews(std::wstring& err) {
  if (!dataSource_) {
    err = AgisTr(AgisUiStr::LayerErrOvrUnsupported);
    return false;
  }
  return dataSource_->clearOverviews(gdalDatasetForDataSource(), err);
}
