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

#include <windows.h>
#include <windowsx.h>
#include <commctrl.h>
#include <commdlg.h>
#include <shellapi.h>

#include "app/preview/tile_preview/tile_preview_protocol_picker.h"
#include <gdiplus.h>

#include "common/ui/ui_font.h"
#include "common/app_core/main_app.h"
#include "common/app_core/main_globals.h"

#ifndef GIS_DESKTOP_HAVE_GDAL
#define GIS_DESKTOP_HAVE_GDAL 0
#endif
#if GIS_DESKTOP_HAVE_GDAL
#include "common/runtime/agis_gdal_runtime_env.h"
#include <cpl_error.h>
#include <gdal.h>
#include <gdal_priv.h>
#include <ogrsf_frmts.h>
#endif

// --- 瓦片：平面四叉树 (slippy z/x/y) + 3D Tiles BVH/体积元数据（tileset.json 根 region）---

/// Slippy 平面预览的屏幕展开方式（瓦片内容仍为 Web Mercator 纹理；非网格模式按经纬度或 sinφ 摆放，属示意拼贴）。
enum class TileSlippyProjection : uint8_t {
  /// 等比例经纬度（Plate Carrée）：同一 ° 经、° 纬在屏幕上等长。
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
    return L"等经纬";
  case TileSlippyProjection::kWebMercatorGrid:
    return L"XYZ 网格";
  case TileSlippyProjection::kCylindricalEqualArea:
    return L"等积圆柱";
  default:
    return L"";
  }
}

enum class TileSampleResult { kOk, kNoRaster, kContainerUnsupported };

struct TileFindResult {
  TileSampleResult code = TileSampleResult::kNoRaster;
  std::wstring path;
};

static bool IsRasterTileExtension(const std::wstring& ext) {
  return _wcsicmp(ext.c_str(), L".png") == 0 || _wcsicmp(ext.c_str(), L".jpg") == 0 ||
         _wcsicmp(ext.c_str(), L".jpeg") == 0 || _wcsicmp(ext.c_str(), L".webp") == 0 ||
         _wcsicmp(ext.c_str(), L".bmp") == 0;
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
        *diagOut = L"路径无法转为 UTF-8。";
      }
      return nullptr;
    }
    utf8.assign(static_cast<size_t>(n), '\0');
    WideCharToMultiByte(CP_UTF8, 0, pathW.c_str(), static_cast<int>(pathW.size()), utf8.data(), n, nullptr, nullptr);
  }
  GDALDatasetH ds = GDALOpenEx(utf8.c_str(), GDAL_OF_RASTER | GDAL_OF_READONLY, nullptr, nullptr, nullptr);
  if (!ds) {
    if (diagOut) {
      *diagOut = L"GDAL 无法以栅格方式打开该文件（驱动缺失、需 PROJ/GDAL_DATA，或不是平铺栅格内容）。";
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
      *diagOut = L"数据集无有效栅格尺寸。";
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
      *diagOut = L"RasterIO 读缩略图失败（波段类型可能非 Byte，或仅为矢量 GeoPackage）。";
    }
    return nullptr;
  }
  auto bmp = std::make_unique<Gdiplus::Bitmap>(outW, outH, PixelFormat24bppRGB);
  if (!bmp || bmp->GetLastStatus() != Gdiplus::Ok) {
    return nullptr;
  }
  Gdiplus::BitmapData bd{};
  Gdiplus::Rect r(0, 0, outW, outH);
  if (bmp->LockBits(&r, Gdiplus::ImageLockModeWrite, PixelFormat24bppRGB, &bd) != Gdiplus::Ok) {
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
    std::wstring w = L"【3D Tiles】无法读取 tileset.json。\n路径：\n" + tilesetJsonW;
    return w;
  }
  std::wstring dash = L"【3D Tiles · 元数据预览】\n";
  dash += L"AGIS 内建说明与目录扫描（不加载 glTF/b3dm 网格）。完整浏览请用 Cesium 或「系统默认打开」。\n";
  dash += L"对接 C++ 运行时请参考仓库 3rdparty/README-CESIUM-NATIVE.md（cesium-native 源码已在 3rdparty/cesium-native-*）。\n\n";
  if (!bvHintLines.empty()) {
    dash += bvHintLines;
    dash += L"\n\n";
  }
  std::string aver;
  if (TryExtractTilesetAssetVersion(raw, &aver)) {
    dash += L"asset.version（粗解析）: ";
    dash += Utf8JsonToWide(aver);
    dash += L"\n";
  }
  double ge = 0;
  if (TryParseFirstDoubleAfterKey(raw, "\"geometricError\"", &ge)) {
    dash += L"首个 geometricError（粗解析，多为根节点）: ";
    dash += std::to_wstring(ge);
    dash += L"\n";
  }
  const auto contentDir = ThreeDTilesContentDirectory(rootW, tilesetJsonW);
  ThreeDTilesContentStats st{};
  ScanThreeDTilesPayloadFiles(contentDir, &st);
  const size_t tileFiles = st.b3dm + st.i3dm + st.pnts + st.cmpt;
  dash += L"\n内容瓦片文件（子目录扫描≤12000，按扩展名计数）：\n";
  dash += L"  b3dm=" + std::to_wstring(st.b3dm) + L" i3dm=" + std::to_wstring(st.i3dm) + L" pnts=" +
          std::to_wstring(st.pnts) + L" cmpt=" + std::to_wstring(st.cmpt) + L"\n";
  dash += L"  glb=" + std::to_wstring(st.glb) + L" gltf=" + std::to_wstring(st.gltf) + L"\n";
  if (tileFiles == 0 && st.glb == 0 && st.gltf == 0) {
    dash += L"（未见常见载荷扩展名：可能仅外链 URL、或路径不在当前目录树下。）\n";
  }
  std::vector<std::string> uris;
  CollectSampleUris(raw, 8, &uris);
  if (!uris.empty()) {
    dash += L"\ntileset 内 uri 抽样（至多 8 条，去重）：\n";
    for (const auto& u : uris) {
      dash += L"  · ";
      dash += Utf8JsonToWide(u);
      dash += L"\n";
    }
  }
  dash += L"\n根目录/内容根：\n  ";
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
    return L"(tileset.json 无法读取)";
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
  wo << L"【BVH / 3D Tiles】Cesium 瓦片树为层次包围体；根节点常用 region/box。\n";
  if (haveReg) {
    wo << L"根 region→经纬度(°): W=" << west << L" S=" << south << L" E=" << east << L" N=" << north;
    wo << L" ；高程约(m) zmin=" << zminM << L" zmax=" << zmaxM << L"\n";
  } else {
    wo << L"未解析到标准数字 region 数组（可能被压缩或格式非预期）。\n";
  }
  wo << L"子树提示: \"children\" 出现 " << childHits
     << L" 次。\n【八叉树】对 3D box 体积的八分细分常见于嵌套子 tile；本预览不解析子网格、不渲染 glTF/b3dm，完整三维请用 "
        L"Cesium/系统打开。";
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

  TileSlippyProjection slippyProjection = TileSlippyProjection::kEquirectangular;
  bool showOptionsPanel = false;
  bool uiShowTopHint = true;
  bool uiShowSlippyMapHud = true;
  bool uiShowTileGrid = true;
  bool uiShowTileLabels = true;
};

static constexpr size_t kTilePreviewBitmapCacheMax = 160;
static constexpr UINT kTilePreviewRequestOpenMsg = WM_APP + 301;

static std::wstring g_pendingTilePreviewRoot;

/// 底部单行状态条高度（半透明信息栏）。
static constexpr int kTilePreviewBottomStatus = 28;

/// 顶部说明区高度：随模式变化（含「打开方式」+ 动态 hint）。
/// Slippy 地图模式仅保留一行快捷说明 + 动态 hint，避免固定 212px 把地图挤到窗口下半区。
static int TilePreviewTopBarPx(const TilePreviewState* st) {
  if (!st) {
    return 212;
  }
  switch (st->mode) {
  case TilePreviewState::Mode::kThreeDTilesMeta:
    return 288;
  case TilePreviewState::Mode::kSlippyQuadtree:
    return st->uiShowTopHint ? 72 : 40;
  default:
    return 212;
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
    const RECT img = TileSlippyImageArea(cr, st);
    if (!PtInRect(&img, clientPt)) {
      swprintf_s(line, L"客户区 (%d, %d) | 将鼠标移入地图区域查看经纬度", clientPt.x, clientPt.y);
      st->tilePointerStatusLine = line;
      return;
    }
    st->pointerOverImage = true;
    TilePreviewUpdateSlippyPointerGeo(st, img, cr, clientPt, line, std::size(line));
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
      swprintf_s(line, L"客户区 (%d, %d) | 图像像素 (%d, %d) | 源图尺寸 %d×%d（无地理参照）", clientPt.x, clientPt.y, px, py, iw, ih);
    } else {
      swprintf_s(line, L"客户区 (%d, %d) | 将鼠标移入缩略图区域查看像素坐标", clientPt.x, clientPt.y);
    }
    st->tilePointerStatusLine = line;
    return;
  }

  if (st->mode == TilePreviewState::Mode::kThreeDTilesMeta) {
    swprintf_s(line, L"客户区 (%d, %d) | 3D Tiles 元数据模式：无平面地图坐标（见上方 BVH/层级说明）", clientPt.x, clientPt.y);
    st->tilePointerStatusLine = line;
    return;
  }

  swprintf_s(line, L"客户区 (%d, %d)", clientPt.x, clientPt.y);
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
  TileFillRectSemi(hdc, bgRc.left, bgRc.top, bgRc.right - bgRc.left, bgRc.bottom - bgRc.top, bgAlpha, 250, 252, 255);
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
  TileFillRectSemi(hdc, tr.left, tr.top, tr.right - tr.left, tr.bottom - tr.top, bgAlpha, 252, 254, 255);
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
static Gdiplus::Bitmap* TileBitmapCachePeek(TilePreviewState* st, uint64_t key) {
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
  const RECT img = TileSlippyImageArea(clientCr, st);
  const int aw = (std::max)(1, static_cast<int>(img.right - img.left));
  const int ah = (std::max)(1, static_cast<int>(img.bottom - img.top));
  POINT focusPt{};
  if (cursorClient && PtInRect(&img, *cursorClient)) {
    focusPt = *cursorClient;
  } else {
    focusPt.x = img.left + aw / 2;
    focusPt.y = img.top + ah / 2;
  }
  const float ppt = st->pixelsPerTile;
  const double worldW = static_cast<double>(aw) / static_cast<double>(ppt);
  const double worldH = static_cast<double>(ah) / static_cast<double>(ppt);
  const double worldLeft = st->centerTx - worldW * 0.5;
  const double worldTop = st->centerTy - worldH * 0.5;
  const int dim0 = 1 << st->viewZ;
  const double focX = static_cast<double>(focusPt.x - img.left);
  const double focY = static_cast<double>(focusPt.y - img.top);
  double nu = (worldLeft + focX / static_cast<double>(ppt)) / static_cast<double>((std::max)(1, dim0));
  double nv = (worldTop + focY / static_cast<double>(ppt)) / static_cast<double>((std::max)(1, dim0));
  nu = (std::clamp)(nu, 0.0, 1.0);
  nv = (std::clamp)(nv, 0.0, 1.0);
  st->viewZ = newZ;
  const int dim1 = 1 << newZ;
  st->centerTx = nu * static_cast<double>(dim1) + worldW * 0.5 - focX / static_cast<double>(ppt);
  st->centerTy = nv * static_cast<double>(dim1) + worldH * 0.5 - focY / static_cast<double>(ppt);
  const double lim = static_cast<double>(1 << newZ) + 4.0;
  st->centerTx = (std::clamp)(st->centerTx, -2.0, lim);
  st->centerTy = (std::clamp)(st->centerTy, -2.0, lim);
  TilePruneBitmapCacheNotAtZ(st, newZ);
}

static RECT TileOpenButtonRect(RECT cr) {
  return RECT{cr.left + 12, cr.top + 10, cr.left + 92, cr.top + 34};
}

static RECT TileOptionsButtonRect(RECT cr) {
  return RECT{cr.left + 100, cr.top + 10, cr.left + 186, cr.top + 34};
}

static RECT TileSlippyOptionsPanelRect(RECT cr) {
  constexpr int kPanelW = 296;
  constexpr int kPanelH = 232;
  constexpr int kMargin = 12;
  const int top = cr.top + 42;
  return {cr.right - kPanelW - kMargin, top, cr.right - kMargin, top + kPanelH};
}

static void TilePaintSlippyOptionsPanel(HDC hdc, RECT cr, TilePreviewState* st) {
  if (!st || !st->showOptionsPanel || st->mode != TilePreviewState::Mode::kSlippyQuadtree) {
    return;
  }
  const RECT prc = TileSlippyOptionsPanelRect(cr);
  TileFillRectSemi(hdc, prc.left, prc.top, prc.right - prc.left, prc.bottom - prc.top, 250, 252, 254, 255);
  HBRUSH frameBr = CreateSolidBrush(RGB(186, 198, 218));
  FrameRect(hdc, &prc, frameBr);
  DeleteObject(frameBr);
  if (HFONT f = UiGetAppFont()) {
    SelectObject(hdc, f);
  }
  SetBkMode(hdc, TRANSPARENT);
  SetTextColor(hdc, RGB(28, 36, 52));
  RECT titleRc{prc.left + 10, prc.top + 6, prc.right - 10, prc.top + 24};
  DrawTextW(hdc, L"显示与投影", -1, &titleRc, DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX);
  static const wchar_t* kProjPickLabel[] = {L"等比例经纬度（Plate Carrée）", L"XYZ 片元线性（Web Mercator 网格）",
                                            L"等积圆柱（Lambert · sin φ）"};
  for (int i = 0; i < kTileSlippyProjectionCount; ++i) {
    wchar_t line[180]{};
    const bool sel = static_cast<int>(st->slippyProjection) == i;
    swprintf_s(line, L"%s  %s", sel ? L"●" : L"○", kProjPickLabel[i]);
    RECT rowRc{prc.left + 10, prc.top + 26 + i * 24, prc.right - 8, prc.top + 26 + i * 24 + 22};
    DrawTextW(hdc, line, -1, &rowRc, DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX);
  }
  RECT secRc{prc.left + 10, prc.top + 104, prc.right - 8, prc.top + 122};
  DrawTextW(hdc, L"界面元素", -1, &secRc, DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX);
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
  drawCheckRow(prc.top + 124, st->uiShowTopHint, L"顶栏说明（打开方式 / 提示）");
  drawCheckRow(prc.top + 148, st->uiShowSlippyMapHud, L"地图内 HUD（层级与操作说明）");
  drawCheckRow(prc.top + 172, st->uiShowTileGrid, L"瓦片网格线");
  drawCheckRow(prc.top + 196, st->uiShowTileLabels, L"瓦片标注（z/x/y 与像素尺寸）");
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
  if (relY >= 26 && relY < 26 + 24 * kTileSlippyProjectionCount && relX >= 8) {
    const int i = (relY - 26) / 24;
    if (i >= 0 && i < kTileSlippyProjectionCount) {
      st->slippyProjection = static_cast<TileSlippyProjection>(i);
      changed = true;
    }
  } else if (relX >= 10 && relX < 260) {
    auto toggleRow = [&](int y0, bool* flag) {
      if (pt.y >= prc.top + y0 && pt.y < prc.top + y0 + 20) {
        *flag = !*flag;
        changed = true;
      }
    };
    toggleRow(124, &st->uiShowTopHint);
    toggleRow(148, &st->uiShowSlippyMapHud);
    toggleRow(172, &st->uiShowTileGrid);
    toggleRow(196, &st->uiShowTileLabels);
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
  const wchar_t* cap = rep.errorTitle.empty() ? L"瓦片预览" : rep.errorTitle.c_str();
  MessageBoxW(owner, rep.errorText.c_str(), cap, MB_OK | MB_ICONWARNING);
}

static const wchar_t kTilePreviewOpenMethods[] =
    L"【打开方式】\n"
    L"· 命令行：AGIS-TilePreview.exe <文件夹或单文件路径>\n"
    L"· 窗口左上角「Open…」浏览选择\n"
    L"· 将文件或文件夹拖入本窗口（拖放）\n"
    L"· 从工作台等入口带路径参数启动\n"
    L"\n"
    L"【支持的数据】\n"
    L"· XYZ / TMS 瓦片根目录（如 {z}/{x}/{y}.png；可含 tilejson.json）\n"
    L"· tileset.json（3D Tiles：BVH / 元数据面板）\n"
    L"· 单张栅格 PNG / JPG / JPEG / WebP / BMP（缩放置中）\n"
    L"· .mbtiles / .gpkg（需 GDAL：全球拼图缩略预览）";

/// Slippy 顶栏一行：完整「打开方式」见其它模式或产品文档；此处为地图区让出高度。
static const wchar_t kTilePreviewSlippyTopLine[] =
    L"Open… 或拖入瓦片根目录。滚轮：Z 层级；Shift+滚轮：视口中心；Ctrl+滚轮：片元像素；拖拽平移。";

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
  st->tilePointerStatusLine = L"移动鼠标查看坐标";
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
    st->hint = L"【操作提示】地图模式：滚轮切换 Z（光标中心；Shift+滚轮以视口中心）；Ctrl+滚轮调整片元像素尺寸；拖拽平移。";
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
        hs << L"【MBTiles / GeoPackage】GDAL 栅格缩略预览（全球拼图下采样至 <=2048px）。\n路径：\n"
           << st->rootPath;
        if (!gdalDiag.empty()) {
          hs << L"\n" << gdalDiag;
        }
        st->hint = hs.str();
        InvalidateRect(hwnd, nullptr, FALSE);
        markOk();
        return;
      }
      markFail(L"MBTiles / GeoPackage 预览失败",
               L"【MBTiles / GeoPackage】GDAL 预览失败：\n" + gdalDiag +
                   L"\n请检查 GDAL/PROJ/gdal_data 配置，或导出 XYZ 目录后再预览。");
      return;
    }
  }
#else
  if (std::filesystem::is_regular_file(rootFs, ecPath)) {
    const std::wstring ext = rootFs.extension().wstring();
    if (_wcsicmp(ext.c_str(), L".mbtiles") == 0 || _wcsicmp(ext.c_str(), L".gpkg") == 0) {
      markFail(L"构建未启用 GDAL",
               L"当前构建未启用 GDAL，无法预览 .mbtiles / .gpkg。\n请使用带 GDAL 的构建，或改为打开 XYZ 瓦片目录 / 单张栅格图。");
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
    st->mode = TilePreviewState::Mode::kSlippyQuadtree;
    st->viewZ = (std::min)(st->indexMaxZ, 6);
    const double sz = double(1u << st->viewZ);
    st->centerTx = sz * 0.5;
    st->centerTy = sz * 0.5;
    std::wostringstream hs;
    hs << L"【平面四叉树 / XYZ】已索引 " << st->tileCount << L" 个图块。拖拽平移，滚轮换级，Ctrl+滚轮改片元尺度。";
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
    markFail(L"不支持的容器",
             L"单文件 MBTiles / GeoPackage 不能直接解码为交互瓦片。\n请系统默认打开或导出 XYZ 后预览。");
    return;
  }
  if (found.code == TileSampleResult::kOk && !found.path.empty()) {
    st->samplePath = found.path;
    auto loaded = std::make_unique<Gdiplus::Bitmap>(st->samplePath.c_str());
    if (loaded && loaded->GetLastStatus() == Gdiplus::Ok) {
      st->bmp = std::move(loaded);
      st->hint = L"单图采样预览：\n" + st->samplePath;
      InvalidateRect(hwnd, nullptr, FALSE);
      markOk();
      return;
    }
    markFail(L"栅格加载失败", L"无法加载栅格文件（GDI+ 解码失败）：\n" + st->samplePath);
    return;
  }

  markFail(L"无法预览",
           L"未找到可预览的内容：\n"
           L"· 目录下无 z/x/y（或 TMS）瓦片结构；且\n"
           L"· 无 tileset.json / 有效 .json；且\n"
           L"· 未找到可读的 PNG/JPG/WebP/BMP 栅格。\n"
           L"请确认路径正确，或通过「Open…」按协议重新选择。");
}

/// Slippy 绘制布局（瓦片层与 HUD 层共用；分阶段绘制以保证 Z 序：瓦片 → 顶栏 UI → 地图 HUD → 底栏）。
struct TileSlippyPaintLayout {
  RECT imgArea{};
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
  /// 每度经纬在屏幕上的像素（Plate Carrée / 等积圆柱的经向尺度）。
  double ppdEq = 0.0;
  /// 等积圆柱：R_pix = ppdEq * 180/π，用于 (sin φ - sin φ₀) 项。
  double rPixCea = 0.0;
};

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
  SlippyTileXYToLonLat(st->centerTx, st->centerTy, z, &lay->viewCenterLon, &lay->viewCenterLat);
  lay->ppdEq = static_cast<double>(st->pixelsPerTile) * static_cast<double>(lay->dim) / 360.0;
  lay->rPixCea = lay->ppdEq * (180.0 / kTileGeoPi);
  lay->imgCenterX = static_cast<double>(lay->imgArea.left + lay->imgArea.right) * 0.5;
  lay->imgCenterY = static_cast<double>(lay->imgArea.top + lay->imgArea.bottom) * 0.5;
}

static void SlippyLonLatToScreenEq(const TileSlippyPaintLayout& lay, double lonDeg, double latDeg, int* sx, int* sy) {
  if (!sx || !sy) {
    return;
  }
  *sx = static_cast<int>(std::lround(lay.imgCenterX + (lonDeg - lay.viewCenterLon) * lay.ppdEq));
  *sy = static_cast<int>(std::lround(lay.imgCenterY - (latDeg - lay.viewCenterLat) * lay.ppdEq));
}

static void SlippyLonLatToScreenCea(const TileSlippyPaintLayout& lay, double lonDeg, double latDeg, int* sx, int* sy) {
  if (!sx || !sy) {
    return;
  }
  const double latR = latDeg * kTileGeoPi / 180.0;
  const double cLatR = lay.viewCenterLat * kTileGeoPi / 180.0;
  *sx = static_cast<int>(std::lround(lay.imgCenterX + (lonDeg - lay.viewCenterLon) * lay.ppdEq));
  *sy = static_cast<int>(std::lround(lay.imgCenterY - (std::sin(latR) - std::sin(cLatR)) * lay.rPixCea));
}

static void SlippyGetTileScreenBounds(const TilePreviewState* st, const TileSlippyPaintLayout& lay, int tx, int ty, RECT* out) {
  if (!st || !out) {
    return;
  }
  if (st->slippyProjection == TileSlippyProjection::kWebMercatorGrid) {
    const RECT& imgArea = lay.imgArea;
    const int sx = imgArea.left +
                   static_cast<int>(std::lround((static_cast<double>(tx) - lay.worldLeft) * st->pixelsPerTile));
    const int sy =
        imgArea.top + static_cast<int>(std::lround((static_cast<double>(ty) - lay.worldTop) * st->pixelsPerTile));
    const int sw = (std::max)(1, static_cast<int>(std::ceil(st->pixelsPerTile)) + 1);
    *out = {sx, sy, sx + sw, sy + sw};
    return;
  }
  const int z = st->viewZ;
  double lon0, lat0, lon1, lat1, lon2, lat2, lon3, lat3;
  SlippyTileXYToLonLat(static_cast<double>(tx), static_cast<double>(ty), z, &lon0, &lat0);
  SlippyTileXYToLonLat(static_cast<double>(tx + 1), static_cast<double>(ty), z, &lon1, &lat1);
  SlippyTileXYToLonLat(static_cast<double>(tx), static_cast<double>(ty + 1), z, &lon2, &lat2);
  SlippyTileXYToLonLat(static_cast<double>(tx + 1), static_cast<double>(ty + 1), z, &lon3, &lat3);
  int sxa, sya, sxb, syb, sxc, syc, sxd, syd;
  if (st->slippyProjection == TileSlippyProjection::kEquirectangular) {
    SlippyLonLatToScreenEq(lay, lon0, lat0, &sxa, &sya);
    SlippyLonLatToScreenEq(lay, lon1, lat1, &sxb, &syb);
    SlippyLonLatToScreenEq(lay, lon2, lat2, &sxc, &syc);
    SlippyLonLatToScreenEq(lay, lon3, lat3, &sxd, &syd);
  } else {
    SlippyLonLatToScreenCea(lay, lon0, lat0, &sxa, &sya);
    SlippyLonLatToScreenCea(lay, lon1, lat1, &sxb, &syb);
    SlippyLonLatToScreenCea(lay, lon2, lat2, &sxc, &syc);
    SlippyLonLatToScreenCea(lay, lon3, lat3, &sxd, &syd);
  }
  const int minx = (std::min)({sxa, sxb, sxc, sxd});
  const int maxx = (std::max)({sxa, sxb, sxc, sxd});
  const int miny = (std::min)({sya, syb, syc, syd});
  const int maxy = (std::max)({sya, syb, syc, syd});
  *out = {minx, miny, (std::max)(minx + 1, maxx), (std::max)(miny + 1, maxy)};
}

static bool TileSlippyBuildPaintLayout(RECT cr, TilePreviewState* st, TileSlippyPaintLayout* out) {
  if (!st || !out || st->slippyPaths.empty()) {
    return false;
  }
  out->imgArea = TileSlippyImageArea(cr, st);
  out->aw = (std::max)(1, static_cast<int>(out->imgArea.right - out->imgArea.left));
  out->ah = (std::max)(1, static_cast<int>(out->imgArea.bottom - out->imgArea.top));
  out->worldW = static_cast<double>(out->aw) / static_cast<double>(st->pixelsPerTile);
  out->worldH = static_cast<double>(out->ah) / static_cast<double>(st->pixelsPerTile);
  out->worldLeft = st->centerTx - out->worldW * 0.5;
  out->worldTop = st->centerTy - out->worldH * 0.5;
  out->dim = 1 << st->viewZ;
  out->tx0 = static_cast<int>(std::floor(out->worldLeft));
  out->ty0 = static_cast<int>(std::floor(out->worldTop));
  out->tx1 = static_cast<int>(std::ceil(out->worldLeft + out->worldW));
  out->ty1 = static_cast<int>(std::ceil(out->worldTop + out->worldH));
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

static void TilePreviewUpdateSlippyPointerGeo(TilePreviewState* st, const RECT& img, const RECT& cr, POINT clientPt,
                                              wchar_t* lineBuf, size_t lineCap) {
  if (!st || !lineBuf || lineCap < 64u) {
    return;
  }
  TileSlippyPaintLayout lay{};
  if (!TileSlippyBuildPaintLayout(cr, st, &lay) || lay.tooMany) {
    swprintf_s(lineBuf, lineCap, L"客户区 (%d, %d) | 可视块过多，无法读点", clientPt.x, clientPt.y);
    return;
  }
  if (st->slippyProjection == TileSlippyProjection::kWebMercatorGrid) {
    const int aw = (std::max)(1, static_cast<int>(img.right - img.left));
    const int ah = (std::max)(1, static_cast<int>(img.bottom - img.top));
    const double worldW = static_cast<double>(aw) / static_cast<double>(st->pixelsPerTile);
    const double worldH = static_cast<double>(ah) / static_cast<double>(st->pixelsPerTile);
    const double worldLeft = st->centerTx - worldW * 0.5;
    const double worldTop = st->centerTy - worldH * 0.5;
    const double tileX =
        worldLeft + static_cast<double>(clientPt.x - img.left) / static_cast<double>(st->pixelsPerTile);
    const double tileY =
        worldTop + static_cast<double>(clientPt.y - img.top) / static_cast<double>(st->pixelsPerTile);
    SlippyTileXYToLonLat(tileX, tileY, st->viewZ, &st->pointerLon, &st->pointerLat);
  } else if (st->slippyProjection == TileSlippyProjection::kEquirectangular) {
    st->pointerLon = lay.viewCenterLon + (static_cast<double>(clientPt.x) - lay.imgCenterX) / lay.ppdEq;
    st->pointerLat = lay.viewCenterLat - (static_cast<double>(clientPt.y) - lay.imgCenterY) / lay.ppdEq;
  } else {
    const double s0 = std::sin(lay.viewCenterLat * kTileGeoPi / 180.0);
    const double sAt = s0 - (static_cast<double>(clientPt.y) - lay.imgCenterY) / lay.rPixCea;
    const double clamped = (std::clamp)(sAt, -1.0, 1.0);
    st->pointerLat = std::asin(clamped) * 180.0 / kTileGeoPi;
    st->pointerLon = lay.viewCenterLon + (static_cast<double>(clientPt.x) - lay.imgCenterX) / lay.ppdEq;
  }
  st->pointerGeoValid = true;
  swprintf_s(lineBuf, lineCap, L"客户区 (%d, %d) | λ %.6f° φ %.6f° | 投影 %s | 椭球高 —（瓦片为 Web Mercator 纹理，非网格模式为示意拼贴）",
             clientPt.x, clientPt.y, st->pointerLon, st->pointerLat, TileSlippyProjectionShortLabel(st->slippyProjection));
}

static void TilePaintOneSlippyTileGeo(Gdiplus::Graphics& g, TilePreviewState* st, const TileSlippyPaintLayout& lay,
                                      int tx, int ty, TileSlippyProjection proj, Gdiplus::SolidBrush& miss) {
  const uint64_t key = PackTileKey(st->viewZ, tx, ty);
  auto pit = st->slippyPaths.find(key);
  const int z = st->viewZ;
  double lon0, lat0, lon1, lat1, lon2, lat2, lon3, lat3;
  SlippyTileXYToLonLat(static_cast<double>(tx), static_cast<double>(ty), z, &lon0, &lat0);
  SlippyTileXYToLonLat(static_cast<double>(tx + 1), static_cast<double>(ty), z, &lon1, &lat1);
  SlippyTileXYToLonLat(static_cast<double>(tx), static_cast<double>(ty + 1), z, &lon2, &lat2);
  SlippyTileXYToLonLat(static_cast<double>(tx + 1), static_cast<double>(ty + 1), z, &lon3, &lat3);
  int sxa, sya, sxb, syb, sxc, syc, sxd, syd;
  if (proj == TileSlippyProjection::kEquirectangular) {
    SlippyLonLatToScreenEq(lay, lon0, lat0, &sxa, &sya);
    SlippyLonLatToScreenEq(lay, lon1, lat1, &sxb, &syb);
    SlippyLonLatToScreenEq(lay, lon2, lat2, &sxc, &syc);
    SlippyLonLatToScreenEq(lay, lon3, lat3, &sxd, &syd);
  } else {
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

static void TilePaintSlippyTiles(HDC hdc, TilePreviewState* st, const TileSlippyPaintLayout& lay) {
  if (!lay.valid || lay.tooMany) {
    return;
  }
  const RECT& imgArea = lay.imgArea;
  Gdiplus::Graphics g(hdc);
  g.SetInterpolationMode(Gdiplus::InterpolationModeLowQuality);
  g.SetSmoothingMode(Gdiplus::SmoothingModeNone);
  g.SetPixelOffsetMode(Gdiplus::PixelOffsetModeHalf);
  g.SetCompositingQuality(Gdiplus::CompositingQualityHighSpeed);
  Gdiplus::SolidBrush miss(Gdiplus::Color(255, 238, 240, 245));
  if (st->slippyProjection == TileSlippyProjection::kWebMercatorGrid) {
    for (int ty = lay.ty0; ty <= lay.ty1; ++ty) {
      for (int tx = lay.tx0; tx <= lay.tx1; ++tx) {
        const uint64_t key = PackTileKey(st->viewZ, tx, ty);
        auto pit = st->slippyPaths.find(key);
        const int sx = imgArea.left +
                       static_cast<int>(std::lround((static_cast<double>(tx) - lay.worldLeft) * st->pixelsPerTile));
        const int sy =
            imgArea.top + static_cast<int>(std::lround((static_cast<double>(ty) - lay.worldTop) * st->pixelsPerTile));
        const int sw = (std::max)(1, static_cast<int>(std::ceil(st->pixelsPerTile)) + 1);
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
  SetTextColor(hdc, RGB(28, 36, 52));

  wchar_t status[512]{};
  if (lay.tooMany) {
    swprintf_s(status, L"当前显示层级 Z = %d（金字塔最大 Z = %d）\n可视块过多 (%d×%d)：请滚轮降低 Z 或 Ctrl+滚轮缩小片元。", st->viewZ,
               st->indexMaxZ, lay.spanX, lay.spanY);
  } else if (st->uiShowSlippyMapHud) {
    swprintf_s(status,
               L"投影：%s | Z = %d（最大 %d）| 片元约 %.0f×%.0f px\n"
               L"可视瓦片列 [%d..%d] 行 [%d..%d] | 已索引 %zu 块\n"
               L"拖拽平移 | 滚轮换级 | Shift+滚轮视口中心 | Ctrl+滚轮片元尺度 | 「选项」改投影与界面",
               TileSlippyProjectionShortLabel(st->slippyProjection), st->viewZ, st->indexMaxZ,
               static_cast<double>(st->pixelsPerTile), static_cast<double>(st->pixelsPerTile), lay.tx0, lay.tx1, lay.ty0,
               lay.ty1, st->tileCount);
  }
  const int zPad = 6;
  const int zBandH = lay.tooMany ? 68 : (st->uiShowSlippyMapHud ? 78 : 0);
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
        swprintf_s(dimLine, L"%d×%d px", bw, bh);
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
      TilePreviewLoadReport loadRep{};
      TilePreviewLoadFromPath(hwnd, st, st->rootPath, &loadRep);
      if (!st->rootPath.empty() && !loadRep.ok) {
        TilePreviewShowLoadError(hwnd, loadRep);
      }
      DragAcceptFiles(hwnd, TRUE);
      return 0;
    }
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
          const double dx = static_cast<double>(x - st->lastDragPt.x);
          const double dy = static_cast<double>(y - st->lastDragPt.y);
          st->centerTx -= dx / static_cast<double>(st->pixelsPerTile);
          st->centerTy -= dy / static_cast<double>(st->pixelsPerTile);
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
        st->tilePointerStatusLine = L"鼠标已离开窗口";
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
          MessageBoxW(hwnd, pickerErr.c_str(), L"打开瓦片", MB_OK | MB_ICONWARNING);
        }
        return 0;
      }
      std::wstring mismatch;
      if (!TilePreviewValidatePathMatchesProtocol(proto, path, &mismatch)) {
        MessageBoxW(hwnd, mismatch.c_str(), L"路径与协议不符", MB_OK | MB_ICONWARNING);
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
      HBRUSH bg = CreateSolidBrush(RGB(252, 252, 252));
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
        SetTextColor(hdc, RGB(32, 42, 64));
        RECT openRc = TileOpenButtonRect(cr);
        HBRUSH btnBg = CreateSolidBrush(RGB(232, 240, 252));
        FillRect(hdc, &openRc, btnBg);
        DeleteObject(btnBg);
        Rectangle(hdc, openRc.left, openRc.top, openRc.right, openRc.bottom);
        DrawTextW(hdc, L"Open...", -1, &openRc, DT_CENTER | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX);
        if (st->mode == TilePreviewState::Mode::kSlippyQuadtree) {
          RECT optBtnRc = TileOptionsButtonRect(cr);
          HBRUSH optBg = CreateSolidBrush(st->showOptionsPanel ? RGB(200, 216, 248) : RGB(236, 242, 252));
          FillRect(hdc, &optBtnRc, optBg);
          DeleteObject(optBg);
          Rectangle(hdc, optBtnRc.left, optBtnRc.top, optBtnRc.right, optBtnRc.bottom);
          DrawTextW(hdc, L"选项", -1, &optBtnRc, DT_CENTER | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX);
        }
        const RECT optBtnRc = TileOptionsButtonRect(cr);
        const int hintLeft =
            (st->mode == TilePreviewState::Mode::kSlippyQuadtree)
                ? ((std::max)(openRc.right, optBtnRc.right) + 8)
                : (openRc.right + 8);
        RECT hintRc{hintLeft, 8, cr.right - 10, cr.top + TilePreviewTopBarPx(st) - 6};
        if (hintRc.bottom < hintRc.top + 20) {
          hintRc.bottom = hintRc.top + 20;
        }
        {
          std::wstring panel;
          if (st->mode == TilePreviewState::Mode::kSlippyQuadtree) {
            if (st->uiShowTopHint) {
              panel = std::wstring(kTilePreviewSlippyTopLine) + L"\n\n" + st->hint;
            } else {
              panel = st->hint.empty() ? L"顶栏说明已关闭（「选项」可再打开）。" : st->hint;
            }
          } else {
            panel = std::wstring(kTilePreviewOpenMethods) + L"\n\n──\n" + st->hint;
          }
          TileDrawTextBlockSemiBg(hdc, panel.c_str(), hintRc, 6, 218);
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
      return 0;
    default:
      break;
  }
  return DefWindowProcW(hwnd, msg, wParam, lParam);
}

void OpenTileRasterPreviewWindow(HWND owner, const std::wstring& path) {
  g_pendingTilePreviewRoot = path;
  const DWORD exStyle = owner ? WS_EX_TOOLWINDOW : 0;
  HWND tw = CreateWindowExW(exStyle, kTilePreviewClass, L"瓦片预览 · 四叉树 / BVH 元数据",
                            WS_OVERLAPPEDWINDOW | WS_CLIPCHILDREN, CW_USEDEFAULT, CW_USEDEFAULT, 960, 720, owner,
                            nullptr, GetModuleHandleW(nullptr), nullptr);
  if (tw) {
    AgisCenterWindowInMonitorWorkArea(tw, owner ? owner : g_hwndMain);
    ShowWindow(tw, SW_SHOW);
  }
}

