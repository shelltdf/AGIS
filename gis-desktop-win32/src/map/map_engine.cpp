#include "map/map_engine.h"

#include "map/map_engine_internal.h"
#include "map/map_utf8.h"

#include <algorithm>
#include <cmath>
#include <cstring>
#include <cstdio>
#include <cwctype>
#include <memory>
#include <string>
#include <utility>
#include <vector>

#include <windowsx.h>
#include <commdlg.h>

#include <gdiplus.h>

#include "app/resource.h"
#include "app/ui_font.h"
#include "core/app_log.h"
#include "ui/gdiplus_ui.h"

#pragma comment(lib, "comdlg32.lib")

#if GIS_DESKTOP_HAVE_GDAL
#include "cpl_conv.h"
#include "gdal_alg.h"
#include "gdal_priv.h"
#include "ogr_core.h"
#include "ogrsf_frmts.h"
#endif

namespace {

/** 左侧图层自绘列表：每项高度与左侧可见性点击区宽度。 */
constexpr int kLayerListItemHeight = 80;
constexpr int kVisToggleWidth = 32;

void ApplyUiFontToChildren(HWND parent) {
  const HFONT f = UiGetAppFont();
  for (HWND c = GetWindow(parent, GW_CHILD); c; c = GetWindow(c, GW_HWNDNEXT)) {
    SendMessageW(c, WM_SETFONT, reinterpret_cast<WPARAM>(f), TRUE);
  }
}

}  // namespace

/** 列表块内一行显示用的短驱动名（避免过长）。 */
static const wchar_t* MapLayerDriverKindShort(MapLayerDriverKind k) {
  switch (k) {
    case MapLayerDriverKind::kGdalFile:
      return L"GDAL 文件";
    case MapLayerDriverKind::kTmsXyz:
      return L"TMS/XYZ";
    case MapLayerDriverKind::kWmts:
      return L"WMTS";
    case MapLayerDriverKind::kArcGisRestJson:
      return L"ArcGIS JSON";
    case MapLayerDriverKind::kSoapPlaceholder:
      return L"SOAP（占位）";
    case MapLayerDriverKind::kWmsPlaceholder:
      return L"WMS（占位）";
    default:
      return L"未知";
  }
}

#if GIS_DESKTOP_HAVE_GDAL

#include "map/map_layer_driver_gdal.h"

namespace agis_detail {

void ApplyDefaultGeoTransform(double* gt, int w, int h) {
  gt[0] = 0.0;
  gt[1] = 1.0;
  gt[2] = 0.0;
  gt[3] = 0.0;
  gt[4] = 0.0;
  gt[5] = -1.0;
  (void)w;
  (void)h;
}

// Windows 24 bpp DIB: each scan line is padded to a multiple of 4 bytes. Tight-packed RGB
// (bmx * 3 * bmy) misaligns rows when (bmx * 3) % 4 != 0, corrupting StretchDIBits output
// (typical symptom: magenta/purple speckles and banding).
inline int DibRowBytes24(int widthPx) {
  return ((widthPx * 3 + 3) & ~3);
}

/** 32 bpp top-down DIB：每行 4 字节对齐，避免 StretchDIBits 在部分驱动上对 24bpp 的错位/洋红噪点。 */
inline int DibRowBytes32(int widthPx) {
  return widthPx * 4;
}

void GeoExtentFromGT(const double* gt, int w, int h, ViewExtent* ex) {
  const double xs[4] = {0, static_cast<double>(w), 0, static_cast<double>(w)};
  const double ys[4] = {0, 0, static_cast<double>(h), static_cast<double>(h)};
  double minx = 1e300;
  double miny = 1e300;
  double maxx = -1e300;
  double maxy = -1e300;
  for (int i = 0; i < 4; ++i) {
    const double gx = gt[0] + xs[i] * gt[1] + ys[i] * gt[2];
    const double gy = gt[3] + xs[i] * gt[4] + ys[i] * gt[5];
    minx = std::min(minx, gx);
    miny = std::min(miny, gy);
    maxx = std::max(maxx, gx);
    maxy = std::max(maxy, gy);
  }
  ex->minX = minx;
  ex->minY = miny;
  ex->maxX = maxx;
  ex->maxY = maxy;
}

void AppendUtf8MetaLines(const char* const* meta, std::wstring* out) {
  if (!out || !meta) {
    return;
  }
  for (int i = 0; meta[i] != nullptr; ++i) {
    *out += L"  ";
    *out += WideFromUtf8(meta[i]);
    *out += L"\r\n";
  }
}

/** 列出 GDALMajorObject 上所有元数据域（默认域、IMAGE_STRUCTURE、SUBDATASETS 等）。 */
void AppendGdalObjectMetadataDomains(GDALMajorObjectH obj, std::wstring* out) {
  if (!obj || !out) {
    return;
  }
  char** domains = GDALGetMetadataDomainList(obj);
  if (!domains) {
    return;
  }
  for (int i = 0; domains[i] != nullptr; ++i) {
    const char* dom = domains[i];
    char** meta = GDALGetMetadata(obj, dom);
    if (!meta || meta[0] == nullptr) {
      continue;
    }
    *out += L"【GDAL 元数据";
    if (dom[0] == '\0') {
      *out += L" · 默认域 \"\"";
    } else {
      *out += L" · ";
      *out += WideFromUtf8(dom);
    }
    *out += L"】\r\n";
    AppendUtf8MetaLines(meta, out);
    *out += L"\r\n";
  }
  CSLDestroy(domains);
}

void AppendDriverMetadata(GDALDriver* drv, std::wstring* out) {
  if (!drv || !out) {
    return;
  }
  *out += L"【GDAL 驱动对象】\r\n";
  *out += L"  ShortName: ";
  *out += WideFromUtf8(drv->GetDescription());
  *out += L"\r\n";
  const char* longName = drv->GetMetadataItem(GDAL_DMD_LONGNAME);
  if (longName && longName[0]) {
    *out += L"  LongName: ";
    *out += WideFromUtf8(longName);
    *out += L"\r\n";
  }
  char** dm = drv->GetMetadata();
  if (dm && dm[0]) {
    *out += L"  驱动元数据项:\r\n";
    AppendUtf8MetaLines(dm, out);
  }
  *out += L"\r\n";
}

void AppendDatasetFileList(GDALDataset* ds, std::wstring* out) {
  if (!ds || !out) {
    return;
  }
  char** fl = ds->GetFileList();
  if (!fl || !fl[0]) {
    return;
  }
  *out += L"【数据集组成文件 GDALDataset::GetFileList】\r\n";
  for (int i = 0; fl[i] != nullptr; ++i) {
    *out += L"  ";
    *out += WideFromUtf8(fl[i]);
    *out += L"\r\n";
  }
  CSLDestroy(fl);
  *out += L"\r\n";
}

void AppendGcpSummary(GDALDataset* ds, std::wstring* out) {
  if (!ds || !out) {
    return;
  }
  const int n = ds->GetGCPCount();
  if (n <= 0) {
    return;
  }
  *out += L"【GCP 地面控制点 GDALDataset::GetGCPs】\r\n";
  *out += L"  数量: " + std::to_wstring(n) + L"\r\n";
  const char* gcpProj = ds->GetGCPProjection();
  if (gcpProj && gcpProj[0]) {
    std::wstring w = WideFromUtf8(gcpProj);
    if (w.size() > 500) {
      w.resize(500);
      w += L"…";
    }
    *out += L"  GetGCPProjection:\r\n  ";
    *out += w;
    *out += L"\r\n";
  }
  const GDAL_GCP* gcps = ds->GetGCPs();
  if (gcps) {
    const int show = (std::min)(n, 32);
    for (int i = 0; i < show; ++i) {
      const GDAL_GCP& g = gcps[i];
      *out += L"  [" + std::to_wstring(i) + L"] Id=";
      *out += WideFromUtf8(g.pszId ? g.pszId : "");
      *out += L"  Pixel=" + std::to_wstring(g.dfGCPPixel) + L" Line=" + std::to_wstring(g.dfGCPLine);
      *out += L"  X=" + std::to_wstring(g.dfGCPX) + L" Y=" + std::to_wstring(g.dfGCPY) + L" Z=" + std::to_wstring(g.dfGCPZ);
      *out += L"\r\n";
    }
    if (n > show) {
      *out += L"  … 其余 " + std::to_wstring(n - show) + L" 条未列出\r\n";
    }
  }
  *out += L"\r\n";
}

void AppendRasterBandExtras(GDALRasterBand* b, std::wstring* out) {
  if (!b || !out) {
    return;
  }
  int okNd = 0;
  const double ndv = b->GetNoDataValue(&okNd);
  if (okNd) {
    *out += L"    NoData (GetNoDataValue): " + std::to_wstring(ndv) + L"\r\n";
  }
  int okS = 0;
  int okO = 0;
  const double sc = b->GetScale(&okS);
  const double off = b->GetOffset(&okO);
  if (okS) {
    *out += L"    Scale (GetScale): " + std::to_wstring(sc) + L"\r\n";
  }
  if (okO) {
    *out += L"    Offset (GetOffset): " + std::to_wstring(off) + L"\r\n";
  }
  const char* ut = b->GetUnitType();
  if (ut && ut[0]) {
    *out += L"    UnitType: ";
    *out += WideFromUtf8(ut);
    *out += L"\r\n";
  }
  GDALColorTable* ct = b->GetColorTable();
  if (ct && ct->GetColorEntryCount() > 0) {
    *out += L"    颜色表项数: " + std::to_wstring(ct->GetColorEntryCount()) + L"\r\n";
  }
  const char* desc = b->GetDescription();
  if (desc && desc[0]) {
    *out += L"    波段描述 GetDescription: ";
    *out += WideFromUtf8(desc);
    *out += L"\r\n";
  }
  AppendGdalObjectMetadataDomains(reinterpret_cast<GDALMajorObjectH>(b), out);
}

void AppendOgrLayerDetails(OGRLayer* lay, int index, std::wstring* out) {
  if (!lay || !out) {
    return;
  }
  *out += L"【OGRLayer · GetLayer(" + std::to_wstring(index) + L")】\r\n";
  *out += L"  GetName: ";
  *out += WideFromUtf8(lay->GetName());
  *out += L"\r\n";
  *out += L"  GetGeomType: ";
  *out += WideFromUtf8(OGRGeometryTypeToName(wkbFlatten(lay->GetGeomType())));
  *out += L"\r\n";
  *out += L"  GetFeatureCount(1): " + std::to_wstring(lay->GetFeatureCount(true)) + L"（估算/扫描，依驱动而定）\r\n";
  const char* fid = lay->GetFIDColumn();
  *out += L"  GetFIDColumn: ";
  *out += WideFromUtf8(fid && fid[0] ? fid : "(默认)");
  *out += L"\r\n";
  const char* gcn = lay->GetGeometryColumn();
  if (gcn && gcn[0]) {
    *out += L"  GetGeometryColumn: ";
    *out += WideFromUtf8(gcn);
    *out += L"\r\n";
  }
  const OGRSpatialReference* srs = lay->GetSpatialRef();
  if (srs) {
    char* wkt = nullptr;
    const OGRErr er = srs->exportToWkt(&wkt);
    if (er == OGRERR_NONE && wkt) {
      std::wstring w = WideFromUtf8(wkt);
      if (w.size() > 2000) {
        w.resize(2000);
        w += L"…";
      }
      *out += L"  GetSpatialRef (WKT):\r\n  ";
      *out += w;
      *out += L"\r\n";
    }
    if (wkt) {
      CPLFree(wkt);
    }
  } else {
    *out += L"  GetSpatialRef: （无）\r\n";
  }
  OGRFeatureDefn* defn = lay->GetLayerDefn();
  if (defn) {
    *out += L"  OGRFeatureDefn 字段数 GetFieldCount: " + std::to_wstring(defn->GetFieldCount()) + L"\r\n";
    for (int fi = 0; fi < defn->GetFieldCount(); ++fi) {
      OGRFieldDefn* f = defn->GetFieldDefn(fi);
      if (!f) {
        continue;
      }
      *out += L"    [" + std::to_wstring(fi) + L"] ";
      *out += WideFromUtf8(f->GetNameRef());
      *out += L"  ";
      *out += WideFromUtf8(OGR_GetFieldTypeName(f->GetType()));
      *out += L"\r\n";
    }
  }
  *out += L"  图层元数据（各域）:\r\n";
  AppendGdalObjectMetadataDomains(reinterpret_cast<GDALMajorObjectH>(lay), out);
}

class RasterMapLayer final : public MapLayer {
 public:
  RasterMapLayer(GDALDataset* ds, std::wstring name, std::wstring sourcePath, MapLayerDriverKind driverKind)
      : MapLayer(std::make_unique<GdalRasterMapLayerDriver>(driverKind)),
        ds_(ds),
        name_(std::move(name)),
        sourcePath_(std::move(sourcePath)) {}
  ~RasterMapLayer() override { GDALClose(ds_); }

  std::wstring DisplayName() const override { return name_; }

  MapLayerKind GetKind() const override { return MapLayerKind::kRasterGdal; }

  bool GetExtent(ViewExtent& out) const override {
    const int w = ds_->GetRasterXSize();
    const int h = ds_->GetRasterYSize();
    double gt[6]{};
    if (ds_->GetGeoTransform(gt) != CE_None) {
      ApplyDefaultGeoTransform(gt, w, h);
    }
    GeoExtentFromGT(gt, w, h, &out);
    return out.valid();
  }

  void Draw(HDC hdc, const RECT& client, const ViewExtent& view) const override {
    if (!view.valid()) {
      return;
    }
    const int cw = client.right - client.left;
    const int ch = client.bottom - client.top;
    if (cw <= 0 || ch <= 0) {
      return;
    }
    const int w = ds_->GetRasterXSize();
    const int h = ds_->GetRasterYSize();
    double gt[6]{};
    if (ds_->GetGeoTransform(gt) != CE_None) {
      ApplyDefaultGeoTransform(gt, w, h);
    }
    double inv[6]{};
    if (!GDALInvGeoTransform(gt, inv)) {
      return;
    }

    const double gx0 = std::min(view.minX, view.maxX);
    const double gx1 = std::max(view.minX, view.maxX);
    const double gy0 = std::min(view.minY, view.maxY);
    const double gy1 = std::max(view.minY, view.maxY);

    auto geoToPx = [&](double gx, double gy, double* px, double* py) {
      *px = inv[0] + gx * inv[1] + gy * inv[2];
      *py = inv[3] + gx * inv[4] + gy * inv[5];
    };

    double pminx = 1e300;
    double pminy = 1e300;
    double pmaxx = -1e300;
    double pmaxy = -1e300;
    const double cx[4] = {gx0, gx1, gx0, gx1};
    const double cy[4] = {gy0, gy0, gy1, gy1};
    for (int i = 0; i < 4; ++i) {
      double px = 0;
      double py = 0;
      geoToPx(cx[i], cy[i], &px, &py);
      pminx = std::min(pminx, px);
      pminy = std::min(pminy, py);
      pmaxx = std::max(pmaxx, px);
      pmaxy = std::max(pmaxy, py);
    }

    int rx0 = static_cast<int>(std::floor(pminx));
    int ry0 = static_cast<int>(std::floor(pminy));
    int rx1 = static_cast<int>(std::ceil(pmaxx));
    int ry1 = static_cast<int>(std::ceil(pmaxy));
    rx0 = std::max(0, std::min(rx0, w - 1));
    ry0 = std::max(0, std::min(ry0, h - 1));
    rx1 = std::max(1, std::min(rx1, w));
    ry1 = std::max(1, std::min(ry1, h));
    const int rw = rx1 - rx0;
    const int rh = ry1 - ry0;
    if (rw <= 0 || rh <= 0) {
      return;
    }

    constexpr int kMaxDim = 4096;
    int step = 1;
    while (rw / step > kMaxDim || rh / step > kMaxDim) {
      step *= 2;
    }
    const int bmx = std::max(1, rw / step);
    const int bmy = std::max(1, rh / step);

    GDALRasterBand* bR = ds_->GetRasterBand(1);
    GDALRasterBand* bG = ds_->GetRasterCount() >= 2 ? ds_->GetRasterBand(2) : bR;
    GDALRasterBand* bB = ds_->GetRasterCount() >= 3 ? ds_->GetRasterBand(3) : bR;

    const size_t pixCount = static_cast<size_t>(bmx) * static_cast<size_t>(bmy);
    std::vector<GByte> bufR(pixCount);
    std::vector<GByte> bufG(pixCount);
    std::vector<GByte> bufB(pixCount);
    if (bR->RasterIO(GF_Read, rx0, ry0, rw, rh, bufR.data(), bmx, bmy, GDT_Byte, 0, 0) != CE_None) {
      return;
    }
    if (bG->RasterIO(GF_Read, rx0, ry0, rw, rh, bufG.data(), bmx, bmy, GDT_Byte, 0, 0) != CE_None) {
      return;
    }
    if (bB->RasterIO(GF_Read, rx0, ry0, rw, rh, bufB.data(), bmx, bmy, GDT_Byte, 0, 0) != CE_None) {
      return;
    }

    auto worldToScreenD = [&](double wx, double wy, double* sx, double* sy) {
      const double invW = 1.0 / (view.maxX - view.minX);
      const double invH = 1.0 / (view.maxY - view.minY);
      *sx = (wx - view.minX) * invW * static_cast<double>(cw);
      *sy = (view.maxY - wy) * invH * static_cast<double>(ch);
    };
    /** 像素 (px,py) → 地理 → 屏幕；与矢量 WorldToScreenXY 一致，支持任意仿射 GT（含旋转/剪切）。 */
    auto pixelToScreen = [&](double px, double py, double* sx, double* sy) {
      const double gx = gt[0] + px * gt[1] + py * gt[2];
      const double gy = gt[3] + px * gt[4] + py * gt[5];
      worldToScreenD(gx, gy, sx, sy);
    };

    const int rowBytes = DibRowBytes32(bmx);
    std::vector<GByte> bgra(static_cast<size_t>(rowBytes) * static_cast<size_t>(bmy), 0);
    for (int y = 0; y < bmy; ++y) {
      GByte* row = bgra.data() + static_cast<size_t>(y) * static_cast<size_t>(rowBytes);
      for (int x = 0; x < bmx; ++x) {
        const size_t si = static_cast<size_t>(y) * static_cast<size_t>(bmx) + static_cast<size_t>(x);
        GByte* px = row + static_cast<size_t>(x) * 4u;
        px[0] = bufB[si];
        px[1] = bufG[si];
        px[2] = bufR[si];
        px[3] = 255;
      }
    }

    BITMAPINFO bi{};
    bi.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
    bi.bmiHeader.biWidth = bmx;
    bi.bmiHeader.biHeight = -bmy;
    bi.bmiHeader.biPlanes = 1;
    bi.bmiHeader.biBitCount = 32;
    bi.bmiHeader.biCompression = BI_RGB;
    bi.bmiHeader.biSizeImage = static_cast<DWORD>(static_cast<size_t>(rowBytes) * static_cast<size_t>(bmy));

    // 必须用 PlgBlt 做「像素矩形 → 屏幕」的仿射：地理 GT × 视口线性变换与矢量 WorldToScreenXY 一致。
    // 此前对正北栅格用 StretchDIBits 填轴对齐包络框，相当于对 DIB 做独立 X/Y 缩放，与真实仿射在强缩放
    // （如界面缩放低于 100%、大幅下采样）下不一致，会出现 shp 正常而 GeoTIFF 被拉扁/错位等变形。

    const double px0 = static_cast<double>(rx0);
    const double py0 = static_cast<double>(ry0);
    const double px1 = static_cast<double>(rx0 + rw);
    const double py1 = static_cast<double>(ry0);
    const double px2 = static_cast<double>(rx0);
    const double py2 = static_cast<double>(ry0 + rh);
    double s0x = 0;
    double s0y = 0;
    double s1x = 0;
    double s1y = 0;
    double s2x = 0;
    double s2y = 0;
    pixelToScreen(px0, py0, &s0x, &s0y);
    pixelToScreen(px1, py1, &s1x, &s1y);
    pixelToScreen(px2, py2, &s2x, &s2y);
    double cross = (s1x - s0x) * (s2y - s0y) - (s1y - s0y) * (s2x - s0x);
    if (std::fabs(cross) < 1e-6) {
      return;
    }

    void* dibBits = nullptr;
    HDC hdcMem = CreateCompatibleDC(hdc);
    if (!hdcMem) {
      return;
    }
    HBITMAP hbm = CreateDIBSection(hdc, &bi, DIB_RGB_COLORS, &dibBits, nullptr, 0);
    if (!hbm || !dibBits) {
      DeleteDC(hdcMem);
      return;
    }
    const size_t nbytes = static_cast<size_t>(rowBytes) * static_cast<size_t>(bmy);
    std::memcpy(dibBits, bgra.data(), nbytes);
    const HGDIOBJ oldBmp = SelectObject(hdcMem, hbm);

    POINT plg[3];
    plg[0].x = static_cast<LONG>(std::lround(s0x));
    plg[0].y = static_cast<LONG>(std::lround(s0y));
    plg[1].x = static_cast<LONG>(std::lround(s1x));
    plg[1].y = static_cast<LONG>(std::lround(s1y));
    plg[2].x = static_cast<LONG>(std::lround(s2x));
    plg[2].y = static_cast<LONG>(std::lround(s2y));
    if (cross < 0) {
      std::swap(plg[1], plg[2]);
    }

    const int prevMode = SetStretchBltMode(hdc, HALFTONE);
    PlgBlt(hdc, plg, hdcMem, 0, 0, bmx, bmy, nullptr, 0, 0);
    SetStretchBltMode(hdc, prevMode);

    SelectObject(hdcMem, oldBmp);
    DeleteObject(hbm);
    DeleteDC(hdcMem);
  }

  GDALDataset* gdalDatasetForDriver() const override { return ds_; }
  std::wstring sourcePathForDriver() const override { return sourcePath_; }

  GDALDataset* Dataset() const { return ds_; }

 private:
  GDALDataset* ds_;
  std::wstring name_;
  std::wstring sourcePath_;
};

void WorldToScreenXY(const ViewExtent& view, double wx, double wy, int cw, int ch, int* sx, int* sy) {
  *sx = static_cast<int>((wx - view.minX) / (view.maxX - view.minX) * static_cast<double>(cw));
  *sy = static_cast<int>((view.maxY - wy) / (view.maxY - view.minY) * static_cast<double>(ch));
}

void DrawGeometry(const OGRGeometry* geom, const ViewExtent& view, int cw, int ch, HDC hdc) {
  if (!geom) {
    return;
  }
  const auto t = wkbFlatten(geom->getGeometryType());
  switch (t) {
    case wkbPoint: {
      const auto* p = static_cast<const OGRPoint*>(geom);
      int sx = 0;
      int sy = 0;
      WorldToScreenXY(view, p->getX(), p->getY(), cw, ch, &sx, &sy);
      RECT rc{sx - 2, sy - 2, sx + 3, sy + 3};
      FillRect(hdc, &rc, reinterpret_cast<HBRUSH>(GetStockObject(DKGRAY_BRUSH)));
      break;
    }
    case wkbLineString: {
      const auto* ls = static_cast<const OGRLineString*>(geom);
      const int n = ls->getNumPoints();
      if (n < 2) {
        break;
      }
      std::vector<POINT> pts(static_cast<size_t>(n));
      for (int i = 0; i < n; ++i) {
        int sx = 0;
        int sy = 0;
        WorldToScreenXY(view, ls->getX(i), ls->getY(i), cw, ch, &sx, &sy);
        pts[static_cast<size_t>(i)].x = sx;
        pts[static_cast<size_t>(i)].y = sy;
      }
      Polyline(hdc, pts.data(), n);
      break;
    }
    case wkbPolygon: {
      const auto* poly = static_cast<const OGRPolygon*>(geom);
      const OGRLinearRing* ring = poly->getExteriorRing();
      const int n = ring->getNumPoints();
      if (n < 2) {
        break;
      }
      std::vector<POINT> pts(static_cast<size_t>(n));
      for (int i = 0; i < n; ++i) {
        int sx = 0;
        int sy = 0;
        WorldToScreenXY(view, ring->getX(i), ring->getY(i), cw, ch, &sx, &sy);
        pts[static_cast<size_t>(i)].x = sx;
        pts[static_cast<size_t>(i)].y = sy;
      }
      Polyline(hdc, pts.data(), n);
      break;
    }
    case wkbMultiPolygon:
    case wkbMultiLineString:
    case wkbMultiPoint:
    case wkbGeometryCollection: {
      const auto* gc = static_cast<const OGRGeometryCollection*>(geom);
      const int ng = gc->getNumGeometries();
      for (int i = 0; i < ng; ++i) {
        DrawGeometry(gc->getGeometryRef(i), view, cw, ch, hdc);
      }
      break;
    }
    default:
      break;
  }
}

class VectorMapLayer final : public MapLayer {
 public:
  VectorMapLayer(GDALDataset* ds, std::wstring name, MapLayerDriverKind driverKind)
      : MapLayer(std::make_unique<GdalVectorMapLayerDriver>(driverKind)), ds_(ds), name_(std::move(name)) {}
  ~VectorMapLayer() override { GDALClose(ds_); }

  std::wstring DisplayName() const override { return name_; }

  MapLayerKind GetKind() const override { return MapLayerKind::kVectorGdal; }

  bool GetExtent(ViewExtent& out) const override {
    OGREnvelope env{};
    bool any = false;
    for (int i = 0; i < ds_->GetLayerCount(); ++i) {
      OGRLayer* lay = ds_->GetLayer(i);
      if (!lay) {
        continue;
      }
      OGREnvelope e2{};
      if (lay->GetExtent(&e2) != OGRERR_NONE) {
        continue;
      }
      if (!any) {
        env = e2;
        any = true;
      } else {
        env.Merge(e2);
      }
    }
    if (!any) {
      return false;
    }
    out.minX = env.MinX;
    out.minY = env.MinY;
    out.maxX = env.MaxX;
    out.maxY = env.MaxY;
    return out.valid();
  }

  void Draw(HDC hdc, const RECT& client, const ViewExtent& view) const override {
    if (!view.valid()) {
      return;
    }
    const int cw = client.right - client.left;
    const int ch = client.bottom - client.top;
    if (cw <= 0 || ch <= 0) {
      return;
    }
    HPEN pen = CreatePen(PS_SOLID, 1, RGB(20, 80, 200));
    const HGDIOBJ oldPen = SelectObject(hdc, pen);
    SetBkMode(hdc, TRANSPARENT);
    for (int li = 0; li < ds_->GetLayerCount(); ++li) {
      OGRLayer* lay = ds_->GetLayer(li);
      if (!lay) {
        continue;
      }
      lay->ResetReading();
      OGRFeature* f = nullptr;
      while ((f = lay->GetNextFeature()) != nullptr) {
        OGRGeometry* g = f->GetGeometryRef();
        if (g) {
          DrawGeometry(g, view, cw, ch, hdc);
        }
        OGRFeature::DestroyFeature(f);
      }
    }
    SelectObject(hdc, oldPen);
    DeleteObject(pen);
  }

  GDALDataset* gdalDatasetForDriver() const override { return ds_; }
  std::wstring sourcePathForDriver() const override { return name_; }

 private:
  GDALDataset* ds_;
  std::wstring name_;
};

std::unique_ptr<MapLayer> CreateLayerFromDataset(GDALDataset* ds, const std::wstring& baseName,
                                                 const std::wstring& sourcePath, MapLayerDriverKind driverKind,
                                                 std::wstring& err) {
  const int rc = ds->GetRasterCount();
  const int lc = ds->GetLayerCount();
  if (rc > 0) {
    return std::make_unique<RasterMapLayer>(ds, baseName, sourcePath, driverKind);
  }
  if (lc > 0) {
    return std::make_unique<VectorMapLayer>(ds, baseName, driverKind);
  }
  GDALClose(ds);
  err = L"文件中未找到栅格或矢量图层。";
  return nullptr;
}

std::unique_ptr<MapLayer> CreateLayerFromTmsUrl(const std::wstring& urlIn, std::wstring& err) {
  std::wstring u = urlIn;
  while (!u.empty() && (u.back() == L' ' || u.back() == L'\t' || u.back() == L'\r' || u.back() == L'\n')) {
    u.pop_back();
  }
  while (!u.empty() && (u.front() == L' ' || u.front() == L'\t')) {
    u.erase(u.begin());
  }
  if (u.empty()) {
    err = L"URL 为空。";
    return nullptr;
  }
  const std::string u8 = Utf8FromWide(u);
  std::string conn = "ZXY:";
  conn += u8;
  GDALDataset* ds = static_cast<GDALDataset*>(
      GDALOpenEx(conn.c_str(), GDAL_OF_RASTER | GDAL_OF_SHARED, nullptr, nullptr, nullptr));
  if (!ds) {
    ds = static_cast<GDALDataset*>(
        GDALOpenEx(u8.c_str(), GDAL_OF_RASTER | GDAL_OF_VECTOR | GDAL_OF_SHARED, nullptr, nullptr, nullptr));
  }
  if (!ds) {
    err = L"无法打开数据源（已尝试 ZXY: 与直接 URL）。\n";
    err += u;
    return nullptr;
  }
  if (ds->GetRasterCount() <= 0) {
    GDALClose(ds);
    err = L"该数据源不包含栅格波段。";
    return nullptr;
  }
  std::wstring name = L"TMS";
  const size_t slash = u.find_last_of(L"/\\");
  if (slash != std::wstring::npos && slash + 1 < u.size()) {
    name = u.substr(slash + 1);
    const size_t q = name.find(L'?');
    if (q != std::wstring::npos) {
      name.resize(q);
    }
    if (name.size() > 48) {
      name.resize(48);
    }
  }
  return std::make_unique<RasterMapLayer>(ds, name, u, MapLayerDriverKind::kTmsXyz);
}

std::wstring TrimUrlWhitespace(const std::wstring& in) {
  std::wstring u = in;
  while (!u.empty() && (u.back() == L' ' || u.back() == L'\t' || u.back() == L'\r' || u.back() == L'\n')) {
    u.pop_back();
  }
  while (!u.empty() && (u.front() == L' ' || u.front() == L'\t')) {
    u.erase(u.begin());
  }
  return u;
}

bool StartsWithIgnoreCaseW(const std::wstring& s, const std::wstring& prefix) {
  if (s.size() < prefix.size()) {
    return false;
  }
  for (size_t i = 0; i < prefix.size(); ++i) {
    if (std::towlower(static_cast<wint_t>(s[i])) != std::towlower(static_cast<wint_t>(prefix[i]))) {
      return false;
    }
  }
  return true;
}

/** 供 ArcGIS REST：GDAL WMS 需 URL 中含 /MapServer 或 /ImageServer 且带 f=json。 */
std::wstring NormalizeArcGisRestJsonUrl(const std::wstring& in) {
  std::wstring u = TrimUrlWhitespace(in);
  if (u.empty()) {
    return u;
  }
  std::wstring lower = u;
  for (wchar_t& c : lower) {
    c = static_cast<wchar_t>(std::towlower(static_cast<wint_t>(c)));
  }
  if (lower.find(L"f=json") == std::wstring::npos) {
    if (u.find(L'?') != std::wstring::npos) {
      u += L"&f=json";
    } else {
      u += L"?f=json";
    }
  }
  return u;
}

std::unique_ptr<MapLayer> CreateLayerFromWmtsUrl(const std::wstring& urlIn, std::wstring& err) {
  const std::wstring u = TrimUrlWhitespace(urlIn);
  if (u.empty()) {
    err = L"URL 为空。";
    return nullptr;
  }
  std::string conn;
  if (StartsWithIgnoreCaseW(u, L"WMTS:")) {
    conn = Utf8FromWide(u);
  } else {
    conn = "WMTS:";
    conn += Utf8FromWide(u);
  }
  GDALDataset* ds = static_cast<GDALDataset*>(
      GDALOpenEx(conn.c_str(), GDAL_OF_RASTER | GDAL_OF_SHARED, nullptr, nullptr, nullptr));
  if (!ds) {
    err = L"无法以 WMTS 打开数据源。\n";
    err += u;
    return nullptr;
  }
  if (ds->GetRasterCount() <= 0) {
    GDALClose(ds);
    err = L"该 WMTS 数据源不包含栅格波段。";
    return nullptr;
  }
  std::wstring name = L"WMTS";
  const size_t slash = u.find_last_of(L"/\\");
  if (slash != std::wstring::npos && slash + 1 < u.size()) {
    name = u.substr(slash + 1);
    const size_t q = name.find(L'?');
    if (q != std::wstring::npos) {
      name.resize(q);
    }
    if (name.size() > 48) {
      name.resize(48);
    }
  }
  return std::make_unique<RasterMapLayer>(ds, name, u, MapLayerDriverKind::kWmts);
}

std::unique_ptr<MapLayer> CreateLayerFromArcGisRestJsonUrl(const std::wstring& urlIn, std::wstring& err) {
  std::wstring u = NormalizeArcGisRestJsonUrl(urlIn);
  if (u.empty()) {
    err = L"URL 为空。";
    return nullptr;
  }
  std::wstring lower = u;
  for (wchar_t& c : lower) {
    c = static_cast<wchar_t>(std::towlower(static_cast<wint_t>(c)));
  }
  if (lower.find(L"/mapserver") == std::wstring::npos && lower.find(L"/imageserver") == std::wstring::npos) {
    err = L"ArcGIS REST JSON 需要 MapServer 或 ImageServer 的服务 URL（来自 REST Services Directory）。\n"
          L"示例：…/arcgis/rest/services/…/MapServer";
    return nullptr;
  }
  const std::string u8 = Utf8FromWide(u);
  GDALDataset* ds = static_cast<GDALDataset*>(
      GDALOpenEx(u8.c_str(), GDAL_OF_RASTER | GDAL_OF_SHARED, nullptr, nullptr, nullptr));
  if (!ds) {
    err = L"无法打开 ArcGIS REST 数据源（需可访问的 MapServer/ImageServer JSON）。\n";
    err += u;
    return nullptr;
  }
  if (ds->GetRasterCount() <= 0) {
    GDALClose(ds);
    err = L"该数据源不包含栅格波段。";
    return nullptr;
  }
  std::wstring name = L"ArcGIS";
  const size_t slash = u.find_last_of(L"/\\");
  if (slash != std::wstring::npos && slash + 1 < u.size()) {
    name = u.substr(slash + 1);
    const size_t q = name.find(L'?');
    if (q != std::wstring::npos) {
      name.resize(q);
    }
    if (name.size() > 48) {
      name.resize(48);
    }
  }
  return std::make_unique<RasterMapLayer>(ds, name, u, MapLayerDriverKind::kArcGisRestJson);
}

}  // namespace agis_detail

#endif  // GIS_DESKTOP_HAVE_GDAL

MapEngine& MapEngine::Instance() {
  static MapEngine inst;
  return inst;
}

void MapEngine::Init() {
  MapProj_SystemInit();
#if GIS_DESKTOP_HAVE_GDAL
  GDALAllRegister();
#endif
}

void MapEngine::Shutdown() {
  MapProj_SystemShutdown();
  doc_.layers.clear();
  doc_.view = DefaultGeographicView();
  doc_.refViewWidthDeg = 360.0;
  doc_.refViewHeightDeg = 180.0;
}

void MapEngine::SetRenderBackend(MapRenderBackend backend) {
  mapRenderBackend_ = backend;
  if (mapHwnd_ && IsWindow(mapHwnd_)) {
    if (!MapGpu_Init(mapHwnd_, backend)) {
      AppLogLine(L"[地图] GPU 呈现初始化失败，已回退为 GDI。");
      mapRenderBackend_ = MapRenderBackend::kGdi;
      MapGpu_Init(mapHwnd_, MapRenderBackend::kGdi);
    }
    InvalidateRect(mapHwnd_, nullptr, FALSE);
  }
}

MapRenderBackend MapEngine::GetRenderBackend() const {
  if (mapHwnd_ && IsWindow(mapHwnd_)) {
    return MapGpu_GetActiveBackend();
  }
  return mapRenderBackend_;
}

void MapEngine::RefreshLayerList(HWND listbox) {
  if (!listbox) {
    return;
  }
  SendMessageW(listbox, LB_RESETCONTENT, 0, 0);
  for (const auto& layer : doc_.layers) {
    SendMessageW(listbox, LB_ADDSTRING, 0, reinterpret_cast<LPARAM>(layer->DisplayName().c_str()));
  }
  if (doc_.layers.empty()) {
    SendMessageW(listbox, LB_ADDSTRING, 0, reinterpret_cast<LPARAM>(L"（无图层，使用「图层」菜单添加）"));
  }
  SendMessageW(listbox, LB_SETITEMHEIGHT, 0, MAKELPARAM(kLayerListItemHeight, 0));
}

void MapEngine::MeasureLayerListItem(LPMEASUREITEMSTRUCT mis) {
  if (!mis) {
    return;
  }
  mis->itemHeight = kLayerListItemHeight;
}

void MapEngine::PaintLayerListItem(const DRAWITEMSTRUCT* dis) {
  if (!dis || dis->CtlType != ODT_LISTBOX) {
    return;
  }
  const int item = static_cast<int>(dis->itemID);
  HDC hdc = dis->hDC;
  const RECT rc = dis->rcItem;
  const int w = rc.right - rc.left;
  const int h = rc.bottom - rc.top;
  if (w <= 0 || h <= 0) {
    return;
  }

  Gdiplus::Graphics g(hdc);
  g.SetSmoothingMode(Gdiplus::SmoothingModeAntiAlias);
  g.SetTextRenderingHint(Gdiplus::TextRenderingHintClearTypeGridFit);

  const bool sel = (dis->itemState & ODS_SELECTED) != 0;
  const bool focus = (dis->itemState & ODS_FOCUS) != 0;
  Gdiplus::Color bg(255, 252, 252, 255);
  if (sel) {
    bg = Gdiplus::Color(255, 210, 230, 255);
  } else if (item % 2 == 1) {
    bg = Gdiplus::Color(255, 245, 248, 252);
  }
  Gdiplus::SolidBrush bgBr(bg);
  g.FillRectangle(&bgBr, static_cast<Gdiplus::REAL>(rc.left), static_cast<Gdiplus::REAL>(rc.top),
                  static_cast<Gdiplus::REAL>(w), static_cast<Gdiplus::REAL>(h));

  Gdiplus::Pen sep(Gdiplus::Color(200, 210, 220, 230), 1.0f);
  g.DrawLine(&sep, static_cast<Gdiplus::REAL>(rc.left), static_cast<Gdiplus::REAL>(rc.bottom - 1),
             static_cast<Gdiplus::REAL>(rc.right), static_cast<Gdiplus::REAL>(rc.bottom - 1));

  Gdiplus::FontFamily fam(L"Microsoft YaHei UI");
  Gdiplus::Font titleF(&fam, 12.5f, Gdiplus::FontStyleBold, Gdiplus::UnitPixel);
  Gdiplus::Font metaF(&fam, 10.5f, Gdiplus::FontStyleRegular, Gdiplus::UnitPixel);
  Gdiplus::SolidBrush fg(Gdiplus::Color(255, 28, 45, 72));
  Gdiplus::SolidBrush metaFg(Gdiplus::Color(255, 75, 90, 110));
  Gdiplus::StringFormat fmt{};
  fmt.SetAlignment(Gdiplus::StringAlignmentNear);
  fmt.SetLineAlignment(Gdiplus::StringAlignmentNear);
  fmt.SetTrimming(Gdiplus::StringTrimmingEllipsisCharacter);

  const int n = static_cast<int>(doc_.layers.size());
  if (n == 0 && item == 0) {
    Gdiplus::SolidBrush hint(Gdiplus::Color(255, 130, 135, 150));
    g.DrawString(L"（无图层，使用「图层」菜单添加）", -1, &metaF,
                 Gdiplus::RectF(static_cast<Gdiplus::REAL>(rc.left + 10), static_cast<Gdiplus::REAL>(rc.top + 8),
                                static_cast<Gdiplus::REAL>(w - 20), static_cast<Gdiplus::REAL>(h - 16)),
                 &fmt, &hint);
    (void)focus;
    return;
  }
  if (item < 0 || item >= n) {
    return;
  }
  const auto& layer = doc_.layers[static_cast<size_t>(item)];
  const bool vis = layer->IsLayerVisible();
  Gdiplus::SolidBrush eyeBr(vis ? Gdiplus::Color(255, 0, 140, 90) : Gdiplus::Color(255, 180, 185, 195));
  Gdiplus::Font eyeF(&fam, 16.0f, Gdiplus::FontStyleBold, Gdiplus::UnitPixel);
  const float eyeX = static_cast<float>(rc.left + 8);
  const float eyeY = static_cast<float>(rc.top + 6);
  g.DrawString(vis ? L"●" : L"○", -1, &eyeF, Gdiplus::RectF(eyeX, eyeY, 22.0f, 22.0f), &fmt, &eyeBr);

  const float textL = static_cast<float>(rc.left + 36);
  const float nameW = static_cast<float>(w - 40);
  std::wstring name = layer->DisplayName();
  g.DrawString(name.c_str(), -1, &titleF, Gdiplus::RectF(textL, static_cast<float>(rc.top + 4), nameW, 20.0f), &fmt,
               &fg);

  wchar_t line2[256]{};
  _snwprintf_s(line2, _TRUNCATE, L"可见：%s    驱动：%s", vis ? L"显示" : L"隐藏",
               MapLayerDriverKindShort(layer->DriverKind()));
  g.DrawString(line2, -1, &metaF,
               Gdiplus::RectF(static_cast<float>(rc.left + 8), static_cast<float>(rc.top + 28),
                              static_cast<float>(w - 16), 18.0f),
               &fmt, &metaFg);

  wchar_t line3[128]{};
  _snwprintf_s(line3, _TRUNCATE, L"数据：%s", MapLayerKindLabel(layer->GetKind()));
  g.DrawString(line3, -1, &metaF,
               Gdiplus::RectF(static_cast<float>(rc.left + 8), static_cast<float>(rc.top + 48),
                              static_cast<float>(w - 16), 18.0f),
               &fmt, &metaFg);
  (void)focus;
}

bool MapEngine::OnLayerListClick(HWND listbox, int x, int y) {
  if (!listbox) {
    return false;
  }
  const LRESULT lr = SendMessageW(listbox, LB_ITEMFROMPOINT, 0, MAKELPARAM(x, y));
  if (lr == static_cast<LRESULT>(-1)) {
    return false;
  }
  const int hit = static_cast<int>(LOWORD(lr));
  const int n = static_cast<int>(doc_.layers.size());
  if (n <= 0 || hit < 0 || hit >= n) {
    return false;
  }
  RECT ir{};
  if (SendMessageW(listbox, LB_GETITEMRECT, static_cast<WPARAM>(hit), reinterpret_cast<LPARAM>(&ir)) == LB_ERR) {
    return false;
  }
  const int lx = x - ir.left;
  if (lx < 0 || lx >= kVisToggleWidth) {
    return false;
  }
  auto& lyr = doc_.layers[static_cast<size_t>(hit)];
  lyr->SetLayerVisible(!lyr->IsLayerVisible());
  InvalidateRect(listbox, nullptr, FALSE);
  if (mapHwnd_ && IsWindow(mapHwnd_)) {
    InvalidateRect(mapHwnd_, nullptr, FALSE);
  }
  return true;
}

int MapEngine::GetLayerCount() const { return static_cast<int>(doc_.layers.size()); }

void MapEngine::GetLayerInfoForUi(int index, std::wstring* outTitle, std::wstring* outDriverProps,
                                 std::wstring* outSourceProps) {
  if (!outTitle || !outDriverProps || !outSourceProps) {
    return;
  }
  outTitle->clear();
  outDriverProps->clear();
  outSourceProps->clear();
#if GIS_DESKTOP_HAVE_GDAL
  if (index < 0 || index >= static_cast<int>(doc_.layers.size())) {
    *outTitle = L"未选择图层";
    *outDriverProps = L"在左侧「图层」列表中单击一行，可在此查看驱动与格式相关属性。";
    *outSourceProps = L"选中图层后，此处显示数据源路径、文件列表与数据集描述等。";
    return;
  }
  const auto& layer = doc_.layers[static_cast<size_t>(index)];
  *outTitle = layer->DisplayName();
  *outTitle += L" · ";
  *outTitle += MapLayerDriverKindLabel(layer->DriverKind());
  layer->AppendDriverProperties(outDriverProps);
  layer->AppendSourceProperties(outSourceProps);
  if (outDriverProps->empty()) {
    *outDriverProps = L"（无驱动侧附加属性）";
  }
  if (outSourceProps->empty()) {
    *outSourceProps = L"（无数据源侧附加属性）";
  }
#else
  *outTitle = L"图层属性";
  *outDriverProps = L"当前构建未启用 GDAL。";
  *outSourceProps = L"无矢量/栅格图层信息。";
#endif
}

bool MapEngine::IsRasterGdalLayer(int index) const {
#if GIS_DESKTOP_HAVE_GDAL
  if (index < 0 || index >= static_cast<int>(doc_.layers.size())) {
    return false;
  }
  return doc_.layers[static_cast<size_t>(index)]->GetKind() == MapLayerKind::kRasterGdal;
#else
  (void)index;
  return false;
#endif
}

bool MapEngine::BuildOverviewsForLayer(int index, std::wstring& err) {
#if GIS_DESKTOP_HAVE_GDAL
  if (index < 0 || index >= static_cast<int>(doc_.layers.size())) {
    err = L"无效图层。";
    return false;
  }
  return doc_.layers[static_cast<size_t>(index)]->BuildOverviews(err);
#else
  (void)index;
  err = L"未启用 GDAL。";
  return false;
#endif
}

bool MapEngine::ClearOverviewsForLayer(int index, std::wstring& err) {
#if GIS_DESKTOP_HAVE_GDAL
  if (index < 0 || index >= static_cast<int>(doc_.layers.size())) {
    err = L"无效图层。";
    return false;
  }
  return doc_.layers[static_cast<size_t>(index)]->ClearOverviews(err);
#else
  (void)index;
  err = L"未启用 GDAL。";
  return false;
#endif
}

bool MapEngine::ReplaceLayerSourceFromUi(HWND owner, HWND layerListbox, int index) {
#if !GIS_DESKTOP_HAVE_GDAL
  (void)owner;
  (void)layerListbox;
  (void)index;
  return false;
#else
  if (index < 0 || index >= static_cast<int>(doc_.layers.size())) {
    MessageBoxW(owner, L"请先选择有效图层。", L"AGIS", MB_OK | MB_ICONWARNING);
    return false;
  }
  MapLayerDriverKind kind{};
  std::wstring urlExtra;
  if (!ShowLayerDriverDialog(owner, &kind, &urlExtra)) {
    return false;
  }
  if (kind == MapLayerDriverKind::kWmsPlaceholder) {
    MessageBoxW(owner,
                L"WMS（KVP GetCapabilities/GetMap）尚未接入。\n请使用 WMTS、ArcGIS REST JSON、TMS/XYZ 或本地 GDAL 文件。",
                L"AGIS", MB_OK | MB_ICONINFORMATION);
    return false;
  }
  if (kind == MapLayerDriverKind::kSoapPlaceholder) {
    MessageBoxW(owner,
                L"OGC Web Services SOAP 绑定尚未接入。\n请使用 WMTS、ArcGIS REST JSON、TMS/XYZ 或本地 GDAL 文件。",
                L"AGIS", MB_OK | MB_ICONINFORMATION);
    return false;
  }
  std::wstring err;
  GDALAllRegister();
  if (kind == MapLayerDriverKind::kTmsXyz) {
    while (!urlExtra.empty() &&
           (urlExtra.back() == L' ' || urlExtra.back() == L'\t' || urlExtra.back() == L'\r' || urlExtra.back() == L'\n')) {
      urlExtra.pop_back();
    }
    while (!urlExtra.empty() && (urlExtra.front() == L' ' || urlExtra.front() == L'\t')) {
      urlExtra.erase(urlExtra.begin());
    }
    if (urlExtra.empty()) {
      MessageBoxW(owner, L"请填写瓦片 URL。", L"AGIS", MB_OK | MB_ICONWARNING);
      return false;
    }
    auto layer = agis_detail::CreateLayerFromTmsUrl(urlExtra, err);
    if (!layer) {
      AppLogLine(std::wstring(L"[错误] 更换数据源失败：") + err);
      MessageBoxW(owner, err.c_str(), L"AGIS", MB_OK | MB_ICONERROR);
      return false;
    }
    if (!doc_.ReplaceLayerAt(static_cast<size_t>(index), std::move(layer), err)) {
      AppLogLine(std::wstring(L"[错误] ") + err);
      MessageBoxW(owner, err.c_str(), L"AGIS", MB_OK | MB_ICONERROR);
      return false;
    }
    AppLogLine(std::wstring(L"[图层] 已更换为 TMS：") + urlExtra);
  } else if (kind == MapLayerDriverKind::kWmts) {
    while (!urlExtra.empty() &&
           (urlExtra.back() == L' ' || urlExtra.back() == L'\t' || urlExtra.back() == L'\r' || urlExtra.back() == L'\n')) {
      urlExtra.pop_back();
    }
    while (!urlExtra.empty() && (urlExtra.front() == L' ' || urlExtra.front() == L'\t')) {
      urlExtra.erase(urlExtra.begin());
    }
    if (urlExtra.empty()) {
      MessageBoxW(owner, L"请填写 WMTS GetCapabilities 或服务 URL。", L"AGIS", MB_OK | MB_ICONWARNING);
      return false;
    }
    auto layer = agis_detail::CreateLayerFromWmtsUrl(urlExtra, err);
    if (!layer) {
      AppLogLine(std::wstring(L"[错误] 更换数据源失败：") + err);
      MessageBoxW(owner, err.c_str(), L"AGIS", MB_OK | MB_ICONERROR);
      return false;
    }
    if (!doc_.ReplaceLayerAt(static_cast<size_t>(index), std::move(layer), err)) {
      AppLogLine(std::wstring(L"[错误] ") + err);
      MessageBoxW(owner, err.c_str(), L"AGIS", MB_OK | MB_ICONERROR);
      return false;
    }
    AppLogLine(std::wstring(L"[图层] 已更换为 WMTS：") + urlExtra);
  } else if (kind == MapLayerDriverKind::kArcGisRestJson) {
    while (!urlExtra.empty() &&
           (urlExtra.back() == L' ' || urlExtra.back() == L'\t' || urlExtra.back() == L'\r' || urlExtra.back() == L'\n')) {
      urlExtra.pop_back();
    }
    while (!urlExtra.empty() && (urlExtra.front() == L' ' || urlExtra.front() == L'\t')) {
      urlExtra.erase(urlExtra.begin());
    }
    if (urlExtra.empty()) {
      MessageBoxW(owner, L"请填写 ArcGIS MapServer/ImageServer 的 REST URL。", L"AGIS", MB_OK | MB_ICONWARNING);
      return false;
    }
    auto layer = agis_detail::CreateLayerFromArcGisRestJsonUrl(urlExtra, err);
    if (!layer) {
      AppLogLine(std::wstring(L"[错误] 更换数据源失败：") + err);
      MessageBoxW(owner, err.c_str(), L"AGIS", MB_OK | MB_ICONERROR);
      return false;
    }
    if (!doc_.ReplaceLayerAt(static_cast<size_t>(index), std::move(layer), err)) {
      AppLogLine(std::wstring(L"[错误] ") + err);
      MessageBoxW(owner, err.c_str(), L"AGIS", MB_OK | MB_ICONERROR);
      return false;
    }
    AppLogLine(std::wstring(L"[图层] 已更换为 ArcGIS REST：") + urlExtra);
  } else {
    wchar_t path[MAX_PATH]{};
    OPENFILENAMEW ofn{};
    ofn.lStructSize = sizeof(ofn);
    ofn.hwndOwner = owner;
    ofn.lpstrFile = path;
    ofn.nMaxFile = MAX_PATH;
    ofn.lpstrFilter = L"栅格/矢量\0"
                      L"*.tif;*.tiff;*.png;*.jpg;*.jp2;*.img;*.shp;*.geojson;*.json;*.gpkg;*.kml;*.vrt\0"
                      L"所有文件\0"
                      L"*.*\0\0";
    ofn.nFilterIndex = 1;
    ofn.Flags = OFN_FILEMUSTEXIST | OFN_PATHMUSTEXIST | OFN_EXPLORER;
    if (!GetOpenFileNameW(&ofn)) {
      return false;
    }
    const std::string utf8 = Utf8FromWide(path);
    unsigned gflags = GDAL_OF_RASTER | GDAL_OF_VECTOR | GDAL_OF_SHARED | GDAL_OF_UPDATE;
    GDALDataset* ds = static_cast<GDALDataset*>(GDALOpenEx(utf8.c_str(), gflags, nullptr, nullptr, nullptr));
    if (!ds) {
      gflags = GDAL_OF_RASTER | GDAL_OF_VECTOR | GDAL_OF_SHARED;
      ds = static_cast<GDALDataset*>(GDALOpenEx(utf8.c_str(), gflags, nullptr, nullptr, nullptr));
    }
    if (!ds) {
      err = L"无法打开数据源：";
      err += path;
      MessageBoxW(owner, err.c_str(), L"AGIS", MB_OK | MB_ICONERROR);
      return false;
    }
    std::wstring base = path;
    const size_t slash = base.find_last_of(L"/\\");
    if (slash != std::wstring::npos) {
      base = base.substr(slash + 1);
    }
    auto layer = agis_detail::CreateLayerFromDataset(ds, base, path, MapLayerDriverKind::kGdalFile, err);
    if (!layer) {
      MessageBoxW(owner, err.c_str(), L"AGIS", MB_OK | MB_ICONERROR);
      return false;
    }
    if (!doc_.ReplaceLayerAt(static_cast<size_t>(index), std::move(layer), err)) {
      MessageBoxW(owner, err.c_str(), L"AGIS", MB_OK | MB_ICONERROR);
      return false;
    }
    AppLogLine(std::wstring(L"[图层] 已更换数据源（GDAL）：") + base);
  }
  RefreshLayerList(layerListbox);
  if (mapHwnd_) {
    InvalidateRect(mapHwnd_, nullptr, FALSE);
  }
  return true;
#endif
}

namespace {

const wchar_t kLayerDriverDlgClass[] = L"AGISLayerDriverDlg";

struct LayerDriverDlgCtx {
  MapLayerDriverKind* kindOut = nullptr;
  std::wstring* urlOut = nullptr;
  int result = 0;
  HWND hGdal = nullptr;
  HWND hTms = nullptr;
  HWND hWmts = nullptr;
  HWND hArcgis = nullptr;
  HWND hSoap = nullptr;
  HWND hWms = nullptr;
  HWND hUrl = nullptr;
};

LayerDriverDlgCtx* GetLayerDlgCtx(HWND h) {
  return reinterpret_cast<LayerDriverDlgCtx*>(GetWindowLongPtrW(h, GWLP_USERDATA));
}

void UpdateLayerDlgUrlEnable(HWND dlg) {
  auto* ctx = GetLayerDlgCtx(dlg);
  if (!ctx || !ctx->hUrl) {
    return;
  }
  const bool needUrl =
      (ctx->hTms && SendMessageW(ctx->hTms, BM_GETCHECK, 0, 0) == BST_CHECKED) ||
      (ctx->hWmts && SendMessageW(ctx->hWmts, BM_GETCHECK, 0, 0) == BST_CHECKED) ||
      (ctx->hArcgis && SendMessageW(ctx->hArcgis, BM_GETCHECK, 0, 0) == BST_CHECKED);
  EnableWindow(ctx->hUrl, needUrl ? TRUE : FALSE);
}

LRESULT CALLBACK LayerDriverDlgProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp) {
  switch (msg) {
    case WM_NCCREATE: {
      auto* cs = reinterpret_cast<CREATESTRUCTW*>(lp);
      auto* ctx = reinterpret_cast<LayerDriverDlgCtx*>(cs->lpCreateParams);
      SetWindowLongPtrW(hwnd, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(ctx));
      return TRUE;
    }
    case WM_CREATE: {
      auto* ctx = GetLayerDlgCtx(hwnd);
      if (!ctx) {
        return -1;
      }
      HINSTANCE inst = GetModuleHandleW(nullptr);
      CreateWindowW(L"STATIC", L"选择图层数据源类型：", WS_CHILD | WS_VISIBLE, 16, 10, 400, 18, hwnd, nullptr, inst,
                    nullptr);
      ctx->hGdal =
          CreateWindowW(L"BUTTON", L"GDAL — 本地文件 / 虚拟路径（.tif、.shp、VSI 等）",
                        WS_CHILD | WS_VISIBLE | BS_AUTORADIOBUTTON | WS_GROUP | WS_TABSTOP, 16, 32, 400, 22, hwnd,
                        reinterpret_cast<HMENU>(static_cast<INT_PTR>(IDC_LAYER_DRV_GDAL)), inst, nullptr);
      ctx->hTms = CreateWindowW(
          L"BUTTON", L"TMS / XYZ — 网络瓦片（{z}/{x}/{y}，GDAL XYZ）", WS_CHILD | WS_VISIBLE | BS_AUTORADIOBUTTON | WS_TABSTOP,
          16, 54, 420, 20, hwnd, reinterpret_cast<HMENU>(static_cast<INT_PTR>(IDC_LAYER_DRV_TMS)), inst, nullptr);
      ctx->hWmts =
          CreateWindowW(L"BUTTON", L"WMTS — OGC Web Map Tile Service（GetCapabilities URL，GDAL WMTS）", WS_CHILD | WS_VISIBLE | BS_AUTORADIOBUTTON | WS_TABSTOP,
                        16, 76, 420, 20, hwnd, reinterpret_cast<HMENU>(static_cast<INT_PTR>(IDC_LAYER_DRV_WMTS)), inst,
                        nullptr);
      ctx->hArcgis = CreateWindowW(
          L"BUTTON",
          L"JSON — ArcGIS REST Services Directory（MapServer/ImageServer URL，GDAL 按 JSON 解析瓦片）",
          WS_CHILD | WS_VISIBLE | BS_AUTORADIOBUTTON | WS_TABSTOP, 16, 98, 420, 20, hwnd,
          reinterpret_cast<HMENU>(static_cast<INT_PTR>(IDC_LAYER_DRV_ARCGIS_JSON)), inst, nullptr);
      ctx->hSoap =
          CreateWindowW(L"BUTTON", L"SOAP — OGC Web Services SOAP 绑定（占位，尚未接入）", WS_CHILD | WS_VISIBLE | BS_AUTORADIOBUTTON | WS_TABSTOP,
                        16, 120, 420, 20, hwnd, reinterpret_cast<HMENU>(static_cast<INT_PTR>(IDC_LAYER_DRV_SOAP)), inst,
                        nullptr);
      ctx->hWms = CreateWindowW(L"BUTTON", L"WMS — KVP GetMap/GetCapabilities（占位，尚未接入）", WS_CHILD | WS_VISIBLE | BS_AUTORADIOBUTTON | WS_TABSTOP,
                                16, 142, 420, 20, hwnd, reinterpret_cast<HMENU>(static_cast<INT_PTR>(IDC_LAYER_DRV_WMS)),
                                inst, nullptr);
      SendMessageW(ctx->hGdal, BM_SETCHECK, BST_CHECKED, 0);
      CreateWindowW(L"STATIC", L"URL（TMS / WMTS / ArcGIS REST 时填写）：", WS_CHILD | WS_VISIBLE, 16, 170, 420, 18, hwnd,
                    nullptr, inst, nullptr);
      ctx->hUrl = CreateWindowExW(WS_EX_CLIENTEDGE, L"EDIT", L"",
                                  WS_CHILD | WS_VISIBLE | ES_AUTOHSCROLL | WS_TABSTOP, 16, 190, 420, 24, hwnd,
                                  reinterpret_cast<HMENU>(static_cast<INT_PTR>(IDC_LAYER_URL)), inst, nullptr);
      EnableWindow(ctx->hUrl, FALSE);
      CreateWindowW(L"BUTTON", L"确定", WS_CHILD | WS_VISIBLE | BS_DEFPUSHBUTTON | WS_TABSTOP, 220, 408, 100, 26, hwnd,
                    reinterpret_cast<HMENU>(static_cast<INT_PTR>(IDC_LAYER_DLG_OK)), inst, nullptr);
      CreateWindowW(L"BUTTON", L"取消", WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON | WS_TABSTOP, 330, 408, 100, 26, hwnd,
                    reinterpret_cast<HMENU>(static_cast<INT_PTR>(IDC_LAYER_DLG_CANCEL)), inst, nullptr);
      ApplyUiFontToChildren(hwnd);
      return 0;
    }
    case WM_COMMAND: {
      const int id = LOWORD(wp);
      const int code = HIWORD(wp);
      auto* ctx = GetLayerDlgCtx(hwnd);
      if (id == IDC_LAYER_DRV_GDAL || id == IDC_LAYER_DRV_TMS || id == IDC_LAYER_DRV_WMS || id == IDC_LAYER_DRV_WMTS ||
          id == IDC_LAYER_DRV_ARCGIS_JSON || id == IDC_LAYER_DRV_SOAP) {
        if (code == BN_CLICKED) {
          UpdateLayerDlgUrlEnable(hwnd);
        }
        return 0;
      }
      if (id == IDC_LAYER_DLG_OK) {
        if (!ctx || !ctx->kindOut || !ctx->urlOut) {
          break;
        }
        if (SendMessageW(ctx->hGdal, BM_GETCHECK, 0, 0) == BST_CHECKED) {
          *ctx->kindOut = MapLayerDriverKind::kGdalFile;
        } else if (SendMessageW(ctx->hTms, BM_GETCHECK, 0, 0) == BST_CHECKED) {
          *ctx->kindOut = MapLayerDriverKind::kTmsXyz;
        } else if (SendMessageW(ctx->hWmts, BM_GETCHECK, 0, 0) == BST_CHECKED) {
          *ctx->kindOut = MapLayerDriverKind::kWmts;
        } else if (SendMessageW(ctx->hArcgis, BM_GETCHECK, 0, 0) == BST_CHECKED) {
          *ctx->kindOut = MapLayerDriverKind::kArcGisRestJson;
        } else if (SendMessageW(ctx->hSoap, BM_GETCHECK, 0, 0) == BST_CHECKED) {
          *ctx->kindOut = MapLayerDriverKind::kSoapPlaceholder;
        } else {
          *ctx->kindOut = MapLayerDriverKind::kWmsPlaceholder;
        }
        wchar_t buf[2048]{};
        GetWindowTextW(ctx->hUrl, buf, 2048);
        *ctx->urlOut = buf;
        ctx->result = 1;
        DestroyWindow(hwnd);
        return 0;
      }
      if (id == IDC_LAYER_DLG_CANCEL) {
        if (ctx) {
          ctx->result = 0;
        }
        DestroyWindow(hwnd);
        return 0;
      }
      break;
    }
    case WM_CLOSE:
      if (auto* ctx = GetLayerDlgCtx(hwnd)) {
        ctx->result = 0;
      }
      DestroyWindow(hwnd);
      return 0;
    default:
      break;
  }
  return DefWindowProcW(hwnd, msg, wp, lp);
}

void EnsureLayerDriverDlgClass() {
  static bool done = false;
  if (done) {
    return;
  }
  WNDCLASSW wc{};
  wc.lpfnWndProc = LayerDriverDlgProc;
  wc.hInstance = GetModuleHandleW(nullptr);
  wc.lpszClassName = kLayerDriverDlgClass;
  wc.hbrBackground = reinterpret_cast<HBRUSH>(COLOR_WINDOW + 1);
  wc.hCursor = LoadCursor(nullptr, IDC_ARROW);
  const ATOM a = RegisterClassW(&wc);
  if (a != 0 || GetLastError() == ERROR_CLASS_ALREADY_EXISTS) {
    done = true;
  }
}

}  // namespace

bool MapEngine::ShowLayerDriverDialog(HWND owner, MapLayerDriverKind* outKind, std::wstring* outUrl) {
  if (!outKind || !outUrl) {
    return false;
  }
  EnsureLayerDriverDlgClass();
  LayerDriverDlgCtx ctx{};
  ctx.kindOut = outKind;
  ctx.urlOut = outUrl;
  outUrl->clear();
  int x = CW_USEDEFAULT;
  int y = CW_USEDEFAULT;
  // CreateWindow 的宽高为**含非客户区**的整个窗口；原先固定 dh=456 导致客户区高度不足，底部「确定/取消」被裁切。
  constexpr int kDlgClientW = 460;
  // 最底控件：按钮 top=408 height=26 → 底边 434，留少量下边距
  constexpr int kDlgClientH = 448;
  RECT wr{0, 0, kDlgClientW, kDlgClientH};
  const DWORD kDlgStyle = WS_POPUP | WS_CAPTION | WS_SYSMENU;
  const DWORD kDlgEx = WS_EX_DLGMODALFRAME | WS_EX_WINDOWEDGE;
  AdjustWindowRectEx(&wr, kDlgStyle, FALSE, kDlgEx);
  const int dw = wr.right - wr.left;
  const int dh = wr.bottom - wr.top;
  RECT rc{};
  if (owner && GetWindowRect(owner, &rc)) {
    x = rc.left + ((rc.right - rc.left) - dw) / 2;
    y = rc.top + ((rc.bottom - rc.top) - dh) / 2;
  }
  HWND dlg = CreateWindowExW(kDlgEx, kLayerDriverDlgClass, L"添加图层 — 数据源", kDlgStyle, x, y, dw, dh, owner, nullptr,
                             GetModuleHandleW(nullptr), &ctx);
  if (!dlg) {
    return false;
  }
  EnableWindow(owner, FALSE);
  ShowWindow(dlg, SW_SHOW);
  UpdateWindow(dlg);
  MSG msg{};
  while (IsWindow(dlg) && GetMessageW(&msg, nullptr, 0, 0)) {
    if (!IsDialogMessageW(dlg, &msg)) {
      TranslateMessage(&msg);
      DispatchMessageW(&msg);
    }
  }
  EnableWindow(owner, TRUE);
  if (owner) {
    SetForegroundWindow(owner);
  }
  return ctx.result == 1;
}

namespace {
bool MapHostRenderClientToTopDownBgra(HWND hwnd, const RECT& client, std::vector<uint8_t>* outPixels);
}

bool MapEngine::SaveMapScreenshotToFile(HWND mapHwnd, const wchar_t* path, std::wstring& err) {
  if (!mapHwnd || !path || !path[0]) {
    err = L"参数无效。";
    return false;
  }
  RECT client{};
  GetClientRect(mapHwnd, &client);
  const int w = client.right - client.left;
  const int h = client.bottom - client.top;
  if (w <= 0 || h <= 0) {
    err = L"地图区域大小无效。";
    return false;
  }
  std::vector<uint8_t> pix;
  if (!MapHostRenderClientToTopDownBgra(mapHwnd, client, &pix)) {
    err = L"渲染失败。";
    return false;
  }
  if (!UiSaveBgraTopDownToPngFile(pix.data(), w, h, path)) {
    err = L"写入 PNG 失败。";
    return false;
  }
  return true;
}

void MapEngine::PromptSaveMapScreenshot(HWND owner, HWND mapHwnd) {
  wchar_t path[MAX_PATH]{};
  SYSTEMTIME st{};
  GetLocalTime(&st);
  _snwprintf_s(path, _TRUNCATE, L"AGIS_map_%04u%02u%02u_%02u%02u%02u.png", static_cast<unsigned>(st.wYear),
               static_cast<unsigned>(st.wMonth), static_cast<unsigned>(st.wDay), static_cast<unsigned>(st.wHour),
               static_cast<unsigned>(st.wMinute), static_cast<unsigned>(st.wSecond));
  OPENFILENAMEW ofn{};
  ofn.lStructSize = sizeof(ofn);
  ofn.hwndOwner = owner;
  ofn.lpstrFile = path;
  ofn.nMaxFile = MAX_PATH;
  ofn.lpstrFilter = L"PNG 图像\0*.png\0所有文件\0*.*\0\0";
  ofn.nFilterIndex = 1;
  ofn.Flags = OFN_OVERWRITEPROMPT | OFN_PATHMUSTEXIST;
  ofn.lpstrDefExt = L"png";
  if (!GetSaveFileNameW(&ofn)) {
    return;
  }
  std::wstring err;
  if (SaveMapScreenshotToFile(mapHwnd, path, err)) {
    AppLogLine(std::wstring(L"[截图] 已保存：") + path);
  } else {
    AppLogLine(std::wstring(L"[错误] 截图失败：") + err);
  }
}

void MapEngine::OnAddLayerFromDialog(HWND owner, HWND layerList) {
  MapLayerDriverKind kind{};
  std::wstring urlExtra;
  if (!ShowLayerDriverDialog(owner, &kind, &urlExtra)) {
    return;
  }
  if (kind == MapLayerDriverKind::kWmsPlaceholder) {
    AppLogLine(L"[图层] WMS（KVP）尚未接入。");
    MessageBoxW(owner,
                L"WMS（KVP GetCapabilities/GetMap）尚未接入。\n请使用 WMTS、ArcGIS REST JSON、TMS/XYZ 或本地 GDAL 文件。",
                L"AGIS", MB_OK | MB_ICONINFORMATION);
    return;
  }
  if (kind == MapLayerDriverKind::kSoapPlaceholder) {
    AppLogLine(L"[图层] SOAP 驱动尚未接入。");
    MessageBoxW(owner,
                L"OGC Web Services SOAP 绑定尚未接入。\n请使用 WMTS、ArcGIS REST JSON、TMS/XYZ 或本地 GDAL 文件。",
                L"AGIS", MB_OK | MB_ICONINFORMATION);
    return;
  }
  if (kind == MapLayerDriverKind::kTmsXyz) {
    while (!urlExtra.empty() &&
           (urlExtra.back() == L' ' || urlExtra.back() == L'\t' || urlExtra.back() == L'\r' || urlExtra.back() == L'\n')) {
      urlExtra.pop_back();
    }
    while (!urlExtra.empty() && (urlExtra.front() == L' ' || urlExtra.front() == L'\t')) {
      urlExtra.erase(urlExtra.begin());
    }
    if (urlExtra.empty()) {
      AppLogLine(L"[错误] 请输入 TMS/XYZ 瓦片 URL。");
      MessageBoxW(owner, L"请填写瓦片 URL（模板中含 {z}、{x}、{y}）。", L"AGIS", MB_OK | MB_ICONWARNING);
      return;
    }
    std::wstring err;
    if (!doc_.AddLayerFromTmsUrl(urlExtra, err)) {
      AppLogLine(std::wstring(L"[错误] 添加 TMS 图层失败：") + err);
      MessageBoxW(owner, err.c_str(), L"AGIS", MB_OK | MB_ICONERROR);
      return;
    }
    AppLogLine(std::wstring(L"[图层] 已添加 TMS：") + urlExtra);
    RefreshLayerList(layerList);
    if (mapHwnd_) {
      InvalidateRect(mapHwnd_, nullptr, FALSE);
    }
    return;
  }
  if (kind == MapLayerDriverKind::kWmts) {
    while (!urlExtra.empty() &&
           (urlExtra.back() == L' ' || urlExtra.back() == L'\t' || urlExtra.back() == L'\r' || urlExtra.back() == L'\n')) {
      urlExtra.pop_back();
    }
    while (!urlExtra.empty() && (urlExtra.front() == L' ' || urlExtra.front() == L'\t')) {
      urlExtra.erase(urlExtra.begin());
    }
    if (urlExtra.empty()) {
      AppLogLine(L"[错误] 请输入 WMTS URL。");
      MessageBoxW(owner, L"请填写 WMTS GetCapabilities 或服务 URL（可省略 WMTS: 前缀）。", L"AGIS", MB_OK | MB_ICONWARNING);
      return;
    }
    std::wstring err;
    if (!doc_.AddLayerFromWmtsUrl(urlExtra, err)) {
      AppLogLine(std::wstring(L"[错误] 添加 WMTS 图层失败：") + err);
      MessageBoxW(owner, err.c_str(), L"AGIS", MB_OK | MB_ICONERROR);
      return;
    }
    AppLogLine(std::wstring(L"[图层] 已添加 WMTS：") + urlExtra);
    RefreshLayerList(layerList);
    if (mapHwnd_) {
      InvalidateRect(mapHwnd_, nullptr, FALSE);
    }
    return;
  }
  if (kind == MapLayerDriverKind::kArcGisRestJson) {
    while (!urlExtra.empty() &&
           (urlExtra.back() == L' ' || urlExtra.back() == L'\t' || urlExtra.back() == L'\r' || urlExtra.back() == L'\n')) {
      urlExtra.pop_back();
    }
    while (!urlExtra.empty() && (urlExtra.front() == L' ' || urlExtra.front() == L'\t')) {
      urlExtra.erase(urlExtra.begin());
    }
    if (urlExtra.empty()) {
      AppLogLine(L"[错误] 请输入 ArcGIS REST URL。");
      MessageBoxW(owner, L"请填写 MapServer 或 ImageServer 的服务 URL（REST Services Directory 中的链接）。", L"AGIS",
                  MB_OK | MB_ICONWARNING);
      return;
    }
    std::wstring err;
    if (!doc_.AddLayerFromArcGisRestJsonUrl(urlExtra, err)) {
      AppLogLine(std::wstring(L"[错误] 添加 ArcGIS REST 图层失败：") + err);
      MessageBoxW(owner, err.c_str(), L"AGIS", MB_OK | MB_ICONERROR);
      return;
    }
    AppLogLine(std::wstring(L"[图层] 已添加 ArcGIS REST：") + urlExtra);
    RefreshLayerList(layerList);
    if (mapHwnd_) {
      InvalidateRect(mapHwnd_, nullptr, FALSE);
    }
    return;
  }

  wchar_t path[MAX_PATH]{};
  OPENFILENAMEW ofn{};
  ofn.lStructSize = sizeof(ofn);
  ofn.hwndOwner = owner;
  ofn.lpstrFile = path;
  ofn.nMaxFile = MAX_PATH;
  ofn.lpstrFilter = L"栅格/矢量\0"
                    L"*.tif;*.tiff;*.png;*.jpg;*.jp2;*.img;*.shp;*.geojson;*.json;*.gpkg;*.kml;*.vrt\0"
                    L"所有文件\0"
                    L"*.*\0\0";
  ofn.nFilterIndex = 1;
  ofn.Flags = OFN_FILEMUSTEXIST | OFN_PATHMUSTEXIST | OFN_EXPLORER;
  if (!GetOpenFileNameW(&ofn)) {
    return;
  }
  std::wstring err;
  if (!doc_.AddLayerFromFile(path, err)) {
    AppLogLine(std::wstring(L"[错误] 添加图层失败：") + err);
    return;
  }
  {
    std::wstring base = path;
    const size_t slash = base.find_last_of(L"/\\");
    if (slash != std::wstring::npos) {
      base = base.substr(slash + 1);
    }
    AppLogLine(std::wstring(L"[图层] 已添加（GDAL）：") + base);
  }
  RefreshLayerList(layerList);
  if (mapHwnd_) {
    InvalidateRect(mapHwnd_, nullptr, FALSE);
  }
}

void MapEngine::UpdateMapChrome() {
  if (!mapChromeScale_ || !IsWindow(mapChromeScale_)) {
    return;
  }
  wchar_t buf[32]{};
  _snwprintf_s(buf, _TRUNCATE, L"%d%%", doc_.ScalePercentForUi());
  SetWindowTextW(mapChromeScale_, buf);
}

namespace {

void LayoutMapOverlayControls(HWND hwnd) {
  RECT r{};
  GetClientRect(hwnd, &r);
  const int cw = std::max(0, static_cast<int>(r.right));
  const int ch = std::max(0, static_cast<int>(r.bottom));
  constexpr int m = 8;
  constexpr int btnH = 22;
  constexpr int btnW = 52;
  constexpr int rowGap = 4;
  HWND hShortcut = GetDlgItem(hwnd, IDC_MAP_SHORTCUT_TOGGLE);
  HWND hEdit = GetDlgItem(hwnd, IDC_MAP_SHORTCUT_EDIT);
  HWND hVis = GetDlgItem(hwnd, IDC_MAP_VIS_TOGGLE);
  HWND hGrid = GetDlgItem(hwnd, IDC_MAP_VIS_GRID);
  HWND hFit = GetDlgItem(hwnd, IDC_MAP_FIT);
  HWND hOrig = GetDlgItem(hwnd, IDC_MAP_ORIGIN);
  HWND hReset = GetDlgItem(hwnd, IDC_MAP_RESET);
  HWND hZm = GetDlgItem(hwnd, IDC_MAP_ZOOM_OUT);
  HWND hZp = GetDlgItem(hwnd, IDC_MAP_ZOOM_IN);
  HWND hSc = GetDlgItem(hwnd, IDC_MAP_SCALE_TEXT);
  if (hShortcut) {
    MoveWindow(hShortcut, m, m, 80, btnH, TRUE);
  }
  if (hEdit) {
    const int eh = 100;
    const int ew = std::max(120, std::min(300, cw - 2 * m));
    MoveWindow(hEdit, m, m + btnH + rowGap, ew, eh, TRUE);
  }
  if (hVis) {
    MoveWindow(hVis, std::max(m, cw - 108), m, 100, btnH, TRUE);
  }
  if (hGrid) {
    const int gx = std::max(m, cw - 224);
    const int gy = m + btnH + rowGap;
    MoveWindow(hGrid, gx, gy, 216, btnH, TRUE);
  }
  const int bottomY = std::max(m + btnH + rowGap + 4, ch - 2 * btnH - 2 * m);
  const int bottomY2 = ch - m - btnH;
  if (hFit) {
    MoveWindow(hFit, m, bottomY, btnW, btnH, TRUE);
  }
  if (hOrig) {
    MoveWindow(hOrig, m + btnW + rowGap, bottomY, btnW, btnH, TRUE);
  }
  if (hReset) {
    MoveWindow(hReset, m + 2 * (btnW + rowGap), bottomY, btnW, btnH, TRUE);
  }
  if (hZm && hSc && hZp) {
    const int scaleW = 56;
    MoveWindow(hZm, m, bottomY2, 28, btnH, TRUE);
    MoveWindow(hSc, m + 30, bottomY2, scaleW, btnH, TRUE);
    MoveWindow(hZp, m + 30 + scaleW + rowGap, bottomY2, 28, btnH, TRUE);
  }
}

bool MapHostRenderClientToTopDownBgra(HWND hwnd, const RECT& client, std::vector<uint8_t>* outPixels) {
  if (!outPixels) {
    return false;
  }
  const int cw = client.right - client.left;
  const int ch = client.bottom - client.top;
  if (cw <= 0 || ch <= 0) {
    return false;
  }
  BITMAPINFO bi{};
  bi.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
  bi.bmiHeader.biWidth = cw;
  bi.bmiHeader.biHeight = -ch;
  bi.bmiHeader.biPlanes = 1;
  bi.bmiHeader.biBitCount = 32;
  bi.bmiHeader.biCompression = BI_RGB;
  void* bits = nullptr;
  HDC hdc = GetDC(hwnd);
  if (!hdc) {
    return false;
  }
  HDC mem = CreateCompatibleDC(hdc);
  if (!mem) {
    ReleaseDC(hwnd, hdc);
    return false;
  }
  HBITMAP dib = CreateDIBSection(hdc, &bi, DIB_RGB_COLORS, &bits, nullptr, 0);
  if (!dib || !bits) {
    DeleteDC(mem);
    ReleaseDC(hwnd, hdc);
    return false;
  }
  const HGDIOBJ old = SelectObject(mem, dib);
  const RECT inner{0, 0, cw, ch};
  MapEngine::Instance().Document().Draw(mem, inner);
  UiPaintMapHintOverlay(mem, inner, L"中键拖拽平移 · 滚轮缩放（指针锚点）");
  const size_t nbytes = static_cast<size_t>(cw) * static_cast<size_t>(ch) * 4u;
  outPixels->resize(nbytes);
  std::memcpy(outPixels->data(), bits, nbytes);
  SelectObject(mem, old);
  DeleteObject(dib);
  DeleteDC(mem);
  ReleaseDC(hwnd, hdc);
  return true;
}

}  // namespace

LRESULT CALLBACK MapHostProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) {
  MapEngine& eng = MapEngine::Instance();
  switch (msg) {
    case WM_CREATE:
      eng.mapHwnd_ = hwnd;
      eng.doc_.view = DefaultGeographicView();
      eng.doc_.refViewWidthDeg = 360.0;
      eng.doc_.refViewHeightDeg = 180.0;
      if (!MapGpu_Init(hwnd, eng.mapRenderBackend_)) {
        AppLogLine(L"[地图] GPU 呈现初始化失败，已回退为 GDI。");
        eng.mapRenderBackend_ = MapRenderBackend::kGdi;
        MapGpu_Init(hwnd, MapRenderBackend::kGdi);
      }
      {
        RECT cr{};
        GetClientRect(hwnd, &cr);
        MapGpu_OnResize(cr.right - cr.left, cr.bottom - cr.top);
      }
      {
        HINSTANCE inst = GetModuleHandleW(nullptr);
        CreateWindowW(L"BUTTON", L"快捷键 ▼", WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON, 8, 8, 80, 22, hwnd,
                      reinterpret_cast<HMENU>(static_cast<INT_PTR>(IDC_MAP_SHORTCUT_TOGGLE)), inst, nullptr);
        const wchar_t kHelp[] =
            L"中键拖拽：平移\r\n滚轮：缩放（指针为锚点）\r\n"
            L"左下：适应 / 原点 / 还原与 ±\r\n无全局快捷键（可后续绑定）";
        CreateWindowExW(WS_EX_CLIENTEDGE, L"EDIT", kHelp,
                        WS_CHILD | ES_MULTILINE | ES_READONLY | ES_AUTOVSCROLL | WS_VSCROLL | WS_TABSTOP, 8, 36, 280,
                        100, hwnd, reinterpret_cast<HMENU>(static_cast<INT_PTR>(IDC_MAP_SHORTCUT_EDIT)), inst, nullptr);
        ShowWindow(GetDlgItem(hwnd, IDC_MAP_SHORTCUT_EDIT), SW_HIDE);
        CreateWindowW(L"BUTTON", L"可见性 ▲", WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON, 200, 8, 100, 22, hwnd,
                      reinterpret_cast<HMENU>(static_cast<INT_PTR>(IDC_MAP_VIS_TOGGLE)), inst, nullptr);
        CreateWindowW(L"BUTTON", L"显示经纬网", WS_CHILD | WS_VISIBLE | BS_AUTOCHECKBOX | WS_TABSTOP, 200, 36, 120, 22,
                      hwnd, reinterpret_cast<HMENU>(static_cast<INT_PTR>(IDC_MAP_VIS_GRID)), inst, nullptr);
        SendMessageW(GetDlgItem(hwnd, IDC_MAP_VIS_GRID), BM_SETCHECK, BST_CHECKED, 0);
        CreateWindowW(L"BUTTON", L"适应", WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON | WS_TABSTOP, 8, 200, 52, 22, hwnd,
                      reinterpret_cast<HMENU>(static_cast<INT_PTR>(IDC_MAP_FIT)), inst, nullptr);
        CreateWindowW(L"BUTTON", L"原点", WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON | WS_TABSTOP, 64, 200, 52, 22, hwnd,
                      reinterpret_cast<HMENU>(static_cast<INT_PTR>(IDC_MAP_ORIGIN)), inst, nullptr);
        CreateWindowW(L"BUTTON", L"还原", WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON | WS_TABSTOP, 120, 200, 52, 22, hwnd,
                      reinterpret_cast<HMENU>(static_cast<INT_PTR>(IDC_MAP_RESET)), inst, nullptr);
        CreateWindowW(L"BUTTON", L"−", WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON | WS_TABSTOP, 8, 228, 28, 22, hwnd,
                      reinterpret_cast<HMENU>(static_cast<INT_PTR>(IDC_MAP_ZOOM_OUT)), inst, nullptr);
        eng.mapChromeScale_ =
            CreateWindowW(L"STATIC", L"100%", WS_CHILD | WS_VISIBLE | SS_CENTER | SS_CENTERIMAGE, 40, 228, 56, 22,
                          hwnd, reinterpret_cast<HMENU>(static_cast<INT_PTR>(IDC_MAP_SCALE_TEXT)), inst, nullptr);
        CreateWindowW(L"BUTTON", L"+", WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON | WS_TABSTOP, 100, 228, 28, 22, hwnd,
                      reinterpret_cast<HMENU>(static_cast<INT_PTR>(IDC_MAP_ZOOM_IN)), inst, nullptr);
        LayoutMapOverlayControls(hwnd);
        ApplyUiFontToChildren(hwnd);
        eng.UpdateMapChrome();
      }
      return 0;
    case WM_DESTROY:
      MapGpu_Shutdown(hwnd);
      eng.mapHwnd_ = nullptr;
      eng.mapChromeScale_ = nullptr;
      return 0;
    case WM_SIZE: {
      RECT sr{};
      GetClientRect(hwnd, &sr);
      MapGpu_OnResize(sr.right - sr.left, sr.bottom - sr.top);
      LayoutMapOverlayControls(hwnd);
      return 0;
    }
    case WM_COMMAND: {
      const int id = LOWORD(wParam);
      const int code = HIWORD(wParam);
      if (code == BN_CLICKED) {
        RECT r{};
        GetClientRect(hwnd, &r);
        const int cw = r.right - r.left;
        const int ch = r.bottom - r.top;
        if (id == IDC_MAP_SHORTCUT_TOGGLE) {
          eng.mapShortcutExpanded_ = !eng.mapShortcutExpanded_;
          if (HWND hEd = GetDlgItem(hwnd, IDC_MAP_SHORTCUT_EDIT)) {
            ShowWindow(hEd, eng.mapShortcutExpanded_ ? SW_SHOW : SW_HIDE);
          }
          SetWindowTextW(GetDlgItem(hwnd, IDC_MAP_SHORTCUT_TOGGLE),
                         eng.mapShortcutExpanded_ ? L"快捷键 ▲" : L"快捷键 ▼");
          LayoutMapOverlayControls(hwnd);
          InvalidateRect(hwnd, nullptr, FALSE);
          return 0;
        }
        if (id == IDC_MAP_VIS_TOGGLE) {
          eng.mapVisExpanded_ = !eng.mapVisExpanded_;
          if (HWND hGr = GetDlgItem(hwnd, IDC_MAP_VIS_GRID)) {
            ShowWindow(hGr, eng.mapVisExpanded_ ? SW_SHOW : SW_HIDE);
          }
          SetWindowTextW(GetDlgItem(hwnd, IDC_MAP_VIS_TOGGLE),
                         eng.mapVisExpanded_ ? L"可见性 ▲" : L"可见性 ▼");
          LayoutMapOverlayControls(hwnd);
          InvalidateRect(hwnd, nullptr, FALSE);
          return 0;
        }
        if (id == IDC_MAP_VIS_GRID) {
          const UINT st = static_cast<UINT>(
              SendMessageW(GetDlgItem(hwnd, IDC_MAP_VIS_GRID), BM_GETCHECK, 0, 0));
          eng.doc_.SetShowLatLonGrid(st == BST_CHECKED);
          InvalidateRect(hwnd, nullptr, FALSE);
          return 0;
        }
        if (id == IDC_MAP_FIT) {
          eng.doc_.FitViewToLayers();
          InvalidateRect(hwnd, nullptr, FALSE);
          return 0;
        }
        if (id == IDC_MAP_ORIGIN) {
          eng.doc_.CenterContentOrigin(cw, ch);
          InvalidateRect(hwnd, nullptr, FALSE);
          return 0;
        }
        if (id == IDC_MAP_RESET) {
          eng.doc_.ResetZoom100AnchorCenter(cw, ch);
          InvalidateRect(hwnd, nullptr, FALSE);
          return 0;
        }
        if (id == IDC_MAP_ZOOM_OUT) {
          eng.doc_.ZoomViewAtCenter(1.0 / 1.1, cw, ch);
          InvalidateRect(hwnd, nullptr, FALSE);
          return 0;
        }
        if (id == IDC_MAP_ZOOM_IN) {
          eng.doc_.ZoomViewAtCenter(1.1, cw, ch);
          InvalidateRect(hwnd, nullptr, FALSE);
          return 0;
        }
      }
      break;
    }
    case WM_ERASEBKGND:
      return 1;
    case WM_CONTEXTMENU:
      return 0;
    case WM_LBUTTONDOWN:
      SetFocus(hwnd);
      return 0;
    case WM_MBUTTONDOWN:
      SetFocus(hwnd);
      eng.mdrag_ = true;
      eng.mlast_.x = GET_X_LPARAM(lParam);
      eng.mlast_.y = GET_Y_LPARAM(lParam);
      SetCapture(hwnd);
      return 0;
    case WM_MBUTTONUP:
      eng.mdrag_ = false;
      ReleaseCapture();
      return 0;
    case WM_MOUSEWHEEL: {
      int delta = GET_WHEEL_DELTA_WPARAM(wParam);
      RECT r{};
      GetClientRect(hwnd, &r);
      const int cw = r.right - r.left;
      const int ch = r.bottom - r.top;
      // WM_MOUSEWHEEL 的 lParam 为屏幕坐标，须换算到本窗口客户区，否则缩放锚点错误
      POINT pt{GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam)};
      ScreenToClient(hwnd, &pt);
      const int mx = pt.x;
      const int my = pt.y;
      const double factor = delta > 0 ? 1.1 : 1.0 / 1.1;
      eng.doc_.ZoomAt(mx, my, cw, ch, factor);
      RedrawWindow(hwnd, nullptr, nullptr,
                   RDW_INVALIDATE | RDW_ERASE | RDW_UPDATENOW | RDW_ALLCHILDREN);
      return 0;
    }
    case WM_MOUSEMOVE:
      if (eng.mdrag_ && (wParam & MK_MBUTTON) != 0) {
        const int x = GET_X_LPARAM(lParam);
        const int y = GET_Y_LPARAM(lParam);
        const int dx = x - eng.mlast_.x;
        const int dy = y - eng.mlast_.y;
        eng.mlast_.x = x;
        eng.mlast_.y = y;
        RECT r2{};
        GetClientRect(hwnd, &r2);
        eng.doc_.PanPixels(dx, dy, r2.right - r2.left, r2.bottom - r2.top);
        InvalidateRect(hwnd, nullptr, FALSE);
      }
      return 0;
    case WM_PAINT: {
      PAINTSTRUCT ps{};
      HDC hdc = BeginPaint(hwnd, &ps);
      RECT client{};
      GetClientRect(hwnd, &client);
      const int cw = client.right - client.left;
      const int ch = client.bottom - client.top;
      if (MapGpu_GetActiveBackend() == MapRenderBackend::kGdi) {
        HDC mem = CreateCompatibleDC(hdc);
        HBITMAP bmp = CreateCompatibleBitmap(hdc, cw, ch);
        const HGDIOBJ oldBmp = SelectObject(mem, bmp);
        eng.doc_.Draw(mem, client);
        UiPaintMapHintOverlay(mem, client, L"中键拖拽平移 · 滚轮缩放（指针锚点）");
        BitBlt(hdc, 0, 0, cw, ch, mem, 0, 0, SRCCOPY);
        SelectObject(mem, oldBmp);
        DeleteObject(bmp);
        DeleteDC(mem);
      } else {
        std::vector<uint8_t> pix;
        if (MapHostRenderClientToTopDownBgra(hwnd, client, &pix) && cw > 0 && ch > 0) {
          MapGpu_PresentFrame(hwnd, pix.data(), cw, ch);
        }
      }
      EndPaint(hwnd, &ps);
      return 0;
    }
    default:
      break;
  }
  return DefWindowProcW(hwnd, msg, wParam, lParam);
}
