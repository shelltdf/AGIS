#pragma once

#include "map/map_layer_driver.h"

#include <string>

/**
 * GDAL 栅格图层驱动：属性文案、金字塔等与 GDALRasterBand/GDALDataset 相关。
 */
class GdalRasterMapLayerDriver final : public MapLayerDriver {
 public:
  explicit GdalRasterMapLayerDriver(MapLayerDriverKind k) : kind_(k) {}

  MapLayerDriverKind kind() const override { return kind_; }

  void appendDriverProperties(GDALDataset* ds, const std::wstring& sourcePath, std::wstring* out) const override;
  void appendSourceProperties(GDALDataset* ds, const std::wstring& sourcePath, std::wstring* out) const override;
  bool buildOverviews(GDALDataset* ds, std::wstring& err) override;
  bool clearOverviews(GDALDataset* ds, std::wstring& err) override;

 private:
  MapLayerDriverKind kind_;
};

/**
 * GDAL 矢量图层驱动：OGR 图层列表与元数据等。
 */
class GdalVectorMapLayerDriver final : public MapLayerDriver {
 public:
  explicit GdalVectorMapLayerDriver(MapLayerDriverKind k) : kind_(k) {}

  MapLayerDriverKind kind() const override { return kind_; }

  void appendDriverProperties(GDALDataset* ds, const std::wstring& sourcePath, std::wstring* out) const override;
  void appendSourceProperties(GDALDataset* ds, const std::wstring& sourcePath, std::wstring* out) const override;
  bool buildOverviews(GDALDataset* ds, std::wstring& err) override;
  bool clearOverviews(GDALDataset* ds, std::wstring& err) override;

 private:
  MapLayerDriverKind kind_;
};
