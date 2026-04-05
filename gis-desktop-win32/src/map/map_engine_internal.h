#pragma once

#include "map/map_layer.h"

#include <memory>
#include <string>

#if GIS_DESKTOP_HAVE_GDAL
#include "gdal_priv.h"
#include "ogr_api.h"

namespace agis_detail {

void ApplyDefaultGeoTransform(double* gt, int w, int h);
void GeoExtentFromGT(const double* gt, int w, int h, ViewExtent* ex);

void AppendGdalObjectMetadataDomains(GDALMajorObjectH obj, std::wstring* out);
void AppendDatasetFileList(GDALDataset* ds, std::wstring* out);
void AppendDriverMetadata(GDALDriver* drv, std::wstring* out);
void AppendGcpSummary(GDALDataset* ds, std::wstring* out);
void AppendRasterBandExtras(GDALRasterBand* b, std::wstring* out);
void AppendOgrLayerDetails(OGRLayer* lay, int index, std::wstring* out);

std::unique_ptr<MapLayer> CreateLayerFromDataset(GDALDataset* ds, const std::wstring& baseName,
                                                 const std::wstring& sourcePath, MapLayerDriverKind driverKind,
                                                 std::wstring& err);
std::unique_ptr<MapLayer> CreateLayerFromTmsUrl(const std::wstring& urlIn, std::wstring& err);
std::unique_ptr<MapLayer> CreateLayerFromWmtsUrl(const std::wstring& urlIn, std::wstring& err);
std::unique_ptr<MapLayer> CreateLayerFromArcGisRestJsonUrl(const std::wstring& urlIn, std::wstring& err);

}  // namespace agis_detail

#endif  // GIS_DESKTOP_HAVE_GDAL
