#include "map_engine/map_layer.h"

#include "utils/agis_ui_l10n.h"

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
      return AgisTr(AgisUiStr::LayerKindRasterGdal);
    case MapLayerKind::kVectorGdal:
      return AgisTr(AgisUiStr::LayerKindVectorGdal);
    case MapLayerKind::kOther:
    default:
      return AgisTr(AgisUiStr::LayerKindOther);
  }
}

const wchar_t* MapLayerDriverKindLabel(MapLayerDriverKind k) {
  switch (k) {
    case MapLayerDriverKind::kGdalFile:
      return AgisTr(AgisUiStr::LayerDriverGdalFile);
    case MapLayerDriverKind::kTmsXyz:
      return AgisTr(AgisUiStr::LayerDriverTmsXyz);
    case MapLayerDriverKind::kWmts:
      return AgisTr(AgisUiStr::LayerDriverWmts);
    case MapLayerDriverKind::kArcGisRestJson:
      return AgisTr(AgisUiStr::LayerDriverArcGisJson);
    case MapLayerDriverKind::kSoapPlaceholder:
      return AgisTr(AgisUiStr::LayerDriverSoapPh);
    case MapLayerDriverKind::kWmsPlaceholder:
      return AgisTr(AgisUiStr::LayerDriverWmsPh);
    default:
      return AgisTr(AgisUiStr::LayerUnknown);
  }
}

bool MapLayer::BuildOverviews(std::wstring& err) {
  if (!driver_) {
    err = AgisTr(AgisUiStr::LayerErrOvrUnsupported);
    return false;
  }
  return driver_->buildOverviews(gdalDatasetForDriver(), err);
}

bool MapLayer::ClearOverviews(std::wstring& err) {
  if (!driver_) {
    err = AgisTr(AgisUiStr::LayerErrOvrUnsupported);
    return false;
  }
  return driver_->clearOverviews(gdalDatasetForDriver(), err);
}
