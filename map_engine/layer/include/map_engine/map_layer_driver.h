#pragma once

#include <memory>
#include <string>

/** 数据源连接/协议类型（与图层列表、添加图层对话框一致）。 */
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

/** GDAL 数据集不透明句柄；具体定义见 gdal_priv.h。 */
struct GDALDataset;

/**
 * 抽象「图层驱动」：负责驱动语义下的属性文案、金字塔等与具体后端实现相关的逻辑。
 * 每个 MapLayer 持有一个具体驱动实例（如 GdalRasterMapLayerDriver）。
 */
class MapLayerDriver {
 public:
  virtual ~MapLayerDriver() = default;

  virtual MapLayerDriverKind kind() const = 0;

  /** 属性面板「驱动」侧；ds 由图层在调用时传入（驱动不拥有数据集）。 */
  virtual void appendDriverProperties(GDALDataset* ds, const std::wstring& sourcePath, std::wstring* out) const = 0;
  /** 属性面板「数据源」侧。 */
  virtual void appendSourceProperties(GDALDataset* ds, const std::wstring& sourcePath, std::wstring* out) const = 0;

  virtual bool buildOverviews(GDALDataset* ds, std::wstring& err) = 0;
  virtual bool clearOverviews(GDALDataset* ds, std::wstring& err) = 0;
};

/** 尚未接入真实数据源的占位驱动（SOAP / WMS 等）。 */
class PlaceholderMapLayerDriver final : public MapLayerDriver {
 public:
  explicit PlaceholderMapLayerDriver(MapLayerDriverKind k) : kind_(k) {}

  MapLayerDriverKind kind() const override { return kind_; }

  void appendDriverProperties(GDALDataset* ds, const std::wstring& sourcePath, std::wstring* out) const override;
  void appendSourceProperties(GDALDataset* ds, const std::wstring& sourcePath, std::wstring* out) const override;
  bool buildOverviews(GDALDataset* ds, std::wstring& err) override;
  bool clearOverviews(GDALDataset* ds, std::wstring& err) override;

 private:
  MapLayerDriverKind kind_;
};
