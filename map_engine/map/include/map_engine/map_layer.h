#pragma once

#include <windows.h>

#include "map_engine/map_data_source.h"

#include <memory>
#include <string>
#include <vector>

enum class MapLayerKind { kRasterGdal, kVectorGdal, kOther };

/** UI 与列表块用短标签（UTF-16）。 */
const wchar_t* MapLayerKindLabel(MapLayerKind k);

struct ViewExtent {
  double minX = 0;
  double minY = 0;
  double maxX = 1;
  double maxY = 1;
  bool valid() const { return maxX > minX && maxY > minY; }
};

/** 属性面板与日志用短标签（UTF-16）。 */
const wchar_t* MapDataSourceKindLabel(MapDataSourceKind k);

class MapLayer {
 public:
  virtual ~MapLayer() = default;
  virtual std::wstring DisplayName() const = 0;
  virtual bool GetExtent(ViewExtent& out) const = 0;
  virtual void Draw(HDC hdc, const RECT& client, const ViewExtent& view) const = 0;
  virtual MapLayerKind GetKind() const { return MapLayerKind::kOther; }
  /** 本图层选用的数据源类型（每种类型对应不同属性语义与更换数据源方式）。 */
  MapDataSourceKind DataSourceKind() const;

  /** 属性面板：连接/协议侧（由 MapDataSource 实现）。 */
  void AppendDriverProperties(std::wstring* out) const;
  /** 属性面板：数据源侧（由 MapDataSource 实现）。 */
  void AppendSourceProperties(std::wstring* out) const;
  /** 合并连接侧 + 数据源（兼容旧调用）。 */
  void AppendDetailedProperties(std::wstring* out) const {
    if (!out) {
      return;
    }
    AppendDriverProperties(out);
    AppendSourceProperties(out);
  }
  bool BuildOverviews(std::wstring& err);
  bool ClearOverviews(std::wstring& err);

  bool IsLayerVisible() const { return layerVisible_; }
  void SetLayerVisible(bool on) { layerVisible_ = on; }
  /** 供 .gis 持久化使用的数据源标识（文件路径/URL/名称）。 */
  std::wstring SourcePathForSave() const { return sourcePathForDataSource(); }

 protected:
  explicit MapLayer(std::unique_ptr<MapDataSource> dataSource);

  /** 供 MapDataSource 访问底层 GDAL 数据集；无 GDAL 时返回 nullptr。 */
  virtual GDALDataset* gdalDatasetForDataSource() const;
  /** 属性面板「数据源」路径展示（栅格为打开路径，矢量可为显示名/路径）。 */
  virtual std::wstring sourcePathForDataSource() const;

  std::unique_ptr<MapDataSource> dataSource_;

  /** 列表与地图是否绘制该图层（独立于经纬网等全局可见性）。 */
  bool layerVisible_{true};
};
