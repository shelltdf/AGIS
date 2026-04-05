#pragma once

#include "map/map_layer.h"

#include <memory>
#include <string>

#if GIS_DESKTOP_HAVE_GDAL
#include "gdal_priv.h"

namespace agis_detail {

std::unique_ptr<MapLayer> CreateLayerFromDataset(GDALDataset* ds, const std::wstring& baseName,
                                                 const std::wstring& sourcePath, MapLayerDriverKind driverKind,
                                                 std::wstring& err);
std::unique_ptr<MapLayer> CreateLayerFromTmsUrl(const std::wstring& urlIn, std::wstring& err);
std::unique_ptr<MapLayer> CreateLayerFromWmtsUrl(const std::wstring& urlIn, std::wstring& err);
std::unique_ptr<MapLayer> CreateLayerFromArcGisRestJsonUrl(const std::wstring& urlIn, std::wstring& err);

}  // namespace agis_detail

#endif  // GIS_DESKTOP_HAVE_GDAL
