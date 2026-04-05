#pragma once

struct ViewExtent;

/** 2D 空白地图显示用投影（视口仍为 WGS84 经纬度；绘制/拾取时变换）。有图层时仍以数据坐标系为准，不应用此变换。 */
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
