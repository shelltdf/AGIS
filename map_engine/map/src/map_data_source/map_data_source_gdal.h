#pragma once

#include "map_engine/map_data_source.h"

#include <string>

/**
 * GDAL 栅格数据源：属性文案、金字塔等与 GDALRasterBand/GDALDataset 相关。
 */
class GdalRasterMapDataSource final : public MapDataSource {
 public:
  explicit GdalRasterMapDataSource(MapDataSourceKind k) : kind_(k) {}

  MapDataSourceKind kind() const override { return kind_; }

  void appendDriverProperties(GDALDataset* ds, const std::wstring& sourcePath, std::wstring* out) const override;
  void appendSourceProperties(GDALDataset* ds, const std::wstring& sourcePath, std::wstring* out) const override;
  bool buildOverviews(GDALDataset* ds, std::wstring& err) override;
  bool clearOverviews(GDALDataset* ds, std::wstring& err) override;

 private:
  MapDataSourceKind kind_;
};

/**
 * GDAL 矢量数据源：OGR 图层列表与元数据等。
 */
class GdalVectorMapDataSource final : public MapDataSource {
 public:
  explicit GdalVectorMapDataSource(MapDataSourceKind k) : kind_(k) {}

  MapDataSourceKind kind() const override { return kind_; }

  void appendDriverProperties(GDALDataset* ds, const std::wstring& sourcePath, std::wstring* out) const override;
  void appendSourceProperties(GDALDataset* ds, const std::wstring& sourcePath, std::wstring* out) const override;
  bool buildOverviews(GDALDataset* ds, std::wstring& err) override;
  bool clearOverviews(GDALDataset* ds, std::wstring& err) override;

 private:
  MapDataSourceKind kind_;
};
