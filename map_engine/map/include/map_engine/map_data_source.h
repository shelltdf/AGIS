#pragma once

#include <memory>
#include <string>

/** 数据源连接/协议类型（与图层列表、添加图层对话框一致）。 */
enum class MapDataSourceKind {
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
 * 抽象数据源：按连接类型负责属性文案、金字塔等与具体接入方式相关的逻辑。
 * 每个 MapLayer 持有一个具体实现（如 GdalRasterMapDataSource）。
 */
class MapDataSource {
 public:
  virtual ~MapDataSource() = default;

  virtual MapDataSourceKind kind() const = 0;

  /** 属性面板「连接类型 / 协议」侧；ds 由图层在调用时传入（不拥有数据集）。 */
  virtual void appendDriverProperties(GDALDataset* ds, const std::wstring& sourcePath, std::wstring* out) const = 0;
  /** 属性面板「数据源」侧（路径、URL 等）。 */
  virtual void appendSourceProperties(GDALDataset* ds, const std::wstring& sourcePath, std::wstring* out) const = 0;

  virtual bool buildOverviews(GDALDataset* ds, std::wstring& err) = 0;
  virtual bool clearOverviews(GDALDataset* ds, std::wstring& err) = 0;
};

/** 尚未接入真实数据源的占位实现（SOAP / WMS 等）。 */
class PlaceholderMapDataSource final : public MapDataSource {
 public:
  explicit PlaceholderMapDataSource(MapDataSourceKind k) : kind_(k) {}

  MapDataSourceKind kind() const override { return kind_; }

  void appendDriverProperties(GDALDataset* ds, const std::wstring& sourcePath, std::wstring* out) const override;
  void appendSourceProperties(GDALDataset* ds, const std::wstring& sourcePath, std::wstring* out) const override;
  bool buildOverviews(GDALDataset* ds, std::wstring& err) override;
  bool clearOverviews(GDALDataset* ds, std::wstring& err) override;

 private:
  MapDataSourceKind kind_;
};
