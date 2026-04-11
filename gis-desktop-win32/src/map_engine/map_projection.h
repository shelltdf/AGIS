#pragma once

struct ViewExtent;

// --- 空白地图：输入源 → 算法 → 显示用投影 → 屏幕（概念）---
// **输入源**：导航/视口范围以 **WGS84 经纬度（EPSG:4326）** 描述（`ViewExtent`）；经纬网与拾取在该域上定义。
// **显示用投影**：用户选择的 `MapDisplayProjection`（非地理时为 PROJ 目标 CRS），决定「球面经纬」如何展到绘制平面。
// **算法**：`MapProj_GeoLonLatToScreen` / `MapProj_ScreenToGeoLonLat` 内用 **PROJ（4326→目标 CRS）** 与视口外包络，再 **等比缩放居中** 映射到客户区像素（避免视口与投影包络长宽比不一致时非等比拉伸）。
// **屏幕**：GDI 等按像素绘制；有 **矢量/栅格图层** 时仍以数据坐标系为准，**不**走本套空白地图变换（见 `MapDocument` 注释）。
//
/** 2D 空白地图：**显示用**投影枚举（视口范围仍以 WGS84 经纬度描述；绘制/拾取经 PROJ 与像素映射）。有图层时仍以数据坐标系为准，不应用此变换。 */
enum class MapDisplayProjection : int {
  kGeographicWgs84 = 0,
  kWebMercator3857,
  kWorldMercator3395,
  kPlateCarréeEqc4087,
  kMollweide,
  kRobinson,
  kLambertAzimuthalEqualArea,
  kEqualEarth8857,
  kCount
};

bool MapProj_SystemInit();
void MapProj_SystemShutdown();

/** 当前构建是否可用 PROJ（GDAL 开启时通常为真）。 */
bool MapProj_IsEngineAvailable();

/** 非地理投影是否可用（依赖 PROJ）。 */
bool MapProj_IsProjectionSelectable(MapDisplayProjection p);

const wchar_t* MapProj_MenuLabel(MapDisplayProjection p);

void MapProj_GeoLonLatToScreen(MapDisplayProjection proj, const ViewExtent& geoView, int cw, int ch, double lon,
                               double lat, double* sx, double* sy);

void MapProj_ScreenToGeoLonLat(MapDisplayProjection proj, const ViewExtent& geoView, int cw, int ch, int sx, int sy,
                               double* lon, double* lat);

/** 视口或投影变化时调用，使内部投影包络缓存失效。 */
void MapProj_InvalidateBoundsCache();
