#pragma once

#include "map_engine/map_layer.h"
#include "map_engine/map_projection.h"

#include <memory>
#include <vector>

struct MapDocument {
  MapDocument() = default;
  MapDocument(const MapDocument&) = delete;
  MapDocument& operator=(const MapDocument&) = delete;
  MapDocument(MapDocument&&) = default;
  MapDocument& operator=(MapDocument&&) = default;

  std::vector<std::unique_ptr<MapLayer>> layers;
  ViewExtent view{};
  /** 无图层时是否绘制经纬网（要素可见性）。 */
  bool showLatLonGrid{true};
  /** 「100%」缩放基准：视口经度跨度（度），默认 360（全球）。 */
  double refViewWidthDeg{360.0};
  /** 「100%」缩放基准：视口纬度跨度（度），默认 180（全球南北）。 */
  double refViewHeightDeg{180.0};
  /** 无图层时 2D 经纬网/拾取的显示投影；有图层时仍以数据坐标为准。 */
  MapDisplayProjection displayProjection{MapDisplayProjection::kGeographicWgs84};

  void FitViewToLayers();
  bool AddLayerFromFile(const std::wstring& path, std::wstring& err);
  /** TMS/XYZ 网络瓦片（依赖 GDAL XYZ 驱动，连接串形如 ZXY:https://.../{z}/{x}/{y}.png）。 */
  bool AddLayerFromTmsUrl(const std::wstring& url, std::wstring& err);
  /** WMTS（依赖 GDAL WMTS 驱动，连接串形如 WMTS:https://... 或 GetCapabilities URL）。 */
  bool AddLayerFromWmtsUrl(const std::wstring& url, std::wstring& err);
  /** ArcGIS REST Services Directory JSON（MapServer/ImageServer，由 GDAL WMS 驱动处理含 `f=json` 的 URL）。 */
  bool AddLayerFromArcGisRestJsonUrl(const std::wstring& url, std::wstring& err);
  /** 用新图层替换指定下标（用于更改数据源）。 */
  bool ReplaceLayerAt(size_t index, std::unique_ptr<MapLayer> layer, std::wstring& err);
  /** 按下标删除图层（绘制顺序与列表一致）。 */
  bool RemoveLayerAt(size_t index, std::wstring& err);
  /** 与上一层交换（列表中上移）。 */
  void MoveLayerUp(size_t index);
  /** 与下一层交换（列表中下移）。 */
  void MoveLayerDown(size_t index);
  void Draw(HDC hdcMem, const RECT& client);
  void ScreenToWorld(int sx, int sy, int cw, int ch, double* wx, double* wy) const;
  void ZoomAt(int sx, int sy, int cw, int ch, double factor);
  void PanPixels(int dx, int dy, int cw, int ch);
  void ZoomViewAtCenter(double factor, int cw, int ch);
  /** 以视口中心为锚点恢复到 refViewWidthDeg 对应的「100%」缩放。 */
  void ResetZoom100AnchorCenter(int cw, int ch);
  /** 不改变缩放，将内容包围盒中心移到视口中心（无图层时为 (0,0)）。 */
  void CenterContentOrigin(int cw, int ch);
  int ScalePercentForUi() const;
  void SetShowLatLonGrid(bool on);
  bool GetShowLatLonGrid() const { return showLatLonGrid; }
  void SetDisplayProjection(MapDisplayProjection p);
  MapDisplayProjection GetDisplayProjection() const { return displayProjection; }

 private:
  /** 无图层时：修正无效范围；纬度跨度>180° 时与经度等比例缩放回合法范围。 */
  void NormalizeEmptyMapView();
  /** 无图层时：保持视口经度:纬度 = 360:180（度空间）。 */
  void EnforceLonLatAspect360_180();
};

/** 全球 WGS84 经纬度范围；无图层默认视口等。 */
ViewExtent DefaultGeographicView();
