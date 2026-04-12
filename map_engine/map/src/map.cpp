#include "map_engine/map.h"
#include "map_engine/map_engine.h"

#include "utils/agis_ui_l10n.h"

#include "map_engine/map_engine_internal.h"
#include "utils/utf8_wide.h"

#include "ui_engine/gdiplus_ui.h"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <string>

#if GIS_DESKTOP_HAVE_GDAL
#include "utils/agis_gdal_runtime_env.h"
#include "cpl_conv.h"
#include "gdal_priv.h"
#endif

namespace {

/** 比例尺长度取整（米）：1/2/5×10^n */
double NiceMeters(double meters) {
  if (!(meters > 0.0) || !std::isfinite(meters)) {
    return 1000.0;
  }
  const double p10 = std::pow(10.0, std::floor(std::log10(meters)));
  const double n = meters / p10;
  double f = 10.0;
  if (n <= 1.0) {
    f = 1.0;
  } else if (n <= 2.0) {
    f = 2.0;
  } else if (n <= 5.0) {
    f = 5.0;
  }
  return f * p10;
}

/** 将经度规范到 [-180,180] 便于判断本初子午线 / 日界线 */
double NormalizeLon180(double lonDeg) {
  double x = std::fmod(lonDeg + 180.0, 360.0);
  if (x < 0.0) {
    x += 360.0;
  }
  return x - 180.0;
}

bool IsPrimeMeridianLon(double lonDeg) {
  return std::abs(NormalizeLon180(lonDeg)) < 1e-3;
}

bool IsAntimeridianLon(double lonDeg) {
  return std::abs(std::abs(NormalizeLon180(lonDeg)) - 180.0) < 1e-3;
}

bool IsPoleLat(double latDeg) {
  return std::abs(latDeg - 90.0) < 1e-3 || std::abs(latDeg + 90.0) < 1e-3;
}

bool IsEquatorLat(double latDeg) {
  return std::abs(latDeg) < 1e-3;
}

void FormatLonLabel(wchar_t* buf, size_t nbuf, double lonDeg) {
  if (nbuf < 8) {
    return;
  }
  _snwprintf_s(buf, nbuf, _TRUNCATE, L"%.4g°", lonDeg);
}

void FormatLatLabel(wchar_t* buf, size_t nbuf, double latDeg) {
  if (nbuf < 8) {
    return;
  }
  _snwprintf_s(buf, nbuf, _TRUNCATE, L"%.4g°", latDeg);
}

double NiceGridStepDeg(double spanDeg, int targetLines) {
  if (spanDeg <= 1e-9 || targetLines <= 0) {
    return 1.0;
  }
  const double raw = spanDeg / static_cast<double>(targetLines);
  const double p10 = std::pow(10.0, std::floor(std::log10(raw)));
  const double n = raw / p10;
  double m = 1.0;
  if (n <= 1.0) {
    m = 1.0;
  } else if (n <= 2.0) {
    m = 2.0;
  } else if (n <= 5.0) {
    m = 5.0;
  } else {
    m = 10.0;
  }
  return m * p10;
}

void WorldToScreenMap(const ViewExtent& v, double wx, double wy, int cw, int ch, double* sx, double* sy) {
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

/** 在视口内绘制比例尺（横条长度代表当前纬度处地面距离，单位 m/km）。 */
void DrawScaleBar(HDC hdc, const RECT& inner, const ViewExtent& view) {
  const int cw = inner.right - inner.left;
  const int ch = inner.bottom - inner.top;
  if (cw < 120 || ch < 100 || !view.valid()) {
    return;
  }
  constexpr double kPi = 3.14159265358979323846;
  constexpr double R = 6371000.0;
  const double lonSpan = view.maxX - view.minX;
  const double latSpan = view.maxY - view.minY;
  const double latMid = (view.minY + view.maxY) * 0.5;
  const double latRad = latMid * (kPi / 180.0);
  const double cosLat = std::max(0.02, std::abs(std::cos(latRad)));
  double mPerPx =
      (lonSpan / static_cast<double>(std::max(1, cw))) * (kPi / 180.0) * R * cosLat;
  if (!(mPerPx > 0.0) || !std::isfinite(mPerPx)) {
    mPerPx = (latSpan / static_cast<double>(std::max(1, ch))) * (kPi / 180.0) * R;
  }
  const double targetPx = 120.0;
  double niceM = NiceMeters(mPerPx * targetPx);
  int barPx = static_cast<int>(std::lround(niceM / mPerPx));
  barPx = std::max(48, std::min(barPx, cw - 24));
  niceM = mPerPx * static_cast<double>(barPx);

  const int barH = 6;
  const int x0 = inner.left + 10;
  const int kBottomGap = 88;
  int textY = inner.bottom - kBottomGap;
  int yBar = textY - 14;
  if (yBar < inner.top + 4) {
    yBar = inner.top + 4;
    textY = yBar + 16;
  }

  RECT rc{x0, yBar, x0 + barPx, yBar + barH};
  const HBRUSH fill = CreateSolidBrush(RGB(42, 48, 58));
  FillRect(hdc, &rc, fill);
  DeleteObject(fill);
  HPEN pen = CreatePen(PS_SOLID, 1, RGB(250, 250, 252));
  const HGDIOBJ oldPen = SelectObject(hdc, pen);
  MoveToEx(hdc, x0, yBar, nullptr);
  LineTo(hdc, x0, yBar - 4);
  MoveToEx(hdc, x0 + barPx, yBar, nullptr);
  LineTo(hdc, x0 + barPx, yBar - 4);
  SelectObject(hdc, oldPen);
  DeleteObject(pen);

  wchar_t cap[64]{};
  if (niceM >= 1000.0) {
    const double km = niceM / 1000.0;
    if (km >= 100.0) {
      _snwprintf_s(cap, _TRUNCATE, L"%.0f km", km);
    } else if (km >= 10.0) {
      _snwprintf_s(cap, _TRUNCATE, L"%.1f km", km);
    } else {
      _snwprintf_s(cap, _TRUNCATE, L"%.2f km", km);
    }
  } else if (niceM >= 1.0) {
    _snwprintf_s(cap, _TRUNCATE, L"%.0f m", niceM);
  } else {
    _snwprintf_s(cap, _TRUNCATE, L"%.2f m", niceM);
  }
  HFONT fnt = CreateFontW(-12, 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE, DEFAULT_CHARSET, OUT_DEFAULT_PRECIS,
                          CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY, DEFAULT_PITCH | FF_DONTCARE, L"Microsoft YaHei UI");
  if (fnt) {
    SetBkMode(hdc, TRANSPARENT);
    SetTextColor(hdc, RGB(35, 40, 50));
    const HGDIOBJ oldF = SelectObject(hdc, fnt);
    TextOutW(hdc, x0, textY, cap, lstrlenW(cap));
    SelectObject(hdc, oldF);
    DeleteObject(fnt);
  }
}

void DrawLatLonGrid(HDC hdc, const RECT& inner, const ViewExtent& view, MapDisplayProjection proj) {
  const int cw = inner.right - inner.left;
  const int ch = inner.bottom - inner.top;
  if (cw <= 0 || ch <= 0 || !view.valid()) {
    return;
  }
  const double lonSpan = view.maxX - view.minX;
  const double latSpan = view.maxY - view.minY;
  double stepLon = std::max(NiceGridStepDeg(lonSpan, 12), 1e-9);
  stepLon = std::max(stepLon, lonSpan / 160.0);
  double stepLat = std::max(NiceGridStepDeg(latSpan, 10), 1e-9);
  stepLat = std::max(stepLat, latSpan / 140.0);

  const auto toScreen = [&](double lon, double lat, double* sx, double* sy) {
    MapProj_GeoLonLatToScreen(proj, view, cw, ch, lon, lat, sx, sy);
  };
  const int curveSteps =
      (proj != MapDisplayProjection::kGeographicWgs84 && MapProj_IsProjectionSelectable(proj)) ? 56 : 1;

  const HBRUSH bg = CreateSolidBrush(RGB(245, 250, 255));
  FillRect(hdc, &inner, bg);
  DeleteObject(bg);

  HPEN penMinor = CreatePen(PS_SOLID, 1, RGB(200, 210, 220));
  HPEN penMajor = CreatePen(PS_SOLID, 1, RGB(160, 175, 190));
  HPEN penPrime = CreatePen(PS_SOLID, 3, RGB(55, 75, 105));
  const HGDIOBJ oldPen = SelectObject(hdc, penMinor);
  SetBkMode(hdc, TRANSPARENT);

  const double lon0 = std::floor(view.minX / stepLon) * stepLon;
  int li = 0;
  for (double lon = lon0; lon <= view.maxX + 1e-7; lon += stepLon, ++li) {
    if (IsPrimeMeridianLon(lon) || IsAntimeridianLon(lon)) {
      continue;
    }
    SelectObject(hdc, (li % 5 == 0) ? penMajor : penMinor);
    for (int s = 0; s < curveSteps; ++s) {
      const double t0 = static_cast<double>(s) / static_cast<double>(curveSteps);
      const double t1 = static_cast<double>(s + 1) / static_cast<double>(curveSteps);
      const double latA = view.minY + t0 * (view.maxY - view.minY);
      const double latB = view.minY + t1 * (view.maxY - view.minY);
      double x0 = 0;
      double y0 = 0;
      double x1 = 0;
      double y1 = 0;
      toScreen(lon, latA, &x0, &y0);
      toScreen(lon, latB, &x1, &y1);
      MoveToEx(hdc, static_cast<int>(std::floor(x0)), static_cast<int>(std::floor(y0)), nullptr);
      LineTo(hdc, static_cast<int>(std::floor(x1)), static_cast<int>(std::floor(y1)));
    }
  }

  const double lat0 = std::floor(view.minY / stepLat) * stepLat;
  int gi = 0;
  for (double lat = lat0; lat <= view.maxY + 1e-7; lat += stepLat, ++gi) {
    if (IsPoleLat(lat) || IsEquatorLat(lat)) {
      continue;
    }
    SelectObject(hdc, (gi % 5 == 0) ? penMajor : penMinor);
    for (int s = 0; s < curveSteps; ++s) {
      const double t0 = static_cast<double>(s) / static_cast<double>(curveSteps);
      const double t1 = static_cast<double>(s + 1) / static_cast<double>(curveSteps);
      const double lonA = view.minX + t0 * (view.maxX - view.minX);
      const double lonB = view.minX + t1 * (view.maxX - view.minX);
      double x0 = 0;
      double y0 = 0;
      double x1 = 0;
      double y1 = 0;
      toScreen(lonA, lat, &x0, &y0);
      toScreen(lonB, lat, &x1, &y1);
      MoveToEx(hdc, static_cast<int>(std::floor(x0)), static_cast<int>(std::floor(y0)), nullptr);
      LineTo(hdc, static_cast<int>(std::floor(x1)), static_cast<int>(std::floor(y1)));
    }
  }

  SelectObject(hdc, penPrime);
  auto drawMeridian = [&](double lon) {
    if (lon < view.minX - 1e-6 || lon > view.maxX + 1e-6) {
      return;
    }
    for (int s = 0; s < curveSteps; ++s) {
      const double t0 = static_cast<double>(s) / static_cast<double>(curveSteps);
      const double t1 = static_cast<double>(s + 1) / static_cast<double>(curveSteps);
      const double latA = view.minY + t0 * (view.maxY - view.minY);
      const double latB = view.minY + t1 * (view.maxY - view.minY);
      double xa = 0;
      double ya = 0;
      double xb = 0;
      double yb = 0;
      toScreen(lon, latA, &xa, &ya);
      toScreen(lon, latB, &xb, &yb);
      MoveToEx(hdc, static_cast<int>(std::floor(xa)), static_cast<int>(std::floor(ya)), nullptr);
      LineTo(hdc, static_cast<int>(std::floor(xb)), static_cast<int>(std::floor(yb)));
    }
  };
  auto drawParallel = [&](double lat) {
    if (lat < view.minY - 1e-6 || lat > view.maxY + 1e-6) {
      return;
    }
    for (int s = 0; s < curveSteps; ++s) {
      const double t0 = static_cast<double>(s) / static_cast<double>(curveSteps);
      const double t1 = static_cast<double>(s + 1) / static_cast<double>(curveSteps);
      const double lonA = view.minX + t0 * (view.maxX - view.minX);
      const double lonB = view.minX + t1 * (view.maxX - view.minX);
      double xa = 0;
      double ya = 0;
      double xb = 0;
      double yb = 0;
      toScreen(lonA, lat, &xa, &ya);
      toScreen(lonB, lat, &xb, &yb);
      MoveToEx(hdc, static_cast<int>(std::floor(xa)), static_cast<int>(std::floor(ya)), nullptr);
      LineTo(hdc, static_cast<int>(std::floor(xb)), static_cast<int>(std::floor(yb)));
    }
  };
  drawMeridian(0.0);
  drawMeridian(180.0);
  drawMeridian(-180.0);
  drawParallel(90.0);
  drawParallel(0.0);
  drawParallel(-90.0);

  SelectObject(hdc, oldPen);
  DeleteObject(penMinor);
  DeleteObject(penMajor);
  DeleteObject(penPrime);

  HFONT fnt = CreateFontW(-12, 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE, DEFAULT_CHARSET, OUT_DEFAULT_PRECIS,
                          CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY, DEFAULT_PITCH | FF_DONTCARE, L"Microsoft YaHei UI");
  if (fnt) {
    HFONT fntBold = CreateFontW(-12, 0, 0, 0, FW_BOLD, FALSE, FALSE, FALSE, DEFAULT_CHARSET, OUT_DEFAULT_PRECIS,
                                CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY, DEFAULT_PITCH | FF_DONTCARE, L"Microsoft YaHei UI");
    const HGDIOBJ oldF = SelectObject(hdc, fnt);
    SetBkMode(hdc, TRANSPARENT);
    SetTextColor(hdc, RGB(55, 65, 85));
    wchar_t buf[32]{};
    li = 0;
    for (double lon = lon0; lon <= view.maxX + 1e-7; lon += stepLon, ++li) {
      if (li % 5 != 0) {
        continue;
      }
      if (IsPrimeMeridianLon(lon) || IsAntimeridianLon(lon)) {
        continue;
      }
      double xs = 0;
      double ys = 0;
      toScreen(lon, view.minY, &xs, &ys);
      const int ix = static_cast<int>(std::floor(xs));
      if (ix < -50 || ix > cw + 50) {
        continue;
      }
      FormatLonLabel(buf, 32, lon);
      TextOutW(hdc, std::max(2, ix - 18), ch - 18, buf, lstrlenW(buf));
    }
    gi = 0;
    for (double lat = lat0; lat <= view.maxY + 1e-7; lat += stepLat, ++gi) {
      if (gi % 5 != 0) {
        continue;
      }
      if (IsPoleLat(lat) || IsEquatorLat(lat)) {
        continue;
      }
      double xs = 0;
      double ys = 0;
      toScreen(view.minX, lat, &xs, &ys);
      const int iy = static_cast<int>(std::floor(ys));
      if (iy < -12 || iy > ch + 12) {
        continue;
      }
      FormatLatLabel(buf, 32, lat);
      TextOutW(hdc, inner.left + 4, std::max(2, iy - 10), buf, lstrlenW(buf));
    }
    if (fntBold) {
      SelectObject(hdc, fntBold);
      SetTextColor(hdc, RGB(20, 35, 65));
      auto stampLonBottom = [&](double lon, const wchar_t* text) {
        if (lon < view.minX - 1e-6 || lon > view.maxX + 1e-6) {
          return;
        }
        double xs = 0;
        double ys = 0;
        toScreen(lon, view.minY, &xs, &ys);
        const int ix = static_cast<int>(std::floor(xs));
        if (ix < -80 || ix > cw + 80) {
          return;
        }
        TextOutW(hdc, std::max(2, ix - 22), ch - 18, text, lstrlenW(text));
      };
      auto stampLatLeft = [&](double lat, const wchar_t* text) {
        if (lat < view.minY - 1e-6 || lat > view.maxY + 1e-6) {
          return;
        }
        double xs = 0;
        double ys = 0;
        toScreen(view.minX, lat, &xs, &ys);
        const int iy = static_cast<int>(std::floor(ys));
        if (iy < -20 || iy > ch + 20) {
          return;
        }
        TextOutW(hdc, inner.left + 4, std::max(2, iy - 10), text, lstrlenW(text));
      };
      stampLonBottom(0.0, L"0°");
      stampLonBottom(180.0, L"180°");
      stampLonBottom(-180.0, L"180°");
      stampLatLeft(90.0, L"90°");
      stampLatLeft(0.0, L"0°");
      stampLatLeft(-90.0, L"-90°");
    }
    SelectObject(hdc, oldF);
    DeleteObject(fnt);
    if (fntBold) {
      DeleteObject(fntBold);
    }
  }
}

}  // namespace
ViewExtent DefaultGeographicView() {
  return ViewExtent{-180.0, -90.0, 180.0, 90.0};
}

void Map::FitViewToLayers() {
  if (layers.empty()) {
    view = DefaultGeographicView();
    refViewWidthDeg = 360.0;
    refViewHeightDeg = 180.0;
    MapEngine::Instance().UpdateMapChrome();
    return;
  }
  ViewExtent e{};
  bool any = false;
  for (const auto& layer : layers) {
    if (!layer->IsLayerVisible()) {
      continue;
    }
    ViewExtent le{};
    if (!layer->GetExtent(le)) {
      continue;
    }
    if (!any) {
      e = le;
      any = true;
    } else {
      e.minX = std::min(e.minX, le.minX);
      e.minY = std::min(e.minY, le.minY);
      e.maxX = std::max(e.maxX, le.maxX);
      e.maxY = std::max(e.maxY, le.maxY);
    }
  }
  if (!any || !e.valid()) {
    MapEngine::Instance().UpdateMapChrome();
    return;
  }
  const double mx = (e.maxX - e.minX) * 0.05;
  const double my = (e.maxY - e.minY) * 0.05;
  view.minX = e.minX - mx;
  view.minY = e.minY - my;
  view.maxX = e.maxX + mx;
  view.maxY = e.maxY + my;
  refViewWidthDeg = view.maxX - view.minX;
  refViewHeightDeg = view.maxY - view.minY;
  MapEngine::Instance().UpdateMapChrome();
}

void Map::EnforceLonLatAspect360_180() {
  if (!layers.empty()) {
    return;
  }
  const double w = view.maxX - view.minX;
  const double h = view.maxY - view.minY;
  if (w <= 1e-18 || h <= 1e-18) {
    return;
  }
  constexpr double kLonLatRatio = 360.0 / 180.0;
  const double cx = (view.minX + view.maxX) * 0.5;
  const double cy = (view.minY + view.maxY) * 0.5;
  const double r = w / h;
  if (std::abs(r - kLonLatRatio) < 1e-12) {
    return;
  }
  if (r > kLonLatRatio) {
    const double newH = w / kLonLatRatio;
    view.minY = cy - newH * 0.5;
    view.maxY = cy + newH * 0.5;
  } else {
    const double newW = h * kLonLatRatio;
    view.minX = cx - newW * 0.5;
    view.maxX = cx + newW * 0.5;
  }
}

void Map::NormalizeEmptyMapView() {
  if (!layers.empty()) {
    return;
  }
  const double w = view.maxX - view.minX;
  const double h = view.maxY - view.minY;
  if (w <= 0.0 || h <= 0.0) {
    view = DefaultGeographicView();
    refViewWidthDeg = 360.0;
    refViewHeightDeg = 180.0;
    return;
  }
  // 极端缩小：纬度跨度超过 180° 时压回 [-90,90]，并与经度等比例缩放（避免只压纬度导致变形）
  if (h > 180.0) {
    const double scale = 180.0 / h;
    const double cx = (view.minX + view.maxX) * 0.5;
    const double newW = w * scale;
    view.minX = cx - newW * 0.5;
    view.maxX = cx + newW * 0.5;
    view.minY = -90.0;
    view.maxY = 90.0;
  }
  EnforceLonLatAspect360_180();
}

bool Map::AddLayerFromFile(const std::wstring& path, std::wstring& err) {
#if !GIS_DESKTOP_HAVE_GDAL
  err = AgisPickUiLang(
      L"本程序未启用 GDAL（GIS_DESKTOP_HAVE_GDAL=0）。请用 AGIS_USE_GDAL=on 重新配置并构建桌面工程（CMake）后重试。"
      L"python build.py 并确保 CMake 配置成功（依赖见 3rdparty/README-GDAL-BUILD.md）。若仅需壳程序，请用 AGIS_USE_GDAL=off 编译。",
      L"This build has GDAL disabled (GIS_DESKTOP_HAVE_GDAL=0). Reconfigure and rebuild the desktop project with "
      L"AGIS_USE_GDAL=on (run python build.py; see 3rdparty/README-GDAL-BUILD.md). For a shell-only build, use "
      L"AGIS_USE_GDAL=off.");
  return false;
#else
  const std::string utf8 = Utf8FromWide(path);
  GDALDataset* ds = agis_detail::OpenGdalDatasetForLocalFile(path, utf8, err);
  if (!ds) {
    return false;
  }
  std::wstring base = path;
  const size_t slash = base.find_last_of(L"/\\");
  if (slash != std::wstring::npos) {
    base = base.substr(slash + 1);
  }
  auto layer = agis_detail::CreateLayerFromDataset(ds, base, path, MapDataSourceKind::kGdalFile, err);
  if (!layer) {
    return false;
  }
  layers.push_back(std::move(layer));
  FitViewToLayers();
  return true;
#endif
}

bool Map::AddLayerFromTmsUrl(const std::wstring& url, std::wstring& err) {
#if !GIS_DESKTOP_HAVE_GDAL
  (void)url;
  err = AgisTr(AgisUiStr::DocErrGdalOffTms);
  return false;
#else
  AgisEnsureGdalDataPath();
  GDALAllRegister();
  auto layer = agis_detail::CreateLayerFromTmsUrl(url, err);
  if (!layer) {
    return false;
  }
  layers.push_back(std::move(layer));
  FitViewToLayers();
  return true;
#endif
}

bool Map::AddLayerFromWmtsUrl(const std::wstring& url, std::wstring& err) {
#if !GIS_DESKTOP_HAVE_GDAL
  (void)url;
  err = AgisTr(AgisUiStr::DocErrGdalOffWmts);
  return false;
#else
  AgisEnsureGdalDataPath();
  GDALAllRegister();
  auto layer = agis_detail::CreateLayerFromWmtsUrl(url, err);
  if (!layer) {
    return false;
  }
  layers.push_back(std::move(layer));
  FitViewToLayers();
  return true;
#endif
}

bool Map::AddLayerFromArcGisRestJsonUrl(const std::wstring& url, std::wstring& err) {
#if !GIS_DESKTOP_HAVE_GDAL
  (void)url;
  err = AgisTr(AgisUiStr::DocErrGdalOffArcGis);
  return false;
#else
  AgisEnsureGdalDataPath();
  GDALAllRegister();
  auto layer = agis_detail::CreateLayerFromArcGisRestJsonUrl(url, err);
  if (!layer) {
    return false;
  }
  layers.push_back(std::move(layer));
  FitViewToLayers();
  return true;
#endif
}

bool Map::ReplaceLayerAt(size_t index, std::unique_ptr<MapLayer> layer, std::wstring& err) {
  if (!layer) {
    err = AgisTr(AgisUiStr::DocErrInvalidLayer);
    return false;
  }
  if (index >= layers.size()) {
    err = AgisTr(AgisUiStr::DocErrLayerIndex);
    return false;
  }
  layers[index] = std::move(layer);
  FitViewToLayers();
  return true;
}

bool Map::RemoveLayerAt(size_t index, std::wstring& err) {
  if (index >= layers.size()) {
    err = AgisTr(AgisUiStr::DocErrLayerIndex);
    return false;
  }
  layers.erase(layers.begin() + static_cast<ptrdiff_t>(index));
  FitViewToLayers();
  return true;
}

void Map::MoveLayerUp(size_t index) {
  if (index == 0 || index >= layers.size()) {
    return;
  }
  std::swap(layers[index - 1], layers[index]);
}

void Map::MoveLayerDown(size_t index) {
  if (index + 1 >= layers.size()) {
    return;
  }
  std::swap(layers[index], layers[index + 1]);
}

void Map::Draw(HDC hdcMem, const RECT& client) {
  const int cw = client.right - client.left;
  const int ch = client.bottom - client.top;
  if (cw <= 0 || ch <= 0) {
    FillRect(hdcMem, &client, reinterpret_cast<HBRUSH>(COLOR_WINDOW + 1));
    return;
  }
  const RECT innerLocal{0, 0, cw, ch};
  if (!view.valid()) {
    FillRect(hdcMem, &innerLocal, reinterpret_cast<HBRUSH>(COLOR_WINDOW + 1));
    return;
  }
  if (layers.empty()) {
    if (showLatLonGrid) {
      DrawLatLonGrid(hdcMem, innerLocal, view, displayProjection);
    } else {
      const HBRUSH bg = CreateSolidBrush(RGB(245, 250, 255));
      FillRect(hdcMem, &innerLocal, bg);
      DeleteObject(bg);
    }
    DrawScaleBar(hdcMem, innerLocal, view);
    return;
  }

  FillRect(hdcMem, &innerLocal, reinterpret_cast<HBRUSH>(COLOR_WINDOW + 1));

#if GIS_DESKTOP_HAVE_GDAL
  for (const auto& layer : layers) {
    if (!layer->IsLayerVisible()) {
      continue;
    }
    layer->Draw(hdcMem, innerLocal, view);
  }
#else
  UiPaintMapCenterHint(hdcMem, innerLocal,
                       AgisPickUiLang(L"当前构建未启用 GDAL。\n请安装 PROJ 与 GDAL（见 3rdparty/README-GDAL-BUILD.md），或设置 "
                                      L"AGIS_USE_GDAL=on 与 CMAKE_PREFIX_PATH，再重新配置。",
                                      L"This build does not have GDAL enabled.\nInstall PROJ and GDAL (see "
                                      L"3rdparty/README-GDAL-BUILD.md), or set AGIS_USE_GDAL=on and CMAKE_PREFIX_PATH, "
                                      L"then reconfigure."));
#endif
  DrawScaleBar(hdcMem, innerLocal, view);
}

void Map::ScreenToWorld(int sx, int sy, int cw, int ch, double* wx, double* wy) const {
  if (layers.empty() && MapProj_IsProjectionSelectable(displayProjection)) {
    MapProj_ScreenToGeoLonLat(displayProjection, view, cw, ch, sx, sy, wx, wy);
    return;
  }
  const double worldW = view.maxX - view.minX;
  const double worldH = view.maxY - view.minY;
  if (worldW <= 0.0 || worldH <= 0.0 || cw <= 0 || ch <= 0) {
    *wx = view.minX;
    *wy = view.minY;
    return;
  }
  const double fx = static_cast<double>(sx) / static_cast<double>(cw);
  const double fy = static_cast<double>(sy) / static_cast<double>(ch);
  *wx = view.minX + fx * worldW;
  *wy = view.maxY - fy * worldH;
}

void Map::ZoomAt(int sx, int sy, int cw, int ch, double factor) {
  if (!view.valid() || cw <= 0 || ch <= 0 || factor <= 0.0) {
    return;
  }
  const double worldW = view.maxX - view.minX;
  const double worldH = view.maxY - view.minY;
  if (worldW <= 0.0 || worldH <= 0.0) {
    return;
  }
  double wx = 0;
  double wy = 0;
  ScreenToWorld(sx, sy, cw, ch, &wx, &wy);
  const double newW = worldW / factor;
  const double newH = worldH / factor;
  const double rx = static_cast<double>(sx) / static_cast<double>(cw);
  const double ry = static_cast<double>(sy) / static_cast<double>(ch);
  view.minX = wx - rx * newW;
  view.maxX = view.minX + newW;
  view.maxY = wy + ry * newH;
  view.minY = view.maxY - newH;
  NormalizeEmptyMapView();
  MapEngine::Instance().UpdateMapChrome();
}

void Map::PanPixels(int dx, int dy, int cw, int ch) {
  if (!view.valid() || cw <= 0 || ch <= 0) {
    return;
  }
  const double worldW = view.maxX - view.minX;
  const double worldH = view.maxY - view.minY;
  if (worldW <= 0.0 || worldH <= 0.0) {
    return;
  }
  const double pxPerWorldX = static_cast<double>(cw) / worldW;
  const double pxPerWorldY = static_cast<double>(ch) / worldH;
  view.minX -= static_cast<double>(dx) / pxPerWorldX;
  view.maxX -= static_cast<double>(dx) / pxPerWorldX;
  view.minY += static_cast<double>(dy) / pxPerWorldY;
  view.maxY += static_cast<double>(dy) / pxPerWorldY;
  NormalizeEmptyMapView();
  MapEngine::Instance().UpdateMapChrome();
}

void Map::ZoomViewAtCenter(double factor, int cw, int ch) {
  ZoomAt(cw / 2, ch / 2, cw, ch, factor);
}

void Map::ResetZoom100AnchorCenter(int cw, int ch) {
  if (!view.valid() || cw <= 0 || ch <= 0) {
    return;
  }
  const double cx = (view.minX + view.maxX) * 0.5;
  const double cy = (view.minY + view.maxY) * 0.5;
  const double worldW = view.maxX - view.minX;
  const double worldH = view.maxY - view.minY;
  if (worldW <= 0.0 || worldH <= 0.0) {
    return;
  }
  if (layers.empty()) {
    view.minX = cx - refViewWidthDeg * 0.5;
    view.maxX = cx + refViewWidthDeg * 0.5;
    view.maxY = cy + refViewHeightDeg * 0.5;
    view.minY = cy - refViewHeightDeg * 0.5;
    NormalizeEmptyMapView();
    MapEngine::Instance().UpdateMapChrome();
    return;
  }
  const double ar = worldH / worldW;
  double useW = refViewWidthDeg;
  double useH = useW * ar;
  view.minX = cx - useW * 0.5;
  view.maxX = cx + useW * 0.5;
  view.maxY = cy + useH * 0.5;
  view.minY = cy - useH * 0.5;
  NormalizeEmptyMapView();
  MapEngine::Instance().UpdateMapChrome();
}

void Map::CenterContentOrigin(int cw, int ch) {
  (void)cw;
  (void)ch;
  double cx = 0.0;
  double cy = 0.0;
  ViewExtent ex{};
  bool any = false;
  for (const auto& layer : layers) {
    ViewExtent le{};
    if (!layer->GetExtent(le)) {
      continue;
    }
    if (!any) {
      ex = le;
      any = true;
    } else {
      ex.minX = std::min(ex.minX, le.minX);
      ex.minY = std::min(ex.minY, le.minY);
      ex.maxX = std::max(ex.maxX, le.maxX);
      ex.maxY = std::max(ex.maxY, le.maxY);
    }
  }
  if (any && ex.valid()) {
    cx = (ex.minX + ex.maxX) * 0.5;
    cy = (ex.minY + ex.maxY) * 0.5;
  }
  const double w = view.maxX - view.minX;
  const double h = view.maxY - view.minY;
  view.minX = cx - w * 0.5;
  view.maxX = cx + w * 0.5;
  view.maxY = cy + h * 0.5;
  view.minY = cy - h * 0.5;
  NormalizeEmptyMapView();
  MapEngine::Instance().UpdateMapChrome();
}

int Map::ScalePercentForUi() const {
  const double w = view.maxX - view.minX;
  const double h = view.maxY - view.minY;
  if (w <= 1e-18 || h <= 1e-18) {
    return 100;
  }
  const double pw = refViewWidthDeg / w * 100.0;
  const double ph = refViewHeightDeg / h * 100.0;
  return static_cast<int>(std::lround((pw + ph) * 0.5));
}

void Map::SetShowLatLonGrid(bool on) {
  showLatLonGrid = on;
}

void Map::SetDisplayProjection(MapDisplayProjection p) {
  if (p < MapDisplayProjection::kGeographicWgs84 || p >= MapDisplayProjection::kCount) {
    return;
  }
#if !GIS_DESKTOP_HAVE_GDAL
  if (p != MapDisplayProjection::kGeographicWgs84) {
    p = MapDisplayProjection::kGeographicWgs84;
  }
#endif
  displayProjection = p;
  MapProj_InvalidateBoundsCache();
}

#if GIS_DESKTOP_HAVE_GDAL
namespace agis_detail {

namespace {

bool PathLooksLikeOsm(const std::string& u8) {
  if (u8.size() < 4) {
    return false;
  }
  auto tail_lower = [&u8](size_t len) {
    std::string s = u8.substr(u8.size() - len);
    for (char& c : s) {
      c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    }
    return s;
  };
  if (u8.size() >= 8 && tail_lower(8) == ".osm.pbf") {
    return true;
  }
  if (tail_lower(4) == ".pbf") {
    return true;
  }
  if (tail_lower(4) == ".osm") {
    return true;
  }
  return false;
}

}  // namespace

GDALDataset* OpenGdalDatasetForLocalFile(const std::wstring& pathWide, const std::string& utf8Path,
                                         std::wstring& err) {
  AgisEnsureGdalDataPath();
  GDALAllRegister();
  const unsigned kV = GDAL_OF_VERBOSE_ERROR;
  /** 每次重试前会 `CPLErrorReset()`，须在当次失败后立刻记下 CPL 信息，否则最终被清空。 */
  std::string last_cpl_utf8;

  auto try_open = [&](unsigned flags, const char* const* allowed_drivers,
                      const char* const* open_options) -> GDALDataset* {
    CPLErrorReset();
    GDALDataset* ds = static_cast<GDALDataset*>(
        GDALOpenEx(utf8Path.c_str(), flags, allowed_drivers, open_options, nullptr));
    if (!ds) {
      const char* msg = CPLGetLastErrorMsg();
      if (msg && msg[0]) {
        last_cpl_utf8 = msg;
      }
    }
    return ds;
  };

  static const char* kOsmOnly[] = {"OSM", nullptr};
  static const char* kOsmSqliteIndex[] = {"USE_CUSTOM_INDEXING=NO", nullptr};

  /** OSM Identify 在文件头中查找 `OSMHeader`；默认仅读入约 1KB，大文件首 blob 极罕见情况下可增大。 */
  struct OsmHeaderIngestGuard {
    const char* prev{};
    OsmHeaderIngestGuard() {
      prev = CPLGetThreadLocalConfigOption("GDAL_INGESTED_BYTES_AT_OPEN", nullptr);
      CPLSetThreadLocalConfigOption("GDAL_INGESTED_BYTES_AT_OPEN", "1048576");
    }
    ~OsmHeaderIngestGuard() {
      CPLSetThreadLocalConfigOption("GDAL_INGESTED_BYTES_AT_OPEN", prev);
    }
  };

  if (PathLooksLikeOsm(utf8Path)) {
    const OsmHeaderIngestGuard ingest_guard{};
    if (GDALDataset* ds = try_open(GDAL_OF_VECTOR | GDAL_OF_SHARED | kV, kOsmOnly, nullptr)) {
      return ds;
    }
    if (GDALDataset* ds = try_open(GDAL_OF_RASTER | GDAL_OF_VECTOR | GDAL_OF_SHARED | kV, kOsmOnly, nullptr)) {
      return ds;
    }
    if (GDALDataset* ds = try_open(GDAL_OF_VECTOR | GDAL_OF_SHARED | kV, kOsmOnly, kOsmSqliteIndex)) {
      return ds;
    }
    if (GDALDataset* ds =
            try_open(GDAL_OF_RASTER | GDAL_OF_VECTOR | GDAL_OF_SHARED | kV, kOsmOnly, kOsmSqliteIndex)) {
      return ds;
    }
  }

  if (GDALDataset* ds = try_open(GDAL_OF_VECTOR | GDAL_OF_SHARED | kV, nullptr, nullptr)) {
    return ds;
  }
  if (GDALDataset* ds = try_open(GDAL_OF_RASTER | GDAL_OF_VECTOR | GDAL_OF_SHARED | kV, nullptr, nullptr)) {
    return ds;
  }
  if (GDALDataset* ds = try_open(GDAL_OF_RASTER | GDAL_OF_VECTOR | GDAL_OF_SHARED | GDAL_OF_UPDATE | kV, nullptr,
                                 nullptr)) {
    return ds;
  }
  if (GDALDataset* ds =
          try_open(GDAL_OF_RASTER | GDAL_OF_VECTOR | GDAL_OF_SHARED | GDAL_OF_UPDATE, nullptr, nullptr)) {
    return ds;
  }
  if (GDALDataset* ds = try_open(GDAL_OF_RASTER | GDAL_OF_VECTOR | GDAL_OF_SHARED, nullptr, nullptr)) {
    return ds;
  }

  if (PathLooksLikeOsm(utf8Path)) {
    if (GDALDataset* ds = try_open(GDAL_OF_VECTOR | GDAL_OF_SHARED | kV, nullptr, kOsmSqliteIndex)) {
      return ds;
    }
    if (GDALDataset* ds = try_open(GDAL_OF_RASTER | GDAL_OF_VECTOR | GDAL_OF_SHARED | kV, nullptr, kOsmSqliteIndex)) {
      return ds;
    }
    if (GDALDataset* ds = try_open(GDAL_OF_RASTER | GDAL_OF_VECTOR | GDAL_OF_SHARED | GDAL_OF_UPDATE | kV, nullptr,
                                   kOsmSqliteIndex)) {
      return ds;
    }
    if (GDALDataset* ds = try_open(GDAL_OF_RASTER | GDAL_OF_VECTOR | GDAL_OF_SHARED, nullptr, kOsmSqliteIndex)) {
      return ds;
    }
  }

  err = AgisTr(AgisUiStr::DocErrOpenDsPrefix);
  err += pathWide;
  err += L"\n";
  if (!last_cpl_utf8.empty()) {
    err += WideFromUtf8(last_cpl_utf8.c_str());
    err += L"\n";
  }
  if (PathLooksLikeOsm(utf8Path) && !GDALGetDriverByName("OSM")) {
    err += AgisTr(AgisUiStr::DocErrOsmDriver);
  }
  if (last_cpl_utf8.empty()) {
    VSIStatBufL st{};
    if (VSIStatL(utf8Path.c_str(), &st) != 0) {
      err += AgisTr(AgisUiStr::DocErrFileAccess);
    }
  }
  if (PathLooksLikeOsm(utf8Path)) {
    err += AgisTr(AgisUiStr::DocErrPlanetPbfHint);
  }
  return nullptr;
}

}  // namespace agis_detail
#endif  // GIS_DESKTOP_HAVE_GDAL
