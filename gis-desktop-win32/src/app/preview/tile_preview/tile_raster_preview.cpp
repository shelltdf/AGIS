#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstdlib>
#include <cstdio>
#include <cstring>
#include <cwctype>
#include <filesystem>
#include <fstream>
#include <list>
#include <memory>
#include <optional>
#include <set>
#include <sstream>
#include <string>
#include <utility>
#include <vector>
#include <unordered_map>

#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <windowsx.h>
#include <commctrl.h>
#include <commdlg.h>
#include <shellapi.h>

#include "app/preview/tile_preview/tile_preview_protocol_picker.h"
#include <gdiplus.h>

// 与 gdipluspixelformats.h 中 PixelFormat32bppARGB / PixelFormat24bppRGB 数值一致；避免 Windows 头与 GDI+ 符号宏冲突（如 C2589）。
namespace agis_gdip_pf {
constexpr Gdiplus::PixelFormat k32bppArgb = static_cast<Gdiplus::PixelFormat>(0x26200AU);
constexpr Gdiplus::PixelFormat k24bppRgb = static_cast<Gdiplus::PixelFormat>(0x21808U);
}  // namespace agis_gdip_pf

#include "utils/ui_font.h"
#include "utils/agis_ui_l10n.h"
#include "utils/ui_theme.h"
#include "common/app_core/main_app.h"
#include "common/app_core/satellite_app_menu.h"
#include "core/main_globals.h"

// `tile_preview/`：本地磁盘上的瓦片/栅格信息采样预览（目录 z/x/y、TMS、本地 tileset.json 元数据、单图、MBTiles/GPKG 等）。
// 不实现 HTTP(S)、WMTS、XYZ URL 等网络拉取；输入须为可访问的本机文件或文件夹路径。

#ifndef GIS_DESKTOP_HAVE_GDAL
#define GIS_DESKTOP_HAVE_GDAL 0
#endif
#if GIS_DESKTOP_HAVE_GDAL
#include "utils/agis_gdal_runtime_env.h"
#include <cpl_error.h>
#include <gdal.h>
#include <gdal_priv.h>
#include <ogrsf_frmts.h>
#include <proj.h>
#endif

// --- 瓦片：平面四叉树 (slippy z/x/y) + 3D Tiles BVH/体积元数据（tileset.json 根 region）---
//
// === Slippy 栅格：输入源投影 →（算法）→ 显示用投影/几何 → 屏幕 ===
// **产品语义**：把 **输入源**（按约定为 **EPSG:3857** 的 XYZ 纹理）按用户所选 **显示用展开** 画到窗口上；即「源 CRS 上的瓦片 → 经重投影与重采样 → 在显示几何下呈现 → 像素输出」。
// **实现上的采样方向（与上面箭头相反，数学等价）**：对每个 **输出像素**，从 **屏幕** 逆入 **当前显示平面**（`TileSlippyProjection` + 视口）→ 得到可与 PROJ 衔接的坐标（如 WGS84、CEA 米制）→ **PROJ 变换到输入源 CRS（EPSG:3857）** → 在磁盘纹理上双线性/拼贴取色。这样实现只需持有一份 3857 纹理，不必预生成多份显示 CRS 的切片。
// 1) **输入源投影 / 纹理 CRS**：标准 OSM/Google 类 XYZ 下，z/x/y 与瓦片范围对应 **EPSG:3857**；`SlippyTileIndexToEpsg3857BoundsMeters` 等用于纹素定位。
// 2) **显示用投影 / 显示几何**：`TileSlippyProjection` + center / pixelsPerTile 决定「屏上一点」落在片元线性+4326 语义、XYZ 恒等网格，还是等积圆柱平面；**不改写**磁盘 CRS。
// 3) **算法**：上述逆映射 + **PROJ（显示侧坐标 ↔ EPSG:3857）** + 重采样；整视口可为单遍 `TileSlippyPaintProjResampled`，否则按瓦片拼贴。
// 4) **特例**：`kWebMercatorGrid` 下显示平面与源索引一致，纹理侧无额外 CRS 变换（恒等展开）。
// 单张栅格 / MBTiles / GPKG 等：**输入源 CRS** 以 GDAL 数据集为准，不在此枚举内。
//
/// 用户可选的 **显示用展开 / 显示侧几何**（上条第 2 步）。**不等于**磁盘纹理 CRS；标准 Slippy 纹理侧恒为 EPSG:3857。
enum class TileSlippyProjection : uint8_t {
  /// 产品名仍称「等比例经纬度 / Plate Carrée」：采样平面为 WGS84 度；屏上映射取片元矩形线性（与 XYZ 拓扑一致）再 PROJ→3857 取纹理，避免仿射 lat∝屏 Y 与 Web 墨卡托瓦片竖向错位。非「整屏仿射」意义下横纵像素/度可不等。
  kEquirectangular = 0,
  /// 与 XYZ 索引一致的线性片元网格（传统「滑块地图」展开）。
  kWebMercatorGrid = 1,
  /// 等积圆柱（Lambert）：横轴为经度，纵轴为 sin(φ)，极点压缩更明显。
  kCylindricalEqualArea = 2,
};

static constexpr int kTileSlippyProjectionCount = 3;
static constexpr double kTileGeoPi = 3.14159265358979323846;

static const wchar_t* TileSlippyProjectionShortLabel(TileSlippyProjection p) {
  switch (p) {
  case TileSlippyProjection::kEquirectangular:
    return AgisPickUiLang(L"等经纬", L"Eq. lat/lon");
  case TileSlippyProjection::kWebMercatorGrid:
    return AgisPickUiLang(L"XYZ 网格", L"XYZ grid");
  case TileSlippyProjection::kCylindricalEqualArea:
    return AgisPickUiLang(L"等积圆柱", L"Eq. area");
  default:
    return L"";
  }
}

/// 界面长标签：描述 **显示侧** 展开方式；数据源纹理仍为 EPSG:3857。
static const wchar_t* TileSlippyProjectionLongLabel(TileSlippyProjection p) {
  switch (p) {
  case TileSlippyProjection::kEquirectangular:
    return AgisPickUiLang(L"等比例经纬度（Plate Carrée · 片元线性 + PROJ，与 XYZ 同拓扑）",
                          L"Plate Carrée (tile-linear + PROJ, same topology as XYZ)");
  case TileSlippyProjection::kWebMercatorGrid:
    return AgisPickUiLang(L"XYZ 片元线性（Web Mercator 网格）", L"XYZ tile-linear (Web Mercator grid)");
  case TileSlippyProjection::kCylindricalEqualArea:
    return AgisPickUiLang(L"等积圆柱（Lambert · sin φ）", L"Cylindrical equal-area (Lambert · sin φ)");
  default:
    return L"";
  }
}

/// 等积圆柱显示平面与 PROJ 管线一致（WGS84 datum）。等比例经纬度（选项「等经纬」）仅建 EPSG:4326→3857；等积圆柱另建 CEA 链，避免 CEA 失败拖垮前者。
static constexpr const char kTileSlippyCeaWgs84Proj[] = "+proj=cea +lat_ts=0 +lon_0=0 +datum=WGS84 +type=crs";

enum class TileSampleResult { kOk, kNoRaster, kContainerUnsupported };

struct TileFindResult {
  TileSampleResult code = TileSampleResult::kNoRaster;
  std::wstring path;
};

static bool IsRasterTileExtension(const std::wstring& ext) {
  return _wcsicmp(ext.c_str(), L".png") == 0 || _wcsicmp(ext.c_str(), L".jpg") == 0 ||
         _wcsicmp(ext.c_str(), L".jpeg") == 0 || _wcsicmp(ext.c_str(), L".webp") == 0 ||
         _wcsicmp(ext.c_str(), L".bmp") == 0 || _wcsicmp(ext.c_str(), L".tif") == 0 ||
         _wcsicmp(ext.c_str(), L".tiff") == 0;
}

static bool TryParseNonNegIntW(const std::wstring& s, int* out) {
  if (!out || s.empty()) {
    return false;
  }
  wchar_t* end = nullptr;
  const long v = std::wcstol(s.c_str(), &end, 10);
  if (end == s.c_str() || v < 0 || v > 0x0fffffff) {
    return false;
  }
  *out = static_cast<int>(v);
  return true;
}

static uint64_t PackTileKey(int z, int x, int y) {
  return (static_cast<uint64_t>(static_cast<unsigned char>(z)) << 56) | (static_cast<uint64_t>(x) << 28) |
         static_cast<uint64_t>(y);
}

static TileFindResult FindSampleTileRaster(const std::wstring& pathW) {
  std::error_code ec;
  const std::filesystem::path p(pathW);
  if (std::filesystem::is_regular_file(p, ec)) {
    const std::wstring ext = p.extension().wstring();
    if (IsRasterTileExtension(ext)) {
      return {TileSampleResult::kOk, p.wstring()};
    }
    if (_wcsicmp(ext.c_str(), L".mbtiles") == 0 || _wcsicmp(ext.c_str(), L".gpkg") == 0) {
      return {TileSampleResult::kContainerUnsupported, L""};
    }
    return {TileSampleResult::kNoRaster, L""};
  }
  if (!std::filesystem::is_directory(p, ec)) {
    return {TileSampleResult::kNoRaster, L""};
  }
  int scanned = 0;
  constexpr int kMaxScan = 6000;
  for (std::filesystem::recursive_directory_iterator it(
           p, std::filesystem::directory_options::skip_permission_denied, ec);
       it != std::filesystem::recursive_directory_iterator{} && scanned < kMaxScan; ++it, ++scanned) {
    if (!it->is_regular_file(ec)) {
      continue;
    }
    const std::wstring ext = it->path().extension().wstring();
    if (IsRasterTileExtension(ext)) {
      return {TileSampleResult::kOk, it->path().wstring()};
    }
  }
  return {TileSampleResult::kNoRaster, L""};
}

static bool ReadWholeFileAscii(const std::wstring& pathW, std::string* out) {
  if (!out) {
    return false;
  }
  std::ifstream ifs(std::filesystem::path(pathW), std::ios::binary);
  if (!ifs) {
    return false;
  }
  std::string buf((std::istreambuf_iterator<char>(ifs)), std::istreambuf_iterator<char>());
  out->swap(buf);
  return !out->empty();
}

/// 磁盘为 TMS 行号文件名（AGIS 写出为 `y_tms`）时，预览栅格需按 XYZ 映射：y_xyz = (2^z-1-y_disk)。
static bool DetectTmsTileLayoutOnDisk(const std::filesystem::path& root) {
  std::error_code ec;
  if (std::filesystem::is_regular_file(root / L"tms.xml", ec)) {
    return true;
  }
  std::string raw;
  if (ReadWholeFileAscii((root / L"README.txt").wstring(), &raw)) {
    if (raw.find("protocol=tms") != std::string::npos) {
      return true;
    }
  }
  return false;
}

#if GIS_DESKTOP_HAVE_GDAL
static std::unique_ptr<Gdiplus::Bitmap> TryLoadGdalRasterTileContainerPreview(const std::wstring& pathW,
                                                                              std::wstring* diagOut) {
  if (diagOut) {
    diagOut->clear();
  }
  AgisEnsureGdalDataPath();
  CPLErrorReset();
  GDALAllRegister();
  std::string utf8;
  {
    const int n =
        WideCharToMultiByte(CP_UTF8, 0, pathW.c_str(), static_cast<int>(pathW.size()), nullptr, 0, nullptr, nullptr);
    if (n <= 0) {
      if (diagOut) {
        *diagOut = AgisPickUiLang(L"路径无法转为 UTF-8。", L"Path could not be converted to UTF-8.");
      }
      return nullptr;
    }
    utf8.assign(static_cast<size_t>(n), '\0');
    WideCharToMultiByte(CP_UTF8, 0, pathW.c_str(), static_cast<int>(pathW.size()), utf8.data(), n, nullptr, nullptr);
  }
  GDALDatasetH ds = GDALOpenEx(utf8.c_str(), GDAL_OF_RASTER | GDAL_OF_READONLY, nullptr, nullptr, nullptr);
  if (!ds) {
    if (diagOut) {
      *diagOut = AgisPickUiLang(L"GDAL 无法以栅格方式打开该文件（驱动缺失、需 PROJ/GDAL_DATA，或不是平铺栅格内容）。",
                                L"GDAL could not open this file as raster (missing driver, PROJ/GDAL_DATA, or not tiled "
                                L"raster content).");
      const char* cpl = CPLGetLastErrorMsg();
      if (cpl && cpl[0]) {
        const int wn = MultiByteToWideChar(CP_UTF8, 0, cpl, -1, nullptr, 0);
        if (wn > 1) {
          std::wstring wc(static_cast<size_t>(wn - 1), L'\0');
          MultiByteToWideChar(CP_UTF8, 0, cpl, -1, wc.data(), wn);
          diagOut->append(L"\n");
          diagOut->append(wc);
        }
      }
    }
    return nullptr;
  }
  const int w = GDALGetRasterXSize(ds);
  const int h = GDALGetRasterYSize(ds);
  const int bands = GDALGetRasterCount(ds);
  if (w < 1 || h < 1 || bands < 1) {
    GDALClose(ds);
    if (diagOut) {
      *diagOut = AgisPickUiLang(L"数据集无有效栅格尺寸。", L"Dataset has no valid raster dimensions.");
    }
    return nullptr;
  }
  constexpr int kMaxDim = 2048;
  int outW = w;
  int outH = h;
  if (w >= h) {
    if (outW > kMaxDim) {
      outW = kMaxDim;
      outH = (std::max)(1, static_cast<int>(std::lround(static_cast<double>(h) * static_cast<double>(kMaxDim) /
                                                         static_cast<double>(w))));
    }
  } else {
    if (outH > kMaxDim) {
      outH = kMaxDim;
      outW = (std::max)(1, static_cast<int>(std::lround(static_cast<double>(w) * static_cast<double>(kMaxDim) /
                                                         static_cast<double>(h))));
    }
  }
  std::vector<uint8_t> planeR(static_cast<size_t>(outW) * static_cast<size_t>(outH));
  std::vector<uint8_t> planeG(planeR.size());
  std::vector<uint8_t> planeB(planeR.size());
  auto readPlane = [&](int bandIdx, uint8_t* dst) -> bool {
    GDALRasterBandH bh = GDALGetRasterBand(ds, bandIdx);
    if (!bh) {
      return false;
    }
    return GDALRasterIO(bh, GF_Read, 0, 0, w, h, dst, outW, outH, GDT_Byte, 1, outW) == CE_None;
  };
  bool ok = false;
  if (bands >= 3) {
    ok = readPlane(1, planeR.data()) && readPlane(2, planeG.data()) && readPlane(3, planeB.data());
  } else {
    ok = readPlane(1, planeR.data());
    if (ok) {
      planeG = planeR;
      planeB = planeR;
    }
  }
  GDALClose(ds);
  if (!ok) {
    if (diagOut) {
      *diagOut = AgisPickUiLang(L"RasterIO 读缩略图失败（波段类型可能非 Byte，或仅为矢量 GeoPackage）。",
                                L"RasterIO failed to read thumbnail (bands may not be Byte, or vector-only GeoPackage).");
    }
    return nullptr;
  }
  auto bmp = std::make_unique<Gdiplus::Bitmap>(outW, outH, agis_gdip_pf::k24bppRgb);
  if (!bmp || bmp->GetLastStatus() != Gdiplus::Ok) {
    return nullptr;
  }
  Gdiplus::BitmapData bd{};
  Gdiplus::Rect r(0, 0, outW, outH);
  if (bmp->LockBits(&r, Gdiplus::ImageLockModeWrite, agis_gdip_pf::k24bppRgb, &bd) != Gdiplus::Ok) {
    return nullptr;
  }
  auto* dstBase = static_cast<uint8_t*>(bd.Scan0);
  for (int yy = 0; yy < outH; ++yy) {
    uint8_t* row = dstBase + yy * bd.Stride;
    for (int xx = 0; xx < outW; ++xx) {
      const size_t i = static_cast<size_t>(yy) * static_cast<size_t>(outW) + static_cast<size_t>(xx);
      row[xx * 3 + 0] = planeB[i];
      row[xx * 3 + 1] = planeG[i];
      row[xx * 3 + 2] = planeR[i];
    }
  }
  bmp->UnlockBits(&bd);
  return bmp;
}

/// 尝试读取栅格数据集的 CRS（WKT）；打开失败或为空则返回空串。
static std::string TilePreviewGdalDatasetProjectionRefUtf8(const std::wstring& pathW) {
  AgisEnsureGdalDataPath();
  CPLErrorReset();
  GDALAllRegister();
  std::string utf8;
  const int n =
      WideCharToMultiByte(CP_UTF8, 0, pathW.c_str(), static_cast<int>(pathW.size()), nullptr, 0, nullptr, nullptr);
  if (n <= 0) {
    return {};
  }
  utf8.assign(static_cast<size_t>(n), '\0');
  WideCharToMultiByte(CP_UTF8, 0, pathW.c_str(), static_cast<int>(pathW.size()), utf8.data(), n, nullptr, nullptr);
  GDALDatasetH ds = GDALOpenEx(utf8.c_str(), GDAL_OF_RASTER | GDAL_OF_READONLY, nullptr, nullptr, nullptr);
  if (!ds) {
    return {};
  }
  const char* wkt = GDALGetProjectionRef(ds);
  std::string out = (wkt && wkt[0]) ? std::string(wkt) : std::string();
  GDALClose(ds);
  return out;
}
#endif  // GIS_DESKTOP_HAVE_GDAL

static std::optional<std::wstring> FindTilesetJsonPath(const std::wstring& rootW) {
  std::error_code ec;
  const std::filesystem::path p(rootW);
  if (std::filesystem::is_regular_file(p, ec)) {
    if (_wcsicmp(p.filename().c_str(), L"tileset.json") == 0) {
      return p.wstring();
    }
    if (_wcsicmp(p.extension().c_str(), L".json") == 0) {
      return p.wstring();
    }
    return std::nullopt;
  }
  if (!std::filesystem::is_directory(p, ec)) {
    return std::nullopt;
  }
  const auto tj = p / L"tileset.json";
  if (std::filesystem::is_regular_file(tj, ec)) {
    return tj.wstring();
  }
  return std::nullopt;
}

static std::filesystem::path ThreeDTilesContentDirectory(const std::wstring& rootW, const std::wstring& tilesetJsonW) {
  const std::filesystem::path ts(tilesetJsonW);
  std::error_code ec;
  if (std::filesystem::is_regular_file(ts, ec)) {
    return ts.parent_path();
  }
  return std::filesystem::path(rootW);
}

static std::wstring Utf8JsonToWide(const std::string& u8) {
  if (u8.empty()) {
    return {};
  }
  const int n = MultiByteToWideChar(CP_UTF8, 0, u8.data(), static_cast<int>(u8.size()), nullptr, 0);
  if (n <= 0) {
    return std::wstring(u8.begin(), u8.end());
  }
  std::wstring w(static_cast<size_t>(n), L'\0');
  MultiByteToWideChar(CP_UTF8, 0, u8.data(), static_cast<int>(u8.size()), w.data(), n);
  return w;
}

static bool TryParseFirstDoubleAfterKey(const std::string& raw, const char* key, double* out) {
  if (!out || !key) {
    return false;
  }
  const size_t pos = raw.find(key);
  if (pos == std::string::npos) {
    return false;
  }
  const size_t colon = raw.find(':', pos);
  if (colon == std::string::npos) {
    return false;
  }
  const char* p = raw.c_str() + colon + 1;
  while (*p == ' ' || *p == '\t' || *p == '\n' || *p == '\r') {
    ++p;
  }
  char* endp = nullptr;
  const double v = std::strtod(p, &endp);
  if (endp == p) {
    return false;
  }
  *out = v;
  return true;
}

static bool TryExtractTilesetAssetVersion(const std::string& raw, std::string* ver) {
  if (!ver) {
    return false;
  }
  const size_t apos = raw.find("\"asset\"");
  if (apos == std::string::npos) {
    return false;
  }
  const size_t searchEnd = (std::min)(apos + 900, raw.size());
  const size_t vpos = raw.find("\"version\"", apos);
  if (vpos == std::string::npos || vpos > searchEnd) {
    return false;
  }
  const size_t colon = raw.find(':', vpos);
  if (colon == std::string::npos) {
    return false;
  }
  size_t i = colon + 1;
  while (i < raw.size() && (raw[i] == ' ' || raw[i] == '\t' || raw[i] == '\n' || raw[i] == '\r')) {
    ++i;
  }
  if (i >= raw.size() || raw[i] != '"') {
    return false;
  }
  ++i;
  size_t j = i;
  while (j < raw.size() && raw[j] != '"') {
    if (raw[j] == '\\' && j + 1 < raw.size()) {
      j += 2;
    } else {
      ++j;
    }
  }
  if (j <= i) {
    return false;
  }
  *ver = raw.substr(i, j - i);
  return !ver->empty();
}

static void CollectSampleUris(const std::string& raw, int maxN, std::vector<std::string>* out) {
  if (!out || maxN <= 0) {
    return;
  }
  std::set<std::string> seen;
  size_t pos = 0;
  while (static_cast<int>(out->size()) < maxN && pos < raw.size()) {
    pos = raw.find("\"uri\"", pos);
    if (pos == std::string::npos) {
      break;
    }
    const size_t colon = raw.find(':', pos);
    if (colon == std::string::npos) {
      pos += 5;
      continue;
    }
    size_t i = colon + 1;
    while (i < raw.size() && (raw[i] == ' ' || raw[i] == '\t' || raw[i] == '\n' || raw[i] == '\r')) {
      ++i;
    }
    if (i >= raw.size() || raw[i] != '"') {
      pos = colon + 1;
      continue;
    }
    ++i;
    size_t j = i;
    while (j < raw.size() && raw[j] != '"') {
      if (raw[j] == '\\' && j + 1 < raw.size()) {
        j += 2;
      } else {
        ++j;
      }
    }
    if (j > i) {
      const std::string uri = raw.substr(i, j - i);
      if (!uri.empty() && seen.insert(uri).second) {
        out->push_back(uri);
      }
    }
    pos = j + 1;
  }
}

struct ThreeDTilesContentStats {
  size_t b3dm = 0;
  size_t i3dm = 0;
  size_t pnts = 0;
  size_t cmpt = 0;
  size_t glb = 0;
  size_t gltf = 0;
};

static void ScanThreeDTilesPayloadFiles(const std::filesystem::path& contentRoot, ThreeDTilesContentStats* s) {
  if (!s) {
    return;
  }
  std::error_code ec;
  if (!std::filesystem::is_directory(contentRoot, ec)) {
    return;
  }
  size_t scanned = 0;
  constexpr size_t kMaxScan = 12000;
  for (std::filesystem::recursive_directory_iterator it(
           contentRoot, std::filesystem::directory_options::skip_permission_denied, ec);
       it != std::filesystem::recursive_directory_iterator{} && scanned < kMaxScan; ++it, ++scanned) {
    if (!it->is_regular_file(ec)) {
      continue;
    }
    const std::wstring ext = it->path().extension().wstring();
    if (_wcsicmp(ext.c_str(), L".b3dm") == 0) {
      ++s->b3dm;
    } else if (_wcsicmp(ext.c_str(), L".i3dm") == 0) {
      ++s->i3dm;
    } else if (_wcsicmp(ext.c_str(), L".pnts") == 0) {
      ++s->pnts;
    } else if (_wcsicmp(ext.c_str(), L".cmpt") == 0) {
      ++s->cmpt;
    } else if (_wcsicmp(ext.c_str(), L".glb") == 0) {
      ++s->glb;
    } else if (_wcsicmp(ext.c_str(), L".gltf") == 0) {
      ++s->gltf;
    }
  }
}

static std::wstring BuildThreeDTilesDashboard(const std::wstring& rootW, const std::wstring& tilesetJsonW,
                                              const std::wstring& bvHintLines) {
  std::string raw;
  if (!ReadWholeFileAscii(tilesetJsonW, &raw)) {
    return std::wstring(AgisPickUiLang(L"【3D Tiles】无法读取 tileset.json。\n路径：\n",
                                       L"[3D Tiles] Cannot read tileset.json.\nPath:\n")) +
           tilesetJsonW;
  }
  std::wstring dash = AgisPickUiLang(L"【3D Tiles · 元数据预览】\n", L"[3D Tiles · metadata preview]\n");
  dash += AgisPickUiLang(
      L"AGIS 内建说明与目录扫描（不加载 glTF/b3dm 网格）。完整浏览请用 Cesium 或「系统默认打开」。\n",
      L"AGIS in-window notes and directory scan (does not load glTF/b3dm meshes). For full viewing use Cesium or open "
      L"with the system default app.\n");
  dash += AgisPickUiLang(
      L"对接 C++ 运行时请参考仓库 3rdparty/README-CESIUM-NATIVE.md（cesium-native 源码已在 3rdparty/cesium-native-*）。\n\n",
      L"For a C++ runtime, see repo 3rdparty/README-CESIUM-NATIVE.md (cesium-native sources under 3rdparty/cesium-native-*).\n\n");
  if (!bvHintLines.empty()) {
    dash += bvHintLines;
    dash += L"\n\n";
  }
  std::string aver;
  if (TryExtractTilesetAssetVersion(raw, &aver)) {
    dash += AgisPickUiLang(L"asset.version（粗解析）: ", L"asset.version (rough parse): ");
    dash += Utf8JsonToWide(aver);
    dash += L"\n";
  }
  double ge = 0;
  if (TryParseFirstDoubleAfterKey(raw, "\"geometricError\"", &ge)) {
    dash += AgisPickUiLang(L"首个 geometricError（粗解析，多为根节点）: ",
                           L"First geometricError (rough parse, often root): ");
    dash += std::to_wstring(ge);
    dash += L"\n";
  }
  const auto contentDir = ThreeDTilesContentDirectory(rootW, tilesetJsonW);
  ThreeDTilesContentStats st{};
  ScanThreeDTilesPayloadFiles(contentDir, &st);
  const size_t tileFiles = st.b3dm + st.i3dm + st.pnts + st.cmpt;
  dash += AgisPickUiLang(L"\n内容瓦片文件（子目录扫描≤12000，按扩展名计数）：\n",
                         L"\nContent tile files (subdir scan ≤12000, by extension):\n");
  dash += L"  b3dm=" + std::to_wstring(st.b3dm) + L" i3dm=" + std::to_wstring(st.i3dm) + L" pnts=" +
          std::to_wstring(st.pnts) + L" cmpt=" + std::to_wstring(st.cmpt) + L"\n";
  dash += L"  glb=" + std::to_wstring(st.glb) + L" gltf=" + std::to_wstring(st.gltf) + L"\n";
  if (tileFiles == 0 && st.glb == 0 && st.gltf == 0) {
    dash += AgisPickUiLang(L"（未见常见载荷扩展名：可能仅外链 URL、或路径不在当前目录树下。）\n",
                           L"(No common payload extensions: may be URL-only, or paths outside this tree.)\n");
  }
  std::vector<std::string> uris;
  CollectSampleUris(raw, 8, &uris);
  if (!uris.empty()) {
    dash += AgisPickUiLang(L"\ntileset 内 uri 抽样（至多 8 条，去重）：\n",
                           L"\nSample URIs from tileset (up to 8, deduped):\n");
    for (const auto& u : uris) {
      dash += L"  · ";
      dash += Utf8JsonToWide(u);
      dash += L"\n";
    }
  }
  dash += AgisPickUiLang(L"\n根目录/内容根：\n  ", L"\nRoot / content root:\n  ");
  dash += contentDir.wstring();
  dash += L"\n";
  return dash;
}

/// 自 tileset.json 粗提取首个 region（弧度）并换算为度 + 粗计 children 出现次数（BVH 层级提示）。
static std::wstring RoughTilesetBvHintForFile(const std::wstring& tilesetJsonPathW) {
  std::error_code ec;
  if (!std::filesystem::is_regular_file(std::filesystem::path(tilesetJsonPathW), ec)) {
    return {};
  }
  std::string raw;
  if (!ReadWholeFileAscii(tilesetJsonPathW, &raw)) {
    return AgisPickUiLang(L"(tileset.json 无法读取)", L"(Could not read tileset.json)");
  }
  size_t rpos = raw.find("\"region\"");
  double west = 0, south = 0, east = 0, north = 0, zminM = 0, zmaxM = 0;
  bool haveReg = false;
  if (rpos != std::string::npos) {
    size_t lb = raw.find('[', rpos);
    if (lb != std::string::npos) {
      const char* p = raw.c_str() + lb + 1;
      double vals[6]{};
      int got = 0;
      for (; got < 6 && p && *p; ++got) {
        while (*p && (*p == ' ' || *p == '\t' || *p == '\n' || *p == '\r' || *p == ',')) {
          ++p;
        }
        if (!*p) {
          break;
        }
        char* endp = nullptr;
        vals[got] = std::strtod(p, &endp);
        if (endp == p) {
          break;
        }
        p = endp;
      }
      if (got >= 4) {
        constexpr double kRad2Deg = 180.0 / 3.14159265358979323846;
        west = vals[0] * kRad2Deg;
        south = vals[1] * kRad2Deg;
        east = vals[2] * kRad2Deg;
        north = vals[3] * kRad2Deg;
        if (got >= 6) {
          zminM = vals[4];
          zmaxM = vals[5];
        }
        haveReg = true;
      }
    }
  }
  int childHits = 0;
  for (size_t i = 0; i + 10 < raw.size(); ++i) {
    if (raw.compare(i, 10, "\"children\"") == 0) {
      ++childHits;
    }
  }
  std::wostringstream wo;
  wo << AgisPickUiLang(L"【BVH / 3D Tiles】Cesium 瓦片树为层次包围体；根节点常用 region/box。\n",
                       L"[BVH / 3D Tiles] Cesium tileset uses a hierarchy of bounding volumes; roots often use region/box.\n");
  if (haveReg) {
    if (AgisGetUiLanguage() == AgisUiLanguage::kEn) {
      wo << L"Root region → lon/lat (°): W=" << west << L" S=" << south << L" E=" << east << L" N=" << north;
      wo << L" ; elev approx (m) zmin=" << zminM << L" zmax=" << zmaxM << L"\n";
    } else {
      wo << L"根 region→经纬度(°): W=" << west << L" S=" << south << L" E=" << east << L" N=" << north;
      wo << L" ；高程约(m) zmin=" << zminM << L" zmax=" << zmaxM << L"\n";
    }
  } else {
    wo << AgisPickUiLang(L"未解析到标准数字 region 数组（可能被压缩或格式非预期）。\n",
                         L"No standard numeric region array parsed (compressed or unexpected format).\n");
  }
  wo << AgisPickUiLang(L"子树提示: \"children\" 出现 ", L"Subtree hint: \"children\" appears ") << childHits
     << AgisPickUiLang(
            L" 次。\n【八叉树】对 3D box 体积的八分细分常见于嵌套子 tile；本预览不解析子网格、不渲染 glTF/b3dm，完整三维请用 "
            L"Cesium/系统打开。",
            L" times.\n[Octree] nested tiles often octree-split 3D boxes; this preview does not parse child meshes or "
            L"render glTF/b3dm—use Cesium or system open for full 3D.");
  return wo.str();
}

static size_t IndexSlippyQuadtree(const std::wstring& rootW, bool tmsYFilenamesOnDisk,
                                  std::unordered_map<uint64_t, std::wstring>* paths, int* maxZOut) {
  paths->clear();
  if (maxZOut) {
    *maxZOut = 0;
  }
  std::error_code ec;
  const std::filesystem::path root(rootW);
  if (!std::filesystem::is_directory(root, ec)) {
    return 0;
  }
  size_t scanned = 0;
  constexpr size_t kMaxScan = 12000;
  int localMaxZ = 0;
  for (std::filesystem::recursive_directory_iterator it(
           root, std::filesystem::directory_options::skip_permission_denied, ec);
       it != std::filesystem::recursive_directory_iterator{} && scanned < kMaxScan; ++it, ++scanned) {
    if (!it->is_regular_file(ec)) {
      continue;
    }
    const std::wstring ext = it->path().extension().wstring();
    if (!IsRasterTileExtension(ext)) {
      continue;
    }
    std::filesystem::path rel = std::filesystem::relative(it->path(), root, ec);
    if (ec || rel.empty()) {
      continue;
    }
    std::vector<std::wstring> comp;
    for (auto& part : rel) {
      comp.push_back(part.wstring());
    }
    if (comp.size() < 3) {
      continue;
    }
    int z = 0, x = 0, yDisk = 0;
    if (!TryParseNonNegIntW(comp[comp.size() - 3], &z) || !TryParseNonNegIntW(comp[comp.size() - 2], &x)) {
      continue;
    }
    const std::wstring& fname = comp.back();
    const size_t dot = fname.find_last_of(L'.');
    const std::wstring ystem = dot == std::wstring::npos ? fname : fname.substr(0, dot);
    if (!TryParseNonNegIntW(ystem, &yDisk)) {
      continue;
    }
    if (z > 29) {
      continue;
    }
    const int dim = 1 << z;
    if (x >= dim || yDisk >= dim) {
      continue;
    }
    const int yKey = tmsYFilenamesOnDisk ? (dim - 1 - yDisk) : yDisk;
    (*paths)[PackTileKey(z, x, yKey)] = it->path().wstring();
    localMaxZ = (std::max)(localMaxZ, z);
  }
  if (maxZOut) {
    *maxZOut = localMaxZ;
  }
  return paths->size();
}

struct TilePreviewState {
  enum class Mode { kSingleRaster, kSlippyQuadtree, kThreeDTilesMeta };
  std::wstring rootPath;
  std::wstring samplePath;
  std::wstring hint;
  std::wstring bvHint;
  std::unique_ptr<Gdiplus::Bitmap> bmp;
  Mode mode = Mode::kSingleRaster;
  std::unordered_map<uint64_t, std::wstring> slippyPaths;
  /// 解码后的瓦片面 LRU：front 最久未用；换 Z 时剔除非当前层键，控制常驻内存。
  std::list<uint64_t> tileTexLru;
  std::unordered_map<uint64_t, std::pair<std::list<uint64_t>::iterator, std::unique_ptr<Gdiplus::Bitmap>>> tileTexCache;
  int indexMaxZ = 0;
  size_t tileCount = 0;
  int viewZ = 0;
  double centerTx = 0.5;
  double centerTy = 0.5;
  /** 屏幕上每逻辑瓦片边长（像素）；接近 256 时与常见地图瓦片尺度一致。 */
  float pixelsPerTile = 256.f;
  bool dragging = false;
  POINT lastDragPt{};
  /// 鼠标（客户区）与地理读数，供底部状态条使用。
  POINT lastPointerClient{};
  bool pointerOverImage = false;
  bool pointerGeoValid = false;
  double pointerLon = 0.0;
  double pointerLat = 0.0;
  /// 底部单行状态（客户区坐标、经纬度/像素等），在 WM_PAINT 中绘制。
  std::wstring tilePointerStatusLine;
  bool tileMouseTrackingLeave = false;
  /// 非拖拽时用于节流 WM_PAINT：仅当光标落入新格（2^3 像素）才整窗刷新，避免每像素触发整屏 GDI+。
  int lastPointerQuantX = INT_MIN;
  int lastPointerQuantY = INT_MIN;

  /// 显示用投影/展开（`TileSlippyProjection`）；Slippy 源纹理 CRS 仍为 EPSG:3857，见文件头流水线说明。
  TileSlippyProjection slippyProjection = TileSlippyProjection::kEquirectangular;
  bool showOptionsPanel = false;
  bool uiShowTopHint = true;
  bool uiShowSlippyMapHud = true;
  bool uiShowTileGrid = true;
  bool uiShowTileLabels = true;
  /// 索引时磁盘行号是否为 TMS（文件名 y 经翻转映射为 XYZ ty）；仅 Slippy 模式有意义。
  bool slippyDiskTmsYIndex = false;
};

static constexpr size_t kTilePreviewBitmapCacheMax = 160;
static constexpr UINT kTilePreviewRequestOpenMsg = WM_APP + 301;

static std::wstring g_pendingTilePreviewRoot;

/// 底部单行状态条高度（半透明信息栏）。
static constexpr int kTilePreviewBottomStatus = 28;

/// 顶部说明区高度：Slippy / 单图 为紧凑顶栏（仅动态 hint）；3D Tiles 元数据区仍较高。
static int TilePreviewTopBarPx(const TilePreviewState* st) {
  if (!st) {
    return 72;
  }
  switch (st->mode) {
  case TilePreviewState::Mode::kThreeDTilesMeta:
    return 288;
  case TilePreviewState::Mode::kSlippyQuadtree:
    return st->uiShowTopHint ? 72 : 40;
  default:
    return st->uiShowTopHint ? 72 : 40;
  }
}

static RECT TilePreviewStatusBarRect(RECT cr) {
  const int h = kTilePreviewBottomStatus;
  return {cr.left, (std::max)(cr.top, cr.bottom - h), cr.right, cr.bottom};
}

static RECT TileSlippyImageArea(RECT cr, const TilePreviewState* st) {
  const int margin = 10;
  const int top = cr.top + TilePreviewTopBarPx(st);
  const int bot = cr.bottom - margin - kTilePreviewBottomStatus;
  if (bot <= top + 8) {
    return {cr.left + margin, top, cr.right - margin, top + 8};
  }
  return {cr.left + margin, top, cr.right - margin, bot};
}

/// 与单图预览 WM_PAINT 中缩放逻辑一致，用于命中测试与状态栏。
static bool TilePreviewComputeRasterDestRect(const TilePreviewState* st, RECT imgArea, RECT* outDest) {
  if (!st || !outDest || !st->bmp || st->bmp->GetLastStatus() != Gdiplus::Ok) {
    return false;
  }
  const int iw = st->bmp->GetWidth();
  const int ih = st->bmp->GetHeight();
  if (iw <= 0 || ih <= 0) {
    return false;
  }
  const int aw = (std::max)(1, static_cast<int>(imgArea.right - imgArea.left));
  const int ah = (std::max)(1, static_cast<int>(imgArea.bottom - imgArea.top));
  const float scale = (std::min)(static_cast<float>(aw) / static_cast<float>(iw), static_cast<float>(ah) / static_cast<float>(ih));
  const int dw = static_cast<int>(static_cast<float>(iw) * scale);
  const int dh = static_cast<int>(static_cast<float>(ih) * scale);
  const int dx = imgArea.left + (aw - dw) / 2;
  const int dy = imgArea.top + (ah - dh) / 2;
  *outDest = {dx, dy, dx + dw, dy + dh};
  return true;
}

/// XYZ 片元坐标（当前 Z 下，0..2^z）→ WGS84 经纬度（度），球面墨卡托反算。
static void SlippyTileXYToLonLat(double tileX, double tileY, int z, double* lonDeg, double* latDeg) {
  if (!lonDeg || !latDeg || z < 0 || z > 30) {
    return;
  }
  const double n = static_cast<double>(1u << static_cast<unsigned>(z));
  *lonDeg = tileX / n * 360.0 - 180.0;
  const double latRad = std::atan(std::sinh(kTileGeoPi * (1.0 - 2.0 * tileY / n)));
  *latDeg = latRad * 180.0 / kTileGeoPi;
}

/// 视口在片元浮点矩形 [wl,wl+ww]×[wt,wt+wh] 下的 WGS84 轴对齐包络（四角墨卡托反算；跨日界线时未特殊处理）。
static void SlippyViewportLonLatBounds(int z, double wl, double wt, double ww, double wh, double* lonMin,
                                       double* lonMax, double* latMin, double* latMax) {
  if (!lonMin || !lonMax || !latMin || !latMax || z < 0 || z > 30) {
    return;
  }
  double c[4][2]{};
  auto corner = [&](int i, double tx, double ty) { SlippyTileXYToLonLat(tx, ty, z, &c[i][0], &c[i][1]); };
  corner(0, wl, wt);
  corner(1, wl + ww, wt);
  corner(2, wl, wt + wh);
  corner(3, wl + ww, wt + wh);
  *lonMin = *lonMax = c[0][0];
  *latMin = *latMax = c[0][1];
  for (int i = 1; i < 4; ++i) {
    *lonMin = (std::min)(*lonMin, c[i][0]);
    *lonMax = (std::max)(*lonMax, c[i][0]);
    *latMin = (std::min)(*latMin, c[i][1]);
    *latMax = (std::max)(*latMax, c[i][1]);
  }
}

/// 在 full 内取最大轴对齐子矩形，使 **宽:高 = Δlon:Δlat**（等经纬/等积圆柱下与 Plate Carrée「一度经≈一度纬」像素比一致）；地理视域仍为 (wl,wt,ww,wh) 片元窗，映射到子矩形内 u,v∈[0,1]。
static RECT TileSlippyLetterboxMapRectForEqualLonLatPx(RECT fullImg, TileSlippyProjection proj, int viewZ, double wl,
                                                       double wt, double ww, double wh) {
  if (proj == TileSlippyProjection::kWebMercatorGrid) {
    return fullImg;
  }
  const int aw0 = (std::max)(1, static_cast<int>(fullImg.right - fullImg.left));
  const int ah0 = (std::max)(1, static_cast<int>(fullImg.bottom - fullImg.top));
  double lmn = 0;
  double lmx = 0;
  double bmn = 0;
  double bmx = 0;
  SlippyViewportLonLatBounds(viewZ, wl, wt, ww, wh, &lmn, &lmx, &bmn, &bmx);
  double lonSpan = lmx - lmn;
  double latSpan = bmx - bmn;
  lonSpan = (std::max)(lonSpan, 1e-6);
  latSpan = (std::max)(latSpan, 1e-6);
  const double targetAspect = lonSpan / latSpan;
  const double car = static_cast<double>(aw0) / static_cast<double>(ah0);
  int iw = aw0;
  int ih = ah0;
  if (car > targetAspect) {
    ih = ah0;
    iw = static_cast<int>(std::lround(static_cast<double>(ah0) * targetAspect));
    iw = (std::clamp)(iw, 4, aw0);
  } else {
    iw = aw0;
    ih = static_cast<int>(std::lround(static_cast<double>(aw0) / targetAspect));
    ih = (std::clamp)(ih, 4, ah0);
  }
  const int ix = fullImg.left + (aw0 - iw) / 2;
  const int iy = fullImg.top + (ah0 - ih) / 2;
  return {ix, iy, ix + iw, iy + ih};
}

struct SlippyViewFrame {
  RECT fullImg{};
  RECT mapImg{};
  double worldLeft = 0.0;
  double worldTop = 0.0;
  double worldW = 1.0;
  double worldH = 1.0;
};

static void TileSlippyComputeViewFrame(RECT clientCr, const TilePreviewState* st, SlippyViewFrame* out) {
  if (!st || !out) {
    return;
  }
  out->fullImg = TileSlippyImageArea(clientCr, st);
  const int aw0 = (std::max)(1, static_cast<int>(out->fullImg.right - out->fullImg.left));
  const int ah0 = (std::max)(1, static_cast<int>(out->fullImg.bottom - out->fullImg.top));
  const double ppt = static_cast<double>(st->pixelsPerTile);
  out->worldW = static_cast<double>(aw0) / ppt;
  out->worldH = static_cast<double>(ah0) / ppt;
  out->worldLeft = st->centerTx - out->worldW * 0.5;
  out->worldTop = st->centerTy - out->worldH * 0.5;
  out->mapImg = TileSlippyLetterboxMapRectForEqualLonLatPx(out->fullImg, st->slippyProjection, st->viewZ,
                                                           out->worldLeft, out->worldTop, out->worldW, out->worldH);
}

/// 整数片元 (tx,ty) 在当前 Z 下于球面 Web 墨卡托（与常见 XYZ 纹理一致的 EPSG:3857 米制范围）中的轴对齐外包。
/// ty=0 为北侧；X 向东、Y 向北，与 EPSG:3857 一致。
static void SlippyTileIndexToEpsg3857BoundsMeters(int z, int tx, int ty, double* minXM, double* maxXM, double* minYM,
                                                double* maxYM) {
  if (!minXM || !maxXM || !minYM || !maxYM || z < 0 || z > 30) {
    return;
  }
  constexpr double kOriginM = 20037508.342789244;
  const double n = static_cast<double>(1u << static_cast<unsigned>(z));
  const double w = 2.0 * kOriginM;
  const double res = w / n;
  *minXM = static_cast<double>(tx) * res - kOriginM;
  *maxXM = static_cast<double>(tx + 1) * res - kOriginM;
  *maxYM = kOriginM - static_cast<double>(ty) * res;
  *minYM = kOriginM - static_cast<double>(ty + 1) * res;
}

static void TilePreviewUpdateSlippyPointerGeo(TilePreviewState* st, const RECT& img, const RECT& cr, POINT clientPt,
                                              wchar_t* lineBuf, size_t lineCap);

static void TilePreviewUpdatePointerGeo(TilePreviewState* st, HWND hwnd, POINT clientPt) {
  if (!st || !hwnd) {
    return;
  }
  st->lastPointerClient = clientPt;
  st->pointerOverImage = false;
  st->pointerGeoValid = false;
  RECT cr{};
  GetClientRect(hwnd, &cr);
  wchar_t line[512]{};

  if (st->mode == TilePreviewState::Mode::kSlippyQuadtree) {
    SlippyViewFrame vf{};
    TileSlippyComputeViewFrame(cr, st, &vf);
    if (!PtInRect(&vf.mapImg, clientPt)) {
      if (AgisGetUiLanguage() == AgisUiLanguage::kEn) {
        swprintf_s(line, L"Client (%d, %d) | Move over the map to see lon/lat", clientPt.x, clientPt.y);
      } else {
        swprintf_s(line, L"客户区 (%d, %d) | 将鼠标移入地图绘制区查看经纬度", clientPt.x, clientPt.y);
      }
      st->tilePointerStatusLine = line;
      return;
    }
    st->pointerOverImage = true;
    TilePreviewUpdateSlippyPointerGeo(st, vf.mapImg, cr, clientPt, line, std::size(line));
    st->tilePointerStatusLine = line;
    return;
  }

  if (st->mode == TilePreviewState::Mode::kSingleRaster && st->bmp && st->bmp->GetLastStatus() == Gdiplus::Ok) {
    const RECT imgArea = TileSlippyImageArea(cr, st);
    RECT dest{};
    if (TilePreviewComputeRasterDestRect(st, imgArea, &dest) && PtInRect(&dest, clientPt)) {
      st->pointerOverImage = true;
      const int iw = st->bmp->GetWidth();
      const int ih = st->bmp->GetHeight();
      const int dw = (std::max)(1, static_cast<int>(dest.right - dest.left));
      const int dh = (std::max)(1, static_cast<int>(dest.bottom - dest.top));
      const double u = (static_cast<double>(clientPt.x - dest.left) + 0.5) / static_cast<double>(dw);
      const double v = (static_cast<double>(clientPt.y - dest.top) + 0.5) / static_cast<double>(dh);
      const int px = (std::clamp)(static_cast<int>(u * static_cast<double>(iw - 1)), 0, iw - 1);
      const int py = (std::clamp)(static_cast<int>(v * static_cast<double>(ih - 1)), 0, ih - 1);
      if (AgisGetUiLanguage() == AgisUiLanguage::kEn) {
        swprintf_s(line, L"Client (%d, %d) | image px (%d, %d) | source %d×%d (no georef)", clientPt.x, clientPt.y, px, py,
                   iw, ih);
      } else {
        swprintf_s(line, L"客户区 (%d, %d) | 图像像素 (%d, %d) | 源图尺寸 %d×%d（无地理参照）", clientPt.x, clientPt.y, px,
                   py, iw, ih);
      }
    } else {
      if (AgisGetUiLanguage() == AgisUiLanguage::kEn) {
        swprintf_s(line, L"Client (%d, %d) | Move over the thumbnail for pixel coords", clientPt.x, clientPt.y);
      } else {
        swprintf_s(line, L"客户区 (%d, %d) | 将鼠标移入缩略图区域查看像素坐标", clientPt.x, clientPt.y);
      }
    }
    st->tilePointerStatusLine = line;
    return;
  }

  if (st->mode == TilePreviewState::Mode::kThreeDTilesMeta) {
    if (AgisGetUiLanguage() == AgisUiLanguage::kEn) {
      swprintf_s(line, L"Client (%d, %d) | 3D Tiles metadata: no flat-map coords (see BVH notes above)", clientPt.x,
                 clientPt.y);
    } else {
      swprintf_s(line, L"客户区 (%d, %d) | 3D Tiles 元数据模式：无平面地图坐标（见上方 BVH/层级说明）", clientPt.x,
                 clientPt.y);
    }
    st->tilePointerStatusLine = line;
    return;
  }

  if (AgisGetUiLanguage() == AgisUiLanguage::kEn) {
    swprintf_s(line, L"Client (%d, %d)", clientPt.x, clientPt.y);
  } else {
    swprintf_s(line, L"客户区 (%d, %d)", clientPt.x, clientPt.y);
  }
  st->tilePointerStatusLine = line;
}

static void TileFillRectSemi(HDC hdc, int x, int y, int w, int h, BYTE a, BYTE r, BYTE g, BYTE b) {
  if (w <= 0 || h <= 0) {
    return;
  }
  Gdiplus::Graphics gx(hdc);
  Gdiplus::SolidBrush br(Gdiplus::Color(a, r, g, b));
  gx.FillRectangle(&br, x, y, w, h);
}

static bool TilePreviewUiDark() { return AgisEffectiveUiDark(); }

/// 文字衬底（半透明矩形）在亮/暗主题下的 RGB。
static void TilePreviewTextBackdropRgb(BYTE* r, BYTE* g, BYTE* b) {
  if (TilePreviewUiDark()) {
    *r = 40;
    *g = 44;
    *b = 54;
  } else {
    *r = 251;
    *g = 253;
    *b = 255;
  }
}

/// 在半透明衬底上绘制多行文字（GDI），避免与底图混在一起。
static void TileDrawTextBlockSemiBg(HDC hdc, const wchar_t* text, RECT boxRc, int pad, BYTE bgAlpha) {
  if (!text) {
    return;
  }
  RECT measureRc = boxRc;
  DrawTextW(hdc, text, -1, &measureRc, DT_CALCRECT | DT_LEFT | DT_TOP | DT_WORDBREAK | DT_NOPREFIX);
  RECT bgRc = measureRc;
  InflateRect(&bgRc, pad, pad);
  if (bgRc.right > boxRc.right) {
    bgRc.right = boxRc.right;
  }
  if (bgRc.bottom > boxRc.bottom) {
    bgRc.bottom = boxRc.bottom;
  }
  BYTE br = 0, bg = 0, bb = 0;
  TilePreviewTextBackdropRgb(&br, &bg, &bb);
  TileFillRectSemi(hdc, bgRc.left, bgRc.top, bgRc.right - bgRc.left, bgRc.bottom - bgRc.top, bgAlpha, br, bg, bb);
  RECT drawRc = measureRc;
  if (drawRc.right > boxRc.right) {
    drawRc.right = boxRc.right;
  }
  if (drawRc.bottom > boxRc.bottom) {
    drawRc.bottom = boxRc.bottom;
  }
  DrawTextW(hdc, text, -1, &drawRc, DT_LEFT | DT_TOP | DT_WORDBREAK | DT_NOPREFIX);
}

static void TileDrawTextLineSemiBg(HDC hdc, const wchar_t* text, RECT lineRc, int padH, int padV, BYTE bgAlpha) {
  if (!text) {
    return;
  }
  RECT tr{};
  tr.left = lineRc.left;
  tr.top = lineRc.top;
  tr.right = lineRc.right;
  tr.bottom = lineRc.bottom;
  DrawTextW(hdc, text, -1, &tr, DT_CALCRECT | DT_LEFT | DT_TOP | DT_SINGLELINE | DT_NOPREFIX);
  InflateRect(&tr, padH, padV);
  BYTE lr = 0, lg = 0, lb = 0;
  TilePreviewTextBackdropRgb(&lr, &lg, &lb);
  TileFillRectSemi(hdc, tr.left, tr.top, tr.right - tr.left, tr.bottom - tr.top, bgAlpha, lr, lg, lb);
  DrawTextW(hdc, text, -1, &tr, DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX);
}

static void TilePruneBitmapCacheNotAtZ(TilePreviewState* st, int z) {
  if (!st) {
    return;
  }
  for (auto it = st->tileTexCache.begin(); it != st->tileTexCache.end();) {
    const uint64_t k = it->first;
    const int kz = static_cast<int>(static_cast<unsigned char>(k >> 56));
    if (kz != z) {
      st->tileTexLru.erase(it->second.first);
      it = st->tileTexCache.erase(it);
    } else {
      ++it;
    }
  }
}

static Gdiplus::Bitmap* TileBitmapCacheGet(TilePreviewState* st, uint64_t key, const std::wstring& tilePath) {
  if (!st) {
    return nullptr;
  }
  auto it = st->tileTexCache.find(key);
  if (it != st->tileTexCache.end()) {
    st->tileTexLru.erase(it->second.first);
    st->tileTexLru.push_back(key);
    it->second.first = std::prev(st->tileTexLru.end());
    Gdiplus::Bitmap* bm = it->second.second.get();
    if (bm && bm->GetLastStatus() == Gdiplus::Ok) {
      return bm;
    }
    st->tileTexLru.pop_back();
    st->tileTexCache.erase(it);
  }
  auto loaded = std::make_unique<Gdiplus::Bitmap>(tilePath.c_str());
  if (!loaded || loaded->GetLastStatus() != Gdiplus::Ok) {
    return nullptr;
  }
  Gdiplus::Bitmap* raw = loaded.get();
  st->tileTexLru.push_back(key);
  auto lit = std::prev(st->tileTexLru.end());
  st->tileTexCache[key] = std::make_pair(lit, std::move(loaded));
  while (st->tileTexCache.size() > kTilePreviewBitmapCacheMax) {
    const uint64_t old = st->tileTexLru.front();
    st->tileTexLru.pop_front();
    st->tileTexCache.erase(old);
  }
  return raw;
}

/// 仅查找已缓存位图，不调整 LRU（供 HUD 读尺寸，避免与瓦片绘制 pass 重复 Touch 同键）。
static Gdiplus::Bitmap* TileBitmapCachePeek(const TilePreviewState* st, uint64_t key) {
  if (!st) {
    return nullptr;
  }
  auto it = st->tileTexCache.find(key);
  if (it == st->tileTexCache.end()) {
    return nullptr;
  }
  Gdiplus::Bitmap* bm = it->second.second.get();
  if (bm && bm->GetLastStatus() == Gdiplus::Ok) {
    return bm;
  }
  return nullptr;
}

/// 以焦点（地图坐标归一化 nu,nv 保持不变）对齐切换缩放级，类似在线地图滚轮缩放。
static void TileZoomSlippy(TilePreviewState* st, RECT clientCr, const POINT* cursorClient, int dz) {
  if (!st || dz == 0) {
    return;
  }
  int newZ = st->viewZ + dz;
  newZ = (std::clamp)(newZ, 0, (std::max)(0, st->indexMaxZ));
  if (newZ == st->viewZ) {
    return;
  }
  SlippyViewFrame vf{};
  TileSlippyComputeViewFrame(clientCr, st, &vf);
  const RECT& map = vf.mapImg;
  const int iw = (std::max)(1, static_cast<int>(map.right - map.left));
  const int ih = (std::max)(1, static_cast<int>(map.bottom - map.top));
  POINT focusPt{};
  if (cursorClient && PtInRect(&map, *cursorClient)) {
    focusPt = *cursorClient;
  } else {
    focusPt.x = map.left + iw / 2;
    focusPt.y = map.top + ih / 2;
  }
  const double worldW = vf.worldW;
  const double worldH = vf.worldH;
  const double worldLeft = vf.worldLeft;
  const double worldTop = vf.worldTop;
  const int dim0 = 1 << st->viewZ;
  const double focX = static_cast<double>(focusPt.x - map.left);
  const double focY = static_cast<double>(focusPt.y - map.top);
  const double tileXF = worldLeft + focX * worldW / static_cast<double>(iw);
  const double tileYF = worldTop + focY * worldH / static_cast<double>(ih);
  double nu = tileXF / static_cast<double>((std::max)(1, dim0));
  double nv = tileYF / static_cast<double>((std::max)(1, dim0));
  nu = (std::clamp)(nu, 0.0, 1.0);
  nv = (std::clamp)(nv, 0.0, 1.0);
  st->viewZ = newZ;
  const int dim1 = 1 << newZ;
  st->centerTx = nu * static_cast<double>(dim1) + worldW * 0.5 - focX * worldW / static_cast<double>(iw);
  st->centerTy = nv * static_cast<double>(dim1) + worldH * 0.5 - focY * worldH / static_cast<double>(ih);
  const double lim = static_cast<double>(1 << newZ) + 4.0;
  st->centerTx = (std::clamp)(st->centerTx, -2.0, lim);
  st->centerTy = (std::clamp)(st->centerTy, -2.0, lim);
  TilePruneBitmapCacheNotAtZ(st, newZ);
}

static RECT TileOpenButtonRect(RECT cr) {
  return RECT{cr.left + 12, cr.top + 10, cr.left + 92, cr.top + 34};
}

/// 紧挨「Open…」右侧。
static RECT TileCloseButtonRect(RECT cr) {
  const RECT o = TileOpenButtonRect(cr);
  constexpr int kW = 68;
  return RECT{o.right + 4, o.top, o.right + 4 + kW, o.bottom};
}

static RECT TileOptionsButtonRect(RECT cr) {
  const RECT c = TileCloseButtonRect(cr);
  constexpr int kW = 86;
  return RECT{c.right + 4, c.top, c.right + 4 + kW, c.bottom};
}

/// 已成功加载可导出场景 JSON 的内容（Slippy 已索引、单图已解码、或 3D Tiles 元数据路径有效）。
static bool TilePreviewHasLoadedPreviewContent(const TilePreviewState* st) {
  if (!st) {
    return false;
  }
  switch (st->mode) {
  case TilePreviewState::Mode::kSlippyQuadtree:
    return st->tileCount > 0 && !st->slippyPaths.empty();
  case TilePreviewState::Mode::kSingleRaster:
    return st->bmp && st->bmp->GetLastStatus() == Gdiplus::Ok;
  case TilePreviewState::Mode::kThreeDTilesMeta:
    return !st->rootPath.empty();
  default:
    return false;
  }
}

/// 「复制场景信息」：Slippy 模式下在「选项」右侧；否则在「关闭」右侧。仅在有已加载内容时显示。
static RECT TileCopySceneInfoButtonRect(RECT cr, const TilePreviewState* st) {
  constexpr int kCopyW = 152;
  int left = 0;
  if (st && st->mode == TilePreviewState::Mode::kSlippyQuadtree) {
    const RECT opt = TileOptionsButtonRect(cr);
    left = opt.right + 4;
  } else {
    const RECT cls = TileCloseButtonRect(cr);
    left = cls.right + 4;
  }
  return RECT{left, cr.top + 10, left + kCopyW, cr.top + 34};
}

static RECT TileSlippyOptionsPanelRect(RECT cr) {
  constexpr int kPanelW = 300;
  constexpr int kPanelH = 302;
  constexpr int kMargin = 12;
  const int top = cr.top + 42;
  return {cr.right - kPanelW - kMargin, top, cr.right - kMargin, top + kPanelH};
}

static void TilePaintSlippyOptionsPanel(HDC hdc, RECT cr, TilePreviewState* st) {
  if (!st || !st->showOptionsPanel || st->mode != TilePreviewState::Mode::kSlippyQuadtree) {
    return;
  }
  const RECT prc = TileSlippyOptionsPanelRect(cr);
  const bool dark = TilePreviewUiDark();
  if (dark) {
    TileFillRectSemi(hdc, prc.left, prc.top, prc.right - prc.left, prc.bottom - prc.top, 250, 34, 38, 48);
  } else {
    TileFillRectSemi(hdc, prc.left, prc.top, prc.right - prc.left, prc.bottom - prc.top, 250, 252, 254, 255);
  }
  HBRUSH frameBr = CreateSolidBrush(dark ? RGB(72, 80, 98) : RGB(186, 198, 218));
  FrameRect(hdc, &prc, frameBr);
  DeleteObject(frameBr);
  if (HFONT f = UiGetAppFont()) {
    SelectObject(hdc, f);
  }
  SetBkMode(hdc, TRANSPARENT);
  SetTextColor(hdc, dark ? RGB(230, 234, 242) : RGB(28, 36, 52));
  RECT titleRc{prc.left + 10, prc.top + 6, prc.right - 10, prc.top + 22};
  DrawTextW(hdc,
            AgisPickUiLang(L"输入源投影 → 算法 → 显示 → 屏幕",
                           L"Source projection → algorithm → display → screen"),
            -1, &titleRc, DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX);
  RECT ioRc{prc.left + 10, prc.top + 22, prc.right - 10, prc.top + 102};
  DrawTextW(hdc,
#if GIS_DESKTOP_HAVE_GDAL
            AgisPickUiLang(
                L"① 输入源投影：标准 XYZ 纹理 = EPSG:3857（与 z/x/y 一致）\n"
                L"② 算法：重投影/重采样（PROJ 等）把「显示几何上的点」变到 ① 再取纹素；代码里对每个像素：屏→显示→3857→纹理\n"
                L"③ 显示：下列为显示用投影/几何；④ 合成到窗口像素。「XYZ 片元线性」下显示与源一致，② 无额外 CRS 步",
                L"① Source: standard XYZ texture = EPSG:3857 (matches z/x/y)\n"
                L"② Algorithm: reproject/resample (PROJ) maps display-geometry points to ① then samples texels; per pixel: "
                L"screen→display→3857→texture\n"
                L"③ Display: options below; ④ composite to pixels. Under XYZ tile-linear, display matches source; ② adds "
                L"no extra CRS step"),
#else
            AgisPickUiLang(L"① 输入源：EPSG:3857 标准 XYZ 纹理\n"
                           L"②④ 当前无 GDAL/PROJ：非 XYZ 为近似几何；请用带 GDAL 的构建做严格 源→显示 重采样。",
                           L"① Source: EPSG:3857 XYZ texture\n"
                           L"②④ No GDAL/PROJ: non-XYZ is approximate; use a GDAL build for strict source→display "
                           L"resampling."),
#endif
            -1, &ioRc, DT_LEFT | DT_TOP | DT_WORDBREAK | DT_NOPREFIX);
  constexpr int kProjRow0 = 110;
  for (int i = 0; i < kTileSlippyProjectionCount; ++i) {
    wchar_t line[200]{};
    const bool sel = static_cast<int>(st->slippyProjection) == i;
    swprintf_s(line, L"%s  %s", sel ? L"●" : L"○", TileSlippyProjectionLongLabel(static_cast<TileSlippyProjection>(i)));
    RECT rowRc{prc.left + 10, prc.top + kProjRow0 + i * 24, prc.right - 8, prc.top + kProjRow0 + i * 24 + 22};
    DrawTextW(hdc, line, -1, &rowRc, DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX);
  }
  RECT secRc{prc.left + 10, prc.top + 188, prc.right - 8, prc.top + 206};
  DrawTextW(hdc, AgisPickUiLang(L"界面元素", L"UI elements"), -1, &secRc, DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX);
  auto drawCheckRow = [&](int y, bool on, const wchar_t* label) {
    const RECT box{prc.left + 14, y, prc.left + 28, y + 14};
    Rectangle(hdc, box.left, box.top, box.right, box.bottom);
    if (on) {
      RECT tick = box;
      DrawTextW(hdc, L"√", -1, &tick, DT_CENTER | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX);
    }
    RECT tx{prc.left + 32, y - 2, prc.right - 10, y + 18};
    DrawTextW(hdc, label, -1, &tx, DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX);
  };
  drawCheckRow(prc.top + 210, st->uiShowTopHint,
               AgisPickUiLang(L"顶栏说明（快捷提示）", L"Top bar hints"));
  drawCheckRow(prc.top + 234, st->uiShowSlippyMapHud,
               AgisPickUiLang(L"地图内 HUD（层级与操作说明）", L"In-map HUD (Z and controls)"));
  drawCheckRow(prc.top + 258, st->uiShowTileGrid, AgisPickUiLang(L"瓦片网格线", L"Tile grid lines"));
  drawCheckRow(prc.top + 282, st->uiShowTileLabels,
               AgisPickUiLang(L"瓦片标注（z/x/y 与像素尺寸）", L"Tile labels (z/x/y and pixel size)"));
}

/// @return true 若点击落在面板内并已消费（含空白区域），调用方勿启动地图拖拽。
static bool TileSlippyOptionsPanelHandleClick(HWND hwnd, TilePreviewState* st, RECT cr, POINT pt) {
  if (!st || !st->showOptionsPanel || st->mode != TilePreviewState::Mode::kSlippyQuadtree) {
    return false;
  }
  const RECT prc = TileSlippyOptionsPanelRect(cr);
  if (!PtInRect(&prc, pt)) {
    return false;
  }
  const int relY = pt.y - prc.top;
  const int relX = pt.x - prc.left;
  bool changed = false;
  constexpr int kHitProjRow0 = 110;
  if (relY >= kHitProjRow0 && relY < kHitProjRow0 + 24 * kTileSlippyProjectionCount && relX >= 8) {
    const int i = (relY - kHitProjRow0) / 24;
    if (i >= 0 && i < kTileSlippyProjectionCount) {
      st->slippyProjection = static_cast<TileSlippyProjection>(i);
      changed = true;
    }
  } else if (relX >= 10 && relX < 280) {
    auto toggleRow = [&](int y0, bool* flag) {
      if (pt.y >= prc.top + y0 && pt.y < prc.top + y0 + 20) {
        *flag = !*flag;
        changed = true;
      }
    };
    toggleRow(210, &st->uiShowTopHint);
    toggleRow(234, &st->uiShowSlippyMapHud);
    toggleRow(258, &st->uiShowTileGrid);
    toggleRow(282, &st->uiShowTileLabels);
  }
  if (changed) {
    InvalidateRect(hwnd, nullptr, FALSE);
  }
  return true;
}

struct TilePreviewLoadReport {
  bool ok = true;
  std::wstring errorTitle;
  std::wstring errorText;
};

static void TilePreviewShowLoadError(HWND owner, const TilePreviewLoadReport& rep) {
  if (rep.ok || rep.errorText.empty()) {
    return;
  }
  const wchar_t* cap =
      rep.errorTitle.empty() ? AgisPickUiLang(L"瓦片预览", L"Tile preview") : rep.errorTitle.c_str();
  MessageBoxW(owner, rep.errorText.c_str(), cap, MB_OK | MB_ICONWARNING);
}

/// Slippy 顶栏一行；完整打开方式与支持格式见产品文档 / README，不再占用主视图顶栏。
static std::wstring TilePreviewSlippyTopLineW() {
  return std::wstring(AgisPickUiLang(
      L"本地路径：Open… 或拖入瓦片根目录/文件（无网络服务；标准 XYZ 纹理为 Web 墨卡托 EPSG:3857，「选项」可调显示投影）。滚轮：Z；Shift+滚轮：视口中心；Ctrl+滚轮：片元像素；拖拽平移。",
      L"Local path: Open… or drop a tile root folder/file (no network; standard XYZ textures are Web Mercator EPSG:3857; "
      L"Options changes display projection). Wheel: Z; Shift+wheel: center; Ctrl+wheel: tile pixel size; drag to pan."));
}

static void TilePreviewLoadFromPath(HWND hwnd, TilePreviewState* st, const std::wstring& path, TilePreviewLoadReport* report) {
  if (!st) {
    return;
  }
  const TileSlippyProjection saveProj = st->slippyProjection;
  const bool savePanel = st->showOptionsPanel;
  const bool saveTopHint = st->uiShowTopHint;
  const bool saveMapHud = st->uiShowSlippyMapHud;
  const bool saveGrid = st->uiShowTileGrid;
  const bool saveLabels = st->uiShowTileLabels;
  const auto markOk = [&]() {
    if (report) {
      report->ok = true;
      report->errorTitle.clear();
      report->errorText.clear();
    }
  };
  const auto markFail = [&](const wchar_t* title, std::wstring text) {
    st->hint = std::move(text);
    if (report) {
      report->ok = false;
      report->errorTitle = title;
      report->errorText = st->hint;
    }
    InvalidateRect(hwnd, nullptr, FALSE);
  };

  st->rootPath = path;
  st->samplePath.clear();
  st->hint.clear();
  st->bvHint.clear();
  st->bmp.reset();
  st->mode = TilePreviewState::Mode::kSingleRaster;
  st->slippyPaths.clear();
  st->tileTexLru.clear();
  st->tileTexCache.clear();
  st->slippyDiskTmsYIndex = false;
  st->indexMaxZ = 0;
  st->tileCount = 0;
  st->viewZ = 0;
  st->centerTx = 0.5;
  st->centerTy = 0.5;
  st->pixelsPerTile = 256.f;
  st->dragging = false;
  st->lastPointerClient = {};
  st->pointerOverImage = false;
  st->pointerGeoValid = false;
  st->tilePointerStatusLine = AgisPickUiLang(L"移动鼠标查看坐标", L"Move the pointer to see coordinates");
  st->tileMouseTrackingLeave = false;
  st->lastPointerQuantX = INT_MIN;
  st->lastPointerQuantY = INT_MIN;
  st->slippyProjection = saveProj;
  st->showOptionsPanel = savePanel;
  st->uiShowTopHint = saveTopHint;
  st->uiShowSlippyMapHud = saveMapHud;
  st->uiShowTileGrid = saveGrid;
  st->uiShowTileLabels = saveLabels;

  if (st->rootPath.empty()) {
    st->hint = AgisPickUiLang(
        L"请使用「Open…」、拖放或命令行传入本机路径：XYZ/TMS 目录、单张栅格、本地 tileset.json（3D Tiles 元数据）、或 .mbtiles/.gpkg（需 GDAL）。不支持 http(s)/WMTS 等网络地址。",
        L"Use Open…, drag-drop, or a command-line path: XYZ/TMS folder, single raster, local tileset.json (3D Tiles "
        L"metadata), or .mbtiles/.gpkg (needs GDAL). http(s)/WMTS and other network URLs are not supported.");
    InvalidateRect(hwnd, nullptr, FALSE);
    markOk();
    return;
  }

  std::error_code ecPath;
  const std::filesystem::path rootFs(st->rootPath);
#if GIS_DESKTOP_HAVE_GDAL
  if (std::filesystem::is_regular_file(rootFs, ecPath)) {
    const std::wstring ext = rootFs.extension().wstring();
    if (_wcsicmp(ext.c_str(), L".mbtiles") == 0 || _wcsicmp(ext.c_str(), L".gpkg") == 0) {
      std::wstring gdalDiag;
      if (auto bm = TryLoadGdalRasterTileContainerPreview(st->rootPath, &gdalDiag)) {
        st->bmp = std::move(bm);
        st->mode = TilePreviewState::Mode::kSingleRaster;
        std::wostringstream hs;
        hs << AgisPickUiLang(L"【MBTiles / GeoPackage】GDAL 栅格缩略预览（全球拼图下采样至 <=2048px）。\n路径：\n",
                             L"[MBTiles / GeoPackage] GDAL raster thumbnail (mosaic downsampled to ≤2048 px).\nPath:\n")
           << st->rootPath;
        if (!gdalDiag.empty()) {
          hs << L"\n" << gdalDiag;
        }
        st->hint = hs.str();
        InvalidateRect(hwnd, nullptr, FALSE);
        markOk();
        return;
      }
      markFail(AgisPickUiLang(L"MBTiles / GeoPackage 预览失败", L"MBTiles / GeoPackage preview failed"),
               std::wstring(AgisPickUiLang(L"【MBTiles / GeoPackage】GDAL 预览失败：\n",
                                           L"[MBTiles / GeoPackage] GDAL preview failed:\n")) +
                   gdalDiag +
                   std::wstring(AgisPickUiLang(L"\n请检查 GDAL/PROJ/gdal_data 配置，或导出 XYZ 目录后再预览。",
                                               L"\nCheck GDAL/PROJ/gdal_data, or export an XYZ folder and preview again.")));
      return;
    }
  }
#else
  if (std::filesystem::is_regular_file(rootFs, ecPath)) {
    const std::wstring ext = rootFs.extension().wstring();
    if (_wcsicmp(ext.c_str(), L".mbtiles") == 0 || _wcsicmp(ext.c_str(), L".gpkg") == 0) {
      markFail(AgisPickUiLang(L"构建未启用 GDAL", L"Build without GDAL"),
               AgisPickUiLang(L"当前构建未启用 GDAL，无法预览 .mbtiles / .gpkg。\n请使用带 GDAL 的构建，或改为打开 XYZ 瓦片目录 / 单张栅格图。",
                              L"This build has no GDAL; cannot preview .mbtiles / .gpkg.\nUse a GDAL-enabled build, or open "
                              L"an XYZ tile folder / a single raster image."));
      return;
    }
  }
#endif

  const std::optional<std::wstring> tilesetPathOpt = FindTilesetJsonPath(st->rootPath);
  std::wstring bvForTileset;
  if (tilesetPathOpt.has_value()) {
    bvForTileset = RoughTilesetBvHintForFile(*tilesetPathOpt);
  }
  st->bvHint = bvForTileset;
  bool tmsFlip = false;
  if (std::filesystem::is_directory(rootFs, ecPath)) {
    tmsFlip = DetectTmsTileLayoutOnDisk(rootFs);
  }
  st->tileCount = IndexSlippyQuadtree(st->rootPath, tmsFlip, &st->slippyPaths, &st->indexMaxZ);
  if (st->tileCount >= 1) {
    st->slippyDiskTmsYIndex = tmsFlip;
    st->mode = TilePreviewState::Mode::kSlippyQuadtree;
    st->viewZ = (std::min)(st->indexMaxZ, 6);
    const double sz = double(1u << st->viewZ);
    st->centerTx = sz * 0.5;
    st->centerTy = sz * 0.5;
    std::wostringstream hs;
    if (AgisGetUiLanguage() == AgisUiLanguage::kEn) {
      hs << L"[Slippy quadtree / XYZ] Indexed " << st->tileCount
         << L" tiles. Drag to pan, wheel changes Z, Ctrl+wheel changes tile pixel size.";
#if GIS_DESKTOP_HAVE_GDAL
      hs << L"\nWith GDAL: equirectangular / equal-area display uses PROJ (display CRS → EPSG:3857) per-pixel resampling.";
#endif
    } else {
      hs << L"【平面四叉树 / XYZ】已索引 " << st->tileCount
         << L" 个图块。拖拽平移，滚轮换级，Ctrl+滚轮改片元尺度。"
#if GIS_DESKTOP_HAVE_GDAL
         << L"\n启用 GDAL 时：等经纬 / 等积圆柱显示由 PROJ（显示 CRS→EPSG:3857）逐像素重采样。"
#endif
          ;
    }
    st->hint = hs.str();
    InvalidateRect(hwnd, nullptr, FALSE);
    markOk();
    return;
  }
  if (tilesetPathOpt.has_value()) {
    st->mode = TilePreviewState::Mode::kThreeDTilesMeta;
    st->hint = BuildThreeDTilesDashboard(st->rootPath, *tilesetPathOpt, bvForTileset);
    InvalidateRect(hwnd, nullptr, FALSE);
    markOk();
    return;
  }

  const TileFindResult found = FindSampleTileRaster(st->rootPath);
  if (found.code == TileSampleResult::kContainerUnsupported) {
    markFail(AgisPickUiLang(L"不支持的容器", L"Unsupported container"),
             AgisPickUiLang(L"单文件 MBTiles / GeoPackage 不能直接解码为交互瓦片。\n请系统默认打开或导出 XYZ 后预览。",
                            L"Single-file MBTiles / GeoPackage cannot be decoded as interactive tiles here.\nOpen with the "
                            L"system default or export XYZ, then preview."));
    return;
  }
  if (found.code == TileSampleResult::kOk && !found.path.empty()) {
    st->samplePath = found.path;
    auto loaded = std::make_unique<Gdiplus::Bitmap>(st->samplePath.c_str());
    if (loaded && loaded->GetLastStatus() == Gdiplus::Ok) {
      st->bmp = std::move(loaded);
      st->hint = std::wstring(AgisPickUiLang(L"单图采样预览：\n", L"Single-image sample preview:\n")) + st->samplePath;
      InvalidateRect(hwnd, nullptr, FALSE);
      markOk();
      return;
    }
    markFail(AgisPickUiLang(L"栅格加载失败", L"Raster load failed"),
             std::wstring(AgisPickUiLang(L"无法加载栅格文件（GDI+ 解码失败）：\n",
                                         L"Could not load raster (GDI+ decode failed):\n")) +
                 st->samplePath);
    return;
  }

  markFail(AgisPickUiLang(L"无法预览", L"Nothing to preview"),
           AgisPickUiLang(L"未找到可预览的本地内容：\n"
                          L"· 目录下无 z/x/y（或 TMS）瓦片结构；且\n"
                          L"· 无本地 tileset.json / 有效 .json；且\n"
                          L"· 未找到可读的 PNG/JPG/WebP/BMP/TIF 栅格。\n"
                          L"本窗口仅支持磁盘路径，不支持网络 URL。请确认路径或通过「Open…」按类型重新选择。",
                          L"No local previewable content:\n"
                          L"· No z/x/y (or TMS) tile layout under the folder; and\n"
                          L"· No local tileset.json / valid .json; and\n"
                          L"· No readable PNG/JPG/WebP/BMP/TIF raster.\n"
                          L"This window only accepts disk paths, not network URLs. Check the path or pick a type via Open…."));
}

/// Slippy 绘制布局（瓦片层与 HUD 层共用；分阶段绘制以保证 Z 序：瓦片 → 顶栏 UI → 地图 HUD → 底栏）。
struct TileSlippyPaintLayout {
  /// 地图绘制与指针命中区（等经纬/等积圆柱下可能为 fullImgArea 内 letterbox 子矩形）。
  RECT imgArea{};
  /// 与 `TileSlippyImageArea` 一致的外层客户区；条带区无瓦片绘制，仅衬底。
  RECT fullImgArea{};
  int aw = 1;
  int ah = 1;
  double worldW = 1.0;
  double worldH = 1.0;
  double worldLeft = 0.0;
  double worldTop = 0.0;
  int dim = 0;
  int tx0 = 0;
  int ty0 = 0;
  int tx1 = 0;
  int ty1 = 0;
  int spanX = 0;
  int spanY = 0;
  bool tooMany = false;
  bool valid = false;
  /// 视口四角（片元浮点矩形）对应的 WGS84，顺序 TL, TR, BL, BR；供等经纬 / 等积圆柱与指针反算。
  double cornerLon[4]{};
  double cornerLat[4]{};
  double viewCenterLon = 0.0;
  double viewCenterLat = 0.0;
  double imgCenterX = 0.0;
  double imgCenterY = 0.0;
  /// 每度经度、纬度在屏幕上的近似像素（JSON/状态）；等经纬下横纵尺度通常不等——经向来自片元矩形线性跨度，纬向来自四角纬度包络（墨卡托非线性）。
  double ppdLon = 0.0;
  double ppdLat = 0.0;
  /// 兼容字段：片元线性模式下与 `ppdLon` 相同；等积圆柱经向尺度；供 JSON `pixelsPerDegreeLonApprox` 等。
  double ppdEq = 0.0;
  /// 等积圆柱：R_pix = ppdEq * 180/π，用于 (sin φ - sin φ₀) 项。
  double rPixCea = 0.0;
  /// 显示平面四角在「显示 CRS」中的坐标（等经纬=WGS84°；等积圆柱=CEA 米）；GDAL 构建下由 PROJ 填写。
  double dispCornerX[4]{};
  double dispCornerY[4]{};
};

static void SlippyPixelToBilinearInQuad(const RECT& img, int px, int py, const double vx[4], const double vy[4],
                                        double* ox, double* oy) {
  if (!ox || !oy) {
    return;
  }
  const int imgW = static_cast<int>(img.right - img.left);
  const int imgH = static_cast<int>(img.bottom - img.top);
  const double aw = static_cast<double>((std::max)(1, imgW));
  const double ah = static_cast<double>((std::max)(1, imgH));
  double u = (static_cast<double>(px - img.left) + 0.5) / aw;
  double v = (static_cast<double>(py - img.top) + 0.5) / ah;
  u = (std::clamp)(u, 0.0, 1.0);
  v = (std::clamp)(v, 0.0, 1.0);
  const double tx = (1.0 - u) * vx[0] + u * vx[1];
  const double ty = (1.0 - u) * vy[0] + u * vy[1];
  const double bx = (1.0 - u) * vx[2] + u * vx[3];
  const double by = (1.0 - u) * vy[2] + u * vy[3];
  *ox = (1.0 - v) * tx + v * bx;
  *oy = (1.0 - v) * ty + v * by;
}

/// 与 `TileSlippyBuildPaintLayout` 一致：地图客户区与视口 `worldLeft`/`worldTop`/`worldW`/`worldH`（片元浮点坐标）线性对应。
/// 用于整视口 PROJ 重采样与指针反算，避免对四角经纬度双线性插值与 `SlippyTileXYToLonLat` 非线性不一致（相对瓦片绘制区错位）。
static void SlippyPixelCenterToFractionalTile(const RECT& img, int px, int py, const TileSlippyPaintLayout& lay,
                                              double* outTileX, double* outTileY) {
  if (!outTileX || !outTileY) {
    return;
  }
  const int imgW = static_cast<int>(img.right - img.left);
  const int imgH = static_cast<int>(img.bottom - img.top);
  const double aw = static_cast<double>((std::max)(1, imgW));
  const double ah = static_cast<double>((std::max)(1, imgH));
  double u = (static_cast<double>(px - img.left) + 0.5) / aw;
  double v = (static_cast<double>(py - img.top) + 0.5) / ah;
  u = (std::clamp)(u, 0.0, 1.0);
  v = (std::clamp)(v, 0.0, 1.0);
  *outTileX = lay.worldLeft + u * lay.worldW;
  *outTileY = lay.worldTop + v * lay.worldH;
}

/// 与 `SlippyPixelCenterToFractionalTile` 互逆：片元浮点坐标 → 客户区像素中心（双精度）。
/// @param clampUvToMap true 时将 u,v 限制在 [0,1]（指针/拼贴命中与旧行为一致）；false 时线性外推，供瓦片四角外包与 `imageArea` 求交，避免靠边瓦片橙框因角点被夹到边界而压扁。
static void SlippyFractionalTileToClientPixelCenterEx(const TileSlippyPaintLayout& lay, double tileX, double tileY,
                                                      double* outCx, double* outCy, bool clampUvToMap) {
  if (!outCx || !outCy) {
    return;
  }
  const RECT& img = lay.imgArea;
  const double aw = static_cast<double>((std::max)(1, lay.aw));
  const double ah = static_cast<double>((std::max)(1, lay.ah));
  double u = (tileX - lay.worldLeft) / lay.worldW;
  double v = (tileY - lay.worldTop) / lay.worldH;
  if (clampUvToMap) {
    u = (std::clamp)(u, 0.0, 1.0);
    v = (std::clamp)(v, 0.0, 1.0);
  }
  *outCx = static_cast<double>(img.left) + u * aw;
  *outCy = static_cast<double>(img.top) + v * ah;
}

/// 指针读数等：u,v 夹到 [0,1]，与 `SlippyPixelCenterToFractionalTile` 互逆。
static void SlippyFractionalTileToClientPixelCenter(const TileSlippyPaintLayout& lay, double tileX, double tileY,
                                                    double* outCx, double* outCy) {
  SlippyFractionalTileToClientPixelCenterEx(lay, tileX, tileY, outCx, outCy, true);
}

/// 片元 X 可小于 0 或大于 dim（平移视口），反算经度会落在 [-180,180] 之外；折叠到主经度范围。
static double SlippyWrapLongitudeDeg(double lonDeg) {
  double x = std::fmod(lonDeg + 180.0, 360.0);
  if (x < 0.0) {
    x += 360.0;
  }
  return x - 180.0;
}

/// 与球面 Web 墨卡托瓦片常用裁剪纬度一致，避免 JSON 中出现不可地图化的极区纬度。
static double SlippyClampLatitudeDeg(double latDeg) {
  constexpr double kMax = 85.0511287798;
  return (std::clamp)(latDeg, -kMax, kMax);
}

/// 与 `SlippyLonLatToScreenCea` 互逆（横轴经度线性、纵轴 sinφ 线性）。
static void SlippyScreenToLonLatInverseCea(const TileSlippyPaintLayout& lay, double clientX, double clientY,
                                           double* lonDeg, double* latDeg) {
  if (!lonDeg || !latDeg) {
    return;
  }
  *lonDeg = lay.viewCenterLon + (clientX - lay.imgCenterX) / lay.ppdLon;
  const double cLatR = lay.viewCenterLat * kTileGeoPi / 180.0;
  const double s0 = std::sin(cLatR);
  const double sAt = s0 - (clientY - lay.imgCenterY) / lay.rPixCea;
  const double cl = (std::clamp)(sAt, -1.0, 1.0);
  *latDeg = std::asin(cl) * 180.0 / kTileGeoPi;
}

#if GIS_DESKTOP_HAVE_GDAL
static constexpr double kWebMercatorOriginShiftM = 20037508.342789244;

static PJ_CONTEXT* g_tileSlippyProjCtx = nullptr;
static PJ* g_tileSlippyPj4326To3857 = nullptr;
static PJ* g_tileSlippyPjCeaTo3857 = nullptr;
static PJ* g_tileSlippyPj4326ToCea = nullptr;
static PJ* g_tileSlippyPjCeaTo4326 = nullptr;
/** 瓦片预览 HWND 计数；最后一个窗口 WM_DESTROY 时释放 PROJ，避免多窗口共用时提前销毁。 */
static int g_tileSlippyPreviewOpenWndCount = 0;

static void TileSlippyProjDestroyForTilePreview() {
  if (g_tileSlippyPj4326To3857) {
    proj_destroy(g_tileSlippyPj4326To3857);
    g_tileSlippyPj4326To3857 = nullptr;
  }
  if (g_tileSlippyPjCeaTo3857) {
    proj_destroy(g_tileSlippyPjCeaTo3857);
    g_tileSlippyPjCeaTo3857 = nullptr;
  }
  if (g_tileSlippyPj4326ToCea) {
    proj_destroy(g_tileSlippyPj4326ToCea);
    g_tileSlippyPj4326ToCea = nullptr;
  }
  if (g_tileSlippyPjCeaTo4326) {
    proj_destroy(g_tileSlippyPjCeaTo4326);
    g_tileSlippyPjCeaTo4326 = nullptr;
  }
  if (g_tileSlippyProjCtx) {
    proj_context_destroy(g_tileSlippyProjCtx);
    g_tileSlippyProjCtx = nullptr;
  }
}

/// 供 `proj_context_set_search_paths` 使用，指针需在 context 存活期内有效。
static std::string g_tileSlippyProjSearchPathUtf8;

static void TileSlippyProjApplySearchPaths(PJ_CONTEXT* ctx) {
  if (!ctx) {
    return;
  }
  wchar_t modulePath[MAX_PATH]{};
  if (GetModuleFileNameW(nullptr, modulePath, MAX_PATH) == 0) {
    return;
  }
  const std::filesystem::path exeDir = std::filesystem::path(modulePath).parent_path();
  static constexpr const wchar_t* kSubdirs[] = {L"proj_data", L"share\\proj"};
  const std::filesystem::path markerDb = L"proj.db";
  std::filesystem::path probeDir = exeDir;
  for (int i = 0; i < 16; ++i) {
    for (const wchar_t* sub : kSubdirs) {
      const std::filesystem::path base = probeDir / sub;
      std::error_code ec;
      if (!std::filesystem::is_regular_file(base / markerDb, ec)) {
        continue;
      }
      std::filesystem::path dir = std::filesystem::weakly_canonical(base, ec);
      if (ec) {
        dir = base;
      }
      const std::wstring w = dir.wstring();
      const int n =
          WideCharToMultiByte(CP_UTF8, 0, w.c_str(), static_cast<int>(w.size()), nullptr, 0, nullptr, nullptr);
      if (n <= 0) {
        return;
      }
      g_tileSlippyProjSearchPathUtf8.assign(static_cast<size_t>(n), '\0');
      WideCharToMultiByte(CP_UTF8, 0, w.c_str(), static_cast<int>(w.size()), g_tileSlippyProjSearchPathUtf8.data(), n,
                          nullptr, nullptr);
      const char* paths[1] = {g_tileSlippyProjSearchPathUtf8.c_str()};
      proj_context_set_search_paths(ctx, 1, paths);
      return;
    }
    std::filesystem::path parent = probeDir.parent_path();
    if (parent == probeDir) {
      break;
    }
    probeDir = std::move(parent);
  }
}

static bool TileSlippyProjEnsureContext() {
  if (g_tileSlippyProjCtx) {
    return true;
  }
  AgisEnsureGdalDataPath();
  g_tileSlippyProjCtx = proj_context_create();
  if (!g_tileSlippyProjCtx) {
    return false;
  }
  TileSlippyProjApplySearchPaths(g_tileSlippyProjCtx);
  return true;
}

/// EPSG:4326 在 EPSG 库内轴序为「纬度、经度」；`proj_coord(lon,lat,...)` 为 GIS 惯用「经度、纬度」。
/// `proj_normalize_for_visualization` 得到接受 lon/lat 输入的变换（否则会把 λ 当 φ、φ 当 λ，整幅图呈 90° 量级错乱/纵向条带）。
static PJ* TileSlippyProjCreateLonLatNormalized(PJ_CONTEXT* ctx, const char* srcCrs, const char* dstCrs) {
  PJ* raw = proj_create_crs_to_crs(ctx, srcCrs, dstCrs, nullptr);
  if (!raw) {
    return nullptr;
  }
  PJ* norm = proj_normalize_for_visualization(ctx, raw);
  if (!norm) {
    return raw;
  }
  proj_destroy(raw);
  return norm;
}

static bool TileSlippyProjEnsure4326To3857() {
  if (g_tileSlippyPj4326To3857) {
    return true;
  }
  if (!TileSlippyProjEnsureContext()) {
    return false;
  }
  g_tileSlippyPj4326To3857 =
      TileSlippyProjCreateLonLatNormalized(g_tileSlippyProjCtx, "EPSG:4326", "EPSG:3857");
  return g_tileSlippyPj4326To3857 != nullptr;
}

static bool TileSlippyProjEnsureCeaPipelines() {
  if (g_tileSlippyPjCeaTo3857 && g_tileSlippyPj4326ToCea && g_tileSlippyPjCeaTo4326) {
    return true;
  }
  if (!TileSlippyProjEnsureContext()) {
    return false;
  }
  if (!g_tileSlippyPjCeaTo3857) {
    g_tileSlippyPjCeaTo3857 =
        proj_create_crs_to_crs(g_tileSlippyProjCtx, kTileSlippyCeaWgs84Proj, "EPSG:3857", nullptr);
  }
  if (!g_tileSlippyPj4326ToCea) {
    g_tileSlippyPj4326ToCea =
        TileSlippyProjCreateLonLatNormalized(g_tileSlippyProjCtx, "EPSG:4326", kTileSlippyCeaWgs84Proj);
  }
  if (!g_tileSlippyPjCeaTo4326) {
    g_tileSlippyPjCeaTo4326 =
        TileSlippyProjCreateLonLatNormalized(g_tileSlippyProjCtx, kTileSlippyCeaWgs84Proj, "EPSG:4326");
  }
  return g_tileSlippyPjCeaTo3857 && g_tileSlippyPj4326ToCea && g_tileSlippyPjCeaTo4326;
}

/// 当前「显示投影」下整视口 PROJ 重采样是否具备全部变换（与场景 JSON 一致）。
static bool TileSlippyProjLayoutPipelinesReady(TileSlippyProjection proj) {
  switch (proj) {
  case TileSlippyProjection::kWebMercatorGrid:
    return true;
  case TileSlippyProjection::kEquirectangular:
    return TileSlippyProjEnsure4326To3857();
  case TileSlippyProjection::kCylindricalEqualArea:
    return TileSlippyProjEnsureCeaPipelines();
  default:
    return false;
  }
}

static void TileSlippyFillDispCornersFromGeo(TilePreviewState* st, TileSlippyPaintLayout* lay) {
  if (!st || !lay || !lay->valid) {
    return;
  }
  if (st->slippyProjection == TileSlippyProjection::kWebMercatorGrid) {
    return;
  }
  if (st->slippyProjection == TileSlippyProjection::kEquirectangular) {
    for (int i = 0; i < 4; ++i) {
      lay->dispCornerX[i] = lay->cornerLon[i];
      lay->dispCornerY[i] = lay->cornerLat[i];
    }
    return;
  }
  if (!TileSlippyProjEnsureCeaPipelines()) {
    return;
  }
  for (int i = 0; i < 4; ++i) {
    PJ_COORD c = proj_coord(lay->cornerLon[i], lay->cornerLat[i], 0, 0);
    c = proj_trans(g_tileSlippyPj4326ToCea, PJ_FWD, c);
    lay->dispCornerX[i] = c.xyz.x;
    lay->dispCornerY[i] = c.xyz.y;
  }
}

static void Slippy3857MetersToFractionalTile(int z, double mx, double my, double* fx, double* fy) {
  const double n = static_cast<double>(1u << static_cast<unsigned>((std::max)(0, z)));
  const double w = 2.0 * kWebMercatorOriginShiftM;
  *fx = (mx + kWebMercatorOriginShiftM) / w * n;
  *fy = (kWebMercatorOriginShiftM - my) / w * n;
}

static bool SlippySampleTileBilinearUv(TilePreviewState* st, int z, int tx, int ty, double u, double v, BYTE* pr,
                                       BYTE* pg, BYTE* pb) {
  if (!st || !pr || !pg || !pb) {
    return false;
  }
  u = (std::clamp)(u, 0.0, 1.0);
  v = (std::clamp)(v, 0.0, 1.0);
  const int dim = 1 << z;
  if (tx < 0 || ty < 0 || tx >= dim || ty >= dim) {
    return false;
  }
  const uint64_t key = PackTileKey(z, tx, ty);
  auto pit = st->slippyPaths.find(key);
  if (pit == st->slippyPaths.end()) {
    return false;
  }
  Gdiplus::Bitmap* bm = TileBitmapCacheGet(st, key, pit->second);
  if (!bm || bm->GetLastStatus() != Gdiplus::Ok) {
    return false;
  }
  const int iw = bm->GetWidth();
  const int ih = bm->GetHeight();
  if (iw < 1 || ih < 1) {
    return false;
  }
  const double px = u * static_cast<double>(iw - 1);
  const double py = v * static_cast<double>(ih - 1);
  int x0 = static_cast<int>((std::floor)(px));
  int y0 = static_cast<int>((std::floor)(py));
  const int x1 = (std::min)(x0 + 1, iw - 1);
  const int y1 = (std::min)(y0 + 1, ih - 1);
  x0 = (std::clamp)(x0, 0, iw - 1);
  y0 = (std::clamp)(y0, 0, ih - 1);
  const double fu = px - (std::floor)(px);
  const double fv = py - (std::floor)(py);
  Gdiplus::Rect r(0, 0, iw, ih);
  Gdiplus::BitmapData bd{};
  Gdiplus::Status lk = bm->LockBits(&r, Gdiplus::ImageLockModeRead, agis_gdip_pf::k32bppArgb, &bd);
  if (lk != Gdiplus::Ok) {
    lk = bm->LockBits(&r, Gdiplus::ImageLockModeRead, agis_gdip_pf::k24bppRgb, &bd);
  }
  if (lk != Gdiplus::Ok) {
    return false;
  }
  auto sampleRgb = [&](int sx, int sy, double* rr, double* gg, double* bb) {
    const auto* row = static_cast<const BYTE*>(bd.Scan0) + sy * bd.Stride;
    if (bd.PixelFormat == agis_gdip_pf::k32bppArgb) {
      const BYTE* p = row + sx * 4;
      *bb = static_cast<double>(p[0]);
      *gg = static_cast<double>(p[1]);
      *rr = static_cast<double>(p[2]);
    } else {
      const BYTE* p = row + sx * 3;
      *bb = static_cast<double>(p[0]);
      *gg = static_cast<double>(p[1]);
      *rr = static_cast<double>(p[2]);
    }
  };
  double r00, g00, b00, r10, g10, b10, r01, g01, b01, r11, g11, b11;
  sampleRgb(x0, y0, &r00, &g00, &b00);
  sampleRgb(x1, y0, &r10, &g10, &b10);
  sampleRgb(x0, y1, &r01, &g01, &b01);
  sampleRgb(x1, y1, &r11, &g11, &b11);
  bm->UnlockBits(&bd);
  const double w00 = (1.0 - fu) * (1.0 - fv);
  const double w10 = fu * (1.0 - fv);
  const double w01 = (1.0 - fu) * fv;
  const double w11 = fu * fv;
  *pr = static_cast<BYTE>((std::clamp)(r00 * w00 + r10 * w10 + r01 * w01 + r11 * w11, 0.0, 255.0));
  *pg = static_cast<BYTE>((std::clamp)(g00 * w00 + g10 * w10 + g01 * w01 + g11 * w11, 0.0, 255.0));
  *pb = static_cast<BYTE>((std::clamp)(b00 * w00 + b10 * w10 + b01 * w01 + b11 * w11, 0.0, 255.0));
  return true;
}

static bool TileSlippyPaintProjResampled(HDC hdc, TilePreviewState* st, const TileSlippyPaintLayout& lay) {
  if (!st) {
    return false;
  }
  if (st->slippyProjection == TileSlippyProjection::kEquirectangular) {
    if (!TileSlippyProjEnsure4326To3857()) {
      return false;
    }
  } else if (st->slippyProjection == TileSlippyProjection::kCylindricalEqualArea) {
    if (!TileSlippyProjEnsureCeaPipelines()) {
      return false;
    }
  } else {
    return false;
  }
  const RECT& img = lay.imgArea;
  const int aw = lay.aw;
  const int ah = lay.ah;
  if (aw < 1 || ah < 1) {
    return false;
  }
  auto outBmp = std::make_unique<Gdiplus::Bitmap>(aw, ah, agis_gdip_pf::k24bppRgb);
  if (!outBmp || outBmp->GetLastStatus() != Gdiplus::Ok) {
    return false;
  }
  Gdiplus::Rect lockR(0, 0, aw, ah);
  Gdiplus::BitmapData bd{};
  if (outBmp->LockBits(&lockR, Gdiplus::ImageLockModeWrite, agis_gdip_pf::k24bppRgb, &bd) != Gdiplus::Ok) {
    return false;
  }
  auto* base = static_cast<BYTE*>(bd.Scan0);
  const int z = st->viewZ;
  const double n = static_cast<double>(1u << static_cast<unsigned>(z));
  for (int j = 0; j < ah; ++j) {
    BYTE* row = base + j * bd.Stride;
    const int py = img.top + j;
    for (int i = 0; i < aw; ++i) {
      const int px = img.left + i;
      double lonDeg = 0;
      double latDeg = 0;
      if (st->slippyProjection == TileSlippyProjection::kEquirectangular) {
        double tileX = 0;
        double tileY = 0;
        SlippyPixelCenterToFractionalTile(img, px, py, lay, &tileX, &tileY);
        lonDeg = tileX / n * 360.0 - 180.0;
        const double latRad = std::atan(std::sinh(kTileGeoPi * (1.0 - 2.0 * tileY / n)));
        latDeg = latRad * 180.0 / kTileGeoPi;
      } else {
        const double cx = static_cast<double>(px) + 0.5;
        const double cy = static_cast<double>(py) + 0.5;
        SlippyScreenToLonLatInverseCea(lay, cx, cy, &lonDeg, &latDeg);
      }
      if (st->slippyProjection != TileSlippyProjection::kEquirectangular) {
        lonDeg = SlippyWrapLongitudeDeg(lonDeg);
      }
      latDeg = SlippyClampLatitudeDeg(latDeg);
      double mx = 0;
      double my = 0;
      if (st->slippyProjection == TileSlippyProjection::kEquirectangular) {
        PJ_COORD c = proj_coord(lonDeg, latDeg, 0, 0);
        c = proj_trans(g_tileSlippyPj4326To3857, PJ_FWD, c);
        mx = c.xyz.x;
        my = c.xyz.y;
      } else {
        PJ_COORD c = proj_coord(lonDeg, latDeg, 0, 0);
        c = proj_trans(g_tileSlippyPj4326ToCea, PJ_FWD, c);
        if (!std::isfinite(c.xyz.x) || !std::isfinite(c.xyz.y)) {
          row[i * 3 + 0] = 245;
          row[i * 3 + 1] = 240;
          row[i * 3 + 2] = 238;
          continue;
        }
        PJ_COORD c3857 = proj_coord(c.xyz.x, c.xyz.y, 0, 0);
        c3857 = proj_trans(g_tileSlippyPjCeaTo3857, PJ_FWD, c3857);
        mx = c3857.xyz.x;
        my = c3857.xyz.y;
      }
      if (!std::isfinite(mx) || !std::isfinite(my)) {
        row[i * 3 + 0] = 245;
        row[i * 3 + 1] = 240;
        row[i * 3 + 2] = 238;
        continue;
      }
      double fx = 0;
      double fy = 0;
      Slippy3857MetersToFractionalTile(z, mx, my, &fx, &fy);
      if (fx < 0 || fy < 0 || fx >= n || fy >= n) {
        row[i * 3 + 0] = 245;
        row[i * 3 + 1] = 240;
        row[i * 3 + 2] = 238;
        continue;
      }
      const int ttx = static_cast<int>((std::floor)(fx));
      const int tty = static_cast<int>((std::floor)(fy));
      const double lu = fx - (std::floor)(fx);
      const double lv = fy - (std::floor)(fy);
      BYTE rr = 238;
      BYTE gg = 240;
      BYTE bb = 245;
      if (!SlippySampleTileBilinearUv(st, z, ttx, tty, lu, lv, &rr, &gg, &bb)) {
        rr = 238;
        gg = 240;
        bb = 245;
      }
      row[i * 3 + 0] = bb;
      row[i * 3 + 1] = gg;
      row[i * 3 + 2] = rr;
    }
  }
  outBmp->UnlockBits(&bd);
  Gdiplus::Graphics gx(hdc);
  gx.DrawImage(outBmp.get(), img.left, img.top, aw, ah);
  return true;
}
#endif  // GIS_DESKTOP_HAVE_GDAL

static void TileSlippyPaintLayoutFillGeo(TilePreviewState* st, TileSlippyPaintLayout* lay) {
  if (!st || !lay || !lay->valid) {
    return;
  }
  const int z = st->viewZ;
  const double wl = lay->worldLeft;
  const double wt = lay->worldTop;
  const double wr = lay->worldLeft + lay->worldW;
  const double wb = lay->worldTop + lay->worldH;
  SlippyTileXYToLonLat(wl, wt, z, &lay->cornerLon[0], &lay->cornerLat[0]);
  SlippyTileXYToLonLat(wr, wt, z, &lay->cornerLon[1], &lay->cornerLat[1]);
  SlippyTileXYToLonLat(wl, wb, z, &lay->cornerLon[2], &lay->cornerLat[2]);
  SlippyTileXYToLonLat(wr, wb, z, &lay->cornerLon[3], &lay->cornerLat[3]);
  double centerLon = 0.0;
  double centerLat = 0.0;
  SlippyTileXYToLonLat(st->centerTx, st->centerTy, z, &centerLon, &centerLat);
  lay->ppdEq = static_cast<double>(st->pixelsPerTile) * static_cast<double>(lay->dim) / 360.0;
  lay->rPixCea = lay->ppdEq * (180.0 / kTileGeoPi);
  lay->imgCenterX = static_cast<double>(lay->imgArea.left + lay->imgArea.right) * 0.5;
  lay->imgCenterY = static_cast<double>(lay->imgArea.top + lay->imgArea.bottom) * 0.5;
  if (st->slippyProjection == TileSlippyProjection::kEquirectangular) {
    const double minLa =
        (std::min)({lay->cornerLat[0], lay->cornerLat[1], lay->cornerLat[2], lay->cornerLat[3]});
    const double maxLa =
        (std::max)({lay->cornerLat[0], lay->cornerLat[1], lay->cornerLat[2], lay->cornerLat[3]});
    double latSpan = maxLa - minLa;
    constexpr double kSpanEps = 1e-9;
    if (latSpan < kSpanEps) {
      latSpan = kSpanEps;
    }
    const double n = static_cast<double>(lay->dim);
    const double lonLinearDeg = (std::max)(std::abs(lay->worldW * 360.0 / n), kSpanEps);
    lay->ppdLon = static_cast<double>(lay->aw) / lonLinearDeg;
    lay->ppdLat = static_cast<double>(lay->ah) / latSpan;
    lay->viewCenterLon = centerLon;
    lay->viewCenterLat = centerLat;
    lay->ppdEq = lay->ppdLon;
  } else {
    lay->viewCenterLon = centerLon;
    lay->viewCenterLat = centerLat;
    lay->ppdLon = lay->ppdEq;
    lay->ppdLat = lay->ppdEq;
  }
#if GIS_DESKTOP_HAVE_GDAL
  TileSlippyFillDispCornersFromGeo(st, lay);
#endif
}

static void SlippyLonLatToScreenCea(const TileSlippyPaintLayout& lay, double lonDeg, double latDeg, int* sx, int* sy) {
  if (!sx || !sy) {
    return;
  }
  const double latR = latDeg * kTileGeoPi / 180.0;
  const double cLatR = lay.viewCenterLat * kTileGeoPi / 180.0;
  *sx = static_cast<int>(std::lround(lay.imgCenterX + (lonDeg - lay.viewCenterLon) * lay.ppdLon));
  *sy = static_cast<int>(std::lround(lay.imgCenterY - (std::sin(latR) - std::sin(cLatR)) * lay.rPixCea));
}

/// 该片 GDI+ DrawImage 轴对齐目的地（片元线性 **不** clamp u,v；不与 `imageArea` 求交）。导出 `rasterBlitDestClient` 与此一致。
static void SlippyGetTileClientDestRaw(const TilePreviewState* st, const TileSlippyPaintLayout& lay, int tx, int ty, RECT* out) {
  if (!st || !out) {
    return;
  }
  if (st->slippyProjection == TileSlippyProjection::kWebMercatorGrid) {
    const RECT& imgArea = lay.imgArea;
    const double ppt = static_cast<double>(st->pixelsPerTile);
    const int sx = imgArea.left + static_cast<int>(std::lround((static_cast<double>(tx) - lay.worldLeft) * ppt));
    const int sy = imgArea.top + static_cast<int>(std::lround((static_cast<double>(ty) - lay.worldTop) * ppt));
    const int sw = (std::max)(1, static_cast<int>((std::ceil)(st->pixelsPerTile)) + 1);
    const uint64_t key = PackTileKey(st->viewZ, tx, ty);
    if (st->slippyPaths.find(key) != st->slippyPaths.end()) {
      if (Gdiplus::Bitmap* bm = TileBitmapCachePeek(st, key)) {
        const int iw = bm->GetWidth();
        const int ih = bm->GetHeight();
        if (iw > 0 && ih > 0 && std::abs(iw - sw) <= 1 && std::abs(ih - sw) <= 1) {
          *out = {sx, sy, sx + iw, sy + ih};
          return;
        }
      }
    }
    *out = {sx, sy, sx + sw, sy + sw};
    return;
  }
  int sxa, sya, sxb, syb, sxc, syc, sxd, syd;
  if (st->slippyProjection == TileSlippyProjection::kEquirectangular) {
    double cxa, cya, cxb, cyb, cxc, cyc, cxd, cyd;
    SlippyFractionalTileToClientPixelCenterEx(lay, static_cast<double>(tx), static_cast<double>(ty), &cxa, &cya, false);
    SlippyFractionalTileToClientPixelCenterEx(lay, static_cast<double>(tx + 1), static_cast<double>(ty), &cxb, &cyb, false);
    SlippyFractionalTileToClientPixelCenterEx(lay, static_cast<double>(tx), static_cast<double>(ty + 1), &cxc, &cyc, false);
    SlippyFractionalTileToClientPixelCenterEx(lay, static_cast<double>(tx + 1), static_cast<double>(ty + 1), &cxd, &cyd, false);
    sxa = static_cast<int>(std::lround(cxa));
    sya = static_cast<int>(std::lround(cya));
    sxb = static_cast<int>(std::lround(cxb));
    syb = static_cast<int>(std::lround(cyb));
    sxc = static_cast<int>(std::lround(cxc));
    syc = static_cast<int>(std::lround(cyc));
    sxd = static_cast<int>(std::lround(cxd));
    syd = static_cast<int>(std::lround(cyd));
  } else {
    const int z = st->viewZ;
    double lon0, lat0, lon1, lat1, lon2, lat2, lon3, lat3;
    SlippyTileXYToLonLat(static_cast<double>(tx), static_cast<double>(ty), z, &lon0, &lat0);
    SlippyTileXYToLonLat(static_cast<double>(tx + 1), static_cast<double>(ty), z, &lon1, &lat1);
    SlippyTileXYToLonLat(static_cast<double>(tx), static_cast<double>(ty + 1), z, &lon2, &lat2);
    SlippyTileXYToLonLat(static_cast<double>(tx + 1), static_cast<double>(ty + 1), z, &lon3, &lat3);
    SlippyLonLatToScreenCea(lay, lon0, lat0, &sxa, &sya);
    SlippyLonLatToScreenCea(lay, lon1, lat1, &sxb, &syb);
    SlippyLonLatToScreenCea(lay, lon2, lat2, &sxc, &syc);
    SlippyLonLatToScreenCea(lay, lon3, lat3, &sxd, &syd);
  }
  const int minx = (std::min)({sxa, sxb, sxc, sxd});
  const int maxx = (std::max)({sxa, sxb, sxc, sxd});
  const int miny = (std::min)({sya, syb, syc, syd});
  const int maxy = (std::max)({sya, syb, syc, syd});
  const int rw = (std::max)(1, maxx - minx);
  const int rh = (std::max)(1, maxy - miny);
  *out = {minx, miny, minx + rw, miny + rh};
}

/// 瓦片网格辅助框（橘黄）：在 Raw 目的地与 `lay.imgArea` 求交，只画地图区内可见段；避免整视口 PROJ 时靠边格因 u,v clamp 被压成细条。
static void SlippyGetTileScreenBounds(const TilePreviewState* st, const TileSlippyPaintLayout& lay, int tx, int ty, RECT* out) {
  if (!st || !out) {
    return;
  }
  RECT raw{};
  SlippyGetTileClientDestRaw(st, lay, tx, ty, &raw);
  RECT vis{};
  if (IntersectRect(&vis, &raw, &lay.imgArea)) {
    *out = vis;
  } else {
    SetRectEmpty(out);
  }
}

/// 与 TilePaintSlippyTiles / TilePaintOneSlippyTileGeo 一致，供场景 JSON 描述「橙色网格」与「光栅绘制目的地」。
/// @param rasterIsFullViewportProj 由调用方一次性判定（非 WebMercator 且 PROJ 重采样成功时整幅地图为单遍着色）。
static void SlippyGetTileRasterBlitRectForExport(TilePreviewState* st, const TileSlippyPaintLayout& lay, int tx, int ty,
                                                bool rasterIsFullViewportProj, RECT* out_blitRect,
                                                bool* out_webMercNativeBmpSize) {
  if (!st || !out_blitRect || !out_webMercNativeBmpSize) {
    return;
  }
  *out_webMercNativeBmpSize = false;
  SetRectEmpty(out_blitRect);
  if (rasterIsFullViewportProj) {
    return;
  }
  SlippyGetTileClientDestRaw(st, lay, tx, ty, out_blitRect);
  if (st->slippyProjection == TileSlippyProjection::kWebMercatorGrid) {
    const uint64_t key = PackTileKey(st->viewZ, tx, ty);
    if (st->slippyPaths.find(key) != st->slippyPaths.end()) {
      if (Gdiplus::Bitmap* bm = TileBitmapCachePeek(st, key)) {
        const int iw = bm->GetWidth();
        const int ih = bm->GetHeight();
        const int sw = (std::max)(1, static_cast<int>((std::ceil)(st->pixelsPerTile)) + 1);
        if (iw > 0 && ih > 0 && std::abs(iw - sw) <= 1 && std::abs(ih - sw) <= 1) {
          *out_webMercNativeBmpSize = true;
        }
      }
    }
  }
}

static bool TileSlippyBuildPaintLayout(RECT cr, TilePreviewState* st, TileSlippyPaintLayout* out) {
  if (!st || !out || st->slippyPaths.empty()) {
    return false;
  }
  SlippyViewFrame vf{};
  TileSlippyComputeViewFrame(cr, st, &vf);
  out->fullImgArea = vf.fullImg;
  out->imgArea = vf.mapImg;
  out->aw = (std::max)(1, static_cast<int>(out->imgArea.right - out->imgArea.left));
  out->ah = (std::max)(1, static_cast<int>(out->imgArea.bottom - out->imgArea.top));
  out->worldW = vf.worldW;
  out->worldH = vf.worldH;
  out->worldLeft = vf.worldLeft;
  out->worldTop = vf.worldTop;
  out->dim = 1 << st->viewZ;
  out->tx0 = static_cast<int>((std::floor)(out->worldLeft));
  out->ty0 = static_cast<int>((std::floor)(out->worldTop));
  out->tx1 = static_cast<int>((std::ceil)(out->worldLeft + out->worldW));
  out->ty1 = static_cast<int>((std::ceil)(out->worldTop + out->worldH));
  out->tx0 = (std::max)(0, out->tx0);
  out->ty0 = (std::max)(0, out->ty0);
  out->tx1 = (std::min)(out->dim - 1, out->tx1);
  out->ty1 = (std::min)(out->dim - 1, out->ty1);
  out->spanX = out->tx1 - out->tx0 + 1;
  out->spanY = out->ty1 - out->ty0 + 1;
  out->tooMany = out->spanX * out->spanY > 220;
  out->valid = true;
  TileSlippyPaintLayoutFillGeo(st, out);
  return true;
}

static void JsonAppendRectClient(std::string& j, const RECT& r) {
  const int w = (std::max)(0, static_cast<int>(r.right - r.left));
  const int h = (std::max)(0, static_cast<int>(r.bottom - r.top));
  j += "{ \"left\": ";
  j += std::to_string(r.left);
  j += ", \"top\": ";
  j += std::to_string(r.top);
  j += ", \"right\": ";
  j += std::to_string(r.right);
  j += ", \"bottom\": ";
  j += std::to_string(r.bottom);
  j += ", \"widthPx\": ";
  j += std::to_string(w);
  j += ", \"heightPx\": ";
  j += std::to_string(h);
  j += " }";
}

static void Utf8AppendJsonEscapedString(std::string* out, const std::wstring& ws) {
  if (!out) {
    return;
  }
  out->push_back('"');
  const int cb = WideCharToMultiByte(CP_UTF8, 0, ws.c_str(), -1, nullptr, 0, nullptr, nullptr);
  if (cb <= 1) {
    out->push_back('"');
    return;
  }
  std::string u(static_cast<size_t>(cb - 1), '\0');
  WideCharToMultiByte(CP_UTF8, 0, ws.c_str(), -1, u.data(), cb, nullptr, nullptr);
  for (unsigned char ch : u) {
    switch (ch) {
    case '"':
      *out += "\\\"";
      break;
    case '\\':
      *out += "\\\\";
      break;
    case '\b':
      *out += "\\b";
      break;
    case '\f':
      *out += "\\f";
      break;
    case '\n':
      *out += "\\n";
      break;
    case '\r':
      *out += "\\r";
      break;
    case '\t':
      *out += "\\t";
      break;
    default:
      if (ch < 0x20u) {
        char buf[12]{};
        snprintf(buf, sizeof(buf), "\\u%04x", ch);
        *out += buf;
      } else {
        out->push_back(static_cast<char>(ch));
      }
    }
  }
  out->push_back('"');
}

static void Utf8AppendJsonEscapedStringPick(std::string* out, const wchar_t* zh, const wchar_t* en) {
  Utf8AppendJsonEscapedString(out, std::wstring(AgisPickUiLang(zh, en)));
}

/// 已为 UTF-8 的字串写入 JSON 字符串字面量（转义控制字符与引号）。
static void Utf8AppendJsonEscapedUtf8String(std::string* out, const std::string& utf8) {
  if (!out) {
    return;
  }
  out->push_back('"');
  for (unsigned char ch : utf8) {
    switch (ch) {
    case '"':
      *out += "\\\"";
      break;
    case '\\':
      *out += "\\\\";
      break;
    case '\b':
      *out += "\\b";
      break;
    case '\f':
      *out += "\\f";
      break;
    case '\n':
      *out += "\\n";
      break;
    case '\r':
      *out += "\\r";
      break;
    case '\t':
      *out += "\\t";
      break;
    default:
      if (ch < 0x20u) {
        char buf[12]{};
        snprintf(buf, sizeof(buf), "\\u%04x", ch);
        *out += buf;
      } else {
        out->push_back(static_cast<char>(ch));
      }
    }
  }
  out->push_back('"');
}

static const char* TileSlippyProjectionJsonId(TileSlippyProjection p) {
  switch (p) {
  case TileSlippyProjection::kEquirectangular:
    return "equirectangular";
  case TileSlippyProjection::kWebMercatorGrid:
    return "web_mercator_grid";
  case TileSlippyProjection::kCylindricalEqualArea:
    return "cylindrical_equal_area";
  default:
    return "unknown";
  }
}

static std::string TilePreviewBuildSceneJsonUtf8(HWND hwnd, TilePreviewState* st) {
  std::string j;
  j += "{\n";
  j += "  \"app\": \"agis.tile_raster_preview\",\n";
  j += "  \"schemaVersion\": 11,\n";
  SYSTEMTIME lt{};
  GetLocalTime(&lt);
  {
    wchar_t ts[64]{};
    swprintf_s(ts, L"%04u-%02u-%02uT%02u:%02u:%02u", static_cast<unsigned>(lt.wYear),
               static_cast<unsigned>(lt.wMonth), static_cast<unsigned>(lt.wDay), static_cast<unsigned>(lt.wHour),
               static_cast<unsigned>(lt.wMinute), static_cast<unsigned>(lt.wSecond));
    j += "  \"exportedAtLocal\": ";
    Utf8AppendJsonEscapedString(&j, ts);
    j += ",\n";
  }
  j += "  \"build\": {\n";
#if GIS_DESKTOP_HAVE_GDAL
  j += "    \"gdalProj\": true\n";
#else
  j += "    \"gdalProj\": false\n";
#endif
  j += "  },\n";
  RECT cr{};
  GetClientRect(hwnd, &cr);
  const int cw = (std::max)(1, static_cast<int>(cr.right - cr.left));
  const int ch = (std::max)(1, static_cast<int>(cr.bottom - cr.top));
  j += "  \"client\": { \"width\": ";
  j += std::to_string(cw);
  j += ", \"height\": ";
  j += std::to_string(ch);
  j += " },\n";
  bool slippyRasterFullViewportProj = false;
#if GIS_DESKTOP_HAVE_GDAL
  bool slippyProjPipelinesReady = false;
  if (st->mode == TilePreviewState::Mode::kSlippyQuadtree) {
    slippyProjPipelinesReady = TileSlippyProjLayoutPipelinesReady(st->slippyProjection);
    if (st->slippyProjection != TileSlippyProjection::kWebMercatorGrid) {
      slippyRasterFullViewportProj = slippyProjPipelinesReady;
    }
  }
#else
  constexpr bool slippyProjPipelinesReady = false;
#endif
  j += "  \"rootPath\": ";
  Utf8AppendJsonEscapedString(&j, st->rootPath);
  j += ",\n";
  j += "  \"dataSource\": {\n    \"kind\": ";
  if (st->rootPath.empty()) {
    j += "\"empty\"";
  } else {
    switch (st->mode) {
    case TilePreviewState::Mode::kSlippyQuadtree:
      j += "\"slippy_quadtree\"";
      break;
    case TilePreviewState::Mode::kSingleRaster:
      j += "\"single_raster\"";
      break;
    case TilePreviewState::Mode::kThreeDTilesMeta:
      j += "\"three_d_tiles_meta\"";
      break;
    default:
      j += "\"unknown\"";
      break;
    }
  }
  j += ",\n    \"rootPath\": ";
  Utf8AppendJsonEscapedString(&j, st->rootPath);
  j += ",\n    \"samplePath\": ";
  if (st->samplePath.empty()) {
    j += "null";
  } else {
    Utf8AppendJsonEscapedString(&j, st->samplePath);
  }
  j += ",\n    \"slippy\": ";
  if (st->mode == TilePreviewState::Mode::kSlippyQuadtree && !st->rootPath.empty()) {
    j += "{\n      \"indexedTileFiles\": ";
    j += std::to_string(st->tileCount);
    j += ",\n      \"indexMaxZ\": ";
    j += std::to_string(st->indexMaxZ);
    j += ",\n      \"diskTileRowOrder\": ";
    j += st->slippyDiskTmsYIndex ? "\"tms_y_on_disk_mapped_to_xyz_ty\"" : "\"xyz\"";
    j += ",\n      \"pathPatternNoteZh\": ";
    Utf8AppendJsonEscapedStringPick(
        &j, L"{z}/{x}/{y} 栅格；磁盘为 TMS 行号时文件名 y 经 (2^z-1-y) 映射为 XYZ 的 ty。",
        L"{z}/{x}/{y} rasters; with TMS row filenames on disk, y maps to XYZ ty via (2^z-1-y).");
    j += "\n    }";
  } else {
    j += "null";
  }
  j += ",\n    \"singleRasterNoteZh\": ";
  if (st->mode == TilePreviewState::Mode::kSingleRaster && !st->rootPath.empty()) {
    std::error_code ec;
    const std::filesystem::path rp(st->rootPath);
    if (std::filesystem::is_regular_file(rp, ec)) {
      const std::wstring ext = rp.extension().wstring();
      if (_wcsicmp(ext.c_str(), L".mbtiles") == 0) {
        Utf8AppendJsonEscapedStringPick(
            &j, L"根路径为单文件 MBTiles：GDAL 下采样缩略预览，地理参照以数据集 CRS 为准。",
            L"Root is a single MBTiles file: GDAL downsampled thumbnail; georef follows dataset CRS.");
      } else if (_wcsicmp(ext.c_str(), L".gpkg") == 0) {
        Utf8AppendJsonEscapedStringPick(&j, L"根路径为单文件 GeoPackage 栅格：GDAL 下采样缩略预览。",
                                        L"Root is a single GeoPackage raster: GDAL downsampled thumbnail.");
      } else {
        Utf8AppendJsonEscapedStringPick(&j, L"根路径为单张栅格文件。", L"Root is a single raster file.");
      }
    } else {
      Utf8AppendJsonEscapedStringPick(
          &j, L"目录下递归采样首张可解码栅格；无内嵌地理参照时仅为像素预览。",
          L"Recursively picks first decodable raster under the folder; pixel-only preview if no embedded georef.");
    }
  } else {
    j += "null";
  }
  j += ",\n    \"threeDTilesTilesetPath\": ";
  if (st->mode == TilePreviewState::Mode::kThreeDTilesMeta && !st->rootPath.empty()) {
    if (const auto tsp = FindTilesetJsonPath(st->rootPath)) {
      Utf8AppendJsonEscapedString(&j, *tsp);
    } else {
      j += "null";
    }
  } else {
    j += "null";
  }
  j += "\n  },\n";
  j += "  \"projection\": {\n    \"buildUsesGdalProj\": ";
#if GIS_DESKTOP_HAVE_GDAL
  j += "true";
#else
  j += "false";
#endif
  j += ",\n    \"pipeline\": {\n      \"conceptualFlowZh\": ";
  Utf8AppendJsonEscapedStringPick(
      &j, L"【语义顺序】输入源投影（Slippy 纹理 EPSG:3857）→ 重投影/重采样算法 → 显示用几何（选项中的 TileSlippyProjection）→ 光栅合成到屏幕。"
      L"【实现顺序】对每个输出像素：屏幕 → 逆入显示几何 → PROJ 到 EPSG:3857 → 读源纹理（与语义链等价、方向相反）。",
      L"[Semantic order] Source projection (Slippy texture EPSG:3857) → reproject/resample → display geometry "
      L"(TileSlippyProjection in options) → composite to screen. [Implementation order] Per output pixel: screen → "
      L"inverse display geometry → PROJ to EPSG:3857 → sample texture (same chain, opposite direction).");
  j += ",\n      \"sourceTextureCrs\": \"EPSG:3857\",\n      \"sourceRoleZh\": ";
  Utf8AppendJsonEscapedStringPick(
      &j, L"Slippy 标准瓦片：磁盘 z/x/y 纹理的地理语义为 Web 墨卡托（输入源投影/CRS）；与 textureTileAssumption 一致。",
      L"Standard Slippy tiles: on-disk z/x/y semantics are Web Mercator (source texture CRS); matches textureTileAssumption.");
  j += ",\n      \"algorithmZh\": ";
  Utf8AppendJsonEscapedStringPick(
      &j,
      L"桥接「显示几何坐标」与「输入源 EPSG:3857」：逆映射 + PROJ + 双线性/拼贴；整视口可为单遍重采样。"
      L"「XYZ 片元线性」下显示与源索引一致，无额外 CRS 变换（恒等）。",
      L"Bridges display-geometry coordinates and source EPSG:3857: inverse map + PROJ + bilinear/blit; full viewport can be "
      L"one resample pass. Under XYZ tile-linear, display matches source indices with no extra CRS transform (identity).");
  j += ",\n      \"displayRoleZh\": ";
  Utf8AppendJsonEscapedStringPick(
      &j, L"viewportDisplay 与选项单选：用户所选「显示用」平面或索引展开（显示投影/几何语义），不改写源文件。",
      L"viewportDisplay mirrors the option: chosen display plane or index layout (display projection semantics); source files "
      L"unchanged.");
  j += "\n    },\n    \"textureTileAssumption\": ";
  if (st->mode == TilePreviewState::Mode::kSlippyQuadtree) {
    j += "{\n      \"pixelGridInterpretation\": \"web_mercator_slippy_xyz_texture\",\n";
    j += "      \"crsAuthority\": \"EPSG:3857\",\n";
    j += "      \"meridianModelZh\": ";
    Utf8AppendJsonEscapedStringPick(
        &j, L"与常见 OSM/Google XYZ 纹理一致的球面 Web 墨卡托米制范围；各瓦片 bbox3857Meters 与此一致，PROJ 椭球 3857 边界仅有细微差别。",
        L"Spherical Web Mercator meter extents like common OSM/Google XYZ; per-tile bbox3857Meters matches; PROJ spheroid 3857 "
        L"edges differ slightly.");
    j += ",\n      \"tileIndexTyNorthUp\": true\n    }";
  } else {
    j += "null";
  }
  j += ",\n    \"viewportDisplay\": {\n";
  if (st->mode == TilePreviewState::Mode::kSlippyQuadtree) {
    j += "      \"layoutProjectionId\": \"";
    j += TileSlippyProjectionJsonId(st->slippyProjection);
    j += "\",\n      \"layoutProjectionLabelZh\": ";
    Utf8AppendJsonEscapedString(&j, std::wstring(TileSlippyProjectionLongLabel(st->slippyProjection)));
    j += ",\n      \"displayPlane\": ";
    switch (st->slippyProjection) {
    case TileSlippyProjection::kWebMercatorGrid:
      j += "{ \"type\": \"fractional_tile_index_plane\", \"noteZh\": ";
      Utf8AppendJsonEscapedStringPick(
          &j, L"屏幕为当前 Z 下片元索引的线性展开；不做地理投影展点，纹理仍按 3857 解码。",
          L"Screen is linear fractional-tile indices at current Z; no geographic plot warp; textures decoded as 3857.");
      j += " }";
      break;
    case TileSlippyProjection::kEquirectangular:
      j += "{ \"type\": \"slippy_fractional_tile_linear_screen\", \"samplingCrs\": \"EPSG:4326\", \"axisZh\": ";
      Utf8AppendJsonEscapedStringPick(
          &j, L"等比例经纬度（Plate Carrée）语义：采样用 WGS84 经纬度度。屏上与 XYZ 片元网格同拓扑——片元浮点矩形线性对应地图区；每点经 SlippyTileXYToLonLat（纬向墨卡托非线性）得 WGS84，GDAL 下 PROJ→EPSG:3857 双线性取纹理。非整屏仿射 Plate Carrée，故 JSON 中 pixelsPerDegreeLon/Lat 可不等。",
          L"Equirectangular (Plate Carrée) sampling uses WGS84 degrees. Screen shares XYZ tile topology—fractional tile rect "
          L"maps linearly to the map; each point → SlippyTileXYToLonLat (nonlinear in latitude) → WGS84, then PROJ→EPSG:3857 "
          L"and bilinear sample. Not global affine Plate Carrée, so pixelsPerDegreeLon/Lat may differ.");
      j += " }";
      break;
    case TileSlippyProjection::kCylindricalEqualArea:
      j += "{ \"type\": \"cea_metric_plane\", \"projInit\": ";
      Utf8AppendJsonEscapedUtf8String(&j, std::string(kTileSlippyCeaWgs84Proj));
      j += ", \"noteZh\": ";
      Utf8AppendJsonEscapedStringPick(
          &j, L"显示平面为等积圆柱（Lambert CEA）米制坐标；指针与整视口重采样使用 PROJ：CEA↔EPSG:4326、CEA→EPSG:3857。",
          L"Display plane is Lambert cylindrical equal-area (CEA) metres; pointer and full-viewport resample use PROJ: "
          L"CEA↔EPSG:4326, CEA→EPSG:3857.");
      j += " }";
      break;
    default:
      j += "null";
      break;
    }
    j += ",\n      \"compositeToScreen\": ";
    j += slippyRasterFullViewportProj ? "\"per_pixel_proj_full_viewport\"" : "\"per_tile_bitmap_blit\"";
    j += ",\n      \"projResamplePipelineZh\": ";
#if GIS_DESKTOP_HAVE_GDAL
    Utf8AppendJsonEscapedStringPick(
        &j, L"非「XYZ 网格」时：等比例经纬度模式每像素 SlippyPixelCenterToFractionalTile→SlippyTileXYToLonLat 得 WGS84，经 PROJ EPSG:4326→EPSG:3857 再 Slippy3857MetersToFractionalTile 纹理双线性（屏上映射与 XYZ 片元拓扑一致，非整屏「1°经像素长=1°纬像素长」的仿射 Plate Carrée）。等积圆柱为客户区像素与 SlippyLonLatToScreenCea 互逆得 WGS84 后走 WGS84→CEA→EPSG:3857。",
        L"Not XYZ grid: equirectangular path per pixel SlippyPixelCenterToFractionalTile→SlippyTileXYToLonLat→WGS84, PROJ "
        L"EPSG:4326→EPSG:3857, Slippy3857MetersToFractionalTile bilinear (same tile topology on screen, not global affine "
        L"Plate Carrée). CEA: client pixels ↔ SlippyLonLatToScreenCea → WGS84 → WGS84→CEA→EPSG:3857.");
#else
    Utf8AppendJsonEscapedStringPick(
        &j, L"当前构建无 GDAL/PROJ：等经纬/等积圆柱为示意几何，非严格重采样。",
        L"No GDAL/PROJ in this build: equirectangular/CEA are approximate, not strict resampling.");
#endif
    j += ",\n      \"projTransformersReady\": ";
#if GIS_DESKTOP_HAVE_GDAL
    j += slippyProjPipelinesReady ? "true" : "false";
#else
    j += "false";
#endif
    j += "\n";
  } else if (st->mode == TilePreviewState::Mode::kSingleRaster) {
    j += "      \"layoutProjectionId\": \"none_raster_fit_window\",\n";
    j += "      \"layoutProjectionLabelZh\": ";
    Utf8AppendJsonEscapedStringPick(&j, L"单图缩放置中，屏幕像素与地理无变换",
                                    L"Single image scaled to fit; no geospatial transform of screen pixels.");
    j += ",\n      \"displayPlane\": null,\n";
    j += "      \"compositeToScreen\": \"gdiplus_scaled_blit\",\n";
    j += "      \"projTransformersReady\": false,\n";
    j += "      \"gdalDatasetCrsWkt\": ";
#if GIS_DESKTOP_HAVE_GDAL
    {
      std::wstring gdalPath = st->samplePath.empty() ? st->rootPath : st->samplePath;
      std::error_code ec;
      if (!gdalPath.empty() && std::filesystem::is_regular_file(std::filesystem::path(gdalPath), ec)) {
        const std::string wkt = TilePreviewGdalDatasetProjectionRefUtf8(gdalPath);
        if (!wkt.empty()) {
          Utf8AppendJsonEscapedUtf8String(&j, wkt);
        } else {
          j += "null";
        }
      } else {
        j += "null";
      }
    }
#else
    j += "null";
#endif
    j += "\n";
  } else {
    j += "      \"layoutProjectionId\": \"three_d_tiles_metadata\",\n";
    j += "      \"layoutProjectionLabelZh\": ";
    Utf8AppendJsonEscapedStringPick(&j, L"3D Tiles 元数据视图；不做平面投影瓦片合成",
                                    L"3D Tiles metadata view; no flat projected tile compositing.");
    j += ",\n      \"displayPlane\": null,\n";
    j += "      \"compositeToScreen\": \"n_a\",\n";
    j += "      \"projTransformersReady\": false\n";
  }
  j += "    },\n    \"displayOptionsEcho\": {\n      \"pixelsPerTile\": ";
  if (st->mode == TilePreviewState::Mode::kSlippyQuadtree) {
    char b[64]{};
    snprintf(b, sizeof(b), "%.6g", static_cast<double>(st->pixelsPerTile));
    j += b;
  } else {
    j += "null";
  }
  j += ",\n      \"viewZ\": ";
  if (st->mode == TilePreviewState::Mode::kSlippyQuadtree) {
    j += std::to_string(st->viewZ);
  } else {
    j += "null";
  }
  j += ",\n      \"noteZh\": ";
  Utf8AppendJsonEscapedStringPick(&j, L"与选项面板中 Slippy 尺度一致；开关类见根字段 ui。",
                                  L"Matches Slippy scale in Options; toggles are under root ui.");
  j += "\n    }\n  },\n";
  j += "  \"ui\": {\n";
  j += "    \"showOptionsPanel\": ";
  j += st->showOptionsPanel ? "true" : "false";
  j += ",\n";
  j += "    \"showTopHint\": ";
  j += st->uiShowTopHint ? "true" : "false";
  j += ",\n";
  j += "    \"showSlippyMapHud\": ";
  j += st->uiShowSlippyMapHud ? "true" : "false";
  j += ",\n";
  j += "    \"showTileGrid\": ";
  j += st->uiShowTileGrid ? "true" : "false";
  j += ",\n";
  j += "    \"showTileLabels\": ";
  j += st->uiShowTileLabels ? "true" : "false";
  j += "\n";
  j += "  },\n";
  j += "  \"statusBarLine\": ";
  Utf8AppendJsonEscapedString(&j, st->tilePointerStatusLine);
  j += ",\n";
  j += "  \"pointer\": {\n";
  j += "    \"clientX\": ";
  j += std::to_string(st->lastPointerClient.x);
  j += ",\n    \"clientY\": ";
  j += std::to_string(st->lastPointerClient.y);
  j += ",\n    \"overImage\": ";
  j += st->pointerOverImage ? "true" : "false";
  j += ",\n    \"geoValid\": ";
  j += st->pointerGeoValid ? "true" : "false";
  j += ",\n    \"lonDeg\": ";
  if (st->pointerGeoValid) {
    char b[64]{};
    snprintf(b, sizeof(b), "%.9g", SlippyWrapLongitudeDeg(st->pointerLon));
    j += b;
  } else {
    j += "null";
  }
  j += ",\n    \"latDeg\": ";
  if (st->pointerGeoValid) {
    char b[64]{};
    snprintf(b, sizeof(b), "%.9g", SlippyClampLatitudeDeg(st->pointerLat));
    j += b;
  } else {
    j += "null";
  }
  j += "\n  },\n";
  switch (st->mode) {
  case TilePreviewState::Mode::kSlippyQuadtree: {
    j += "  \"mode\": \"slippy_quadtree\",\n";
    TileSlippyPaintLayout lay{};
    TileSlippyBuildPaintLayout(cr, st, &lay);
    j += "  \"hint\": ";
    Utf8AppendJsonEscapedString(&j, st->hint);
    j += ",\n";
    j += "  \"slippy\": {\n";
    j += "    \"tileCount\": ";
    j += std::to_string(st->tileCount);
    j += ",\n    \"indexMaxZ\": ";
    j += std::to_string(st->indexMaxZ);
    j += ",\n    \"viewZ\": ";
    j += std::to_string(st->viewZ);
    j += ",\n    \"centerTx\": ";
    {
      char b[64]{};
      snprintf(b, sizeof(b), "%.9g", st->centerTx);
      j += b;
    }
    j += ",\n    \"centerTy\": ";
    {
      char b[64]{};
      snprintf(b, sizeof(b), "%.9g", st->centerTy);
      j += b;
    }
    j += ",\n    \"pixelsPerTile\": ";
    {
      char b[64]{};
      snprintf(b, sizeof(b), "%.6g", static_cast<double>(st->pixelsPerTile));
      j += b;
    }
    j += ",\n    \"projection\": \"";
    j += TileSlippyProjectionJsonId(st->slippyProjection);
    j += "\",\n";
    j += "    \"projectionLabelZh\": ";
    Utf8AppendJsonEscapedString(&j, std::wstring(TileSlippyProjectionLongLabel(st->slippyProjection)));
    j += ",\n";
    j += "    \"layoutValid\": ";
    j += lay.valid ? "true" : "false";
    j += ",\n    \"layoutTooManyTilesToPaint\": ";
    j += lay.tooMany ? "true" : "false";
    j += ",\n";
    j += "    \"imageArea\": { \"left\": ";
    j += std::to_string(lay.imgArea.left);
    j += ", \"top\": ";
    j += std::to_string(lay.imgArea.top);
    j += ", \"right\": ";
    j += std::to_string(lay.imgArea.right);
    j += ", \"bottom\": ";
    j += std::to_string(lay.imgArea.bottom);
    j += " },\n";
    j += "    \"fullImageArea\": { \"left\": ";
    j += std::to_string(lay.fullImgArea.left);
    j += ", \"top\": ";
    j += std::to_string(lay.fullImgArea.top);
    j += ", \"right\": ";
    j += std::to_string(lay.fullImgArea.right);
    j += ", \"bottom\": ";
    j += std::to_string(lay.fullImgArea.bottom);
    j += " },\n";
    j += "    \"imagePixels\": { \"width\": ";
    j += std::to_string(lay.aw);
    j += ", \"height\": ";
    j += std::to_string(lay.ah);
    j += " },\n";
    j += "    \"world\": { \"left\": ";
    {
      char b[64]{};
      snprintf(b, sizeof(b), "%.9g", lay.worldLeft);
      j += b;
    }
    j += ", \"top\": ";
    {
      char b[64]{};
      snprintf(b, sizeof(b), "%.9g", lay.worldTop);
      j += b;
    }
    j += ", \"width\": ";
    {
      char b[64]{};
      snprintf(b, sizeof(b), "%.9g", lay.worldW);
      j += b;
    }
    j += ", \"height\": ";
    {
      char b[64]{};
      snprintf(b, sizeof(b), "%.9g", lay.worldH);
      j += b;
    }
    j += " },\n";
    j += "    \"gridDimAtZ\": ";
    j += std::to_string(lay.dim);
    j += ",\n    \"visibleTileIndexRange\": { \"tx0\": ";
    j += std::to_string(lay.tx0);
    j += ", \"ty0\": ";
    j += std::to_string(lay.ty0);
    j += ", \"tx1\": ";
    j += std::to_string(lay.tx1);
    j += ", \"ty1\": ";
    j += std::to_string(lay.ty1);
    j += ", \"spanX\": ";
    j += std::to_string(lay.spanX);
    j += ", \"spanY\": ";
    j += std::to_string(lay.spanY);
    j += " },\n";
    j += "    \"viewportRasterMode\": ";
    j += slippyRasterFullViewportProj ? "\"per_pixel_proj_full_viewport\"" : "\"per_tile_bitmap_blit\"";
    j += ",\n    \"visibleTilesExport\": {\n";
    j += "      \"skippedPaintDueToTooMany\": ";
    j += lay.tooMany ? "true" : "false";
    j += ",\n      \"logicalTileCount\": ";
    {
      const int n = lay.spanX * lay.spanY;
      j += std::to_string(n);
    }
    j += ",\n      \"exportNoteZh\": ";
    Utf8AppendJsonEscapedStringPick(
        &j, L"gridHudRectClient：橘黄框＝SlippyGetTileClientDestRaw 与 imageArea 求交（SlippyGetTileScreenBounds）；等经纬四角 u,v 不夹取后再求交，避免靠边格压扁。仅供肉眼对照 z/x/y。"
            L"rasterBlitDestClient：SlippyGetTileClientDestRaw（未与 imageArea 求交），与 per-tile DrawImage 目的地一致；per_pixel_proj_full_viewport 时为 null。"
            L"textureDecodeCached：仅 LRU 已缓存时非 null。"
            L"bbox3857Meters：该片在球面 Web 墨卡托 XYZ 下的纹理空间外包（米），与瓦片 PNG 的 EPSG:3857 覆盖一致；PROJ 椭球 3857 边界略有差异。",
        L"gridHudRectClient: orange rect = SlippyGetTileClientDestRaw ∩ imageArea (SlippyGetTileScreenBounds); equirectangular "
        L"corners use u,v before clamp to avoid squashed edge tiles—for visual z/x/y check only. "
        L"rasterBlitDestClient: SlippyGetTileClientDestRaw (not clipped to imageArea), matches per-tile DrawImage dest; null "
        L"when per_pixel_proj_full_viewport. textureDecodeCached: non-null only if LRU holds decoded bitmap. "
        L"bbox3857Meters: texture-space bounds (m) on spherical Web Mercator XYZ, aligned with tile PNG EPSG:3857 extent; "
        L"PROJ spheroid edges may differ slightly.");
    j += ",\n";
    constexpr int kMaxTileExportList = 2048;
    const int logicalN = lay.spanX * lay.spanY;
    const bool listTruncated = logicalN > kMaxTileExportList;
    j += "      \"listedTileCountCap\": ";
    j += std::to_string(kMaxTileExportList);
    j += ",\n      \"listTruncated\": ";
    j += listTruncated ? "true" : "false";
    j += ",\n      \"tiles\": [\n";
    {
      int listed = 0;
      for (int ty = lay.ty0; ty <= lay.ty1 && listed < kMaxTileExportList; ++ty) {
        for (int tx = lay.tx0; tx <= lay.tx1 && listed < kMaxTileExportList; ++tx) {
          if (listed > 0) {
            j += ",\n";
          }
          const uint64_t key = PackTileKey(st->viewZ, tx, ty);
          RECT gridRc{};
          SlippyGetTileScreenBounds(st, lay, tx, ty, &gridRc);
          RECT blitRc{};
          bool nativeBmp = false;
          SlippyGetTileRasterBlitRectForExport(st, lay, tx, ty, slippyRasterFullViewportProj, &blitRc, &nativeBmp);
          auto pit = st->slippyPaths.find(key);
          const bool inIdx = pit != st->slippyPaths.end();
          Gdiplus::Bitmap* peekBm = inIdx ? TileBitmapCachePeek(st, key) : nullptr;
          j += "        { \"z\": ";
          j += std::to_string(st->viewZ);
          j += ", \"tx\": ";
          j += std::to_string(tx);
          j += ", \"ty\": ";
          j += std::to_string(ty);
          j += ", \"packedKeyHex\": \"";
          {
            char kh[24]{};
            snprintf(kh, sizeof(kh), "%016llx", static_cast<unsigned long long>(key));
            j += kh;
          }
          j += "\", \"inIndex\": ";
          j += inIdx ? "true" : "false";
          j += ", \"sourcePath\": ";
          if (inIdx) {
            Utf8AppendJsonEscapedString(&j, pit->second);
          } else {
            j += "null";
          }
          j += ", \"textureDecodeCached\": ";
          if (peekBm && peekBm->GetLastStatus() == Gdiplus::Ok) {
            j += "{ \"width\": ";
            j += std::to_string(peekBm->GetWidth());
            j += ", \"height\": ";
            j += std::to_string(peekBm->GetHeight());
            j += ", \"gdiplusOk\": true }";
          } else {
            j += "null";
          }
          j += ", \"gridHudRectClient\": ";
          JsonAppendRectClient(j, gridRc);
          j += ", \"rasterBlitDestClient\": ";
          if (slippyRasterFullViewportProj || IsRectEmpty(&blitRc)) {
            j += "null";
          } else {
            JsonAppendRectClient(j, blitRc);
          }
          j += ", \"xyzNativeBitmapPixelBlit\": ";
          j += (!slippyRasterFullViewportProj && st->slippyProjection == TileSlippyProjection::kWebMercatorGrid && nativeBmp)
                   ? "true"
                   : "false";
          j += ", \"bbox3857Meters\": { \"crs\": \"EPSG:3857\", \"axisModel\": \"easting_northing_typical_xyz_tile\", ";
          {
            double bx0 = 0;
            double bx1 = 0;
            double by0 = 0;
            double by1 = 0;
            SlippyTileIndexToEpsg3857BoundsMeters(st->viewZ, tx, ty, &bx0, &bx1, &by0, &by1);
            char buf[64]{};
            j += "\"minX\": ";
            snprintf(buf, sizeof(buf), "%.9g", bx0);
            j += buf;
            j += ", \"maxX\": ";
            snprintf(buf, sizeof(buf), "%.9g", bx1);
            j += buf;
            j += ", \"minY\": ";
            snprintf(buf, sizeof(buf), "%.9g", by0);
            j += buf;
            j += ", \"maxY\": ";
            snprintf(buf, sizeof(buf), "%.9g", by1);
            j += buf;
          }
          j += " }";
          j += " }";
          ++listed;
        }
      }
    }
    j += "\n      ]\n    },\n";
    j += "    \"viewportCornersWgs84\": [\n";
    for (int i = 0; i < 4; ++i) {
      const double lonN = SlippyWrapLongitudeDeg(lay.cornerLon[i]);
      const double latN = SlippyClampLatitudeDeg(lay.cornerLat[i]);
      j += "      { \"lonDeg\": ";
      char b1[64]{};
      snprintf(b1, sizeof(b1), "%.9g", lonN);
      j += b1;
      j += ", \"latDeg\": ";
      char b2[64]{};
      snprintf(b2, sizeof(b2), "%.9g", latN);
      j += b2;
      j += (i < 3) ? " },\n" : " }\n";
    }
    j += "    ],\n";
    j += "    \"viewCenterWgs84\": { \"lonDeg\": ";
    {
      char b[64]{};
      snprintf(b, sizeof(b), "%.9g", SlippyWrapLongitudeDeg(lay.viewCenterLon));
      j += b;
    }
    j += ", \"latDeg\": ";
    {
      char b[64]{};
      snprintf(b, sizeof(b), "%.9g", SlippyClampLatitudeDeg(lay.viewCenterLat));
      j += b;
    }
    j += " },\n";
    j += "    \"pixelsPerDegreeLonApprox\": ";
    {
      char b[64]{};
      snprintf(b, sizeof(b), "%.9g", lay.ppdLon);
      j += b;
    }
    j += ",\n    \"pixelsPerDegreeLatApprox\": ";
    {
      char b[64]{};
      snprintf(b, sizeof(b), "%.9g", lay.ppdLat);
      j += b;
    }
    j += ",\n    \"rPixCeaApprox\": ";
    {
      char b[64]{};
      snprintf(b, sizeof(b), "%.9g", lay.rPixCea);
      j += b;
    }
    j += ",\n";
#if GIS_DESKTOP_HAVE_GDAL
    j += "    \"displayPlaneCorners\": [\n";
    for (int i = 0; i < 4; ++i) {
      double cx = lay.dispCornerX[i];
      double cy = lay.dispCornerY[i];
      if (st->slippyProjection == TileSlippyProjection::kEquirectangular) {
        cx = SlippyWrapLongitudeDeg(cx);
        cy = SlippyClampLatitudeDeg(cy);
      }
      j += "      { \"x\": ";
      char dx[64]{};
      snprintf(dx, sizeof(dx), "%.9g", cx);
      j += dx;
      j += ", \"y\": ";
      char dy[64]{};
      snprintf(dy, sizeof(dy), "%.9g", cy);
      j += dy;
      j += (i < 3) ? " },\n" : " }\n";
    }
    j += "    ]\n";
#else
    j += "    \"displayPlaneCorners\": null\n";
#endif
    j += "  }\n";
    break;
  }
  case TilePreviewState::Mode::kSingleRaster: {
    j += "  \"mode\": \"single_raster\",\n";
    j += "  \"samplePath\": ";
    Utf8AppendJsonEscapedString(&j, st->samplePath);
    j += ",\n";
    j += "  \"hint\": ";
    Utf8AppendJsonEscapedString(&j, st->hint);
    j += ",\n";
    j += "  \"bitmap\": ";
    if (st->bmp && st->bmp->GetLastStatus() == Gdiplus::Ok) {
      j += "{ \"width\": ";
      j += std::to_string(st->bmp->GetWidth());
      j += ", \"height\": ";
      j += std::to_string(st->bmp->GetHeight());
      j += " }\n";
    } else {
      j += "null\n";
    }
    break;
  }
  case TilePreviewState::Mode::kThreeDTilesMeta:
  default: {
    j += "  \"mode\": \"three_d_tiles_meta\",\n";
    j += "  \"bvHint\": ";
    Utf8AppendJsonEscapedString(&j, st->bvHint);
    j += ",\n";
    j += "  \"hint\": ";
    Utf8AppendJsonEscapedString(&j, st->hint);
    j += "\n";
    break;
  }
  }
  j += "}\n";
  return j;
}

static bool TilePreviewCopyUtf8JsonToClipboard(HWND owner, const std::string& utf8) {
  if (utf8.empty()) {
    return false;
  }
  const int nwc = MultiByteToWideChar(CP_UTF8, 0, utf8.c_str(), static_cast<int>(utf8.size()), nullptr, 0);
  if (nwc <= 0) {
    return false;
  }
  std::wstring w(static_cast<size_t>(nwc), L'\0');
  if (MultiByteToWideChar(CP_UTF8, 0, utf8.c_str(), static_cast<int>(utf8.size()), w.data(), nwc) != nwc) {
    return false;
  }
  if (!OpenClipboard(owner)) {
    return false;
  }
  EmptyClipboard();
  const SIZE_T nbytes = (static_cast<SIZE_T>(w.size()) + 1u) * sizeof(wchar_t);
  HGLOBAL hg = GlobalAlloc(GMEM_MOVEABLE, nbytes);
  if (!hg) {
    CloseClipboard();
    return false;
  }
  void* p = GlobalLock(hg);
  if (!p) {
    GlobalFree(hg);
    CloseClipboard();
    return false;
  }
  memcpy(p, w.data(), w.size() * sizeof(wchar_t));
  static_cast<wchar_t*>(p)[w.size()] = L'\0';
  GlobalUnlock(hg);
  if (!SetClipboardData(CF_UNICODETEXT, hg)) {
    GlobalFree(hg);
    CloseClipboard();
    return false;
  }
  CloseClipboard();
  return true;
}

static void TilePreviewUpdateSlippyPointerGeo(TilePreviewState* st, const RECT& img, const RECT& cr, POINT clientPt,
                                              wchar_t* lineBuf, size_t lineCap) {
  if (!st || !lineBuf || lineCap < 64u) {
    return;
  }
  TileSlippyPaintLayout lay{};
  if (!TileSlippyBuildPaintLayout(cr, st, &lay) || lay.tooMany) {
    if (AgisGetUiLanguage() == AgisUiLanguage::kEn) {
      swprintf_s(lineBuf, lineCap, L"Client (%d, %d) | Too many visible tiles; cannot sample", clientPt.x, clientPt.y);
    } else {
      swprintf_s(lineBuf, lineCap, L"客户区 (%d, %d) | 可视块过多，无法读点", clientPt.x, clientPt.y);
    }
    return;
  }
  if (st->slippyProjection == TileSlippyProjection::kWebMercatorGrid) {
    const int iw = (std::max)(1, lay.aw);
    const int ih = (std::max)(1, lay.ah);
    const double worldW = lay.worldW;
    const double worldH = lay.worldH;
    const double worldLeft = lay.worldLeft;
    const double worldTop = lay.worldTop;
    const double tileX =
        worldLeft + static_cast<double>(clientPt.x - img.left) * worldW / static_cast<double>(iw);
    const double tileY =
        worldTop + static_cast<double>(clientPt.y - img.top) * worldH / static_cast<double>(ih);
    SlippyTileXYToLonLat(tileX, tileY, st->viewZ, &st->pointerLon, &st->pointerLat);
  } else if (st->slippyProjection == TileSlippyProjection::kEquirectangular) {
    double tileX = 0;
    double tileY = 0;
    SlippyPixelCenterToFractionalTile(img, clientPt.x, clientPt.y, lay, &tileX, &tileY);
    SlippyTileXYToLonLat(tileX, tileY, st->viewZ, &st->pointerLon, &st->pointerLat);
  } else {
    const double cx = static_cast<double>(clientPt.x) + 0.5;
    const double cy = static_cast<double>(clientPt.y) + 0.5;
    SlippyScreenToLonLatInverseCea(lay, cx, cy, &st->pointerLon, &st->pointerLat);
  }
  st->pointerGeoValid = true;
  const double ptrLonDisp = SlippyWrapLongitudeDeg(st->pointerLon);
  const double ptrLatDisp = SlippyClampLatitudeDeg(st->pointerLat);
  if (AgisGetUiLanguage() == AgisUiLanguage::kEn) {
    swprintf_s(lineBuf, lineCap,
#if GIS_DESKTOP_HAVE_GDAL
               L"Client (%d, %d) | λ %.6f° φ %.6f° (WGS84) | display %s | data EPSG:3857 | PROJ | ellipsoidal h —",
#else
               L"Client (%d, %d) | λ %.6f° φ %.6f° | display %s | data EPSG:3857 | ellipsoidal h —",
#endif
               clientPt.x, clientPt.y, ptrLonDisp, ptrLatDisp, TileSlippyProjectionShortLabel(st->slippyProjection));
  } else {
    swprintf_s(lineBuf, lineCap,
#if GIS_DESKTOP_HAVE_GDAL
               L"客户区 (%d, %d) | λ %.6f° φ %.6f°（WGS84）| 显示 %s | 数据 EPSG:3857 | PROJ | 椭球高 —",
#else
               L"客户区 (%d, %d) | λ %.6f° φ %.6f° | 显示 %s | 数据 EPSG:3857 | 椭球高 —",
#endif
               clientPt.x, clientPt.y, ptrLonDisp, ptrLatDisp, TileSlippyProjectionShortLabel(st->slippyProjection));
  }
}

static void TilePaintOneSlippyTileGeo(Gdiplus::Graphics& g, TilePreviewState* st, const TileSlippyPaintLayout& lay,
                                      int tx, int ty, TileSlippyProjection proj, Gdiplus::SolidBrush& miss) {
  const uint64_t key = PackTileKey(st->viewZ, tx, ty);
  auto pit = st->slippyPaths.find(key);
  int sxa, sya, sxb, syb, sxc, syc, sxd, syd;
  if (proj == TileSlippyProjection::kEquirectangular) {
    double cxa, cya, cxb, cyb, cxc, cyc, cxd, cyd;
    SlippyFractionalTileToClientPixelCenterEx(lay, static_cast<double>(tx), static_cast<double>(ty), &cxa, &cya, false);
    SlippyFractionalTileToClientPixelCenterEx(lay, static_cast<double>(tx + 1), static_cast<double>(ty), &cxb, &cyb, false);
    SlippyFractionalTileToClientPixelCenterEx(lay, static_cast<double>(tx), static_cast<double>(ty + 1), &cxc, &cyc, false);
    SlippyFractionalTileToClientPixelCenterEx(lay, static_cast<double>(tx + 1), static_cast<double>(ty + 1), &cxd, &cyd, false);
    sxa = static_cast<int>(std::lround(cxa));
    sya = static_cast<int>(std::lround(cya));
    sxb = static_cast<int>(std::lround(cxb));
    syb = static_cast<int>(std::lround(cyb));
    sxc = static_cast<int>(std::lround(cxc));
    syc = static_cast<int>(std::lround(cyc));
    sxd = static_cast<int>(std::lround(cxd));
    syd = static_cast<int>(std::lround(cyd));
  } else {
    const int z = st->viewZ;
    double lon0, lat0, lon1, lat1, lon2, lat2, lon3, lat3;
    SlippyTileXYToLonLat(static_cast<double>(tx), static_cast<double>(ty), z, &lon0, &lat0);
    SlippyTileXYToLonLat(static_cast<double>(tx + 1), static_cast<double>(ty), z, &lon1, &lat1);
    SlippyTileXYToLonLat(static_cast<double>(tx), static_cast<double>(ty + 1), z, &lon2, &lat2);
    SlippyTileXYToLonLat(static_cast<double>(tx + 1), static_cast<double>(ty + 1), z, &lon3, &lat3);
    SlippyLonLatToScreenCea(lay, lon0, lat0, &sxa, &sya);
    SlippyLonLatToScreenCea(lay, lon1, lat1, &sxb, &syb);
    SlippyLonLatToScreenCea(lay, lon2, lat2, &sxc, &syc);
    SlippyLonLatToScreenCea(lay, lon3, lat3, &sxd, &syd);
  }
  const int minx = (std::min)({sxa, sxb, sxc, sxd});
  const int maxx = (std::max)({sxa, sxb, sxc, sxd});
  const int miny = (std::min)({sya, syb, syc, syd});
  const int maxy = (std::max)({sya, syb, syc, syd});
  const int rw = (std::max)(1, maxx - minx);
  const int rh = (std::max)(1, maxy - miny);
  if (pit == st->slippyPaths.end()) {
    g.FillRectangle(&miss, minx, miny, rw, rh);
    return;
  }
  Gdiplus::Bitmap* bm = TileBitmapCacheGet(st, key, pit->second);
  if (bm) {
    g.DrawImage(bm, minx, miny, rw, rh);
  } else {
    g.FillRectangle(&miss, minx, miny, rw, rh);
  }
}

/// fullImg 与 mapImg 之间填中性色，避免 letterbox 条带露出窗口底或误以为是数据区。
static void TileFillSlippyLetterboxGaps(HDC hdc, const RECT& full, const RECT& inner) {
  if (!hdc || full.left >= full.right || full.top >= full.bottom) {
    return;
  }
  if (full.left == inner.left && full.right == inner.right && full.top == inner.top && full.bottom == inner.bottom) {
    return;
  }
  HBRUSH br = CreateSolidBrush(TilePreviewUiDark() ? RGB(22, 24, 30) : RGB(236, 240, 245));
  if (inner.top > full.top) {
    RECT r{full.left, full.top, full.right, inner.top};
    FillRect(hdc, &r, br);
  }
  if (inner.bottom < full.bottom) {
    RECT r{full.left, inner.bottom, full.right, full.bottom};
    FillRect(hdc, &r, br);
  }
  const int midTop = (std::max)(full.top, inner.top);
  const int midBot = (std::min)(full.bottom, inner.bottom);
  if (midBot > midTop) {
    if (inner.left > full.left) {
      RECT r{full.left, midTop, inner.left, midBot};
      FillRect(hdc, &r, br);
    }
    if (inner.right < full.right) {
      RECT r{inner.right, midTop, full.right, midBot};
      FillRect(hdc, &r, br);
    }
  }
  DeleteObject(br);
}

static void TilePaintSlippyTiles(HDC hdc, TilePreviewState* st, const TileSlippyPaintLayout& lay) {
  if (!lay.valid || lay.tooMany) {
    return;
  }
  TileFillSlippyLetterboxGaps(hdc, lay.fullImgArea, lay.imgArea);
  const RECT& imgArea = lay.imgArea;
  Gdiplus::Graphics g(hdc);
  g.SetInterpolationMode(Gdiplus::InterpolationModeLowQuality);
  g.SetSmoothingMode(Gdiplus::SmoothingModeNone);
  g.SetPixelOffsetMode(Gdiplus::PixelOffsetModeHalf);
  g.SetCompositingQuality(Gdiplus::CompositingQualityHighSpeed);
  g.SetClip(Gdiplus::Rect(static_cast<INT>(imgArea.left), static_cast<INT>(imgArea.top),
                          static_cast<INT>((std::max)(1, lay.aw)), static_cast<INT>((std::max)(1, lay.ah))));
  Gdiplus::SolidBrush miss(TilePreviewUiDark() ? Gdiplus::Color(255, 44, 48, 60) : Gdiplus::Color(255, 238, 240, 245));
  if (st->slippyProjection == TileSlippyProjection::kWebMercatorGrid) {
    for (int ty = lay.ty0; ty <= lay.ty1; ++ty) {
      for (int tx = lay.tx0; tx <= lay.tx1; ++tx) {
        const uint64_t key = PackTileKey(st->viewZ, tx, ty);
        auto pit = st->slippyPaths.find(key);
        const int sx = imgArea.left +
                       static_cast<int>(std::lround((static_cast<double>(tx) - lay.worldLeft) * st->pixelsPerTile));
        const int sy =
            imgArea.top + static_cast<int>(std::lround((static_cast<double>(ty) - lay.worldTop) * st->pixelsPerTile));
        const int sw = (std::max)(1, static_cast<int>((std::ceil)(st->pixelsPerTile)) + 1);
        if (pit == st->slippyPaths.end()) {
          g.FillRectangle(&miss, sx, sy, sw, sw);
          continue;
        }
        Gdiplus::Bitmap* bm = TileBitmapCacheGet(st, key, pit->second);
        if (bm) {
          const int iw = bm->GetWidth();
          const int ih = bm->GetHeight();
          if (iw > 0 && ih > 0 && std::abs(iw - sw) <= 1 && std::abs(ih - sw) <= 1) {
            g.DrawImage(bm, sx, sy);
          } else {
            g.DrawImage(bm, sx, sy, sw, sw);
          }
        } else {
          g.FillRectangle(&miss, sx, sy, sw, sw);
        }
      }
    }
    return;
  }
#if GIS_DESKTOP_HAVE_GDAL
  if (TileSlippyPaintProjResampled(hdc, st, lay)) {
    return;
  }
#endif
  const TileSlippyProjection gp = st->slippyProjection;
  for (int ty = lay.ty0; ty <= lay.ty1; ++ty) {
    for (int tx = lay.tx0; tx <= lay.tx1; ++tx) {
      TilePaintOneSlippyTileGeo(g, st, lay, tx, ty, gp, miss);
    }
  }
}

static void TilePaintSlippyHud(HDC hdc, TilePreviewState* st, const TileSlippyPaintLayout& lay) {
  if (!lay.valid) {
    return;
  }
  const RECT& imgArea = lay.imgArea;
  if (HFONT f = UiGetAppFont()) {
    SelectObject(hdc, f);
  }
  SetBkMode(hdc, TRANSPARENT);
  SetTextColor(hdc, TilePreviewUiDark() ? RGB(230, 234, 242) : RGB(28, 36, 52));

  wchar_t status[768]{};
  if (lay.tooMany) {
    if (AgisGetUiLanguage() == AgisUiLanguage::kEn) {
      swprintf_s(status,
                 L"View Z = %d (max Z = %d)\nToo many visible tiles (%d×%d): wheel down to lower Z or Ctrl+wheel to "
                 L"shrink tile pixels.",
                 st->viewZ, st->indexMaxZ, lay.spanX, lay.spanY);
    } else {
      swprintf_s(status, L"当前显示层级 Z = %d（金字塔最大 Z = %d）\n可视块过多 (%d×%d)：请滚轮降低 Z 或 Ctrl+滚轮缩小片元。",
                 st->viewZ, st->indexMaxZ, lay.spanX, lay.spanY);
    }
  } else if (st->uiShowSlippyMapHud) {
    const wchar_t* orangeHint = AgisPickUiLang(
        L"橘黄框（辅助）：本格 Raw 贴图矩形与地图区之交的可见段。\n",
        L"Orange box: visible part of raw tile rect intersected with the map area.\n");
    if (AgisGetUiLanguage() == AgisUiLanguage::kEn) {
      swprintf_s(status,
#if GIS_DESKTOP_HAVE_GDAL
                 L"Input: EPSG:3857 → display: %s (PROJ; non-XYZ uses per-pixel resampling)\n"
#else
                 L"Input: EPSG:3857 → display: %s (approximate blit without GDAL)\n"
#endif
                 L"Z = %d (max %d) | ~%.0f×%.0f px/tile | cols [%d..%d] rows [%d..%d] | %zu tiles indexed\n"
                 L"%s"
                 L"Drag pan | Wheel | Shift/Ctrl+wheel | Options",
                 TileSlippyProjectionLongLabel(st->slippyProjection), st->viewZ, st->indexMaxZ,
                 static_cast<double>(st->pixelsPerTile), static_cast<double>(st->pixelsPerTile), lay.tx0, lay.tx1,
                 lay.ty0, lay.ty1, st->tileCount, orangeHint);
    } else {
      swprintf_s(status,
#if GIS_DESKTOP_HAVE_GDAL
                 L"输入：EPSG:3857 → 显示：%s（PROJ 严格变换；非 XYZ 为逐像素重采样）\n"
#else
                 L"输入：EPSG:3857 → 显示：%s（无 GDAL 时为近似拼贴）\n"
#endif
                 L"Z = %d（最大 %d）| 片元约 %.0f×%.0f px | 可视列 [%d..%d] 行 [%d..%d] | 已索引 %zu 块\n"
                 L"%s"
                 L"拖拽平移 | 滚轮 | Shift/Ctrl+滚轮 | 「选项」",
                 TileSlippyProjectionLongLabel(st->slippyProjection), st->viewZ, st->indexMaxZ,
                 static_cast<double>(st->pixelsPerTile), static_cast<double>(st->pixelsPerTile), lay.tx0, lay.tx1,
                 lay.ty0, lay.ty1, st->tileCount, orangeHint);
    }
  }
  const int zPad = 6;
  const int zBandH = lay.tooMany ? 68 : (st->uiShowSlippyMapHud ? 102 : 0);
  if (lay.tooMany || st->uiShowSlippyMapHud) {
    RECT zRc{imgArea.left + zPad, imgArea.top + zPad, imgArea.right - zPad, imgArea.top + zPad + zBandH};
    if (zRc.bottom > imgArea.bottom - 10) {
      zRc.bottom = imgArea.bottom - 10;
    }
    if (zRc.top >= zRc.bottom - 8) {
      zRc.top = (std::max)(imgArea.top + 2, zRc.bottom - 40);
    }
    TileDrawTextBlockSemiBg(hdc, status, zRc, 5, 210);
  }

  if (!lay.tooMany && (st->uiShowTileGrid || st->uiShowTileLabels)) {
    const int saved = SaveDC(hdc);
    HPEN pen = CreatePen(PS_SOLID, 1, RGB(255, 120, 0));
    HPEN oldPen = static_cast<HPEN>(SelectObject(hdc, pen));
    HGDIOBJ oldBrush = SelectObject(hdc, GetStockObject(NULL_BRUSH));
    for (int ty = lay.ty0; ty <= lay.ty1; ++ty) {
      for (int tx = lay.tx0; tx <= lay.tx1; ++tx) {
        const uint64_t key = PackTileKey(st->viewZ, tx, ty);
        auto pit = st->slippyPaths.find(key);
        RECT tRc{};
        SlippyGetTileScreenBounds(st, lay, tx, ty, &tRc);
        if (IsRectEmpty(&tRc)) {
          continue;
        }
        const int sx = tRc.left;
        const int sy = tRc.top;
        const int sw = (std::max)(1, static_cast<int>(tRc.right - tRc.left));
        const int sh = (std::max)(1, static_cast<int>(tRc.bottom - tRc.top));
        if (st->uiShowTileGrid) {
          Rectangle(hdc, sx, sy, sx + sw + 1, sy + sh + 1);
        }
        if (!st->uiShowTileLabels) {
          continue;
        }
        int bw = static_cast<int>(std::lround(st->pixelsPerTile));
        int bh = bw;
        if (pit != st->slippyPaths.end()) {
          if (Gdiplus::Bitmap* bm = TileBitmapCachePeek(st, key)) {
            bw = bm->GetWidth();
            bh = bm->GetHeight();
            if (bw < 1) {
              bw = static_cast<int>(std::lround(st->pixelsPerTile));
            }
            if (bh < 1) {
              bh = bw;
            }
          }
        }
        wchar_t zxyLine[80]{};
        wchar_t dimLine[48]{};
        swprintf_s(zxyLine, L"z/x/y = %d / %d / %d", st->viewZ, tx, ty);
        if (AgisGetUiLanguage() == AgisUiLanguage::kEn) {
          swprintf_s(dimLine, L"%d×%d px", bw, bh);
        } else {
          swprintf_s(dimLine, L"%d×%d 像素", bw, bh);
        }
        const int labelRight = (std::min)(sx + 340, static_cast<int>(imgArea.right) - 2);
        RECT trZ{sx + 3, sy + 3, labelRight, sy + 20};
        TileDrawTextLineSemiBg(hdc, zxyLine, trZ, 3, 2, 200);
        RECT trD{sx + 3, sy + 20, labelRight, sy + 40};
        TileDrawTextLineSemiBg(hdc, dimLine, trD, 3, 2, 200);
      }
    }
    SelectObject(hdc, oldBrush);
    SelectObject(hdc, oldPen);
    DeleteObject(pen);
    RestoreDC(hdc, saved);
  }
}

LRESULT CALLBACK TilePreviewWndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) {
  switch (msg) {
    case WM_ERASEBKGND:
      return 1;
    case WM_CREATE: {
      auto* st = new TilePreviewState();
      st->rootPath = g_pendingTilePreviewRoot;
      g_pendingTilePreviewRoot.clear();
      SetWindowLongPtrW(hwnd, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(st));
#if GIS_DESKTOP_HAVE_GDAL
      ++g_tileSlippyPreviewOpenWndCount;
#endif
      TilePreviewLoadReport loadRep{};
      TilePreviewLoadFromPath(hwnd, st, st->rootPath, &loadRep);
      if (!st->rootPath.empty() && !loadRep.ok) {
        TilePreviewShowLoadError(hwnd, loadRep);
      }
      DragAcceptFiles(hwnd, TRUE);
      AgisSetSatelliteLangThemeMenu(hwnd);
      AgisApplyTheme(hwnd);
      return 0;
    }
    case WM_INITMENUPOPUP:
      AgisOnSatelliteLangThemeMenuPopup(hwnd, reinterpret_cast<HMENU>(wParam));
      return 0;
    case WM_SETTINGCHANGE:
      if (lParam && wcscmp(reinterpret_cast<const wchar_t*>(lParam), L"ImmersiveColorSet") == 0 &&
          g_themeMenu == AgisThemeMenu::kFollowSystem) {
        AgisApplyTheme(hwnd);
        InvalidateRect(hwnd, nullptr, FALSE);
      }
      break;
    case WM_COMMAND:
      if (HIWORD(wParam) == 0 || HIWORD(wParam) == 1) {
        if (AgisTryHandleSatelliteLangThemeMenuCommand(
                hwnd, LOWORD(wParam), AgisTilePreviewOnLanguageChanged,
                [](HWND h) { InvalidateRect(h, nullptr, FALSE); })) {
          return 0;
        }
      }
      break;
    case WM_SIZE:
      return 0;
    case WM_LBUTTONDOWN: {
      if (auto* st = reinterpret_cast<TilePreviewState*>(GetWindowLongPtrW(hwnd, GWLP_USERDATA))) {
        RECT cr{};
        GetClientRect(hwnd, &cr);
        const POINT pt{GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam)};
        const RECT openRc = TileOpenButtonRect(cr);
        if (PtInRect(&openRc, pt)) {
          PostMessageW(hwnd, kTilePreviewRequestOpenMsg, 0, 0);
          return 0;
        }
        const RECT closeRc = TileCloseButtonRect(cr);
        if (PtInRect(&closeRc, pt)) {
          PostMessageW(hwnd, WM_CLOSE, 0, 0);
          return 0;
        }
        if (TilePreviewHasLoadedPreviewContent(st)) {
          const RECT copyRc = TileCopySceneInfoButtonRect(cr, st);
          if (PtInRect(&copyRc, pt)) {
            const std::string json = TilePreviewBuildSceneJsonUtf8(hwnd, st);
            if (!TilePreviewCopyUtf8JsonToClipboard(hwnd, json)) {
              MessageBoxW(hwnd,
                          AgisPickUiLang(L"无法写入剪贴板（或 UTF-8 转换失败）。",
                                         L"Could not write the clipboard (or UTF-8 conversion failed)."),
                          AgisPickUiLang(L"瓦片预览", L"Tile preview"), MB_OK | MB_ICONWARNING);
            }
            return 0;
          }
        }
        if (st->mode == TilePreviewState::Mode::kSlippyQuadtree) {
          const RECT optBtnRc = TileOptionsButtonRect(cr);
          if (PtInRect(&optBtnRc, pt)) {
            st->showOptionsPanel = !st->showOptionsPanel;
            InvalidateRect(hwnd, nullptr, FALSE);
            return 0;
          }
          if (TileSlippyOptionsPanelHandleClick(hwnd, st, cr, pt)) {
            return 0;
          }
          st->dragging = true;
          st->lastDragPt.x = GET_X_LPARAM(lParam);
          st->lastDragPt.y = GET_Y_LPARAM(lParam);
          SetCapture(hwnd);
        }
      }
      return 0;
    }
    case WM_LBUTTONUP: {
      if (auto* st = reinterpret_cast<TilePreviewState*>(GetWindowLongPtrW(hwnd, GWLP_USERDATA))) {
        if (st->dragging) {
          st->dragging = false;
          ReleaseCapture();
          InvalidateRect(hwnd, nullptr, FALSE);
        }
      }
      return 0;
    }
    case WM_MOUSEMOVE: {
      if (auto* st = reinterpret_cast<TilePreviewState*>(GetWindowLongPtrW(hwnd, GWLP_USERDATA))) {
        const int x = GET_X_LPARAM(lParam);
        const int y = GET_Y_LPARAM(lParam);
        if (!st->tileMouseTrackingLeave) {
          TRACKMOUSEEVENT tme{};
          tme.cbSize = sizeof(tme);
          tme.dwFlags = TME_LEAVE;
          tme.hwndTrack = hwnd;
          if (TrackMouseEvent(&tme)) {
            st->tileMouseTrackingLeave = true;
          }
        }
        TilePreviewUpdatePointerGeo(st, hwnd, POINT{x, y});
        const bool draggingMap =
            (st->mode == TilePreviewState::Mode::kSlippyQuadtree && st->dragging);
        if (draggingMap) {
          InvalidateRect(hwnd, nullptr, FALSE);
        } else {
          constexpr int kQuantShift = 3;
          const int qx = x >> kQuantShift;
          const int qy = y >> kQuantShift;
          if (qx != st->lastPointerQuantX || qy != st->lastPointerQuantY) {
            st->lastPointerQuantX = qx;
            st->lastPointerQuantY = qy;
            InvalidateRect(hwnd, nullptr, FALSE);
          }
        }
        if (st->mode == TilePreviewState::Mode::kSlippyQuadtree && st->dragging) {
          RECT crDrag{};
          GetClientRect(hwnd, &crDrag);
          SlippyViewFrame vfDrag{};
          TileSlippyComputeViewFrame(crDrag, st, &vfDrag);
          const int diw = (std::max)(1, static_cast<int>(vfDrag.mapImg.right - vfDrag.mapImg.left));
          const int dih = (std::max)(1, static_cast<int>(vfDrag.mapImg.bottom - vfDrag.mapImg.top));
          const double dx = static_cast<double>(x - st->lastDragPt.x);
          const double dy = static_cast<double>(y - st->lastDragPt.y);
          st->centerTx -= dx * vfDrag.worldW / static_cast<double>(diw);
          st->centerTy -= dy * vfDrag.worldH / static_cast<double>(dih);
          st->lastDragPt.x = x;
          st->lastDragPt.y = y;
          const double lim = double(1 << st->viewZ) + 2.0;
          st->centerTx = (std::clamp)(st->centerTx, -1.0, lim);
          st->centerTy = (std::clamp)(st->centerTy, -1.0, lim);
        }
      }
      return 0;
    }
    case WM_MOUSELEAVE: {
      if (auto* st = reinterpret_cast<TilePreviewState*>(GetWindowLongPtrW(hwnd, GWLP_USERDATA))) {
        st->tileMouseTrackingLeave = false;
        st->pointerOverImage = false;
        st->pointerGeoValid = false;
        st->lastPointerQuantX = INT_MIN;
        st->lastPointerQuantY = INT_MIN;
        st->tilePointerStatusLine = AgisPickUiLang(L"鼠标已离开窗口", L"Pointer left the window");
        InvalidateRect(hwnd, nullptr, FALSE);
      }
      return 0;
    }
    case WM_DROPFILES: {
      HDROP hDrop = reinterpret_cast<HDROP>(wParam);
      auto* st = reinterpret_cast<TilePreviewState*>(GetWindowLongPtrW(hwnd, GWLP_USERDATA));
      wchar_t pathBuf[MAX_PATH * 4]{};
      const UINT n = DragQueryFileW(hDrop, 0xFFFFFFFF, nullptr, 0);
      if (st && n > 0 && DragQueryFileW(hDrop, 0, pathBuf, static_cast<UINT>(std::size(pathBuf))) > 0) {
        TilePreviewLoadReport loadRep{};
        TilePreviewLoadFromPath(hwnd, st, pathBuf, &loadRep);
        if (!loadRep.ok) {
          TilePreviewShowLoadError(hwnd, loadRep);
        }
      }
      DragFinish(hDrop);
      return 0;
    }
    case WM_MOUSEWHEEL: {
      if (auto* st = reinterpret_cast<TilePreviewState*>(GetWindowLongPtrW(hwnd, GWLP_USERDATA))) {
        if (st->mode == TilePreviewState::Mode::kSlippyQuadtree) {
          const int delta = GET_WHEEL_DELTA_WPARAM(wParam);
          const bool shift = (GetKeyState(VK_SHIFT) & 0x8000) != 0;
          const bool ctrl = (GetKeyState(VK_CONTROL) & 0x8000) != 0;
          POINT pt{GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam)};
          ScreenToClient(hwnd, &pt);
          RECT cr{};
          GetClientRect(hwnd, &cr);
          if (ctrl) {
            const float factor = delta > 0 ? 1.08f : 0.92f;
            st->pixelsPerTile = (std::clamp)(st->pixelsPerTile * factor, 48.f, 520.f);
          } else {
            const int dz = delta > 0 ? 1 : -1;
            TileZoomSlippy(st, cr, shift ? nullptr : &pt, dz);
          }
          InvalidateRect(hwnd, nullptr, FALSE);
          return 0;
        }
      }
      break;
    }
    case kTilePreviewRequestOpenMsg: {
      auto* st = reinterpret_cast<TilePreviewState*>(GetWindowLongPtrW(hwnd, GWLP_USERDATA));
      if (!st) {
        return 0;
      }
      std::wstring path;
      AgisTilePreviewProtocol proto{};
      std::wstring pickerErr;
      if (!TilePreviewShowProtocolAndPickPath(hwnd, &path, &proto, &pickerErr)) {
        if (!pickerErr.empty()) {
          MessageBoxW(hwnd, pickerErr.c_str(),
                      AgisPickUiLang(L"打开本地瓦片", L"Open local tiles"), MB_OK | MB_ICONWARNING);
        }
        return 0;
      }
      std::wstring mismatch;
      if (!TilePreviewValidatePathMatchesProtocol(proto, path, &mismatch)) {
        MessageBoxW(hwnd, mismatch.c_str(),
                    AgisPickUiLang(L"路径与协议不符", L"Path does not match protocol"), MB_OK | MB_ICONWARNING);
        return 0;
      }
      TilePreviewLoadReport loadRep{};
      TilePreviewLoadFromPath(hwnd, st, path, &loadRep);
      if (!loadRep.ok) {
        TilePreviewShowLoadError(hwnd, loadRep);
      }
      return 0;
    }
    case WM_PAINT: {
      PAINTSTRUCT ps{};
      HDC hdcWnd = BeginPaint(hwnd, &ps);
      RECT cr{};
      GetClientRect(hwnd, &cr);
      const int cw = (std::max)(1, static_cast<int>(cr.right - cr.left));
      const int ch = (std::max)(1, static_cast<int>(cr.bottom - cr.top));
      HDC hdc = CreateCompatibleDC(hdcWnd);
      HBITMAP memBmp = CreateCompatibleBitmap(hdcWnd, cw, ch);
      HBITMAP oldBmp = static_cast<HBITMAP>(SelectObject(hdc, memBmp));
      const bool uiDark = TilePreviewUiDark();
      HBRUSH bg = CreateSolidBrush(uiDark ? RGB(30, 30, 34) : RGB(252, 252, 252));
      FillRect(hdc, &cr, bg);
      DeleteObject(bg);
      if (auto* st = reinterpret_cast<TilePreviewState*>(GetWindowLongPtrW(hwnd, GWLP_USERDATA))) {
        TileSlippyPaintLayout slippyLay{};
        const bool slippyPaint =
            (st->mode == TilePreviewState::Mode::kSlippyQuadtree) && TileSlippyBuildPaintLayout(cr, st, &slippyLay);
        if (slippyPaint) {
          TilePaintSlippyTiles(hdc, st, slippyLay);
        }
        if (HFONT f = UiGetAppFont()) {
          SelectObject(hdc, f);
        }
        SetBkMode(hdc, TRANSPARENT);
        SetTextColor(hdc, uiDark ? RGB(224, 228, 236) : RGB(32, 42, 64));
        RECT openRc = TileOpenButtonRect(cr);
        HBRUSH btnBg = CreateSolidBrush(uiDark ? RGB(52, 62, 84) : RGB(232, 240, 252));
        FillRect(hdc, &openRc, btnBg);
        DeleteObject(btnBg);
        Rectangle(hdc, openRc.left, openRc.top, openRc.right, openRc.bottom);
        DrawTextW(hdc, AgisPickUiLang(L"打开…", L"Open…"), -1, &openRc, DT_CENTER | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX);
        RECT closeRc = TileCloseButtonRect(cr);
        HBRUSH closeBg = CreateSolidBrush(uiDark ? RGB(72, 42, 42) : RGB(252, 236, 236));
        FillRect(hdc, &closeRc, closeBg);
        DeleteObject(closeBg);
        Rectangle(hdc, closeRc.left, closeRc.top, closeRc.right, closeRc.bottom);
        DrawTextW(hdc, AgisPickUiLang(L"关闭", L"Close"), -1, &closeRc, DT_CENTER | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX);
        if (st->mode == TilePreviewState::Mode::kSlippyQuadtree) {
          RECT optBtnRc = TileOptionsButtonRect(cr);
          HBRUSH optBg = CreateSolidBrush(
              st->showOptionsPanel ? (uiDark ? RGB(72, 88, 120) : RGB(200, 216, 248))
                                   : (uiDark ? RGB(44, 50, 64) : RGB(236, 242, 252)));
          FillRect(hdc, &optBtnRc, optBg);
          DeleteObject(optBg);
          Rectangle(hdc, optBtnRc.left, optBtnRc.top, optBtnRc.right, optBtnRc.bottom);
          DrawTextW(hdc, AgisPickUiLang(L"选项", L"Options"), -1, &optBtnRc, DT_CENTER | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX);
        }
        if (TilePreviewHasLoadedPreviewContent(st)) {
          RECT copyBtnRc = TileCopySceneInfoButtonRect(cr, st);
          HBRUSH copyBg = CreateSolidBrush(uiDark ? RGB(44, 50, 64) : RGB(236, 242, 252));
          FillRect(hdc, &copyBtnRc, copyBg);
          DeleteObject(copyBg);
          Rectangle(hdc, copyBtnRc.left, copyBtnRc.top, copyBtnRc.right, copyBtnRc.bottom);
          DrawTextW(hdc, AgisPickUiLang(L"复制场景信息", L"Copy scene JSON"), -1, &copyBtnRc, DT_CENTER | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX);
        }
        int rightMost = static_cast<int>(closeRc.right);
        if (st->mode == TilePreviewState::Mode::kSlippyQuadtree) {
          rightMost = (std::max)(rightMost, static_cast<int>(TileOptionsButtonRect(cr).right));
        }
        if (TilePreviewHasLoadedPreviewContent(st)) {
          rightMost = (std::max)(rightMost, static_cast<int>(TileCopySceneInfoButtonRect(cr, st).right));
        }
        const int hintLeft = rightMost + 8;
        RECT hintRc{hintLeft, 8, cr.right - 10, cr.top + TilePreviewTopBarPx(st) - 6};
        if (hintRc.bottom < hintRc.top + 20) {
          hintRc.bottom = hintRc.top + 20;
        }
        {
          std::wstring panel;
          if (st->mode == TilePreviewState::Mode::kSlippyQuadtree) {
            if (st->uiShowTopHint) {
              panel = TilePreviewSlippyTopLineW() + L"\n\n" + st->hint;
            } else {
              panel = st->hint.empty() ? std::wstring(AgisPickUiLang(L"顶栏说明已关闭（「选项」可再打开）。",
                                                                     L"Top hints are off (enable in Options)."))
                                       : st->hint;
            }
          } else {
            // 未加载/已加载均不再在顶栏固定展示「打开方式 / 支持的数据」长文；仅显示当前 hint（如错误说明、单图说明等）。
            panel = st->hint;
          }
          if (!panel.empty()) {
            TileDrawTextBlockSemiBg(hdc, panel.c_str(), hintRc, 6, 218);
          }
        }
        if (slippyPaint) {
          TilePaintSlippyHud(hdc, st, slippyLay);
        }
        TilePaintSlippyOptionsPanel(hdc, cr, st);
        if (st->mode != TilePreviewState::Mode::kSlippyQuadtree && st->bmp &&
            st->bmp->GetLastStatus() == Gdiplus::Ok) {
          const RECT imgArea = TileSlippyImageArea(cr, st);
          const int aw = (std::max)(1, static_cast<int>(imgArea.right - imgArea.left));
          const int ah = (std::max)(1, static_cast<int>(imgArea.bottom - imgArea.top));
          const int iw = st->bmp->GetWidth();
          const int ih = st->bmp->GetHeight();
          if (iw > 0 && ih > 0) {
            Gdiplus::Graphics g(hdc);
            const float scale = (std::min)(static_cast<float>(aw) / static_cast<float>(iw),
                                           static_cast<float>(ah) / static_cast<float>(ih));
            const int dw = static_cast<int>(static_cast<float>(iw) * scale);
            const int dh = static_cast<int>(static_cast<float>(ih) * scale);
            const int dx = imgArea.left + (aw - dw) / 2;
            const int dy = imgArea.top + (ah - dh) / 2;
            g.SetInterpolationMode(Gdiplus::InterpolationModeHighQualityBicubic);
            g.DrawImage(st->bmp.get(), dx, dy, dw, dh);
          }
        }
        RECT sbr = TilePreviewStatusBarRect(cr);
        TileFillRectSemi(hdc, sbr.left, sbr.top, sbr.right - sbr.left, sbr.bottom - sbr.top, 228, 16, 20, 28);
        SetBkMode(hdc, TRANSPARENT);
        SetTextColor(hdc, RGB(230, 234, 242));
        if (HFONT f2 = UiGetAppFont()) {
          SelectObject(hdc, f2);
        }
        RECT tbr = sbr;
        InflateRect(&tbr, -10, -3);
        DrawTextW(hdc, st->tilePointerStatusLine.c_str(), -1, &tbr,
                  DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX | DT_END_ELLIPSIS);
      }
      BitBlt(hdcWnd, 0, 0, cw, ch, hdc, 0, 0, SRCCOPY);
      SelectObject(hdc, oldBmp);
      DeleteObject(memBmp);
      DeleteDC(hdc);
      EndPaint(hwnd, &ps);
      return 0;
    }
    case WM_CLOSE:
      DestroyWindow(hwnd);
      return 0;
    case WM_DESTROY:
      if (auto* st = reinterpret_cast<TilePreviewState*>(GetWindowLongPtrW(hwnd, GWLP_USERDATA))) {
        delete st;
        SetWindowLongPtrW(hwnd, GWLP_USERDATA, 0);
      }
#if GIS_DESKTOP_HAVE_GDAL
      if (g_tileSlippyPreviewOpenWndCount > 0) {
        --g_tileSlippyPreviewOpenWndCount;
      }
      if (g_tileSlippyPreviewOpenWndCount == 0) {
        TileSlippyProjDestroyForTilePreview();
      }
#endif
      return 0;
    default:
      break;
  }
  return DefWindowProcW(hwnd, msg, wParam, lParam);
}

/// 切换界面语言后刷新主说明区 / 底部状态条文案（不重扫磁盘瓦片索引）。
static void TilePreviewRebuildLocalizedStrings(HWND hwnd, TilePreviewState* st) {
  if (!hwnd || !st) {
    return;
  }
  st->tilePointerStatusLine = AgisPickUiLang(L"移动鼠标查看坐标", L"Move the pointer to see coordinates");
  TilePreviewUpdatePointerGeo(st, hwnd, st->lastPointerClient);

  if (st->rootPath.empty()) {
    st->hint = AgisPickUiLang(
        L"请使用「Open…」、拖放或命令行传入本机路径：XYZ/TMS 目录、单张栅格、本地 tileset.json（3D Tiles 元数据）、或 .mbtiles/.gpkg（需 GDAL）。不支持 http(s)/WMTS 等网络地址。",
        L"Use Open…, drag-drop, or a command-line path: XYZ/TMS folder, single raster, local tileset.json (3D Tiles "
        L"metadata), or .mbtiles/.gpkg (needs GDAL). http(s)/WMTS and other network URLs are not supported.");
    return;
  }

  if (st->mode == TilePreviewState::Mode::kSlippyQuadtree && st->tileCount >= 1) {
    std::wostringstream hs;
    if (AgisGetUiLanguage() == AgisUiLanguage::kEn) {
      hs << L"[Slippy quadtree / XYZ] Indexed " << st->tileCount
         << L" tiles. Drag to pan, wheel changes Z, Ctrl+wheel changes tile pixel size.";
#if GIS_DESKTOP_HAVE_GDAL
      hs << L"\nWith GDAL: equirectangular / equal-area display uses PROJ (display CRS → EPSG:3857) per-pixel resampling.";
#endif
    } else {
      hs << L"【平面四叉树 / XYZ】已索引 " << st->tileCount
         << L" 个图块。拖拽平移，滚轮换级，Ctrl+滚轮改片元尺度。"
#if GIS_DESKTOP_HAVE_GDAL
         << L"\n启用 GDAL 时：等经纬 / 等积圆柱显示由 PROJ（显示 CRS→EPSG:3857）逐像素重采样。"
#endif
          ;
    }
    st->hint = hs.str();
    return;
  }

  if (st->mode == TilePreviewState::Mode::kThreeDTilesMeta) {
    if (const auto tilesetPathOpt = FindTilesetJsonPath(st->rootPath)) {
      st->hint = BuildThreeDTilesDashboard(st->rootPath, *tilesetPathOpt, st->bvHint);
    }
    return;
  }

  if (!st->samplePath.empty() && st->bmp && st->bmp->GetLastStatus() == Gdiplus::Ok) {
    st->hint = std::wstring(AgisPickUiLang(L"单图采样预览：\n", L"Single-image sample preview:\n")) + st->samplePath;
    return;
  }

  TilePreviewLoadFromPath(hwnd, st, st->rootPath, nullptr);
}

void AgisTilePreviewOnLanguageChanged(HWND hwnd) {
  SetWindowTextW(hwnd, AgisPickUiLang(L"本地瓦片预览 · XYZ / 3D Tiles 元数据",
                                       L"Local tile preview · XYZ / 3D Tiles metadata"));
  if (auto* st = reinterpret_cast<TilePreviewState*>(GetWindowLongPtrW(hwnd, GWLP_USERDATA))) {
    TilePreviewRebuildLocalizedStrings(hwnd, st);
  }
  InvalidateRect(hwnd, nullptr, FALSE);
}

void OpenTileRasterPreviewWindow(HWND owner, const std::wstring& path) {
  g_pendingTilePreviewRoot = path;
  const DWORD exStyle = owner ? WS_EX_TOOLWINDOW : 0;
  HWND tw = CreateWindowExW(
      exStyle, kTilePreviewClass,
      AgisPickUiLang(L"本地瓦片预览 · XYZ / 3D Tiles 元数据", L"Local tile preview · XYZ / 3D Tiles metadata"),
      WS_OVERLAPPEDWINDOW | WS_CLIPCHILDREN, CW_USEDEFAULT, CW_USEDEFAULT, 960, 720, owner,
                            nullptr, GetModuleHandleW(nullptr), nullptr);
  if (tw) {
    AgisCenterWindowInMonitorWorkArea(tw, owner ? owner : g_hwndMain);
    ShowWindow(tw, SW_SHOW);
  }
}

