#pragma once

#include <windows.h>

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

enum class MapLayerDriverKind {
  kGdalFile,
  kTmsXyz,
  kWmts,
  /** ArcGIS REST Services Directory 风格（MapServer/ImageServer `f=json`，由 GDAL WMS 驱动解析）。 */
  kArcGisRestJson,
  /** OGC Web Services SOAP 绑定等（占位）。 */
  kSoapPlaceholder,
  /** 经典 WMS KVP GetCapabilities/GetMap（占位）。 */
  kWmsPlaceholder,
};

/** 属性面板与日志用短标签（UTF-16）。 */
const wchar_t* MapLayerDriverKindLabel(MapLayerDriverKind k);

class MapLayer {
 public:
  virtual ~MapLayer() = default;
  virtual std::wstring DisplayName() const = 0;
  virtual bool GetExtent(ViewExtent& out) const = 0;
  virtual void Draw(HDC hdc, const RECT& client, const ViewExtent& view) const = 0;
  virtual MapLayerKind GetKind() const { return MapLayerKind::kOther; }
  /** 本图层选用的数据源驱动（每种驱动对应不同属性语义与更换数据源方式）。 */
  virtual MapLayerDriverKind DriverKind() const { return MapLayerDriverKind::kGdalFile; }
  /** 属性面板：驱动侧（GDAL/OGR 能力、仿射、波段/图层几何等）。 */
  virtual void AppendDriverProperties(std::wstring* out) const { (void)out; }
  /** 属性面板：数据源侧（路径、文件列表、数据集描述与元数据域等）。 */
  virtual void AppendSourceProperties(std::wstring* out) const { (void)out; }
  /** 合并驱动 + 数据源（兼容旧调用）。 */
  void AppendDetailedProperties(std::wstring* out) const {
    if (!out) {
      return;
    }
    AppendDriverProperties(out);
    AppendSourceProperties(out);
  }
  virtual bool BuildOverviews(std::wstring& err);
  virtual bool ClearOverviews(std::wstring& err);

  bool IsLayerVisible() const { return layerVisible_; }
  void SetLayerVisible(bool on) { layerVisible_ = on; }

 protected:
  /** 列表与地图是否绘制该图层（独立于经纬网等全局可见性）。 */
  bool layerVisible_{true};
};
