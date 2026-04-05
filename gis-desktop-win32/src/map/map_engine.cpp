#include "map/map_engine.h"

#include <algorithm>
#include <cmath>
#include <cstring>
#include <cstdio>
#include <cwctype>
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

std::string Utf8FromWide(const std::wstring& w) {
  if (w.empty()) {
    return {};
  }
  const int n = WideCharToMultiByte(CP_UTF8, 0, w.c_str(), -1, nullptr, 0, nullptr, nullptr);
  if (n <= 0) {
    return {};
  }
  std::string out(static_cast<size_t>(n - 1), '\0');
  WideCharToMultiByte(CP_UTF8, 0, w.c_str(), -1, out.data(), n, nullptr, nullptr);
  return out;
}

std::wstring WideFromUtf8(const char* s) {
  if (!s || !s[0]) {
    return {};
  }
  const int n = MultiByteToWideChar(CP_UTF8, 0, s, -1, nullptr, 0);
  if (n <= 0) {
    return {};
  }
  std::wstring w(static_cast<size_t>(n - 1), L'\0');
  MultiByteToWideChar(CP_UTF8, 0, s, -1, w.data(), n);
  return w;
}

}  // namespace

const wchar_t* MapLayerKindLabel(MapLayerKind k) {
  switch (k) {
    case MapLayerKind::kRasterGdal:
      return L"栅格（GDAL）";
    case MapLayerKind::kVectorGdal:
      return L"矢量（GDAL）";
    case MapLayerKind::kOther:
    default:
      return L"其他";
  }
}

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

const wchar_t* MapLayerDriverKindLabel(MapLayerDriverKind k) {
  switch (k) {
    case MapLayerDriverKind::kGdalFile:
      return L"GDAL 本地/虚拟文件";
    case MapLayerDriverKind::kTmsXyz:
      return L"TMS / XYZ";
    case MapLayerDriverKind::kWmts:
      return L"WMTS（OGC Web Map Tile Service）";
    case MapLayerDriverKind::kArcGisRestJson:
      return L"ArcGIS REST JSON（Services Directory，GDAL WMS）";
    case MapLayerDriverKind::kSoapPlaceholder:
      return L"OGC SOAP（占位）";
    case MapLayerDriverKind::kWmsPlaceholder:
      return L"WMS KVP（占位）";
    default:
      return L"未知";
  }
}

bool MapLayer::BuildOverviews(std::wstring& err) {
  err = L"此图层类型不支持金字塔。";
  return false;
}

bool MapLayer::ClearOverviews(std::wstring& err) {
  err = L"此图层类型不支持金字塔。";
  return false;
}

#if GIS_DESKTOP_HAVE_GDAL

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
      : ds_(ds), name_(std::move(name)), sourcePath_(std::move(sourcePath)), driverKind_(driverKind) {}
  ~RasterMapLayer() override { GDALClose(ds_); }

  std::wstring DisplayName() const override { return name_; }

  MapLayerKind GetKind() const override { return MapLayerKind::kRasterGdal; }

  MapLayerDriverKind DriverKind() const override { return driverKind_; }

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

  void AppendSourceProperties(std::wstring* out) const override {
    if (!out) {
      return;
    }
    *out += L"【数据源】\r\n";
    *out += sourcePath_.empty() ? L"（未记录）\r\n" : sourcePath_ + L"\r\n";
    const char* ddesc = ds_->GetDescription();
    *out += L"\r\n【GDALDataset 描述 GetDescription】\r\n";
    *out += (ddesc && ddesc[0]) ? WideFromUtf8(ddesc) : L"（空）";
    *out += L"\r\n\r\n";
    AppendDatasetFileList(ds_, out);
    *out += L"【数据集级元数据 GDALDataset 各域（含 IMAGE_STRUCTURE、SUBDATASETS 等）】\r\n";
    AppendGdalObjectMetadataDomains(reinterpret_cast<GDALMajorObjectH>(ds_), out);
  }

  void AppendDriverProperties(std::wstring* out) const override {
    if (!out) {
      return;
    }
    *out += L"【AGIS 驱动方式】 ";
    *out += MapLayerDriverKindLabel(driverKind_);
    *out += L"\r\n";
    if (driverKind_ == MapLayerDriverKind::kArcGisRestJson) {
      *out += L"【说明】 与 ArcGIS REST Services Directory 中 MapServer/ImageServer 的 JSON 一致；由 GDAL WMS 驱动拉取并映射为栅格。\r\n";
    } else if (driverKind_ == MapLayerDriverKind::kWmts) {
      *out += L"【说明】 使用 GDAL WMTS 驱动；URL 可为 GetCapabilities 地址，亦可写 WMTS:https://… 形式。\r\n";
    } else if (driverKind_ == MapLayerDriverKind::kTmsXyz) {
      *out += L"【说明】 使用 GDAL XYZ/ZXY 模板 URL（含 {z}{x}{y}）。\r\n";
    }
    GDALDriver* drv = ds_->GetDriver();
    if (drv) {
      AppendDriverMetadata(drv, out);
    }
    *out += L"【栅格尺寸 GetRasterXSize/YSize】 ";
    *out += std::to_wstring(ds_->GetRasterXSize()) + L" × " + std::to_wstring(ds_->GetRasterYSize());
    *out += L"\r\n【波段数 GetRasterCount】 " + std::to_wstring(ds_->GetRasterCount());
    const GDALAccess acc = ds_->GetAccess();
    *out += L"\r\n【访问模式 GDALDataset::GetAccess】 ";
    *out += (acc == GA_Update) ? L"GA_Update（可写金字塔等）\r\n" : L"GA_ReadOnly\r\n";
    *out += L"【数据集类型】 ";
    *out += ds_->GetRasterCount() > 0 ? L"栅格\r\n" : L"无栅格波段\r\n";
    double gt[6]{};
    if (ds_->GetGeoTransform(gt) == CE_None) {
      *out += L"【仿射变换 GDALDataset::GetGeoTransform】\r\n";
      for (int i = 0; i < 6; ++i) {
        *out += L"  [" + std::to_wstring(i) + L"] " + std::to_wstring(gt[i]) + L"\r\n";
      }
    } else {
      *out += L"【仿射变换】（GetGeoTransform 未设置或非 CE_None）\r\n";
    }
    const char* wkt = ds_->GetProjectionRef();
    *out += L"【空间参考 GDALDataset::GetProjectionRef】\r\n";
    if (wkt && wkt[0]) {
      std::wstring w = WideFromUtf8(wkt);
      if (w.size() > 2000) {
        w.resize(2000);
        w += L"…";
      }
      *out += w + L"\r\n";
    } else {
      *out += L"（无）\r\n";
    }
    AppendGcpSummary(ds_, out);

    *out += L"【波段详情 GDALRasterBand】\r\n";
    for (int bi = 1; bi <= ds_->GetRasterCount(); ++bi) {
      GDALRasterBand* b = ds_->GetRasterBand(bi);
      if (!b) {
        continue;
      }
      int bx = 0;
      int by = 0;
      b->GetBlockSize(&bx, &by);
      *out += L"  ── 波段 " + std::to_wstring(bi) + L" GetRasterBand(" + std::to_wstring(bi) + L") ──\r\n";
      *out += L"    数据类型 GetRasterDataType: ";
      *out += WideFromUtf8(GDALGetDataTypeName(b->GetRasterDataType()));
      *out += L"\r\n    颜色解释 GetColorInterpretation: ";
      *out += WideFromUtf8(GDALGetColorInterpretationName(b->GetColorInterpretation()));
      *out += L"\r\n    尺寸: " + std::to_wstring(b->GetXSize()) + L" × " + std::to_wstring(b->GetYSize());
      *out += L"\r\n    块大小 GetBlockSize: " + std::to_wstring(bx) + L" × " + std::to_wstring(by);
      const int noc = b->GetOverviewCount();
      *out += L"\r\n    金字塔 GetOverviewCount: " + std::to_wstring(noc) + L"\r\n";
      for (int oi = 0; oi < noc; ++oi) {
        GDALRasterBand* ov = b->GetOverview(oi);
        if (ov) {
          *out += L"      [Overview " + std::to_wstring(oi) + L"] " + std::to_wstring(ov->GetXSize()) + L" × " +
                  std::to_wstring(ov->GetYSize()) + L"\r\n";
        }
      }
      AppendRasterBandExtras(b, out);
    }
    *out += L"\r\n提示：本地 GeoTIFF 等可写文件可用「生成金字塔」写入 .ovr；只读或网络源可能失败。\r\n";
    ViewExtent ex{};
    if (GetExtent(ex) && ex.valid()) {
      *out += L"\r\n【地图范围（由仿射变换推算）】\r\nminX " + std::to_wstring(ex.minX) + L"\r\nminY " +
              std::to_wstring(ex.minY) + L"\r\nmaxX " + std::to_wstring(ex.maxX) + L"\r\nmaxY " +
              std::to_wstring(ex.maxY) + L"\r\n";
    }
  }

  GDALDataset* Dataset() const { return ds_; }

  bool BuildOverviews(std::wstring& err) override {
    int levels[] = {2, 4, 8, 16};
    CPLErr e =
        ds_->BuildOverviews("NEAREST", 4, levels, 0, nullptr, nullptr, nullptr, nullptr);
    if (e != CE_None) {
      err = WideFromUtf8(CPLGetLastErrorMsg());
      if (err.empty()) {
        err = L"BuildOverviews 失败（需可写数据集，部分格式不支持）";
      }
      return false;
    }
    ds_->FlushCache();
    return true;
  }

  bool ClearOverviews(std::wstring& err) override {
    // GDAL 3.x：无 ClearOverviews；nOverviews==0 表示删除金字塔（见 gdaldataset.cpp 文档）
    CPLErr e =
        ds_->BuildOverviews("NEAREST", 0, nullptr, 0, nullptr, nullptr, nullptr, nullptr);
    if (e != CE_None) {
      err = WideFromUtf8(CPLGetLastErrorMsg());
      if (err.empty()) {
        err = L"删除金字塔失败（需可写数据集，部分格式不支持）";
      }
      return false;
    }
    ds_->FlushCache();
    return true;
  }

 private:
  GDALDataset* ds_;
  std::wstring name_;
  std::wstring sourcePath_;
  MapLayerDriverKind driverKind_{MapLayerDriverKind::kGdalFile};
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
      : ds_(ds), name_(std::move(name)), driverKind_(driverKind) {}
  ~VectorMapLayer() override { GDALClose(ds_); }

  std::wstring DisplayName() const override { return name_; }

  MapLayerKind GetKind() const override { return MapLayerKind::kVectorGdal; }

  MapLayerDriverKind DriverKind() const override { return driverKind_; }

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

  void AppendSourceProperties(std::wstring* out) const override {
    if (!out) {
      return;
    }
    *out += L"【数据源】\r\n";
    *out += name_.empty() ? L"（未记录）\r\n" : name_ + L"\r\n";
    const char* ddesc = ds_->GetDescription();
    *out += L"\r\n【GDALDataset 描述 GetDescription】\r\n";
    *out += (ddesc && ddesc[0]) ? WideFromUtf8(ddesc) : L"（空）";
    *out += L"\r\n\r\n";
    AppendDatasetFileList(ds_, out);
    *out += L"【数据集级元数据 GDALDataset 各域】\r\n";
    AppendGdalObjectMetadataDomains(reinterpret_cast<GDALMajorObjectH>(ds_), out);
  }

  void AppendDriverProperties(std::wstring* out) const override {
    if (!out) {
      return;
    }
    *out += L"【AGIS 驱动方式】 ";
    *out += MapLayerDriverKindLabel(driverKind_);
    *out += L"\r\n";
    GDALDriver* drv = ds_->GetDriver();
    if (drv) {
      AppendDriverMetadata(drv, out);
    }
    *out += L"【矢量图层数 GDALDataset::GetLayerCount】 " + std::to_wstring(ds_->GetLayerCount()) + L"\r\n";
    const GDALAccess acc = ds_->GetAccess();
    *out += L"【访问模式】 ";
    *out += (acc == GA_Update) ? L"GA_Update\r\n" : L"GA_ReadOnly\r\n";
    const char* wkt = ds_->GetProjectionRef();
    *out += L"【数据集坐标系 GDALDataset::GetProjectionRef】\r\n";
    if (wkt && wkt[0]) {
      std::wstring w = WideFromUtf8(wkt);
      if (w.size() > 2000) {
        w.resize(2000);
        w += L"…";
      }
      *out += w + L"\r\n\r\n";
    } else {
      *out += L"（无）\r\n\r\n";
    }
    for (int i = 0; i < ds_->GetLayerCount(); ++i) {
      OGRLayer* lay = ds_->GetLayer(i);
      if (!lay) {
        continue;
      }
      *out += L"\r\n";
      AppendOgrLayerDetails(lay, i, out);
    }
  }

 private:
  GDALDataset* ds_;
  std::wstring name_;
  MapLayerDriverKind driverKind_{MapLayerDriverKind::kGdalFile};
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

namespace {

ViewExtent DefaultGeographicView() {
  return ViewExtent{-180.0, -90.0, 180.0, 90.0};
}

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

static MapDocument g_doc;
static HWND g_mapHwnd = nullptr;
/** 用户选择的 2D 呈现后端；地图 HWND 创建后由 MapGpu_Init 生效。 */
static MapRenderBackend g_mapRenderBackend = MapRenderBackend::kGdi;
static bool g_mdrag = false;
static POINT g_mlast{0, 0};
static HWND g_mapChromeScale = nullptr;
/** 快捷键说明面板：规则要求默认折叠 */
static bool g_mapShortcutExpanded = false;
/** 要素可见性面板：规则要求默认展开 */
static bool g_mapVisExpanded = true;

void MapEngine_UpdateMapChrome() {
  MapProj_InvalidateBoundsCache();
  if (!g_mapChromeScale || !IsWindow(g_mapChromeScale)) {
    return;
  }
  wchar_t b[32]{};
  _snwprintf_s(b, _TRUNCATE, L"%d%%", g_doc.ScalePercentForUi());
  SetWindowTextW(g_mapChromeScale, b);
}

void MapDocument::FitViewToLayers() {
  if (layers.empty()) {
    view = DefaultGeographicView();
    refViewWidthDeg = 360.0;
    refViewHeightDeg = 180.0;
    MapEngine_UpdateMapChrome();
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
    MapEngine_UpdateMapChrome();
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
  MapEngine_UpdateMapChrome();
}

void MapDocument::EnforceLonLatAspect360_180() {
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

void MapDocument::NormalizeEmptyMapView() {
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

bool MapDocument::AddLayerFromFile(const std::wstring& path, std::wstring& err) {
#if !GIS_DESKTOP_HAVE_GDAL
  err = L"本程序未启用 GDAL（GIS_DESKTOP_HAVE_GDAL=0）。请在 gis-desktop-win32 下用 AGIS_USE_GDAL=on 重新运行 "
        L"python build.py 并确保 CMake 配置成功（依赖见 3rdparty/README-GDAL-BUILD.md）。若仅需壳程序，请用 AGIS_USE_GDAL=off 编译。";
  return false;
#else
  GDALAllRegister();
  const std::string utf8 = Utf8FromWide(path);
  unsigned openFlags = GDAL_OF_RASTER | GDAL_OF_VECTOR | GDAL_OF_SHARED | GDAL_OF_UPDATE;
  GDALDataset* ds = static_cast<GDALDataset*>(
      GDALOpenEx(utf8.c_str(), openFlags, nullptr, nullptr, nullptr));
  if (!ds) {
    openFlags = GDAL_OF_RASTER | GDAL_OF_VECTOR | GDAL_OF_SHARED;
    ds = static_cast<GDALDataset*>(GDALOpenEx(utf8.c_str(), openFlags, nullptr, nullptr, nullptr));
  }
  if (!ds) {
    err = L"无法打开数据源：";
    err += path;
    return false;
  }
  std::wstring base = path;
  const size_t slash = base.find_last_of(L"/\\");
  if (slash != std::wstring::npos) {
    base = base.substr(slash + 1);
  }
  auto layer = agis_detail::CreateLayerFromDataset(ds, base, path, MapLayerDriverKind::kGdalFile, err);
  if (!layer) {
    return false;
  }
  layers.push_back(std::move(layer));
  FitViewToLayers();
  return true;
#endif
}

bool MapDocument::AddLayerFromTmsUrl(const std::wstring& url, std::wstring& err) {
#if !GIS_DESKTOP_HAVE_GDAL
  (void)url;
  err = L"本程序未启用 GDAL，无法使用 TMS/XYZ。请用 AGIS_USE_GDAL=on 重新构建。";
  return false;
#else
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

bool MapDocument::AddLayerFromWmtsUrl(const std::wstring& url, std::wstring& err) {
#if !GIS_DESKTOP_HAVE_GDAL
  (void)url;
  err = L"本程序未启用 GDAL，无法使用 WMTS。请用 AGIS_USE_GDAL=on 重新构建。";
  return false;
#else
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

bool MapDocument::AddLayerFromArcGisRestJsonUrl(const std::wstring& url, std::wstring& err) {
#if !GIS_DESKTOP_HAVE_GDAL
  (void)url;
  err = L"本程序未启用 GDAL，无法使用 ArcGIS REST。请用 AGIS_USE_GDAL=on 重新构建。";
  return false;
#else
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

bool MapDocument::ReplaceLayerAt(size_t index, std::unique_ptr<MapLayer> layer, std::wstring& err) {
  if (!layer) {
    err = L"无效图层";
    return false;
  }
  if (index >= layers.size()) {
    err = L"图层索引越界";
    return false;
  }
  layers[index] = std::move(layer);
  FitViewToLayers();
  return true;
}

bool MapDocument::RemoveLayerAt(size_t index, std::wstring& err) {
  if (index >= layers.size()) {
    err = L"图层索引越界";
    return false;
  }
  layers.erase(layers.begin() + static_cast<ptrdiff_t>(index));
  FitViewToLayers();
  return true;
}

void MapDocument::MoveLayerUp(size_t index) {
  if (index == 0 || index >= layers.size()) {
    return;
  }
  std::swap(layers[index - 1], layers[index]);
}

void MapDocument::MoveLayerDown(size_t index) {
  if (index + 1 >= layers.size()) {
    return;
  }
  std::swap(layers[index], layers[index + 1]);
}

void MapDocument::Draw(HDC hdcMem, const RECT& client) {
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
  UiPaintMapCenterHint(
      hdcMem, innerLocal,
      L"当前构建未启用 GDAL。\n请安装 PROJ 与 GDAL（见 3rdparty/README-GDAL-BUILD.md），或设置 AGIS_USE_GDAL=on 与 CMAKE_PREFIX_PATH，再重新配置。");
#endif
  DrawScaleBar(hdcMem, innerLocal, view);
}

void MapDocument::ScreenToWorld(int sx, int sy, int cw, int ch, double* wx, double* wy) const {
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

void MapDocument::ZoomAt(int sx, int sy, int cw, int ch, double factor) {
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
  MapEngine_UpdateMapChrome();
}

void MapDocument::PanPixels(int dx, int dy, int cw, int ch) {
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
  MapEngine_UpdateMapChrome();
}

void MapDocument::ZoomViewAtCenter(double factor, int cw, int ch) {
  ZoomAt(cw / 2, ch / 2, cw, ch, factor);
}

void MapDocument::ResetZoom100AnchorCenter(int cw, int ch) {
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
    MapEngine_UpdateMapChrome();
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
  MapEngine_UpdateMapChrome();
}

void MapDocument::CenterContentOrigin(int cw, int ch) {
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
  MapEngine_UpdateMapChrome();
}

int MapDocument::ScalePercentForUi() const {
  const double w = view.maxX - view.minX;
  const double h = view.maxY - view.minY;
  if (w <= 1e-18 || h <= 1e-18) {
    return 100;
  }
  const double pw = refViewWidthDeg / w * 100.0;
  const double ph = refViewHeightDeg / h * 100.0;
  return static_cast<int>(std::lround((pw + ph) * 0.5));
}

void MapDocument::SetShowLatLonGrid(bool on) {
  showLatLonGrid = on;
}

void MapDocument::SetDisplayProjection(MapDisplayProjection p) {
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

void MapEngine_Init() {
  MapProj_SystemInit();
#if GIS_DESKTOP_HAVE_GDAL
  GDALAllRegister();
#endif
}

void MapEngine_Shutdown() {
  MapProj_SystemShutdown();
  g_doc.layers.clear();
  g_doc.view = DefaultGeographicView();
  g_doc.refViewWidthDeg = 360.0;
  g_doc.refViewHeightDeg = 180.0;
}

void MapEngine_SetRenderBackend(MapRenderBackend backend) {
  g_mapRenderBackend = backend;
  if (g_mapHwnd && IsWindow(g_mapHwnd)) {
    if (!MapGpu_Init(g_mapHwnd, backend)) {
      AppLogLine(L"[地图] GPU 呈现初始化失败，已回退为 GDI。");
      g_mapRenderBackend = MapRenderBackend::kGdi;
      MapGpu_Init(g_mapHwnd, MapRenderBackend::kGdi);
    }
    InvalidateRect(g_mapHwnd, nullptr, FALSE);
  }
}

MapRenderBackend MapEngine_GetRenderBackend() {
  if (g_mapHwnd && IsWindow(g_mapHwnd)) {
    return MapGpu_GetActiveBackend();
  }
  return g_mapRenderBackend;
}

MapDocument& MapEngine_Document() { return g_doc; }

void MapEngine_RefreshLayerList(HWND listbox) {
  if (!listbox) {
    return;
  }
  SendMessageW(listbox, LB_RESETCONTENT, 0, 0);
  for (const auto& layer : g_doc.layers) {
    SendMessageW(listbox, LB_ADDSTRING, 0, reinterpret_cast<LPARAM>(layer->DisplayName().c_str()));
  }
  if (g_doc.layers.empty()) {
    SendMessageW(listbox, LB_ADDSTRING, 0, reinterpret_cast<LPARAM>(L"（无图层，使用「图层」菜单添加）"));
  }
  SendMessageW(listbox, LB_SETITEMHEIGHT, 0, MAKELPARAM(kLayerListItemHeight, 0));
}

void MapEngine_MeasureLayerListItem(LPMEASUREITEMSTRUCT mis) {
  if (!mis) {
    return;
  }
  mis->itemHeight = kLayerListItemHeight;
}

void MapEngine_PaintLayerListItem(const DRAWITEMSTRUCT* dis) {
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

  const int n = static_cast<int>(g_doc.layers.size());
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
  const auto& layer = g_doc.layers[static_cast<size_t>(item)];
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

bool MapEngine_OnLayerListClick(HWND listbox, int x, int y) {
  if (!listbox) {
    return false;
  }
  const LRESULT lr = SendMessageW(listbox, LB_ITEMFROMPOINT, 0, MAKELPARAM(x, y));
  if (lr == static_cast<LRESULT>(-1)) {
    return false;
  }
  const int hit = static_cast<int>(LOWORD(lr));
  const int n = static_cast<int>(g_doc.layers.size());
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
  auto& lyr = g_doc.layers[static_cast<size_t>(hit)];
  lyr->SetLayerVisible(!lyr->IsLayerVisible());
  InvalidateRect(listbox, nullptr, FALSE);
  if (g_mapHwnd && IsWindow(g_mapHwnd)) {
    InvalidateRect(g_mapHwnd, nullptr, FALSE);
  }
  return true;
}

int MapEngine_GetLayerCount() { return static_cast<int>(g_doc.layers.size()); }

void MapEngine_GetLayerInfoForUi(int index, std::wstring* outTitle, std::wstring* outDriverProps,
                                 std::wstring* outSourceProps) {
  if (!outTitle || !outDriverProps || !outSourceProps) {
    return;
  }
  outTitle->clear();
  outDriverProps->clear();
  outSourceProps->clear();
#if GIS_DESKTOP_HAVE_GDAL
  if (index < 0 || index >= static_cast<int>(g_doc.layers.size())) {
    *outTitle = L"未选择图层";
    *outDriverProps = L"在左侧「图层」列表中单击一行，可在此查看驱动与格式相关属性。";
    *outSourceProps = L"选中图层后，此处显示数据源路径、文件列表与数据集描述等。";
    return;
  }
  const auto& layer = g_doc.layers[static_cast<size_t>(index)];
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

bool MapEngine_IsRasterGdalLayer(int index) {
#if GIS_DESKTOP_HAVE_GDAL
  if (index < 0 || index >= static_cast<int>(g_doc.layers.size())) {
    return false;
  }
  return g_doc.layers[static_cast<size_t>(index)]->GetKind() == MapLayerKind::kRasterGdal;
#else
  (void)index;
  return false;
#endif
}

bool MapEngine_BuildOverviewsForLayer(int index, std::wstring& err) {
#if GIS_DESKTOP_HAVE_GDAL
  if (index < 0 || index >= static_cast<int>(g_doc.layers.size())) {
    err = L"无效图层。";
    return false;
  }
  return g_doc.layers[static_cast<size_t>(index)]->BuildOverviews(err);
#else
  (void)index;
  err = L"未启用 GDAL。";
  return false;
#endif
}

bool MapEngine_ClearOverviewsForLayer(int index, std::wstring& err) {
#if GIS_DESKTOP_HAVE_GDAL
  if (index < 0 || index >= static_cast<int>(g_doc.layers.size())) {
    err = L"无效图层。";
    return false;
  }
  return g_doc.layers[static_cast<size_t>(index)]->ClearOverviews(err);
#else
  (void)index;
  err = L"未启用 GDAL。";
  return false;
#endif
}

bool MapEngine_ReplaceLayerSourceFromUi(HWND owner, HWND layerListbox, int index) {
#if !GIS_DESKTOP_HAVE_GDAL
  (void)owner;
  (void)layerListbox;
  (void)index;
  return false;
#else
  if (index < 0 || index >= static_cast<int>(g_doc.layers.size())) {
    MessageBoxW(owner, L"请先选择有效图层。", L"AGIS", MB_OK | MB_ICONWARNING);
    return false;
  }
  MapLayerDriverKind kind{};
  std::wstring urlExtra;
  if (!MapEngine_ShowLayerDriverDialog(owner, &kind, &urlExtra)) {
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
    if (!g_doc.ReplaceLayerAt(static_cast<size_t>(index), std::move(layer), err)) {
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
    if (!g_doc.ReplaceLayerAt(static_cast<size_t>(index), std::move(layer), err)) {
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
    if (!g_doc.ReplaceLayerAt(static_cast<size_t>(index), std::move(layer), err)) {
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
    if (!g_doc.ReplaceLayerAt(static_cast<size_t>(index), std::move(layer), err)) {
      MessageBoxW(owner, err.c_str(), L"AGIS", MB_OK | MB_ICONERROR);
      return false;
    }
    AppLogLine(std::wstring(L"[图层] 已更换数据源（GDAL）：") + base);
  }
  MapEngine_RefreshLayerList(layerListbox);
  if (g_mapHwnd) {
    InvalidateRect(g_mapHwnd, nullptr, FALSE);
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

bool MapEngine_ShowLayerDriverDialog(HWND owner, MapLayerDriverKind* outKind, std::wstring* outUrl) {
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

bool MapEngine_SaveMapScreenshotToFile(HWND mapHwnd, const wchar_t* path, std::wstring& err) {
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

void MapEngine_PromptSaveMapScreenshot(HWND owner, HWND mapHwnd) {
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
  if (MapEngine_SaveMapScreenshotToFile(mapHwnd, path, err)) {
    AppLogLine(std::wstring(L"[截图] 已保存：") + path);
  } else {
    AppLogLine(std::wstring(L"[错误] 截图失败：") + err);
  }
}

void MapEngine_OnAddLayerFromDialog(HWND owner, HWND layerList) {
  MapLayerDriverKind kind{};
  std::wstring urlExtra;
  if (!MapEngine_ShowLayerDriverDialog(owner, &kind, &urlExtra)) {
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
    if (!g_doc.AddLayerFromTmsUrl(urlExtra, err)) {
      AppLogLine(std::wstring(L"[错误] 添加 TMS 图层失败：") + err);
      MessageBoxW(owner, err.c_str(), L"AGIS", MB_OK | MB_ICONERROR);
      return;
    }
    AppLogLine(std::wstring(L"[图层] 已添加 TMS：") + urlExtra);
    MapEngine_RefreshLayerList(layerList);
    if (g_mapHwnd) {
      InvalidateRect(g_mapHwnd, nullptr, FALSE);
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
    if (!g_doc.AddLayerFromWmtsUrl(urlExtra, err)) {
      AppLogLine(std::wstring(L"[错误] 添加 WMTS 图层失败：") + err);
      MessageBoxW(owner, err.c_str(), L"AGIS", MB_OK | MB_ICONERROR);
      return;
    }
    AppLogLine(std::wstring(L"[图层] 已添加 WMTS：") + urlExtra);
    MapEngine_RefreshLayerList(layerList);
    if (g_mapHwnd) {
      InvalidateRect(g_mapHwnd, nullptr, FALSE);
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
    if (!g_doc.AddLayerFromArcGisRestJsonUrl(urlExtra, err)) {
      AppLogLine(std::wstring(L"[错误] 添加 ArcGIS REST 图层失败：") + err);
      MessageBoxW(owner, err.c_str(), L"AGIS", MB_OK | MB_ICONERROR);
      return;
    }
    AppLogLine(std::wstring(L"[图层] 已添加 ArcGIS REST：") + urlExtra);
    MapEngine_RefreshLayerList(layerList);
    if (g_mapHwnd) {
      InvalidateRect(g_mapHwnd, nullptr, FALSE);
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
  if (!g_doc.AddLayerFromFile(path, err)) {
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
  MapEngine_RefreshLayerList(layerList);
  if (g_mapHwnd) {
    InvalidateRect(g_mapHwnd, nullptr, FALSE);
  }
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
  g_doc.Draw(mem, inner);
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
  switch (msg) {
    case WM_CREATE:
      g_mapHwnd = hwnd;
      g_doc.view = DefaultGeographicView();
      g_doc.refViewWidthDeg = 360.0;
      g_doc.refViewHeightDeg = 180.0;
      if (!MapGpu_Init(hwnd, g_mapRenderBackend)) {
        AppLogLine(L"[地图] GPU 呈现初始化失败，已回退为 GDI。");
        g_mapRenderBackend = MapRenderBackend::kGdi;
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
        g_mapChromeScale =
            CreateWindowW(L"STATIC", L"100%", WS_CHILD | WS_VISIBLE | SS_CENTER | SS_CENTERIMAGE, 40, 228, 56, 22,
                          hwnd, reinterpret_cast<HMENU>(static_cast<INT_PTR>(IDC_MAP_SCALE_TEXT)), inst, nullptr);
        CreateWindowW(L"BUTTON", L"+", WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON | WS_TABSTOP, 100, 228, 28, 22, hwnd,
                      reinterpret_cast<HMENU>(static_cast<INT_PTR>(IDC_MAP_ZOOM_IN)), inst, nullptr);
        LayoutMapOverlayControls(hwnd);
        ApplyUiFontToChildren(hwnd);
        MapEngine_UpdateMapChrome();
      }
      return 0;
    case WM_DESTROY:
      MapGpu_Shutdown(hwnd);
      g_mapHwnd = nullptr;
      g_mapChromeScale = nullptr;
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
          g_mapShortcutExpanded = !g_mapShortcutExpanded;
          if (HWND hEd = GetDlgItem(hwnd, IDC_MAP_SHORTCUT_EDIT)) {
            ShowWindow(hEd, g_mapShortcutExpanded ? SW_SHOW : SW_HIDE);
          }
          SetWindowTextW(GetDlgItem(hwnd, IDC_MAP_SHORTCUT_TOGGLE),
                         g_mapShortcutExpanded ? L"快捷键 ▲" : L"快捷键 ▼");
          LayoutMapOverlayControls(hwnd);
          InvalidateRect(hwnd, nullptr, FALSE);
          return 0;
        }
        if (id == IDC_MAP_VIS_TOGGLE) {
          g_mapVisExpanded = !g_mapVisExpanded;
          if (HWND hGr = GetDlgItem(hwnd, IDC_MAP_VIS_GRID)) {
            ShowWindow(hGr, g_mapVisExpanded ? SW_SHOW : SW_HIDE);
          }
          SetWindowTextW(GetDlgItem(hwnd, IDC_MAP_VIS_TOGGLE),
                         g_mapVisExpanded ? L"可见性 ▲" : L"可见性 ▼");
          LayoutMapOverlayControls(hwnd);
          InvalidateRect(hwnd, nullptr, FALSE);
          return 0;
        }
        if (id == IDC_MAP_VIS_GRID) {
          const UINT st = static_cast<UINT>(
              SendMessageW(GetDlgItem(hwnd, IDC_MAP_VIS_GRID), BM_GETCHECK, 0, 0));
          g_doc.SetShowLatLonGrid(st == BST_CHECKED);
          InvalidateRect(hwnd, nullptr, FALSE);
          return 0;
        }
        if (id == IDC_MAP_FIT) {
          g_doc.FitViewToLayers();
          InvalidateRect(hwnd, nullptr, FALSE);
          return 0;
        }
        if (id == IDC_MAP_ORIGIN) {
          g_doc.CenterContentOrigin(cw, ch);
          InvalidateRect(hwnd, nullptr, FALSE);
          return 0;
        }
        if (id == IDC_MAP_RESET) {
          g_doc.ResetZoom100AnchorCenter(cw, ch);
          InvalidateRect(hwnd, nullptr, FALSE);
          return 0;
        }
        if (id == IDC_MAP_ZOOM_OUT) {
          g_doc.ZoomViewAtCenter(1.0 / 1.1, cw, ch);
          InvalidateRect(hwnd, nullptr, FALSE);
          return 0;
        }
        if (id == IDC_MAP_ZOOM_IN) {
          g_doc.ZoomViewAtCenter(1.1, cw, ch);
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
      g_mdrag = true;
      g_mlast.x = GET_X_LPARAM(lParam);
      g_mlast.y = GET_Y_LPARAM(lParam);
      SetCapture(hwnd);
      return 0;
    case WM_MBUTTONUP:
      g_mdrag = false;
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
      g_doc.ZoomAt(mx, my, cw, ch, factor);
      RedrawWindow(hwnd, nullptr, nullptr,
                   RDW_INVALIDATE | RDW_ERASE | RDW_UPDATENOW | RDW_ALLCHILDREN);
      return 0;
    }
    case WM_MOUSEMOVE:
      if (g_mdrag && (wParam & MK_MBUTTON) != 0) {
        const int x = GET_X_LPARAM(lParam);
        const int y = GET_Y_LPARAM(lParam);
        const int dx = x - g_mlast.x;
        const int dy = y - g_mlast.y;
        g_mlast.x = x;
        g_mlast.y = y;
        RECT r2{};
        GetClientRect(hwnd, &r2);
        g_doc.PanPixels(dx, dy, r2.right - r2.left, r2.bottom - r2.top);
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
        g_doc.Draw(mem, client);
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
