#include "map_engine/map_projection.h"

#include "utils/agis_gdal_runtime_env.h"
#include "map_engine/map_layer.h"

#include <algorithm>
#include <cmath>
#include <cstring>

#if GIS_DESKTOP_HAVE_GDAL
#include <proj.h>
#endif

namespace {

#if GIS_DESKTOP_HAVE_GDAL
static PJ_CONTEXT* g_ctx = nullptr;
static PJ* g_tf[static_cast<int>(MapDisplayProjection::kCount)]{};
#endif

static constexpr double kWebMercLatLimit = 85.05112878;

const char* TargetCrsDefinition(MapDisplayProjection p) {
  switch (p) {
    case MapDisplayProjection::kGeographicWgs84:
      return nullptr;
    case MapDisplayProjection::kWebMercator3857:
      return "EPSG:3857";
    case MapDisplayProjection::kWorldMercator3395:
      return "EPSG:3395";
    case MapDisplayProjection::kPlateCarréeEqc4087:
      return "EPSG:4087";
    case MapDisplayProjection::kMollweide:
      return "+proj=moll +lon_0=0 +datum=WGS84 +type=crs";
    case MapDisplayProjection::kRobinson:
      return "+proj=robin +lon_0=0 +datum=WGS84 +type=crs";
    case MapDisplayProjection::kLambertAzimuthalEqualArea:
      return "+proj=laea +lat_0=0 +lon_0=0 +datum=WGS84 +type=crs";
    case MapDisplayProjection::kEqualEarth8857:
      return "EPSG:8857";
    default:
      return nullptr;
  }
}

#if GIS_DESKTOP_HAVE_GDAL
static double ClipLatFor3857(double lat) {
  if (lat > kWebMercLatLimit) {
    return kWebMercLatLimit;
  }
  if (lat < -kWebMercLatLimit) {
    return -kWebMercLatLimit;
  }
  return lat;
}

static void MaybeClipLonLat(MapDisplayProjection p, double* lon, double* lat) {
  if (p == MapDisplayProjection::kWebMercator3857) {
    *lat = ClipLatFor3857(*lat);
  }
  (void)lon;
}

PJ* GetPipeline(MapDisplayProjection p) {
  const int i = static_cast<int>(p);
  if (p == MapDisplayProjection::kGeographicWgs84 || i < 0 ||
      i >= static_cast<int>(MapDisplayProjection::kCount)) {
    return nullptr;
  }
  if (g_tf[i]) {
    return g_tf[i];
  }
  const char* dst = TargetCrsDefinition(p);
  if (!dst || !g_ctx) {
    return nullptr;
  }
  g_tf[i] = proj_create_crs_to_crs(g_ctx, "EPSG:4326", dst, nullptr);
  return g_tf[i];
}

bool ForwardLonLat(PJ* tf, MapDisplayProjection p, double lon, double lat, double* ox, double* oy) {
  if (!tf || !ox || !oy) {
    return false;
  }
  MaybeClipLonLat(p, &lon, &lat);
  PJ_COORD c = proj_coord(lon, lat, 0, 0);
  c = proj_trans(tf, PJ_FWD, c);
  *ox = c.xyz.x;
  *oy = c.xyz.y;
  return std::isfinite(*ox) && std::isfinite(*oy);
}

bool InverseProj(PJ* tf, double x, double y, double* lon, double* lat) {
  if (!tf || !lon || !lat) {
    return false;
  }
  PJ_COORD c = proj_coord(x, y, 0, 0);
  c = proj_trans(tf, PJ_INV, c);
  *lon = c.xyz.x;
  *lat = c.xyz.y;
  return std::isfinite(*lon) && std::isfinite(*lat);
}

void ExpandBounds(double x, double y, double* minx, double* miny, double* maxx, double* maxy) {
  *minx = std::min(*minx, x);
  *miny = std::min(*miny, y);
  *maxx = std::max(*maxx, x);
  *maxy = std::max(*maxy, y);
}

static ViewExtent g_cacheView{};
static MapDisplayProjection g_cacheProj = MapDisplayProjection::kCount;
static double g_cacheMinX = 0;
static double g_cacheMinY = 0;
static double g_cacheMaxX = 0;
static double g_cacheMaxY = 0;
static bool g_cacheOk = false;

static bool ViewExtentNearlyEqual(const ViewExtent& a, const ViewExtent& b) {
  auto nearEq = [](double x, double y) { return std::abs(x - y) < 1e-9; };
  return nearEq(a.minX, b.minX) && nearEq(a.minY, b.minY) && nearEq(a.maxX, b.maxX) && nearEq(a.maxY, b.maxY);
}

bool ComputeProjBounds(PJ* tf, MapDisplayProjection p, const ViewExtent& g, double* minx, double* miny, double* maxx,
                       double* maxy) {
  if (!tf || !minx || !miny || !maxx || !maxy || !g.valid()) {
    return false;
  }
  *minx = 1e300;
  *miny = 1e300;
  *maxx = -1e300;
  *maxy = -1e300;
  constexpr int N = 72;
  auto sample = [&](double lon, double lat) {
    double mx = 0;
    double my = 0;
    if (ForwardLonLat(tf, p, lon, lat, &mx, &my)) {
      ExpandBounds(mx, my, minx, miny, maxx, maxy);
    }
  };
  for (int i = 0; i <= N; ++i) {
    const double t = static_cast<double>(i) / static_cast<double>(N);
    const double lon = g.minX + t * (g.maxX - g.minX);
    sample(lon, g.minY);
  }
  for (int i = 0; i <= N; ++i) {
    const double t = static_cast<double>(i) / static_cast<double>(N);
    const double lon = g.minX + t * (g.maxX - g.minX);
    sample(lon, g.maxY);
  }
  for (int i = 0; i <= N; ++i) {
    const double t = static_cast<double>(i) / static_cast<double>(N);
    const double lat = g.minY + t * (g.maxY - g.minY);
    sample(g.minX, lat);
  }
  for (int i = 0; i <= N; ++i) {
    const double t = static_cast<double>(i) / static_cast<double>(N);
    const double lat = g.minY + t * (g.maxY - g.minY);
    sample(g.maxX, lat);
  }
  if (!(*maxx > *minx && *maxy > *miny) || !std::isfinite(*minx) || !std::isfinite(*maxx)) {
    return false;
  }
  const double padX = (*maxx - *minx) * 1e-6 + 1e-6;
  const double padY = (*maxy - *miny) * 1e-6 + 1e-6;
  *minx -= padX;
  *maxx += padX;
  *miny -= padY;
  *maxy += padY;
  return true;
}

bool EnsureProjBoundsCached(MapDisplayProjection proj, const ViewExtent& geoView) {
  if (proj == MapDisplayProjection::kGeographicWgs84) {
    g_cacheOk = false;
    return false;
  }
  if (g_cacheOk && g_cacheProj == proj && ViewExtentNearlyEqual(geoView, g_cacheView)) {
    return true;
  }
  PJ* tf = GetPipeline(proj);
  if (!tf || !ComputeProjBounds(tf, proj, geoView, &g_cacheMinX, &g_cacheMinY, &g_cacheMaxX, &g_cacheMaxY)) {
    g_cacheOk = false;
    return false;
  }
  g_cacheView = geoView;
  g_cacheProj = proj;
  g_cacheOk = true;
  return true;
}

/// 将当前缓存的投影外包框映射到 cw×ch：用统一比例尺（取 min 以完整落入视口）并居中，避免视口与外包框长宽比不一致时 X/Y 非等比拉伸。
static void MapProj_ProjectedExtentToScreenPx(double mx, double my, int cw, int ch, double* sx, double* sy) {
  if (!sx || !sy) {
    return;
  }
  const double w = g_cacheMaxX - g_cacheMinX;
  const double h = g_cacheMaxY - g_cacheMinY;
  if (!(w > 0.0) || !(h > 0.0) || cw <= 0 || ch <= 0) {
    *sx = 0;
    *sy = 0;
    return;
  }
  const double scale = (std::min)(static_cast<double>(cw) / w, static_cast<double>(ch) / h);
  const double drawnW = w * scale;
  const double drawnH = h * scale;
  const double offX = (static_cast<double>(cw) - drawnW) * 0.5;
  const double offY = (static_cast<double>(ch) - drawnH) * 0.5;
  *sx = (mx - g_cacheMinX) * scale + offX;
  *sy = (g_cacheMaxY - my) * scale + offY;
}

static void MapProj_ScreenPxToProjectedExtent(int sx, int sy, int cw, int ch, double* mx, double* my) {
  if (!mx || !my) {
    return;
  }
  const double w = g_cacheMaxX - g_cacheMinX;
  const double h = g_cacheMaxY - g_cacheMinY;
  if (!(w > 0.0) || !(h > 0.0) || cw <= 0 || ch <= 0) {
    *mx = g_cacheMinX;
    *my = g_cacheMaxY;
    return;
  }
  const double scale = (std::min)(static_cast<double>(cw) / w, static_cast<double>(ch) / h);
  const double drawnW = w * scale;
  const double drawnH = h * scale;
  const double offX = (static_cast<double>(cw) - drawnW) * 0.5;
  const double offY = (static_cast<double>(ch) - drawnH) * 0.5;
  *mx = g_cacheMinX + (static_cast<double>(sx) - offX) / scale;
  *my = g_cacheMaxY - (static_cast<double>(sy) - offY) / scale;
}
#endif  // GIS_DESKTOP_HAVE_GDAL

void WorldToScreenLinear(const ViewExtent& v, double wx, double wy, int cw, int ch, double* sx, double* sy) {
  const double ww = v.maxX - v.minX;
  const double wh = v.maxY - v.minY;
  if (ww <= 0.0 || wh <= 0.0 || cw <= 0 || ch <= 0) {
    *sx = 0;
    *sy = 0;
    return;
  }
  *sx = (wx - v.minX) / ww * static_cast<double>(cw);
  *sy = (v.maxY - wy) / wh * static_cast<double>(ch);
}

}  // namespace

bool MapProj_SystemInit() {
#if GIS_DESKTOP_HAVE_GDAL
  if (!g_ctx) {
    AgisEnsureGdalDataPath();
    g_ctx = proj_context_create();
  }
  return g_ctx != nullptr;
#else
  return true;
#endif
}

void MapProj_SystemShutdown() {
#if GIS_DESKTOP_HAVE_GDAL
  for (auto& t : g_tf) {
    if (t) {
      proj_destroy(t);
      t = nullptr;
    }
  }
  if (g_ctx) {
    proj_context_destroy(g_ctx);
    g_ctx = nullptr;
  }
#endif
}

bool MapProj_IsEngineAvailable() {
#if GIS_DESKTOP_HAVE_GDAL
  return g_ctx != nullptr;
#else
  return false;
#endif
}

bool MapProj_IsProjectionSelectable(MapDisplayProjection p) {
  if (p == MapDisplayProjection::kGeographicWgs84) {
    return true;
  }
#if GIS_DESKTOP_HAVE_GDAL
  return MapProj_IsEngineAvailable() && TargetCrsDefinition(p) != nullptr &&
         p < MapDisplayProjection::kCount;
#else
  return false;
#endif
}

const wchar_t* MapProj_MenuLabel(MapDisplayProjection p) {
  switch (p) {
    case MapDisplayProjection::kGeographicWgs84:
      return L"WGS 84 地理坐标（经纬度）";
    case MapDisplayProjection::kWebMercator3857:
      return L"Web 墨卡托（EPSG:3857）";
    case MapDisplayProjection::kWorldMercator3395:
      return L"世界墨卡托（EPSG:3395）";
    case MapDisplayProjection::kPlateCarréeEqc4087:
      return L"等距圆柱 / Plate Carrée（4087）";
    case MapDisplayProjection::kMollweide:
      return L"摩尔魏德（Mollweide）";
    case MapDisplayProjection::kRobinson:
      return L"罗宾森（Robinson）";
    case MapDisplayProjection::kLambertAzimuthalEqualArea:
      return L"兰伯特方位等积（LAEA 全球）";
    case MapDisplayProjection::kEqualEarth8857:
      return L"Equal Earth（EPSG:8857）";
    default:
      return L"（未知）";
  }
}

const wchar_t* MapProj_MenuLabelEn(MapDisplayProjection p) {
  switch (p) {
    case MapDisplayProjection::kGeographicWgs84:
      return L"WGS 84 geographic (lon/lat)";
    case MapDisplayProjection::kWebMercator3857:
      return L"Web Mercator (EPSG:3857)";
    case MapDisplayProjection::kWorldMercator3395:
      return L"World Mercator (EPSG:3395)";
    case MapDisplayProjection::kPlateCarréeEqc4087:
      return L"Plate Carrée / Equirectangular (4087)";
    case MapDisplayProjection::kMollweide:
      return L"Mollweide";
    case MapDisplayProjection::kRobinson:
      return L"Robinson";
    case MapDisplayProjection::kLambertAzimuthalEqualArea:
      return L"Lambert azimuthal equal-area (global)";
    case MapDisplayProjection::kEqualEarth8857:
      return L"Equal Earth (EPSG:8857)";
    default:
      return L"(unknown)";
  }
}

void MapProj_GeoLonLatToScreen(MapDisplayProjection proj, const ViewExtent& geoView, int cw, int ch, double lon,
                               double lat, double* sx, double* sy) {
  if (!sx || !sy) {
    return;
  }
  if (proj == MapDisplayProjection::kGeographicWgs84 || !MapProj_IsProjectionSelectable(proj)) {
    WorldToScreenLinear(geoView, lon, lat, cw, ch, sx, sy);
    return;
  }
#if GIS_DESKTOP_HAVE_GDAL
  PJ* tf = GetPipeline(proj);
  if (!tf || !EnsureProjBoundsCached(proj, geoView)) {
    WorldToScreenLinear(geoView, lon, lat, cw, ch, sx, sy);
    return;
  }
  double mx = 0;
  double my = 0;
  if (!ForwardLonLat(tf, proj, lon, lat, &mx, &my)) {
    *sx = -1e9;
    *sy = -1e9;
    return;
  }
  const double w = g_cacheMaxX - g_cacheMinX;
  const double h = g_cacheMaxY - g_cacheMinY;
  if (w <= 0.0 || h <= 0.0) {
    WorldToScreenLinear(geoView, lon, lat, cw, ch, sx, sy);
    return;
  }
  MapProj_ProjectedExtentToScreenPx(mx, my, cw, ch, sx, sy);
#else
  WorldToScreenLinear(geoView, lon, lat, cw, ch, sx, sy);
#endif
}

void MapProj_ScreenToGeoLonLat(MapDisplayProjection proj, const ViewExtent& geoView, int cw, int ch, int sx, int sy,
                               double* lon, double* lat) {
  if (!lon || !lat) {
    return;
  }
  if (proj == MapDisplayProjection::kGeographicWgs84 || !MapProj_IsProjectionSelectable(proj)) {
    const double worldW = geoView.maxX - geoView.minX;
    const double worldH = geoView.maxY - geoView.minY;
    if (worldW <= 0.0 || worldH <= 0.0 || cw <= 0 || ch <= 0) {
      *lon = geoView.minX;
      *lat = geoView.minY;
      return;
    }
    const double fx = static_cast<double>(sx) / static_cast<double>(cw);
    const double fy = static_cast<double>(sy) / static_cast<double>(ch);
    *lon = geoView.minX + fx * worldW;
    *lat = geoView.maxY - fy * worldH;
    return;
  }
#if GIS_DESKTOP_HAVE_GDAL
  PJ* tf = GetPipeline(proj);
  if (!tf || !EnsureProjBoundsCached(proj, geoView)) {
    const double worldW = geoView.maxX - geoView.minX;
    const double worldH = geoView.maxY - geoView.minY;
    const double fx = static_cast<double>(sx) / static_cast<double>(cw);
    const double fy = static_cast<double>(sy) / static_cast<double>(ch);
    *lon = geoView.minX + fx * worldW;
    *lat = geoView.maxY - fy * worldH;
    return;
  }
  const double w = g_cacheMaxX - g_cacheMinX;
  const double h = g_cacheMaxY - g_cacheMinY;
  if (w <= 0.0 || h <= 0.0 || cw <= 0 || ch <= 0) {
    *lon = geoView.minX;
    *lat = geoView.minY;
    return;
  }
  double mx = 0;
  double my = 0;
  MapProj_ScreenPxToProjectedExtent(sx, sy, cw, ch, &mx, &my);
  if (!InverseProj(tf, mx, my, lon, lat)) {
    *lon = geoView.minX;
    *lat = geoView.minY;
  }
#else
  const double worldW = geoView.maxX - geoView.minX;
  const double worldH = geoView.maxY - geoView.minY;
  const double fx = static_cast<double>(sx) / static_cast<double>(cw);
  const double fy = static_cast<double>(sy) / static_cast<double>(ch);
  *lon = geoView.minX + fx * worldW;
  *lat = geoView.maxY - fy * worldH;
#endif
}

void MapProj_InvalidateBoundsCache() {
#if GIS_DESKTOP_HAVE_GDAL
  g_cacheOk = false;
#endif
}
