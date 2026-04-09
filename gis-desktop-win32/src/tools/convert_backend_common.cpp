#include "tools/convert_backend_common.h"

#include <algorithm>
#include <chrono>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <sstream>
#include <thread>
#include <vector>
#include <array>
#include <cmath>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <cwctype>
#include <string>
#include <windows.h>
#if GIS_DESKTOP_HAVE_GDAL
#include <gdal.h>
#include <gdal_utils.h>
#include <cpl_conv.h>
#include <ogr_api.h>
#include <ogr_spatialref.h>
#endif

namespace {

/// Wavefront OBJ 无版本魔数；按 Alias/Wavefront 附录，3.0 取代 2.11。本工具仅输出多边形子集（无 bsp/bzp 等已废弃关键字）。
const wchar_t* kObjFileFormatBanner30 =
    L"# File format: Wavefront OBJ 3.0 (polygonal subset — v, vt, vn, f v/vt/vn, mtllib, usemtl, o; no free-form "
    L"curv/surf)\n";

std::wstring ArgValue(int argc, wchar_t** argv, const wchar_t* name) {
  for (int i = 1; i + 1 < argc; ++i) {
    if (_wcsicmp(argv[i], name) == 0) {
      return argv[i + 1];
    }
  }
  return L"";
}

bool HasCjkChars(const std::wstring& s) {
  for (wchar_t c : s) {
    if ((c >= 0x4E00 && c <= 0x9FFF) || (c >= 0x3400 && c <= 0x4DBF) || (c >= 0xF900 && c <= 0xFAFF)) {
      return true;
    }
  }
  return false;
}

bool IsKnownFlag(const wchar_t* s) {
  if (!s) {
    return false;
  }
  static const wchar_t* kFlags[] = {
      L"--help",           L"-h",               L"/?",
      L"--input",          L"--output",         L"--input-type",
      L"--input-subtype",  L"--output-type",    L"--output-subtype",
      L"--coord-system",   L"--vector-mode",    L"--elev-horiz-ratio",
      L"--target-crs",     L"--output-unit",    L"--mesh-spacing",
      L"--texture-format", L"--raster-max-dim",
  };
  for (const wchar_t* f : kFlags) {
    if (_wcsicmp(s, f) == 0) {
      return true;
    }
  }
  return false;
}

bool ValidateAsciiField(const std::wstring& v) {
  for (wchar_t c : v) {
    if (c > 0x7F) {
      return false;
    }
  }
  return true;
}

std::wstring NormalizeOutputUnitW(const std::wstring& s) {
  if (s.find(L"1000") != std::wstring::npos) {
    return L"1000km";
  }
  std::wstring t = s;
  for (auto& c : t) {
    if (c >= L'A' && c <= L'Z') {
      c = static_cast<wchar_t>(c - L'A' + L'a');
    }
  }
  if (t == L"km") {
    return L"km";
  }
  return L"m";
}

double OutputUnitToScale(const std::wstring& u) {
  const std::wstring n = NormalizeOutputUnitW(u);
  if (n == L"1000km") {
    return 1e-6;
  }
  if (n == L"km") {
    return 1e-3;
  }
  return 1.0;
}

}  // namespace

bool IsChineseOsUi() {
  const LANGID ui = GetUserDefaultUILanguage();
  const WORD primary = PRIMARYLANGID(ui);
  return primary == LANG_CHINESE;
}

bool ParseConvertArgs(int argc, wchar_t** argv, ConvertArgs* out) {
  if (!out) {
    return false;
  }
  for (int i = 1; i < argc; ++i) {
    const wchar_t* a = argv[i];
    if (!a || !*a) {
      continue;
    }
    if ((wcsncmp(a, L"--", 2) == 0 || a[0] == L'-' || (a[0] == L'/' && a[1] != L'\\')) && !IsKnownFlag(a)) {
      std::wcerr << L"[ERROR] unsupported option: " << a << L"\n";
      return false;
    }
  }
  out->input = ArgValue(argc, argv, L"--input");
  out->output = ArgValue(argc, argv, L"--output");
  out->input_type = ArgValue(argc, argv, L"--input-type");
  out->input_subtype = ArgValue(argc, argv, L"--input-subtype");
  out->output_type = ArgValue(argc, argv, L"--output-type");
  out->output_subtype = ArgValue(argc, argv, L"--output-subtype");
  out->coord_system = ArgValue(argc, argv, L"--coord-system");
  if (out->coord_system.empty()) {
    out->coord_system = L"projected";
  }
  out->vector_mode = ArgValue(argc, argv, L"--vector-mode");
  if (out->vector_mode.empty()) {
    out->vector_mode = L"geometry";
  }
  out->elev_horiz_ratio = 1.0;
  const std::wstring ratioStr = ArgValue(argc, argv, L"--elev-horiz-ratio");
  if (!ratioStr.empty()) {
    const double r = _wtof(ratioStr.c_str());
    if (std::isfinite(r) && r > 0.0) {
      out->elev_horiz_ratio = r;
    }
  }
  out->target_crs = ArgValue(argc, argv, L"--target-crs");
  out->output_unit = ArgValue(argc, argv, L"--output-unit");
  if (out->output_unit.empty()) {
    out->output_unit = L"m";
  }
  out->mesh_spacing = 1;
  const std::wstring meshArg = ArgValue(argc, argv, L"--mesh-spacing");
  if (!meshArg.empty()) {
    const long v = wcstol(meshArg.c_str(), nullptr, 10);
    if (v >= 1 && v <= 1000000) {
      out->mesh_spacing = static_cast<int>(v);
    }
  }
  out->texture_format = ArgValue(argc, argv, L"--texture-format");
  if (out->texture_format.empty()) {
    out->texture_format = L"png";
  }
  out->raster_read_max_dim = 4096;
  const std::wstring rmax = ArgValue(argc, argv, L"--raster-max-dim");
  if (!rmax.empty()) {
    const long v = wcstol(rmax.c_str(), nullptr, 10);
    if (v >= 64 && v <= 16384) {
      out->raster_read_max_dim = static_cast<int>(v);
    }
  }
  const std::wstring fields[] = {
      out->input_type,      out->input_subtype, out->output_type, out->output_subtype,
      out->coord_system,    out->vector_mode,   out->target_crs,  out->output_unit,
      out->texture_format,
  };
  for (const auto& f : fields) {
    if (!ValidateAsciiField(f) || HasCjkChars(f)) {
      std::wcerr << L"[ERROR] non-path option values must be English/ASCII only.\n";
      return false;
    }
  }
  return !out->input.empty() && !out->output.empty();
}

void PrintConvertBanner(const wchar_t* title, const ConvertArgs& args) {
  std::wcout << L"[AGIS-CONVERT] " << (title ? title : L"convert") << L"\n";
  std::wcout << L"  input:  " << args.input << L"\n";
  std::wcout << L"  output: " << args.output << L"\n";
  std::wcout << L"  inType: " << args.input_type << L" / " << args.input_subtype << L"\n";
  std::wcout << L"  outType:" << args.output_type << L" / " << args.output_subtype << L"\n";
  std::wcout << L"  coord:  " << args.coord_system << L"\n";
  std::wcout << L"  vector: " << args.vector_mode << L"\n";
  std::wcout << L"  elev/horiz ratio: " << args.elev_horiz_ratio << L"\n";
  std::wcout << L"  target CRS: " << (args.target_crs.empty() ? L"<auto>" : args.target_crs) << L"\n";
  std::wcout << L"  output unit: " << args.output_unit << L"\n";
  std::wcout << L"  mesh spacing (model units): " << args.mesh_spacing << L"\n";
  std::wcout << L"  texture format: " << args.texture_format << L"\n";
  std::wcout << L"  raster max read dim: " << args.raster_read_max_dim << L"\n";
}

namespace {

bool EnsureParent(const std::filesystem::path& p) {
  std::error_code ec;
  const auto parent = p.parent_path();
  if (!parent.empty()) {
    std::filesystem::create_directories(parent, ec);
  }
  return true;
}

int WriteTextFile(const std::filesystem::path& p, const std::wstring& text) {
  EnsureParent(p);
  std::ofstream ofs(p, std::ios::binary);
  if (!ofs.is_open()) {
    return 3;
  }
  int utf8Len = WideCharToMultiByte(CP_UTF8, 0, text.c_str(), static_cast<int>(text.size()), nullptr, 0, nullptr, nullptr);
  if (utf8Len <= 0) {
    return 6;
  }
  std::vector<char> utf8(static_cast<size_t>(utf8Len));
  WideCharToMultiByte(CP_UTF8, 0, text.c_str(), static_cast<int>(text.size()), utf8.data(), utf8Len, nullptr, nullptr);
  ofs.write(utf8.data(), static_cast<std::streamsize>(utf8.size()));
  return 0;
}

int WriteBinaryFile(const std::filesystem::path& p, const unsigned char* data, size_t size) {
  EnsureParent(p);
  std::ofstream ofs(p, std::ios::binary);
  if (!ofs.is_open()) {
    return 3;
  }
  ofs.write(reinterpret_cast<const char*>(data), static_cast<std::streamsize>(size));
  return ofs.good() ? 0 : 6;
}

int EnsureDir(const std::filesystem::path& p) {
  std::error_code ec;
  std::filesystem::create_directories(p, ec);
  return ec ? 4 : 0;
}

std::string ReadTextFileUtf8(const std::filesystem::path& p) {
  std::ifstream ifs(p, std::ios::binary);
  if (!ifs.is_open()) {
    return {};
  }
  std::ostringstream oss;
  oss << ifs.rdbuf();
  return oss.str();
}

std::string XmlAttr(const std::string& line, const char* key) {
  const std::string k = std::string(key) + "=\"";
  const size_t p = line.find(k);
  if (p == std::string::npos) {
    return {};
  }
  const size_t b = p + k.size();
  const size_t e = line.find('"', b);
  if (e == std::string::npos) {
    return {};
  }
  return line.substr(b, e - b);
}

double ToDoubleOr(const std::string& s, double fallback) {
  if (s.empty()) return fallback;
  try {
    return std::stod(s);
  } catch (...) {
    return fallback;
  }
}

struct GisDocInfo {
  bool ok = false;
  double minx = -1.0;
  double miny = -1.0;
  double maxx = 1.0;
  double maxy = 1.0;
  int layerCount = 0;
};

std::string WideToUtf8(const std::wstring& ws) {
  if (ws.empty()) return {};
  const int n = WideCharToMultiByte(CP_UTF8, 0, ws.c_str(), static_cast<int>(ws.size()), nullptr, 0, nullptr, nullptr);
  if (n <= 0) return {};
  std::string out(static_cast<size_t>(n), '\0');
  WideCharToMultiByte(CP_UTF8, 0, ws.c_str(), static_cast<int>(ws.size()), out.data(), n, nullptr, nullptr);
  return out;
}

struct RasterExtract {
  bool ok = false;
  int w = 0;
  int h = 0;
  /** 原始数据集像素尺寸（下采样读入时 ≥ w×h，用于一像元地面尺度）。 */
  int srcW = 0;
  int srcH = 0;
  std::vector<float> elev;
  std::vector<unsigned char> rgb;
  double gt[6] = {0, 1, 0, 0, 0, -1};
  std::wstring sourcePath;
  /** WKT of raster georeferencing (empty if unknown). */
  std::string wkt;
};

struct PbrMaps {
  std::vector<unsigned char> normalRgb;
  std::vector<unsigned char> roughness;
  std::vector<unsigned char> metallic;
  std::vector<unsigned char> ao;
};

struct VectorEnvelope {
  double minx = 0.0;
  double miny = 0.0;
  double maxx = 0.0;
  double maxy = 0.0;
};

GisDocInfo ParseGisDocInfo(const std::filesystem::path& p) {
  GisDocInfo out;
  const std::string xml = ReadTextFileUtf8(p);
  if (xml.empty()) {
    return out;
  }
  const size_t displayPos = xml.find("<display ");
  if (displayPos != std::string::npos) {
    const size_t displayEnd = xml.find('>', displayPos);
    const std::string line = xml.substr(displayPos, displayEnd - displayPos);
    out.minx = ToDoubleOr(XmlAttr(line, "viewMinX"), out.minx);
    out.miny = ToDoubleOr(XmlAttr(line, "viewMinY"), out.miny);
    out.maxx = ToDoubleOr(XmlAttr(line, "viewMaxX"), out.maxx);
    out.maxy = ToDoubleOr(XmlAttr(line, "viewMaxY"), out.maxy);
    out.ok = true;
  }
  size_t pos = 0;
  while (true) {
    pos = xml.find("<layer ", pos);
    if (pos == std::string::npos) break;
    out.layerCount += 1;
    pos += 7;
  }
  return out;
}

std::vector<std::wstring> ParseGisLayerSources(const std::filesystem::path& p) {
  std::vector<std::wstring> out;
  const std::string xml = ReadTextFileUtf8(p);
  if (xml.empty()) return out;
  size_t pos = 0;
  while (true) {
    pos = xml.find("<layer ", pos);
    if (pos == std::string::npos) break;
    const size_t e = xml.find('>', pos);
    if (e == std::string::npos) break;
    const std::string line = xml.substr(pos, e - pos);
    const std::string src = XmlAttr(line, "source");
    if (!src.empty()) {
      int wn = MultiByteToWideChar(CP_UTF8, 0, src.c_str(), static_cast<int>(src.size()), nullptr, 0);
      if (wn > 0) {
        std::wstring ws(static_cast<size_t>(wn), L'\0');
        MultiByteToWideChar(CP_UTF8, 0, src.c_str(), static_cast<int>(src.size()), ws.data(), wn);
        std::filesystem::path sp(ws);
        if (sp.is_relative()) sp = p.parent_path() / sp;
        out.push_back(sp.wstring());
      }
    }
    pos = e + 1;
  }
  return out;
}

#if GIS_DESKTOP_HAVE_GDAL
void CollectVectorEnvelopes(const std::vector<std::wstring>& sources, std::vector<VectorEnvelope>* out) {
  if (!out) return;
  out->clear();
  for (const auto& ws : sources) {
    const std::string u8 = WideToUtf8(ws);
    if (u8.empty()) continue;
    GDALDatasetH ds = GDALOpenEx(u8.c_str(), GDAL_OF_VECTOR, nullptr, nullptr, nullptr);
    if (!ds) continue;
    const int nLayer = GDALDatasetGetLayerCount(ds);
    for (int li = 0; li < nLayer; ++li) {
      OGRLayerH lyr = GDALDatasetGetLayer(ds, li);
      if (!lyr) continue;
      OGR_L_ResetReading(lyr);
      OGRFeatureH f = nullptr;
      while ((f = OGR_L_GetNextFeature(lyr)) != nullptr) {
        OGRGeometryH g = OGR_F_GetGeometryRef(f);
        if (g) {
          OGREnvelope env{};
          OGR_G_GetEnvelope(g, &env);
          if (env.MinX <= env.MaxX && env.MinY <= env.MaxY) {
            out->push_back({env.MinX, env.MinY, env.MaxX, env.MaxY});
          }
        }
        OGR_F_Destroy(f);
      }
    }
    GDALClose(ds);
  }
}

void CollectVectorEnvelopesInCrs(const std::vector<std::wstring>& sources, OGRSpatialReferenceH dest,
                                 std::vector<VectorEnvelope>* out) {
  if (!out || !dest) {
    return;
  }
  out->clear();
  for (const auto& ws : sources) {
    const std::string u8 = WideToUtf8(ws);
    if (u8.empty()) {
      continue;
    }
    GDALDatasetH ds = GDALOpenEx(u8.c_str(), GDAL_OF_VECTOR, nullptr, nullptr, nullptr);
    if (!ds) {
      continue;
    }
    const int nLayer = GDALDatasetGetLayerCount(ds);
    for (int li = 0; li < nLayer; ++li) {
      OGRLayerH lyr = GDALDatasetGetLayer(ds, li);
      if (!lyr) {
        continue;
      }
      OGRSpatialReferenceH lyrSr = OGR_L_GetSpatialRef(lyr);
      OGRSpatialReferenceH srcOwned = nullptr;
      if (!lyrSr) {
        srcOwned = OSRNewSpatialReference(nullptr);
        if (!srcOwned || OSRImportFromEPSG(srcOwned, 4326) != OGRERR_NONE) {
          if (srcOwned) {
            OSRDestroySpatialReference(srcOwned);
          }
          continue;
        }
        lyrSr = srcOwned;
      }
      OGRCoordinateTransformationH tr = OCTNewCoordinateTransformation(lyrSr, dest);
      if (srcOwned) {
        OSRDestroySpatialReference(srcOwned);
        srcOwned = nullptr;
      }
      if (!tr) {
        continue;
      }
      OGR_L_ResetReading(lyr);
      OGRFeatureH f = nullptr;
      while ((f = OGR_L_GetNextFeature(lyr)) != nullptr) {
        OGRGeometryH g = OGR_F_GetGeometryRef(f);
        if (g) {
          OGRGeometryH gc = OGR_G_Clone(g);
          if (gc && OGR_G_Transform(gc, tr) == OGRERR_NONE) {
            OGREnvelope env{};
            OGR_G_GetEnvelope(gc, &env);
            if (env.MinX <= env.MaxX && env.MinY <= env.MaxY) {
              out->push_back({env.MinX, env.MinY, env.MaxX, env.MaxY});
            }
          }
          if (gc) {
            OGR_G_DestroyGeometry(gc);
          }
        }
        OGR_F_Destroy(f);
      }
      OCTDestroyCoordinateTransformation(tr);
    }
    GDALClose(ds);
  }
}

OGRSpatialReferenceH CreateTargetSpatialRef(const ConvertArgs& args, const RasterExtract& raster,
                                            const std::vector<std::wstring>& sources) {
  if (!args.target_crs.empty()) {
    OGRSpatialReferenceH t = OSRNewSpatialReference(nullptr);
    const std::string u8 = WideToUtf8(args.target_crs);
    if (t && OSRSetFromUserInput(t, u8.c_str()) == OGRERR_NONE) {
      return t;
    }
    if (t) {
      OSRDestroySpatialReference(t);
    }
    std::wcout << L"[WARN] --target-crs not recognized, using auto selection.\n";
  }
  if (raster.ok && !raster.wkt.empty()) {
    OGRSpatialReferenceH t = OSRNewSpatialReference(raster.wkt.c_str());
    if (t) {
      return t;
    }
  }
  for (const auto& ws : sources) {
    const std::string u8 = WideToUtf8(ws);
    if (u8.empty()) {
      continue;
    }
    GDALDatasetH ds = GDALOpenEx(u8.c_str(), GDAL_OF_VECTOR, nullptr, nullptr, nullptr);
    if (!ds) {
      continue;
    }
    if (GDALDatasetGetLayerCount(ds) > 0) {
      OGRLayerH lyr = GDALDatasetGetLayer(ds, 0);
      if (lyr) {
        OGRSpatialReferenceH ls = OGR_L_GetSpatialRef(lyr);
        if (ls) {
          char* wktp = nullptr;
          if (OSRExportToWkt(ls, &wktp) == OGRERR_NONE && wktp) {
            OGRSpatialReferenceH t = OSRNewSpatialReference(wktp);
            CPLFree(wktp);
            GDALClose(ds);
            if (t) {
              return t;
            }
          }
        }
      }
    }
    GDALClose(ds);
  }
  OGRSpatialReferenceH t = OSRNewSpatialReference(nullptr);
  if (t && OSRImportFromEPSG(t, 4326) == OGRERR_NONE) {
    return t;
  }
  if (t) {
    OSRDestroySpatialReference(t);
  }
  return nullptr;
}

/** 当前读入栅格（w×h 缓冲区）单像元在地面上较短边的近似长度（米）。含下采样时按 srcW/srcH 放大步长。 */
double RasterMinPixelEdgeMeters(const RasterExtract& r, OGRSpatialReferenceH rasterSr) {
  constexpr double kPi = 3.14159265358979323846;
  constexpr double kMetersPerDegLat = 111319.488;
  if (r.w <= 0 || r.h <= 0) {
    return 1e-3;
  }
  const double sx = (r.srcW > 0) ? static_cast<double>(r.srcW) / static_cast<double>(r.w) : 1.0;
  const double sy = (r.srcH > 0) ? static_cast<double>(r.srcH) / static_cast<double>(r.h) : 1.0;
  const double colDx = r.gt[1] * sx;
  const double colDy = r.gt[4] * sx;
  const double rowDx = r.gt[2] * sy;
  const double rowDy = r.gt[5] * sy;
  const double colLen = std::hypot(colDx, colDy);
  const double rowLen = std::hypot(rowDx, rowDy);
  const double pixNative = (std::max)(1e-30, (std::min)(colLen, rowLen));
  if (!rasterSr) {
    const double a1 = std::fabs(r.gt[1]) * sx;
    const double a5 = std::fabs(r.gt[5]) * sy;
    if (a1 < 0.05 && a5 < 0.05 && a1 > 1e-15) {
      const double midX = (r.w > 1) ? (r.w - 1) * 0.5 : 0.0;
      const double midY = (r.h > 1) ? (r.h - 1) * 0.5 : 0.0;
      const double cy = r.gt[3] + midX * r.gt[4] + midY * r.gt[5];
      const double cosLat = std::cos(cy * kPi / 180.0);
      const double mx = std::abs(r.gt[1]) * sx * kMetersPerDegLat * cosLat;
      const double my = std::abs(r.gt[5]) * sy * kMetersPerDegLat;
      return (std::max)(1e-6, (std::min)(mx, my));
    }
    return (std::max)(1e-6, pixNative);
  }
  if (OSRIsGeographic(rasterSr)) {
    const double midX = (r.w > 1) ? (r.w - 1) * 0.5 : 0.0;
    const double midY = (r.h > 1) ? (r.h - 1) * 0.5 : 0.0;
    const double cx = r.gt[0] + midX * r.gt[1] + midY * r.gt[2];
    const double cy = r.gt[3] + midX * r.gt[4] + midY * r.gt[5];
    const double cosLat = std::cos(cy * kPi / 180.0);
    const double mx = std::abs(r.gt[1]) * sx * kMetersPerDegLat * cosLat;
    const double my = std::abs(r.gt[5]) * sy * kMetersPerDegLat;
    return (std::max)(1e-6, (std::min)(mx, my));
  }
  const double toMeters = OSRGetLinearUnits(rasterSr, nullptr);
  if (!(toMeters > 0.0) || !std::isfinite(toMeters)) {
    return (std::max)(1e-6, pixNative);
  }
  return (std::max)(1e-6, pixNative * toMeters);
}

bool TryReadRaster(const std::wstring& path, RasterExtract* out, int maxReadDim) {
  if (!out) return false;
  int kMax = maxReadDim;
  if (kMax < 64) {
    kMax = 64;
  }
  if (kMax > 16384) {
    kMax = 16384;
  }
  const std::string u8 = WideToUtf8(path);
  if (u8.empty()) return false;
  GDALDatasetH ds = GDALOpenEx(u8.c_str(), GDAL_OF_RASTER, nullptr, nullptr, nullptr);
  if (!ds) return false;
  const int srcW = GDALGetRasterXSize(ds);
  const int srcH = GDALGetRasterYSize(ds);
  const int bands = GDALGetRasterCount(ds);
  if (srcW <= 1 || srcH <= 1 || bands <= 0) {
    GDALClose(ds);
    return false;
  }
  int w = srcW;
  int h = srcH;
  out->srcW = srcW;
  out->srcH = srcH;
  if (w > kMax || h > kMax) {
    const double s = (std::max)(static_cast<double>(w) / kMax, static_cast<double>(h) / kMax);
    w = (std::max)(2, static_cast<int>(w / s));
    h = (std::max)(2, static_cast<int>(h / s));
    std::wcout << L"[RASTER] downsample read buffer: " << srcW << L"x" << srcH << L" -> " << w << L"x" << h
               << L" (max side " << kMax << L" px)\n";
  }
  out->w = w;
  out->h = h;
  out->elev.resize(static_cast<size_t>(w) * h);
  out->rgb.resize(static_cast<size_t>(w) * h * 3);
  GDALRasterBandH b1 = GDALGetRasterBand(ds, 1);
  if (GDALRasterIO(b1, GF_Read, 0, 0, srcW, srcH, out->elev.data(), w, h, GDT_Float32, 0, 0) != CE_None) {
    GDALClose(ds);
    return false;
  }
  std::vector<unsigned char> r(static_cast<size_t>(w) * h, 128);
  std::vector<unsigned char> g(static_cast<size_t>(w) * h, 128);
  std::vector<unsigned char> b(static_cast<size_t>(w) * h, 128);
  if (bands >= 3) {
    GDALRasterIO(GDALGetRasterBand(ds, 1), GF_Read, 0, 0, srcW, srcH, r.data(), w, h, GDT_Byte, 0, 0);
    GDALRasterIO(GDALGetRasterBand(ds, 2), GF_Read, 0, 0, srcW, srcH, g.data(), w, h, GDT_Byte, 0, 0);
    GDALRasterIO(GDALGetRasterBand(ds, 3), GF_Read, 0, 0, srcW, srcH, b.data(), w, h, GDT_Byte, 0, 0);
  } else {
    GDALRasterIO(b1, GF_Read, 0, 0, srcW, srcH, r.data(), w, h, GDT_Byte, 0, 0);
    g = r;
    b = r;
  }
  for (int i = 0; i < w * h; ++i) {
    out->rgb[static_cast<size_t>(i) * 3 + 0] = r[static_cast<size_t>(i)];
    out->rgb[static_cast<size_t>(i) * 3 + 1] = g[static_cast<size_t>(i)];
    out->rgb[static_cast<size_t>(i) * 3 + 2] = b[static_cast<size_t>(i)];
  }
  double gt[6]{};
  if (GDALGetGeoTransform(ds, gt) == CE_None) {
    for (int i = 0; i < 6; ++i) out->gt[i] = gt[i];
  }
  out->wkt.clear();
  OGRSpatialReferenceH sr = GDALGetSpatialRef(ds);
  if (sr) {
    char* wktPtr = nullptr;
    if (OSRExportToWkt(sr, &wktPtr) == OGRERR_NONE && wktPtr) {
      out->wkt = wktPtr;
      CPLFree(wktPtr);
    }
  }
  out->sourcePath = path;
  out->ok = true;
  GDALClose(ds);
  return true;
}

int WriteRgbPng(const std::filesystem::path& path, int w, int h, const std::vector<unsigned char>& rgb) {
  GDALDriverH memDrv = GDALGetDriverByName("MEM");
  GDALDriverH pngDrv = GDALGetDriverByName("PNG");
  if (!memDrv || !pngDrv) return 7;
  GDALDatasetH mem = GDALCreate(memDrv, "", w, h, 3, GDT_Byte, nullptr);
  if (!mem) return 7;
  const int px = w * h;
  std::vector<unsigned char> r(static_cast<size_t>(px));
  std::vector<unsigned char> g(static_cast<size_t>(px));
  std::vector<unsigned char> b(static_cast<size_t>(px));
  for (int i = 0; i < px; ++i) {
    r[static_cast<size_t>(i)] = rgb[static_cast<size_t>(i) * 3 + 0];
    g[static_cast<size_t>(i)] = rgb[static_cast<size_t>(i) * 3 + 1];
    b[static_cast<size_t>(i)] = rgb[static_cast<size_t>(i) * 3 + 2];
  }
  GDALRasterIO(GDALGetRasterBand(mem, 1), GF_Write, 0, 0, w, h, r.data(), w, h, GDT_Byte, 0, 0);
  GDALRasterIO(GDALGetRasterBand(mem, 2), GF_Write, 0, 0, w, h, g.data(), w, h, GDT_Byte, 0, 0);
  GDALRasterIO(GDALGetRasterBand(mem, 3), GF_Write, 0, 0, w, h, b.data(), w, h, GDT_Byte, 0, 0);
  const std::string u8 = WideToUtf8(path.wstring());
  GDALDatasetH outDs = GDALCreateCopy(pngDrv, u8.c_str(), mem, FALSE, nullptr, nullptr, nullptr);
  GDALClose(mem);
  if (!outDs) return 7;
  GDALClose(outDs);
  return 0;
}

int WriteGrayPng(const std::filesystem::path& path, int w, int h, const std::vector<unsigned char>& gray) {
  GDALDriverH memDrv = GDALGetDriverByName("MEM");
  GDALDriverH pngDrv = GDALGetDriverByName("PNG");
  if (!memDrv || !pngDrv) return 7;
  GDALDatasetH mem = GDALCreate(memDrv, "", w, h, 1, GDT_Byte, nullptr);
  if (!mem) return 7;
  GDALRasterIO(GDALGetRasterBand(mem, 1), GF_Write, 0, 0, w, h, const_cast<unsigned char*>(gray.data()), w, h, GDT_Byte, 0, 0);
  const std::string u8 = WideToUtf8(path.wstring());
  GDALDatasetH outDs = GDALCreateCopy(pngDrv, u8.c_str(), mem, FALSE, nullptr, nullptr, nullptr);
  GDALClose(mem);
  if (!outDs) return 7;
  GDALClose(outDs);
  return 0;
}

int WriteRgbBmp24(const std::filesystem::path& path, int w, int h, const std::vector<unsigned char>& rgb) {
  if (w <= 0 || h <= 0 || rgb.size() < static_cast<size_t>(w) * h * 3) {
    return 7;
  }
  const uint32_t rowStride = ((w * 3 + 3) / 4) * 4;
  const uint32_t imgSize = rowStride * static_cast<uint32_t>(h);
  const uint32_t fileSize = 14 + 40 + imgSize;
  std::vector<unsigned char> file;
  file.resize(fileSize);
  size_t o = 0;
  file[o++] = 'B';
  file[o++] = 'M';
  auto le32 = [&](uint32_t v) {
    file[o++] = static_cast<unsigned char>(v & 0xff);
    file[o++] = static_cast<unsigned char>((v >> 8) & 0xff);
    file[o++] = static_cast<unsigned char>((v >> 16) & 0xff);
    file[o++] = static_cast<unsigned char>((v >> 24) & 0xff);
  };
  auto le16 = [&](uint16_t v) {
    file[o++] = static_cast<unsigned char>(v & 0xff);
    file[o++] = static_cast<unsigned char>((v >> 8) & 0xff);
  };
  le32(fileSize);
  le16(0);
  le16(0);
  le32(14 + 40);
  le32(40);
  le32(static_cast<uint32_t>(w));
  le32(static_cast<uint32_t>(h));
  le16(1);
  le16(24);
  le32(0);
  le32(imgSize);
  le32(0);
  le32(0);
  le32(0);
  le32(0);
  for (int y = 0; y < h; ++y) {
    const int sy = h - 1 - y;
    for (int x = 0; x < w; ++x) {
      const size_t si = (static_cast<size_t>(sy) * w + x) * 3;
      file[o++] = rgb[si + 2];
      file[o++] = rgb[si + 1];
      file[o++] = rgb[si + 0];
    }
    const uint32_t pad = rowStride - static_cast<uint32_t>(w) * 3;
    for (uint32_t p = 0; p < pad; ++p) {
      file[o++] = 0;
    }
  }
  return WriteBinaryFile(path, file.data(), file.size());
}

int WriteRgbTga24(const std::filesystem::path& path, int w, int h, const std::vector<unsigned char>& rgb) {
  if (w <= 0 || h <= 0 || rgb.size() < static_cast<size_t>(w) * h * 3) {
    return 7;
  }
  std::vector<unsigned char> tga(18 + static_cast<size_t>(w) * h * 3);
  tga[2] = 2;
  tga[12] = static_cast<unsigned char>(w & 0xff);
  tga[13] = static_cast<unsigned char>((w >> 8) & 0xff);
  tga[14] = static_cast<unsigned char>(h & 0xff);
  tga[15] = static_cast<unsigned char>((h >> 8) & 0xff);
  tga[16] = 24;
  tga[17] = 0x20;
  size_t o = 18;
  for (int y = 0; y < h; ++y) {
    for (int x = 0; x < w; ++x) {
      const size_t si = (static_cast<size_t>(y) * w + x) * 3;
      tga[o++] = rgb[si + 0];
      tga[o++] = rgb[si + 1];
      tga[o++] = rgb[si + 2];
    }
  }
  return WriteBinaryFile(path, tga.data(), tga.size());
}

#if GIS_DESKTOP_HAVE_GDAL
int WriteRgbGeoTiff(const std::filesystem::path& path, int w, int h, const std::vector<unsigned char>& rgb) {
  GDALDriverH memDrv = GDALGetDriverByName("MEM");
  GDALDriverH gtiffDrv = GDALGetDriverByName("GTiff");
  if (!memDrv || !gtiffDrv) return 7;
  GDALDatasetH mem = GDALCreate(memDrv, "", w, h, 3, GDT_Byte, nullptr);
  if (!mem) return 7;
  const int px = w * h;
  std::vector<unsigned char> r(static_cast<size_t>(px));
  std::vector<unsigned char> g(static_cast<size_t>(px));
  std::vector<unsigned char> b(static_cast<size_t>(px));
  for (int i = 0; i < px; ++i) {
    r[static_cast<size_t>(i)] = rgb[static_cast<size_t>(i) * 3 + 0];
    g[static_cast<size_t>(i)] = rgb[static_cast<size_t>(i) * 3 + 1];
    b[static_cast<size_t>(i)] = rgb[static_cast<size_t>(i) * 3 + 2];
  }
  GDALRasterIO(GDALGetRasterBand(mem, 1), GF_Write, 0, 0, w, h, r.data(), w, h, GDT_Byte, 0, 0);
  GDALRasterIO(GDALGetRasterBand(mem, 2), GF_Write, 0, 0, w, h, g.data(), w, h, GDT_Byte, 0, 0);
  GDALRasterIO(GDALGetRasterBand(mem, 3), GF_Write, 0, 0, w, h, b.data(), w, h, GDT_Byte, 0, 0);
  const std::string u8 = WideToUtf8(path.wstring());
  char** opts = nullptr;
  opts = CSLSetNameValue(opts, "COMPRESS", "DEFLATE");
  GDALDatasetH outDs = GDALCreateCopy(gtiffDrv, u8.c_str(), mem, FALSE, opts, nullptr, nullptr);
  CSLDestroy(opts);
  GDALClose(mem);
  if (!outDs) return 7;
  GDALClose(outDs);
  return 0;
}

int WriteRgbGdalBmp(const std::filesystem::path& path, int w, int h, const std::vector<unsigned char>& rgb) {
  GDALDriverH memDrv = GDALGetDriverByName("MEM");
  GDALDriverH bmpDrv = GDALGetDriverByName("BMP");
  if (!memDrv || !bmpDrv) {
    return WriteRgbBmp24(path, w, h, rgb);
  }
  GDALDatasetH mem = GDALCreate(memDrv, "", w, h, 3, GDT_Byte, nullptr);
  if (!mem) return 7;
  const int px = w * h;
  std::vector<unsigned char> r(static_cast<size_t>(px));
  std::vector<unsigned char> g(static_cast<size_t>(px));
  std::vector<unsigned char> b(static_cast<size_t>(px));
  for (int i = 0; i < px; ++i) {
    r[static_cast<size_t>(i)] = rgb[static_cast<size_t>(i) * 3 + 0];
    g[static_cast<size_t>(i)] = rgb[static_cast<size_t>(i) * 3 + 1];
    b[static_cast<size_t>(i)] = rgb[static_cast<size_t>(i) * 3 + 2];
  }
  GDALRasterIO(GDALGetRasterBand(mem, 1), GF_Write, 0, 0, w, h, r.data(), w, h, GDT_Byte, 0, 0);
  GDALRasterIO(GDALGetRasterBand(mem, 2), GF_Write, 0, 0, w, h, g.data(), w, h, GDT_Byte, 0, 0);
  GDALRasterIO(GDALGetRasterBand(mem, 3), GF_Write, 0, 0, w, h, b.data(), w, h, GDT_Byte, 0, 0);
  const std::string u8 = WideToUtf8(path.wstring());
  GDALDatasetH outDs = GDALCreateCopy(bmpDrv, u8.c_str(), mem, FALSE, nullptr, nullptr, nullptr);
  GDALClose(mem);
  if (!outDs) return WriteRgbBmp24(path, w, h, rgb);
  GDALClose(outDs);
  return 0;
}
#endif

int WriteRgbTextureFile(const std::filesystem::path& path, int w, int h, const std::vector<unsigned char>& rgb,
                        const std::wstring& fmtIn) {
  std::wstring f = fmtIn;
  for (auto& c : f) {
    if (c >= L'A' && c <= L'Z') {
      c = static_cast<wchar_t>(c - L'A' + L'a');
    }
  }
  if (f.find(L"tif") != std::wstring::npos || f.find(L"tiff") != std::wstring::npos) {
#if GIS_DESKTOP_HAVE_GDAL
    return WriteRgbGeoTiff(path, w, h, rgb);
#else
    (void)w;
    (void)h;
    (void)rgb;
    return 7;
#endif
  }
  if (f.find(L"bmp") != std::wstring::npos) {
#if GIS_DESKTOP_HAVE_GDAL
    return WriteRgbGdalBmp(path, w, h, rgb);
#else
    return WriteRgbBmp24(path, w, h, rgb);
#endif
  }
  if (f.find(L"tga") != std::wstring::npos) {
    return WriteRgbTga24(path, w, h, rgb);
  }
  return WriteRgbPng(path, w, h, rgb);
}

PbrMaps BuildPbrFromElevation(const RasterExtract& r) {
  PbrMaps out;
  if (!r.ok || r.w <= 1 || r.h <= 1) return out;
  const int w = r.w, h = r.h;
  out.normalRgb.resize(static_cast<size_t>(w) * h * 3, 128);
  out.roughness.resize(static_cast<size_t>(w) * h, 160);
  out.metallic.resize(static_cast<size_t>(w) * h, 0);
  out.ao.resize(static_cast<size_t>(w) * h, 255);
  auto elevAt = [&](int x, int y) {
    x = (std::max)(0, (std::min)(w - 1, x));
    y = (std::max)(0, (std::min)(h - 1, y));
    return r.elev[static_cast<size_t>(y) * w + x];
  };
  for (int y = 0; y < h; ++y) {
    for (int x = 0; x < w; ++x) {
      const float hl = elevAt(x - 1, y);
      const float hr = elevAt(x + 1, y);
      const float hd = elevAt(x, y - 1);
      const float hu = elevAt(x, y + 1);
      float nx = hl - hr;
      float ny = hd - hu;
      float nz = 2.0f;
      const float nl = std::sqrt(nx * nx + ny * ny + nz * nz);
      nx /= nl;
      ny /= nl;
      nz /= nl;
      const size_t i3 = (static_cast<size_t>(y) * w + x) * 3;
      out.normalRgb[i3 + 0] = static_cast<unsigned char>((nx * 0.5f + 0.5f) * 255.0f);
      out.normalRgb[i3 + 1] = static_cast<unsigned char>((ny * 0.5f + 0.5f) * 255.0f);
      out.normalRgb[i3 + 2] = static_cast<unsigned char>((nz * 0.5f + 0.5f) * 255.0f);
      const float slope = (std::min)(1.0f, std::sqrt((hl - hr) * (hl - hr) + (hd - hu) * (hd - hu)) * 0.05f);
      out.roughness[static_cast<size_t>(y) * w + x] = static_cast<unsigned char>((0.35f + 0.55f * slope) * 255.0f);
      out.metallic[static_cast<size_t>(y) * w + x] = 0;
      out.ao[static_cast<size_t>(y) * w + x] = static_cast<unsigned char>((0.7f + 0.3f * (1.0f - slope)) * 255.0f);
    }
  }
  return out;
}
#endif

namespace {

void PixelFromGeo(const double gt[6], double x, double y, double* col, double* row) {
  const double det = gt[1] * gt[5] - gt[2] * gt[4];
  if (std::fabs(det) < 1e-18) {
    *col = 0;
    *row = 0;
    return;
  }
  const double dx = x - gt[0];
  const double dy = y - gt[3];
  *col = (dx * gt[5] - dy * gt[2]) / det;
  *row = (gt[1] * dy - gt[4] * dx) / det;
}

float SampleBilinearF(const std::vector<float>& e, int W, int H, double col, double row) {
  if (W <= 0 || H <= 0 || e.size() < static_cast<size_t>(W * H)) {
    return 0.f;
  }
  col = (std::clamp)(col, 0.0, static_cast<double>(W - 1));
  row = (std::clamp)(row, 0.0, static_cast<double>(H - 1));
  const int x0 = static_cast<int>(std::floor(col));
  const int y0 = static_cast<int>(std::floor(row));
  const int x1 = (std::min)(x0 + 1, W - 1);
  const int y1 = (std::min)(y0 + 1, H - 1);
  const double tx = col - x0;
  const double ty = row - y0;
  const float v00 = e[static_cast<size_t>(y0) * W + x0];
  const float v10 = e[static_cast<size_t>(y0) * W + x1];
  const float v01 = e[static_cast<size_t>(y1) * W + x0];
  const float v11 = e[static_cast<size_t>(y1) * W + x1];
  return static_cast<float>((1.0 - tx) * (1.0 - ty) * static_cast<double>(v00) + tx * (1.0 - ty) * static_cast<double>(v10) +
                            (1.0 - tx) * ty * static_cast<double>(v01) + tx * ty * static_cast<double>(v11));
}

unsigned char SampleBilinearRgb(const std::vector<unsigned char>& rgb, int W, int H, int chan, double col, double row) {
  if (W <= 0 || H <= 0 || rgb.size() < static_cast<size_t>(W * H * 3)) {
    return 0;
  }
  col = (std::clamp)(col, 0.0, static_cast<double>(W - 1));
  row = (std::clamp)(row, 0.0, static_cast<double>(H - 1));
  const int x0 = static_cast<int>(std::floor(col));
  const int y0 = static_cast<int>(std::floor(row));
  const int x1 = (std::min)(x0 + 1, W - 1);
  const int y1 = (std::min)(y0 + 1, H - 1);
  const double tx = col - x0;
  const double ty = row - y0;
  auto at = [&](int xi, int yi) -> double {
    return static_cast<double>(rgb[(static_cast<size_t>(yi) * W + xi) * 3 + chan]);
  };
  const double v =
      (1.0 - tx) * (1.0 - ty) * at(x0, y0) + tx * (1.0 - ty) * at(x1, y0) + (1.0 - tx) * ty * at(x0, y1) + tx * ty * at(x1, y1);
  const int o = static_cast<int>(std::lround(v));
  return static_cast<unsigned char>((std::clamp)(o, 0, 255));
}

void ResampleBilinearRgbBuffer(const std::vector<unsigned char>& src, int sw, int sh, std::vector<unsigned char>* dst,
                               int dw, int dh) {
  if (!dst || sw < 2 || sh < 2 || dw < 2 || dh < 2) {
    if (dst) {
      *dst = src;
    }
    return;
  }
  dst->resize(static_cast<size_t>(dw * dh * 3));
  for (int j = 0; j < dh; ++j) {
    for (int i = 0; i < dw; ++i) {
      const double u = (dw > 1) ? static_cast<double>(i) / static_cast<double>(dw - 1) : 0.0;
      const double v = (dh > 1) ? static_cast<double>(j) / static_cast<double>(dh - 1) : 0.0;
      const double sx = u * static_cast<double>(sw - 1);
      const double sy = v * static_cast<double>(sh - 1);
      for (int c = 0; c < 3; ++c) {
        (*dst)[(static_cast<size_t>(j) * dw + i) * 3 + c] = SampleBilinearRgb(src, sw, sh, c, sx, sy);
      }
    }
  }
}

void ResampleBilinearElev(const std::vector<float>& src, int sw, int sh, std::vector<float>* dst, int dw, int dh) {
  if (!dst || sw < 2 || sh < 2 || dw < 2 || dh < 2) {
    if (dst) {
      *dst = src;
    }
    return;
  }
  dst->resize(static_cast<size_t>(dw * dh));
  for (int j = 0; j < dh; ++j) {
    for (int i = 0; i < dw; ++i) {
      const double u = (dw > 1) ? static_cast<double>(i) / static_cast<double>(dw - 1) : 0.0;
      const double v = (dh > 1) ? static_cast<double>(j) / static_cast<double>(dh - 1) : 0.0;
      const double sx = u * static_cast<double>(sw - 1);
      const double sy = v * static_cast<double>(sh - 1);
      (*dst)[static_cast<size_t>(j) * dw + i] = SampleBilinearF(src, sw, sh, sx, sy);
    }
  }
}

// 地理坐标目标下：将 (lon,lat) 相对参考点的差转为本地近似米，使与高程（米）及 output-unit 一致。
void TargetHorizDeltaToApproxMeters(double refLonDeg, double refLatDeg, double ptLonDeg, double ptLatDeg, double* outMx,
                                    double* outMy) {
  constexpr double kPi = 3.14159265358979323846;
  constexpr double kMetersPerDegLat = 111319.488;
  const double cosLat = std::cos(refLatDeg * kPi / 180.0);
  *outMx = (ptLonDeg - refLonDeg) * kMetersPerDegLat * cosLat;
  *outMy = (ptLatDeg - refLatDeg) * kMetersPerDegLat;
}

}  // namespace

int ConvertGisToModelImpl(const ConvertArgs& args) {
  std::filesystem::path outPath(args.output);
  std::error_code ec;
  if (std::filesystem::is_directory(outPath, ec) || outPath.extension().empty()) {
    std::filesystem::create_directories(outPath, ec);
    outPath /= L"model.obj";
  }
  const std::wstring mtlName = outPath.stem().wstring() + L".mtl";
  std::wcout << L"[OUT] model file: " << outPath.wstring() << L"\n";
  std::wcout << L"[OUT] material file: " << (outPath.parent_path() / mtlName).wstring() << L"\n";
  // 真正转换：优先读取 .gis 内首个可用栅格图层，生成高程网格 + 真实纹理。
  GisDocInfo doc;
  RasterExtract raster;
  std::vector<std::wstring> sources;
  std::vector<VectorEnvelope> vEnvsTarget;
  std::vector<VectorEnvelope> vEnvsBake;
  bool targetIsGeographic = false;
#if GIS_DESKTOP_HAVE_GDAL
  OGRSpatialReferenceH targetSr = nullptr;
  OGRSpatialReferenceH rasterSr = nullptr;
  OGRCoordinateTransformationH rasterToTarget = nullptr;
  /** 目标 CRS → 栅格 CRS：用于规则网格顶点在贴图上的 UV（与高分辨率栅格一致）。 */
  OGRCoordinateTransformationH targetToRasterUv = nullptr;
  /** 已为顶点写出「栅格像元」UV；为 false 时 _albedo 仍用网格 ni×nj 与 vt 一致（如 cecf）。 */
  bool meshAlbedoUseFullRasterUv = false;
#endif
  const std::filesystem::path inPath(args.input);
  const bool isGis = _wcsicmp(inPath.extension().wstring().c_str(), L".gis") == 0;
  if (isGis) {
    doc = ParseGisDocInfo(inPath);
    sources = ParseGisLayerSources(inPath);
  } else {
    sources.push_back(inPath.wstring());
  }
#if GIS_DESKTOP_HAVE_GDAL
  GDALAllRegister();
  if (isGis) {
    for (const auto& s : sources) {
      if (TryReadRaster(s, &raster, args.raster_read_max_dim)) {
        break;
      }
    }
  } else {
    TryReadRaster(inPath.wstring(), &raster, args.raster_read_max_dim);
  }
  targetSr = CreateTargetSpatialRef(args, raster, sources);
  if (targetSr) {
    targetIsGeographic = OSRIsGeographic(targetSr) != 0;
    CollectVectorEnvelopesInCrs(sources, targetSr, &vEnvsTarget);
  }
  if (raster.ok && !raster.wkt.empty()) {
    rasterSr = OSRNewSpatialReference(raster.wkt.c_str());
    if (rasterSr) {
      CollectVectorEnvelopesInCrs(sources, rasterSr, &vEnvsBake);
    }
  }
  if (vEnvsBake.empty()) {
    CollectVectorEnvelopes(sources, &vEnvsBake);
  }
  if (_wcsicmp(args.coord_system.c_str(), L"cecf") != 0 && rasterSr && targetSr) {
    rasterToTarget = OCTNewCoordinateTransformation(rasterSr, targetSr);
  }
  if (raster.ok && _wcsicmp(args.vector_mode.c_str(), L"bake_texture") == 0 && !vEnvsBake.empty()) {
    for (const auto& ev : vEnvsBake) {
      if (std::abs(raster.gt[1]) < 1e-12 || std::abs(raster.gt[5]) < 1e-12) {
        continue;
      }
      int x0 = static_cast<int>((ev.minx - raster.gt[0]) / raster.gt[1]);
      int x1 = static_cast<int>((ev.maxx - raster.gt[0]) / raster.gt[1]);
      int y0 = static_cast<int>((ev.maxy - raster.gt[3]) / raster.gt[5]);
      int y1 = static_cast<int>((ev.miny - raster.gt[3]) / raster.gt[5]);
      if (x0 > x1) {
        std::swap(x0, x1);
      }
      if (y0 > y1) {
        std::swap(y0, y1);
      }
      x0 = (std::max)(0, (std::min)(raster.w - 1, x0));
      x1 = (std::max)(0, (std::min)(raster.w - 1, x1));
      y0 = (std::max)(0, (std::min)(raster.h - 1, y0));
      y1 = (std::max)(0, (std::min)(raster.h - 1, y1));
      for (int x = x0; x <= x1; ++x) {
        for (int yy : {y0, y1}) {
          const size_t i = (static_cast<size_t>(yy) * raster.w + x) * 3;
          raster.rgb[i + 0] = 255;
          raster.rgb[i + 1] = 64;
          raster.rgb[i + 2] = 64;
        }
      }
      for (int yy = y0; yy <= y1; ++yy) {
        for (int x : {x0, x1}) {
          const size_t i = (static_cast<size_t>(yy) * raster.w + x) * 3;
          raster.rgb[i + 0] = 255;
          raster.rgb[i + 1] = 64;
          raster.rgb[i + 2] = 64;
        }
      }
    }
  }
#endif
  const int rw = raster.ok ? raster.w : 25;
  const int rh = raster.ok ? raster.h : 25;
  int mw = rw;
  int mh = rh;
  std::vector<float> meshElevBuf;
  std::vector<unsigned char> meshRgbBuf;
  double meshTminX = 0.0;
  double meshTmaxX = 0.0;
  double meshTminY = 0.0;
  double meshTmaxY = 0.0;
  bool haveTargetMesh = false;
  double meshStepReqMeters = 0.0;
  double meshStepEffMeters = 0.0;
  double meshRasterCellMinMeters = 0.0;
  double meshExportSpanX = 0.0;
  double meshExportSpanY = 0.0;
  const double minx = doc.ok ? doc.minx : -1.0;
  const double miny = doc.ok ? doc.miny : -1.0;
  const double maxx = doc.ok ? doc.maxx : 1.0;
  const double maxy = doc.ok ? doc.maxy : 1.0;
  const double spanX = (std::max)(1e-6, maxx - minx);
  const double spanY = (std::max)(1e-6, maxy - miny);
  const double unitS = OutputUnitToScale(args.output_unit);
#if GIS_DESKTOP_HAVE_GDAL
  int meshSpIn = args.mesh_spacing;
  if (meshSpIn < 1) {
    meshSpIn = 1;
  }
  if (raster.ok && rasterToTarget && rasterSr && targetSr && _wcsicmp(args.coord_system.c_str(), L"cecf") != 0 &&
      meshSpIn >= 1 && rw >= 2 && rh >= 2) {
    auto pixWorld = [&](int ix, int iy) {
      const double pxw = raster.gt[0] + ix * raster.gt[1] + iy * raster.gt[2];
      const double pyw = raster.gt[3] + ix * raster.gt[4] + iy * raster.gt[5];
      return std::pair(pxw, pyw);
    };
    double bminx = 1e100;
    double bmaxx = -1e100;
    double bminy = 1e100;
    double bmaxy = -1e100;
    for (int ix : {0, rw - 1}) {
      for (int iy : {0, rh - 1}) {
        const auto pr = pixWorld(ix, iy);
        double tx = pr.first;
        double ty = pr.second;
        double tz = 0.0;
        if (!OCTTransform(rasterToTarget, 1, &tx, &ty, &tz)) {
          tx = pr.first;
          ty = pr.second;
        }
        bminx = (std::min)(bminx, tx);
        bmaxx = (std::max)(bmaxx, tx);
        bminy = (std::min)(bminy, ty);
        bmaxy = (std::max)(bmaxy, ty);
      }
    }
    const double extX = (std::max)(1e-9, bmaxx - bminx);
    const double extY = (std::max)(1e-9, bmaxy - bminy);
    double extMeshX = extX;
    double extMeshY = extY;
    if (targetIsGeographic) {
      constexpr double kPi = 3.14159265358979323846;
      constexpr double kMetersPerDegLat = 111319.488;
      const double midLatRad = (bminy + bmaxy) * 0.5 * kPi / 180.0;
      const double cosMidLat = std::cos(midLatRad);
      extMeshX = extX * kMetersPerDegLat * cosMidLat;
      extMeshY = extY * kMetersPerDegLat;
    }
    const double stepMetersUser = static_cast<double>(meshSpIn) / unitS;
    const double minPixelM = RasterMinPixelEdgeMeters(raster, rasterSr);
    const double stepMetersEff = (std::max)(stepMetersUser, minPixelM);
    meshStepReqMeters = stepMetersUser;
    meshStepEffMeters = stepMetersEff;
    meshRasterCellMinMeters = minPixelM;
    if (stepMetersEff > stepMetersUser * 1.0000001) {
      std::wcout << L"[MESH] ground step clamped to raster cell: req " << stepMetersUser << L" m, min ~" << minPixelM
                 << L" m -> " << stepMetersEff << L" m\n";
    }
    double targetLinearToM = 1.0;
    if (!targetIsGeographic) {
      const double fac = OSRGetLinearUnits(targetSr, nullptr);
      targetLinearToM = (fac > 0.0 && std::isfinite(fac)) ? fac : 1.0;
    }
    if (targetIsGeographic) {
      meshExportSpanX = extMeshX * unitS;
      meshExportSpanY = extMeshY * unitS;
    } else {
      meshExportSpanX = extX * targetLinearToM * unitS;
      meshExportSpanY = extY * targetLinearToM * unitS;
    }
    const double Wgrid = targetIsGeographic ? extMeshX : extX;
    const double Hgrid = targetIsGeographic ? extMeshY : extY;
    const double stepK = targetIsGeographic ? stepMetersEff : (stepMetersEff / targetLinearToM);
    int kx = static_cast<int>(std::floor(Wgrid / (2.0 * stepK)));
    int ky = static_cast<int>(std::floor(Hgrid / stepK));
    int kcell = (std::max)(1, (std::min)(kx, ky));
    kcell = (std::min)(kcell, 2047);
    int ni = 2 * kcell + 1;
    int nj = kcell + 1;
    ni = (std::clamp)(ni, 2, 4096);
    nj = (std::clamp)(nj, 2, 4096);
    std::wcout << L"[MESH] grid (ni x nj) " << ni << L" x " << nj << L" interior cells ~ 2:1, stepK ~ " << stepK
               << L"\n";
    OGRCoordinateTransformationH targetToRasterMesh = OCTNewCoordinateTransformation(targetSr, rasterSr);
    if (targetToRasterMesh) {
      meshElevBuf.resize(static_cast<size_t>(ni) * nj);
      meshRgbBuf.resize(static_cast<size_t>(ni) * nj * 3);
      meshTminX = bminx;
      meshTmaxX = bmaxx;
      meshTminY = bminy;
      meshTmaxY = bmaxy;
      for (int j = 0; j < nj; ++j) {
        const double fj = (nj > 1) ? static_cast<double>(j) / static_cast<double>(nj - 1) : 0.0;
        const double tyv = meshTminY + fj * (meshTmaxY - meshTminY);
        for (int i = 0; i < ni; ++i) {
          const double fi = (ni > 1) ? static_cast<double>(i) / static_cast<double>(ni - 1) : 0.0;
          const double txv = meshTminX + fi * (meshTmaxX - meshTminX);
          double ox = txv;
          double oy = tyv;
          double oz = 0.0;
          const size_t idx = static_cast<size_t>(j) * ni + i;
          if (!OCTTransform(targetToRasterMesh, 1, &ox, &oy, &oz)) {
            meshElevBuf[idx] = 0.f;
            meshRgbBuf[idx * 3 + 0] = 128;
            meshRgbBuf[idx * 3 + 1] = 128;
            meshRgbBuf[idx * 3 + 2] = 128;
            continue;
          }
          double col = 0.0;
          double row = 0.0;
          PixelFromGeo(raster.gt, ox, oy, &col, &row);
          meshElevBuf[idx] = SampleBilinearF(raster.elev, rw, rh, col, row);
          meshRgbBuf[idx * 3 + 0] = SampleBilinearRgb(raster.rgb, rw, rh, 0, col, row);
          meshRgbBuf[idx * 3 + 1] = SampleBilinearRgb(raster.rgb, rw, rh, 1, col, row);
          meshRgbBuf[idx * 3 + 2] = SampleBilinearRgb(raster.rgb, rw, rh, 2, col, row);
        }
      }
      OCTDestroyCoordinateTransformation(targetToRasterMesh);
      mw = ni;
      mh = nj;
      haveTargetMesh = true;
    }
  }
#endif
  const int w = mw;
  const int h = mh;
  const int gridW = w - 1;
  const int gridH = h - 1;
  double elevRatio = args.elev_horiz_ratio;
  if (!(elevRatio > 0.0) || !std::isfinite(elevRatio)) {
    elevRatio = 1.0;
  }
  float z_min_r = 0.f;
  bool haveZStats = false;
  if (raster.ok) {
    if (haveTargetMesh && !meshElevBuf.empty()) {
      haveZStats = true;
      z_min_r = meshElevBuf[0];
      for (float z : meshElevBuf) {
        z_min_r = (std::min)(z_min_r, z);
      }
    } else if (!raster.elev.empty()) {
      haveZStats = true;
      z_min_r = raster.elev[0];
      for (float z : raster.elev) {
        z_min_r = (std::min)(z_min_r, z);
      }
    }
  }
  double cx = 0.0;
  double cy = 0.0;
  double tcx = 0.0;
  double tcy = 0.0;
  bool horizDeg = false;
  constexpr double kPi = 3.14159265358979323846;
  constexpr double kMetersPerDegLat = 111319.488;
  if (raster.ok) {
    const double midX = (rw - 1) * 0.5;
    const double midY = (rh - 1) * 0.5;
    cx = raster.gt[0] + midX * raster.gt[1] + midY * raster.gt[2];
    cy = raster.gt[3] + midX * raster.gt[4] + midY * raster.gt[5];
    tcx = cx;
    tcy = cy;
#if GIS_DESKTOP_HAVE_GDAL
    if (rasterToTarget) {
      double tzc = 0.0;
      if (!OCTTransform(rasterToTarget, 1, &tcx, &tcy, &tzc)) {
        tcx = cx;
        tcy = cy;
      }
    }
#endif
    const double a1 = std::fabs(raster.gt[1]);
    const double a5 = std::fabs(raster.gt[5]);
    horizDeg = (a1 < 0.05 && a5 < 0.05 && a1 > 1e-15);
  }
  double xyDispSx = 1.0;
  double xyDispSy = 1.0;
#if GIS_DESKTOP_HAVE_GDAL
  if (!raster.ok && !vEnvsTarget.empty()) {
    double sx = 0.0;
    double sy = 0.0;
    for (const auto& e : vEnvsTarget) {
      sx += (e.minx + e.maxx) * 0.5;
      sy += (e.miny + e.maxy) * 0.5;
    }
    const double dn = static_cast<double>(vEnvsTarget.size());
    tcx = sx / dn;
    tcy = sy / dn;
  }
  double rasterPlaneSpanX = 0.0;
  double rasterPlaneSpanY = 0.0;
  if (raster.ok && _wcsicmp(args.coord_system.c_str(), L"cecf") != 0) {
    double bx0 = 1e100, bx1 = -1e100, by0 = 1e100, by1 = -1e100;
    for (int ix : {0, rw - 1}) {
      for (int iy : {0, rh - 1}) {
        const double px = raster.gt[0] + ix * raster.gt[1] + iy * raster.gt[2];
        const double py = raster.gt[3] + ix * raster.gt[4] + iy * raster.gt[5];
        double xo = px;
        double yo = py;
        if (rasterToTarget) {
          double tx = px;
          double ty = py;
          double tz0 = 0.0;
          if (OCTTransform(rasterToTarget, 1, &tx, &ty, &tz0)) {
            if (targetIsGeographic) {
              TargetHorizDeltaToApproxMeters(tcx, tcy, tx, ty, &xo, &yo);
            } else {
              xo = tx - tcx;
              yo = ty - tcy;
            }
          } else {
            if (horizDeg) {
              const double cosLat = std::cos(cy * kPi / 180.0);
              xo = (px - cx) * kMetersPerDegLat * cosLat;
              yo = (py - cy) * kMetersPerDegLat;
            } else {
              xo = px - cx;
              yo = py - cy;
            }
          }
        } else {
          if (horizDeg) {
            const double cosLat = std::cos(cy * kPi / 180.0);
            xo = (px - cx) * kMetersPerDegLat * cosLat;
            yo = (py - cy) * kMetersPerDegLat;
          } else {
            xo = px - cx;
            yo = py - cy;
          }
        }
        xo *= unitS;
        yo *= unitS;
        bx0 = (std::min)(bx0, xo);
        bx1 = (std::max)(bx1, xo);
        by0 = (std::min)(by0, yo);
        by1 = (std::max)(by1, yo);
      }
    }
    rasterPlaneSpanX = (std::max)(1e-30, bx1 - bx0);
    rasterPlaneSpanY = (std::max)(1e-30, by1 - by0);
  }
  {
    const double spanAX =
        (haveTargetMesh && meshExportSpanX > 1e-30) ? meshExportSpanX : rasterPlaneSpanX;
    const double spanAY =
        (haveTargetMesh && meshExportSpanY > 1e-30) ? meshExportSpanY : rasterPlaneSpanY;
    if (_wcsicmp(args.coord_system.c_str(), L"cecf") != 0 && spanAY > 1e-30 && (haveTargetMesh || raster.ok)) {
      const double r = spanAX / spanAY;
      if (r < 2.0 - 1e-9) {
        xyDispSx = 2.0 / r;
      } else if (r > 2.0 + 1e-9) {
        xyDispSy = r / 2.0;
      }
    }
  }
#endif
  std::wstringstream ss;
  ss << kObjFileFormatBanner30 << L"# AGIS model output\n"
     << L"# from GIS input: " << args.input << L"\n"
     << L"# subtype: " << args.input_subtype << L" -> " << args.output_subtype << L"\n"
     << L"# raster_source: " << (raster.ok ? raster.sourcePath : L"<none>") << L"\n";
  if (doc.ok) {
    ss << L"# gis_document_viewport (2D map <display> from .gis — last saved map view, NOT raster footprint): ["
       << minx << L"," << miny << L"] - [" << maxx << L"," << maxy << L"]\n";
  }
  if (raster.ok && rw >= 1 && rh >= 1) {
    double rbMinX = 1e300;
    double rbMaxX = -1e300;
    double rbMinY = 1e300;
    double rbMaxY = -1e300;
    for (int ix : {0, rw - 1}) {
      for (int iy : {0, rh - 1}) {
        const double px = raster.gt[0] + ix * raster.gt[1] + iy * raster.gt[2];
        const double py = raster.gt[3] + ix * raster.gt[4] + iy * raster.gt[5];
        rbMinX = (std::min)(rbMinX, px);
        rbMaxX = (std::max)(rbMaxX, px);
        rbMinY = (std::min)(rbMinY, py);
        rbMaxY = (std::max)(rbMaxY, py);
      }
    }
    ss << L"# raster_bbox_native (GeoTransform axis-aligned hull in raster source CRS; for global EPSG:4326 "
          L"expect ~lon [-180,180], lat [-90,90]): ["
       << rbMinX << L"," << rbMinY << L"] - [" << rbMaxX << L"," << rbMaxY << L"]\n";
  }
  ss << L"# layers: " << doc.layerCount << L"\n"
     << L"# coord_system: " << args.coord_system << L"\n"
     << L"# target_crs: " << (args.target_crs.empty() ? L"<auto>" : args.target_crs) << L"\n"
     << L"# output_unit: " << NormalizeOutputUnitW(args.output_unit) << L" (scale=" << unitS << L")\n"
     << L"# mesh_spacing: " << args.mesh_spacing << L" (model units)\n";
  if (haveTargetMesh) {
    ss << L"# mesh_step_meters: requested=" << meshStepReqMeters << L" effective=" << meshStepEffMeters
       << L" raster_cell_min~=" << meshRasterCellMinMeters << L"\n";
  }
  ss << L"# elev_horiz_ratio: " << elevRatio << L" (horizontal meters vs elevation meters; 1=1:1)\n"
     << L"# horiz_xy_metric_meters: "
     << (targetIsGeographic ? L"target geographic -> local approx m (same length unit as Z)" : L"target projected units")
     << L"\n"
     << L"# display_xy_scale: sx=" << xyDispSx << L" sy=" << xyDispSy << L" (target plane ~2:1 after scale)\n"
     << L"mtllib " << mtlName << L"\n"
     << L"o agis_model\n";
#if GIS_DESKTOP_HAVE_GDAL
  if (haveTargetMesh && rasterSr && targetSr && raster.ok && rw >= 2 && rh >= 2 &&
      _wcsicmp(args.coord_system.c_str(), L"cecf") != 0) {
    targetToRasterUv = OCTNewCoordinateTransformation(targetSr, rasterSr);
    if (targetToRasterUv) {
      meshAlbedoUseFullRasterUv = true;
      std::wcout << L"[TEXTURE] mesh albedo uses full raster " << rw << L"x" << rh
                 << L" with geographic UV (not mesh vertex count)\n";
    }
  }
#endif
  for (int y = 0; y < h; ++y) {
    for (int x = 0; x < w; ++x) {
      const float fx = (gridW > 0) ? static_cast<float>(x) / static_cast<float>(gridW) : 0.0f;
      const float fy = (gridH > 0) ? static_cast<float>(y) / static_cast<float>(gridH) : 0.0f;
      if (haveTargetMesh && _wcsicmp(args.coord_system.c_str(), L"cecf") != 0) {
        const double txv = meshTminX + static_cast<double>(fx) * (meshTmaxX - meshTminX);
        const double tyv = meshTminY + static_cast<double>(fy) * (meshTmaxY - meshTminY);
        const float pz = meshElevBuf[static_cast<size_t>(y) * w + x];
        const double zbase = haveZStats ? static_cast<double>(z_min_r) : 0.0;
        const double zo = (zbase + (static_cast<double>(pz) - zbase) * elevRatio) * unitS;
        double xm = txv - tcx;
        double ym = tyv - tcy;
        if (targetIsGeographic) {
          TargetHorizDeltaToApproxMeters(tcx, tcy, txv, tyv, &xm, &ym);
        }
        const double xo = xm * unitS * xyDispSx;
        const double yo = ym * unitS * xyDispSy;
        ss << L"v " << xo << L" " << yo << L" " << zo << L"\n";
#if GIS_DESKTOP_HAVE_GDAL
        if (targetToRasterUv) {
          double ox = txv;
          double oy = tyv;
          double oz = 0;
          float tu = fx;
          float tv = 1.0f - fy;
          if (OCTTransform(targetToRasterUv, 1, &ox, &oy, &oz)) {
            double col = 0;
            double row = 0;
            PixelFromGeo(raster.gt, ox, oy, &col, &row);
            col = (std::clamp)(col, 0.0, static_cast<double>(rw - 1));
            row = (std::clamp)(row, 0.0, static_cast<double>(rh - 1));
            tu = static_cast<float>((col + 0.5) / static_cast<double>(rw));
            tv = static_cast<float>(1.0 - (row + 0.5) / static_cast<double>(rh));
          }
          ss << L"vt " << tu << L" " << tv << L"\n";
        } else
#endif
        {
          ss << L"vt " << fx << L" " << (1.0f - fy) << L"\n";
        }
        continue;
      }
      double px = minx + spanX * static_cast<double>(fx);
      double py = miny + spanY * static_cast<double>(fy);
      double pz = 0.0;
      if (raster.ok) {
        px = raster.gt[0] + x * raster.gt[1] + y * raster.gt[2];
        py = raster.gt[3] + x * raster.gt[4] + y * raster.gt[5];
        pz = raster.elev[static_cast<size_t>(y) * w + x];
      }
      if (_wcsicmp(args.coord_system.c_str(), L"cecf") == 0) {
        const double lon = px * kPi / 180.0;
        const double lat = py * kPi / 180.0;
        const double R = 6378137.0 + pz * 1000.0 * elevRatio;
        const double xEcef = R * std::cos(lat) * std::cos(lon) * unitS;
        const double yEcef = R * std::cos(lat) * std::sin(lon) * unitS;
        const double zEcef = R * std::sin(lat) * unitS;
        ss << L"v " << xEcef << L" " << yEcef << L" " << zEcef << L"\n";
      } else {
        double xo = px;
        double yo = py;
        double zo = pz;
        if (raster.ok) {
          bool didProj = false;
#if GIS_DESKTOP_HAVE_GDAL
          if (rasterToTarget) {
            double tx = px;
            double ty = py;
            double tz0 = 0.0;
            if (OCTTransform(rasterToTarget, 1, &tx, &ty, &tz0)) {
              if (targetIsGeographic) {
                TargetHorizDeltaToApproxMeters(tcx, tcy, tx, ty, &xo, &yo);
              } else {
                xo = tx - tcx;
                yo = ty - tcy;
              }
              didProj = true;
            }
          }
#endif
          if (!didProj) {
            if (horizDeg) {
              const double cosLat = std::cos(cy * kPi / 180.0);
              xo = (px - cx) * kMetersPerDegLat * cosLat;
              yo = (py - cy) * kMetersPerDegLat;
            } else {
              xo = px - cx;
              yo = py - cy;
            }
          }
          const double zbase = haveZStats ? static_cast<double>(z_min_r) : 0.0;
          zo = zbase + (pz - zbase) * elevRatio;
        } else if (haveZStats) {
          const double zbase = static_cast<double>(z_min_r);
          zo = zbase + (pz - zbase) * elevRatio;
        } else {
          zo = pz * elevRatio;
        }
        xo *= unitS;
        yo *= unitS;
        zo *= unitS;
        xo *= xyDispSx;
        yo *= xyDispSy;
        ss << L"v " << xo << L" " << yo << L" " << zo << L"\n";
      }
      ss << L"vt " << fx << L" " << (1.0f - fy) << L"\n";
    }
  }
  ss << L"vn 0 0 1\n";
  ss << L"usemtl defaultMat\n";
  auto vid = [w](int x, int y) { return y * w + x + 1; };
  for (int y = 0; y < gridH; ++y) {
    for (int x = 0; x < gridW; ++x) {
      const int v00 = vid(x, y);
      const int v10 = vid(x + 1, y);
      const int v01 = vid(x, y + 1);
      const int v11 = vid(x + 1, y + 1);
      ss << L"f " << v00 << L"/" << v00 << L"/1 " << v10 << L"/" << v10 << L"/1 " << v11 << L"/" << v11 << L"/1\n";
      ss << L"f " << v00 << L"/" << v00 << L"/1 " << v11 << L"/" << v11 << L"/1 " << v01 << L"/" << v01 << L"/1\n";
    }
  }
  int baseVertex = w * h + 1;
  if (_wcsicmp(args.vector_mode.c_str(), L"geometry") == 0) {
    for (const auto& ev : vEnvsTarget) {
      const double z = 10.0 * elevRatio * unitS;
      double xa, ya, xb, yb, xc, yc, xd, yd;
      if (targetIsGeographic) {
        TargetHorizDeltaToApproxMeters(tcx, tcy, ev.minx, ev.miny, &xa, &ya);
        TargetHorizDeltaToApproxMeters(tcx, tcy, ev.maxx, ev.miny, &xb, &yb);
        TargetHorizDeltaToApproxMeters(tcx, tcy, ev.maxx, ev.maxy, &xc, &yc);
        TargetHorizDeltaToApproxMeters(tcx, tcy, ev.minx, ev.maxy, &xd, &yd);
        xa *= unitS;
        ya *= unitS;
        xb *= unitS;
        yb *= unitS;
        xc *= unitS;
        yc *= unitS;
        xd *= unitS;
        yd *= unitS;
      } else {
        xa = (ev.minx - tcx) * unitS;
        xb = (ev.maxx - tcx) * unitS;
        xc = xb;
        xd = xa;
        ya = (ev.miny - tcy) * unitS;
        yb = ya;
        yc = (ev.maxy - tcy) * unitS;
        yd = yc;
      }
      xa *= xyDispSx;
      ya *= xyDispSy;
      xb *= xyDispSx;
      yb *= xyDispSy;
      xc *= xyDispSx;
      yc *= xyDispSy;
      xd *= xyDispSx;
      yd *= xyDispSy;
      ss << L"v " << xa << L" " << ya << L" " << z << L"\n";
      ss << L"v " << xb << L" " << yb << L" " << z << L"\n";
      ss << L"v " << xc << L" " << yc << L" " << z << L"\n";
      ss << L"v " << xd << L" " << yd << L" " << z << L"\n";
      ss << L"vt 0 0\nvt 1 0\nvt 1 1\nvt 0 1\n";
      ss << L"f " << baseVertex << L"/1/1 " << (baseVertex + 1) << L"/2/1 " << (baseVertex + 2) << L"/3/1\n";
      ss << L"f " << baseVertex << L"/1/1 " << (baseVertex + 2) << L"/3/1 " << (baseVertex + 3) << L"/4/1\n";
      baseVertex += 4;
    }
  }
#if GIS_DESKTOP_HAVE_GDAL
  if (targetToRasterUv) {
    OCTDestroyCoordinateTransformation(targetToRasterUv);
    targetToRasterUv = nullptr;
  }
  if (rasterToTarget) {
    OCTDestroyCoordinateTransformation(rasterToTarget);
    rasterToTarget = nullptr;
  }
  if (rasterSr) {
    OSRDestroySpatialReference(rasterSr);
    rasterSr = nullptr;
  }
  if (targetSr) {
    OSRDestroySpatialReference(targetSr);
    targetSr = nullptr;
  }
#endif
  int rc = WriteTextFile(outPath, ss.str());
  if (rc != 0) {
    return rc;
  }
  const std::wstring texName = outPath.stem().wstring() + L"_albedo.png";
  const std::wstring normalName = outPath.stem().wstring() + L"_normal.png";
  const std::wstring roughName = outPath.stem().wstring() + L"_roughness.png";
  const std::wstring metalName = outPath.stem().wstring() + L"_metallic.png";
  const std::wstring aoName = outPath.stem().wstring() + L"_ao.png";
  const std::filesystem::path texPath = outPath.parent_path() / texName;
  const std::filesystem::path normalPath = outPath.parent_path() / normalName;
  const std::filesystem::path roughPath = outPath.parent_path() / roughName;
  const std::filesystem::path metalPath = outPath.parent_path() / metalName;
  const std::filesystem::path aoPath = outPath.parent_path() / aoName;
  int trc = 0;
#if GIS_DESKTOP_HAVE_GDAL
  if (raster.ok) {
    RasterExtract texForMaps = raster;
#if GIS_DESKTOP_HAVE_GDAL
    if (haveTargetMesh && !meshElevBuf.empty() && !meshAlbedoUseFullRasterUv) {
      texForMaps.w = w;
      texForMaps.h = h;
      texForMaps.elev = meshElevBuf;
      texForMaps.rgb = meshRgbBuf;
    }
#endif
    if (texForMaps.w >= 2 && texForMaps.h >= 2 && texForMaps.rgb.size() >= static_cast<size_t>(texForMaps.w * texForMaps.h * 3)) {
      int texOutH = (std::max)(2, texForMaps.h);
      int texOutW = 2 * texOutH;
      constexpr int kTexMax = 8192;
      if (texOutW > kTexMax) {
        texOutW = kTexMax;
        texOutH = (std::max)(2, texOutW / 2);
      }
      std::vector<unsigned char> rgb2;
      std::vector<float> elev2;
      ResampleBilinearRgbBuffer(texForMaps.rgb, texForMaps.w, texForMaps.h, &rgb2, texOutW, texOutH);
      if (!texForMaps.elev.empty() && texForMaps.elev.size() == static_cast<size_t>(texForMaps.w * texForMaps.h)) {
        ResampleBilinearElev(texForMaps.elev, texForMaps.w, texForMaps.h, &elev2, texOutW, texOutH);
        texForMaps.elev = std::move(elev2);
      }
      texForMaps.rgb = std::move(rgb2);
      texForMaps.w = texOutW;
      texForMaps.h = texOutH;
    }
    trc = WriteRgbPng(texPath, texForMaps.w, texForMaps.h, texForMaps.rgb);
    if (trc == 0) {
      const PbrMaps p = BuildPbrFromElevation(texForMaps);
      if (!p.normalRgb.empty()) trc = WriteRgbPng(normalPath, texForMaps.w, texForMaps.h, p.normalRgb);
      if (trc == 0 && !p.roughness.empty()) trc = WriteGrayPng(roughPath, texForMaps.w, texForMaps.h, p.roughness);
      if (trc == 0 && !p.metallic.empty()) trc = WriteGrayPng(metalPath, texForMaps.w, texForMaps.h, p.metallic);
      if (trc == 0 && !p.ao.empty()) trc = WriteGrayPng(aoPath, texForMaps.w, texForMaps.h, p.ao);
    }
  } else {
    static const unsigned char kPng1x1Blue[] = {
        0x89, 0x50, 0x4E, 0x47, 0x0D, 0x0A, 0x1A, 0x0A, 0x00, 0x00, 0x00, 0x0D, 0x49, 0x48, 0x44, 0x52,
        0x00, 0x00, 0x00, 0x01, 0x00, 0x00, 0x00, 0x01, 0x08, 0x02, 0x00, 0x00, 0x00, 0x90, 0x77, 0x53,
        0xDE, 0x00, 0x00, 0x00, 0x0C, 0x49, 0x44, 0x41, 0x54, 0x08, 0x99, 0x63, 0x68, 0x78, 0xD8, 0x00,
        0x00, 0x03, 0x32, 0x01, 0x6D, 0xD9, 0xD4, 0x03, 0x80, 0x00, 0x00, 0x00, 0x00, 0x49, 0x45, 0x4E,
        0x44, 0xAE, 0x42, 0x60, 0x82};
    trc = WriteBinaryFile(texPath, kPng1x1Blue, sizeof(kPng1x1Blue));
    if (trc == 0) trc = WriteBinaryFile(normalPath, kPng1x1Blue, sizeof(kPng1x1Blue));
    if (trc == 0) trc = WriteBinaryFile(roughPath, kPng1x1Blue, sizeof(kPng1x1Blue));
    if (trc == 0) trc = WriteBinaryFile(metalPath, kPng1x1Blue, sizeof(kPng1x1Blue));
    if (trc == 0) trc = WriteBinaryFile(aoPath, kPng1x1Blue, sizeof(kPng1x1Blue));
  }
#else
  static const unsigned char kPng1x1Blue[] = {
      0x89, 0x50, 0x4E, 0x47, 0x0D, 0x0A, 0x1A, 0x0A, 0x00, 0x00, 0x00, 0x0D, 0x49, 0x48, 0x44, 0x52,
      0x00, 0x00, 0x00, 0x01, 0x00, 0x00, 0x00, 0x01, 0x08, 0x02, 0x00, 0x00, 0x00, 0x90, 0x77, 0x53,
      0xDE, 0x00, 0x00, 0x00, 0x0C, 0x49, 0x44, 0x41, 0x54, 0x08, 0x99, 0x63, 0x68, 0x78, 0xD8, 0x00,
      0x00, 0x03, 0x32, 0x01, 0x6D, 0xD9, 0xD4, 0x03, 0x80, 0x00, 0x00, 0x00, 0x00, 0x49, 0x45, 0x4E,
      0x44, 0xAE, 0x42, 0x60, 0x82};
  trc = WriteBinaryFile(texPath, kPng1x1Blue, sizeof(kPng1x1Blue));
  if (trc == 0) trc = WriteBinaryFile(normalPath, kPng1x1Blue, sizeof(kPng1x1Blue));
  if (trc == 0) trc = WriteBinaryFile(roughPath, kPng1x1Blue, sizeof(kPng1x1Blue));
  if (trc == 0) trc = WriteBinaryFile(metalPath, kPng1x1Blue, sizeof(kPng1x1Blue));
  if (trc == 0) trc = WriteBinaryFile(aoPath, kPng1x1Blue, sizeof(kPng1x1Blue));
#endif
  if (trc != 0) {
    return trc;
  }
  const std::wstring mtlText =
      L"newmtl defaultMat\n"
      L"Kd 0.8 0.8 0.8\n"
      L"Ka 0.1 0.1 0.1\n"
      L"Ks 0.2 0.2 0.2\n"
      L"map_Kd " + texName + L"\n"
      L"map_Bump " + normalName + L"\n"
      L"map_Pr " + roughName + L"\n"
      L"map_Pm " + metalName + L"\n"
      L"map_AO " + aoName + L"\n";
  return WriteTextFile(outPath.parent_path() / mtlName, mtlText);
}

bool ModelSubtypeIsPointCloudW(const std::wstring& s) {
  std::wstring t = s;
  for (auto& c : t) {
    c = static_cast<wchar_t>(std::towlower(c));
  }
  return t.find(L"pointcloud") != std::wstring::npos || t.find(L"las") != std::wstring::npos ||
         t.find(L"laz") != std::wstring::npos;
}

bool ModelSubtypeIsMeshW(const std::wstring& s) {
  std::wstring t = s;
  for (auto& c : t) {
    c = static_cast<wchar_t>(std::towlower(c));
  }
  return t.find(L"3dmesh") != std::wstring::npos || t.find(L"mesh") != std::wstring::npos;
}

struct LasPointFlt {
  double x = 0;
  double y = 0;
  double z = 0;
  std::uint8_t r = 200;
  std::uint8_t g = 200;
  std::uint8_t b = 200;
};

static void MemCopyF64(void* dst, double v) { std::memcpy(dst, &v, 8); }

static int WriteLas12Pdrf2(const std::filesystem::path& path, const std::vector<LasPointFlt>& pts) {
  if (pts.empty()) {
    return 8;
  }
  double minx = pts[0].x, maxx = pts[0].x, miny = pts[0].y, maxy = pts[0].y, minz = pts[0].z, maxz = pts[0].z;
  for (const auto& p : pts) {
    minx = (std::min)(minx, p.x);
    maxx = (std::max)(maxx, p.x);
    miny = (std::min)(miny, p.y);
    maxy = (std::max)(maxy, p.y);
    minz = (std::min)(minz, p.z);
    maxz = (std::max)(maxz, p.z);
  }
  double xscale = 0.001;
  double yscale = 0.001;
  double zscale = 0.001;
  double xoff = minx;
  double yoff = miny;
  double zoff = minz;
  const double span = (std::max)({maxx - minx, maxy - miny, maxz - minz, 1e-9});
  if (span / xscale > 2.0e9) {
    xscale = yscale = zscale = span / 2.0e9;
  }
  if (pts.size() > 0xffffffffull) {
    std::wcerr << L"[ERROR] LAS 1.2 legacy 记录数超限。\n";
    return 8;
  }
  const std::uint32_t n = static_cast<std::uint32_t>(pts.size());
  std::vector<unsigned char> hdr(227, 0);
  std::memcpy(hdr.data(), "LASF", 4);
  hdr[24] = 1;
  hdr[25] = 2;
  {
    const char* sys = "AGIS";
    const char* gen = "agis_convert_model_to_model";
    std::memcpy(hdr.data() + 26, sys, (std::min)(strlen(sys), size_t(32)));
    std::memcpy(hdr.data() + 58, gen, (std::min)(strlen(gen), size_t(32)));
  }
  *reinterpret_cast<std::uint16_t*>(hdr.data() + 94) = 227;
  *reinterpret_cast<std::uint32_t*>(hdr.data() + 96) = 227;
  *reinterpret_cast<std::uint32_t*>(hdr.data() + 100) = 0;
  hdr[104] = 2;
  *reinterpret_cast<std::uint16_t*>(hdr.data() + 105) = 26;
  *reinterpret_cast<std::uint32_t*>(hdr.data() + 107) = n;
  MemCopyF64(hdr.data() + 131, xscale);
  MemCopyF64(hdr.data() + 139, yscale);
  MemCopyF64(hdr.data() + 147, zscale);
  MemCopyF64(hdr.data() + 155, xoff);
  MemCopyF64(hdr.data() + 163, yoff);
  MemCopyF64(hdr.data() + 171, zoff);
  MemCopyF64(hdr.data() + 179, maxx);
  MemCopyF64(hdr.data() + 187, minx);
  MemCopyF64(hdr.data() + 195, maxy);
  MemCopyF64(hdr.data() + 203, miny);
  MemCopyF64(hdr.data() + 211, maxz);
  MemCopyF64(hdr.data() + 219, minz);

  EnsureParent(path);
  std::ofstream ofs(path, std::ios::binary);
  if (!ofs) {
    return 3;
  }
  ofs.write(reinterpret_cast<const char*>(hdr.data()), hdr.size());
  for (const auto& p : pts) {
    const std::int32_t xi =
        static_cast<std::int32_t>(std::llround((p.x - xoff) / xscale));
    const std::int32_t yi =
        static_cast<std::int32_t>(std::llround((p.y - yoff) / yscale));
    const std::int32_t zi =
        static_cast<std::int32_t>(std::llround((p.z - zoff) / zscale));
    unsigned char rec[26]{};
    std::memcpy(rec + 0, &xi, 4);
    std::memcpy(rec + 4, &yi, 4);
    std::memcpy(rec + 8, &zi, 4);
    rec[14] = 1;  // return flags
    rec[18] = 1;  // point source ID lo
    const std::uint16_t R = static_cast<std::uint16_t>(p.r * 257);
    const std::uint16_t G = static_cast<std::uint16_t>(p.g * 257);
    const std::uint16_t B = static_cast<std::uint16_t>(p.b * 257);
    std::memcpy(rec + 20, &R, 2);
    std::memcpy(rec + 22, &G, 2);
    std::memcpy(rec + 24, &B, 2);
    ofs.write(reinterpret_cast<const char*>(rec), 26);
  }
  return ofs.good() ? 0 : 6;
}

static int ReadLasPoints(const std::filesystem::path& path, std::vector<LasPointFlt>* outPts) {
  if (!outPts) {
    return 7;
  }
  outPts->clear();
  std::ifstream ifs(path, std::ios::binary);
  if (!ifs) {
    return 3;
  }
  char sig[4]{};
  ifs.read(sig, 4);
  if (!ifs || std::memcmp(sig, "LASF", 4) != 0) {
    std::wcerr << L"[ERROR] 非 LAS（缺少 LASF 签名）。\n";
    return 7;
  }
  ifs.seekg(96);
  std::uint32_t offData = 0;
  ifs.read(reinterpret_cast<char*>(&offData), 4);
  ifs.seekg(104);
  unsigned char pdrf = 0;
  ifs.read(reinterpret_cast<char*>(&pdrf), 1);
  std::uint16_t pdlen = 0;
  ifs.read(reinterpret_cast<char*>(&pdlen), 2);
  std::uint32_t nrec = 0;
  ifs.read(reinterpret_cast<char*>(&nrec), 4);
  if (!ifs || offData < 227 || pdlen < 20 || nrec == 0) {
    std::wcerr << L"[ERROR] LAS 头无效或点数为零。\n";
    return 7;
  }
  double xs = 0.001, ys = 0.001, zs = 0.001, xo = 0, yo = 0, zo = 0;
  ifs.seekg(131);
  ifs.read(reinterpret_cast<char*>(&xs), 8);
  ifs.read(reinterpret_cast<char*>(&ys), 8);
  ifs.read(reinterpret_cast<char*>(&zs), 8);
  ifs.read(reinterpret_cast<char*>(&xo), 8);
  ifs.read(reinterpret_cast<char*>(&yo), 8);
  ifs.read(reinterpret_cast<char*>(&zo), 8);
  if (!ifs) {
    return 7;
  }
  ifs.seekg(static_cast<std::streamoff>(offData));
  outPts->reserve(nrec);
  for (std::uint32_t i = 0; i < nrec; ++i) {
    std::vector<unsigned char> buf(pdlen);
    ifs.read(reinterpret_cast<char*>(buf.data()), pdlen);
    if (!ifs) {
      break;
    }
    if (buf.size() < 12) {
      continue;
    }
    std::int32_t xi = 0, yi = 0, zi = 0;
    std::memcpy(&xi, buf.data() + 0, 4);
    std::memcpy(&yi, buf.data() + 4, 4);
    std::memcpy(&zi, buf.data() + 8, 4);
    LasPointFlt p;
    p.x = static_cast<double>(xi) * xs + xo;
    p.y = static_cast<double>(yi) * ys + yo;
    p.z = static_cast<double>(zi) * zs + zo;
    if (pdrf == 2 && buf.size() >= 26) {
      std::uint16_t R = 0, G = 0, B = 0;
      std::memcpy(&R, buf.data() + 20, 2);
      std::memcpy(&G, buf.data() + 22, 2);
      std::memcpy(&B, buf.data() + 24, 2);
      p.r = static_cast<std::uint8_t>((std::min)(255, static_cast<int>(R / 256)));
      p.g = static_cast<std::uint8_t>((std::min)(255, static_cast<int>(G / 256)));
      p.b = static_cast<std::uint8_t>((std::min)(255, static_cast<int>(B / 256)));
    } else if (pdrf == 3 && buf.size() >= 34) {
      std::uint16_t R = 0, G = 0, B = 0;
      std::memcpy(&R, buf.data() + 28, 2);
      std::memcpy(&G, buf.data() + 30, 2);
      std::memcpy(&B, buf.data() + 32, 2);
      p.r = static_cast<std::uint8_t>((std::min)(255, static_cast<int>(R / 256)));
      p.g = static_cast<std::uint8_t>((std::min)(255, static_cast<int>(G / 256)));
      p.b = static_cast<std::uint8_t>((std::min)(255, static_cast<int>(B / 256)));
    }
    outPts->push_back(p);
  }
  return outPts->empty() ? 7 : 0;
}

struct ObjData {
  std::vector<std::array<double, 3>> v;
  std::vector<std::array<double, 2>> vt;
  struct Tri {
    int iv[3];
    int it[3];
    bool hasUv = false;
  };
  std::vector<Tri> tris;
  std::wstring mtlLib;
};

static bool ParseObjFile(const std::filesystem::path& objPath, ObjData* out) {
  if (!out) {
    return false;
  }
  *out = ObjData{};
  std::ifstream ifs(objPath);
  if (!ifs) {
    return false;
  }
  std::string line;
  while (std::getline(ifs, line)) {
    if (line.empty()) {
      continue;
    }
    std::istringstream iss(line);
    std::string tag;
    iss >> tag;
    if (tag == "v") {
      double x = 0, y = 0, z = 0;
      if (iss >> x >> y >> z) {
        out->v.push_back({x, y, z});
      }
    } else if (tag == "vt") {
      double u = 0, vv = 0;
      if (iss >> u >> vv) {
        out->vt.push_back({u, vv});
      }
    } else if (tag == "mtllib") {
      std::string rest;
      std::getline(iss, rest);
      size_t a = rest.find_first_not_of(" \t");
      if (a != std::string::npos) {
        out->mtlLib = (objPath.parent_path() / std::filesystem::path(rest.substr(a))).wstring();
      }
    } else if (tag == "f") {
      struct V {
        int vi;
        int ti;
        bool hasT;
      };
      std::vector<V> poly;
      std::string tok;
      while (iss >> tok) {
        int vi = 0, ti = 0;
        size_t p1 = tok.find('/');
        if (p1 == std::string::npos) {
          vi = std::atoi(tok.c_str());
        } else {
          vi = std::atoi(tok.substr(0, p1).c_str());
          if (p1 + 1 < tok.size() && tok[p1 + 1] != '/') {
            size_t p2 = tok.find('/', p1 + 1);
            std::string vtstr = (p2 == std::string::npos) ? tok.substr(p1 + 1) : tok.substr(p1 + 1, p2 - p1 - 1);
            ti = std::atoi(vtstr.c_str());
          }
        }
        if (vi == 0) {
          continue;
        }
        const bool hasT = p1 != std::string::npos && p1 + 1 < tok.size() && tok[p1 + 1] != '/';
        poly.push_back({vi, ti, hasT});
      }
      if (poly.size() < 3) {
        continue;
      }
      for (size_t k = 1; k + 1 < poly.size(); ++k) {
        ObjData::Tri t{};
        const V& a = poly[0];
        const V& b = poly[k];
        const V& c = poly[k + 1];
        t.iv[0] = a.vi;
        t.iv[1] = b.vi;
        t.iv[2] = c.vi;
        t.hasUv = a.hasT && b.hasT && c.hasT;
        t.it[0] = a.ti;
        t.it[1] = b.ti;
        t.it[2] = c.ti;
        out->tris.push_back(t);
      }
    }
  }
  return !out->v.empty() && !out->tris.empty();
}

static std::wstring FindMapKdInMtl(const std::filesystem::path& mtlPath) {
  std::ifstream ifs(mtlPath);
  if (!ifs) {
    return L"";
  }
  std::string line;
  while (std::getline(ifs, line)) {
    size_t i = 0;
    while (i < line.size() && (line[i] == ' ' || line[i] == '\t')) {
      ++i;
    }
    if (i + 6 <= line.size() && line.compare(i, 6, "map_Kd") == 0) {
      size_t j = i + 6;
      while (j < line.size() && (line[j] == ' ' || line[j] == '\t')) {
        ++j;
      }
      std::string pathRest = line.substr(j);
      while (!pathRest.empty() &&
             (pathRest.back() == '\r' || pathRest.back() == ' ' || pathRest.back() == '\t')) {
        pathRest.pop_back();
      }
      if (!pathRest.empty()) {
        return (mtlPath.parent_path() / std::filesystem::path(pathRest).wstring()).wstring();
      }
    }
  }
  return L"";
}

#if GIS_DESKTOP_HAVE_GDAL
static int ReadTextureRgbFull(const std::wstring& path, int maxDim, std::vector<unsigned char>* rgb, int* outW,
                              int* outH) {
  if (!rgb || !outW || !outH) {
    return 7;
  }
  rgb->clear();
  *outW = 0;
  *outH = 0;
  static bool reg = false;
  if (!reg) {
    GDALAllRegister();
    reg = true;
  }
  const std::string u8 = WideToUtf8(path);
  if (u8.empty()) {
    return 3;
  }
  GDALDatasetH ds = GDALOpenEx(u8.c_str(), GDAL_OF_RASTER, nullptr, nullptr, nullptr);
  if (!ds) {
    return 3;
  }
  const int W = GDALGetRasterXSize(ds);
  const int H = GDALGetRasterYSize(ds);
  const int bc = GDALGetRasterCount(ds);
  if (W < 1 || H < 1 || bc < 1) {
    GDALClose(ds);
    return 7;
  }
  int tw = W;
  int th = H;
  if (tw > maxDim || th > maxDim) {
    const double s = (std::max)(static_cast<double>(tw) / maxDim, static_cast<double>(th) / maxDim);
    tw = (std::max)(2, static_cast<int>(tw / s));
    th = (std::max)(2, static_cast<int>(th / s));
  }
  rgb->resize(static_cast<size_t>(tw) * th * 3);
  std::vector<unsigned char> r(static_cast<size_t>(tw) * th);
  std::vector<unsigned char> g(static_cast<size_t>(tw) * th);
  std::vector<unsigned char> b(static_cast<size_t>(tw) * th);
  if (bc >= 3) {
    GDALRasterIO(GDALGetRasterBand(ds, 1), GF_Read, 0, 0, W, H, r.data(), tw, th, GDT_Byte, 0, 0);
    GDALRasterIO(GDALGetRasterBand(ds, 2), GF_Read, 0, 0, W, H, g.data(), tw, th, GDT_Byte, 0, 0);
    GDALRasterIO(GDALGetRasterBand(ds, 3), GF_Read, 0, 0, W, H, b.data(), tw, th, GDT_Byte, 0, 0);
  } else {
    GDALRasterIO(GDALGetRasterBand(ds, 1), GF_Read, 0, 0, W, H, r.data(), tw, th, GDT_Byte, 0, 0);
    g = r;
    b = r;
  }
  GDALClose(ds);
  for (int i = 0; i < tw * th; ++i) {
    (*rgb)[static_cast<size_t>(i) * 3 + 0] = r[static_cast<size_t>(i)];
    (*rgb)[static_cast<size_t>(i) * 3 + 1] = g[static_cast<size_t>(i)];
    (*rgb)[static_cast<size_t>(i) * 3 + 2] = b[static_cast<size_t>(i)];
  }
  *outW = tw;
  *outH = th;
  return 0;
}
#endif

static bool BarycentricUv(double u0, double v0, double u1, double v1, double u2, double v2, double pu, double pv,
                          double* w0, double* w1, double* w2) {
  const double denom = (v1 - v2) * (u0 - u2) + (u2 - u1) * (v0 - v2);
  if (std::fabs(denom) < 1e-30) {
    return false;
  }
  *w0 = ((v1 - v2) * (pu - u2) + (u2 - u1) * (pv - v2)) / denom;
  *w1 = ((v2 - v0) * (pu - u2) + (u0 - u2) * (pv - v2)) / denom;
  *w2 = 1.0 - *w0 - *w1;
  return *w0 >= -1e-5 && *w1 >= -1e-5 && *w2 >= -1e-5;
}

static int ConvertMeshObjToLasJob(const ConvertArgs& args) {
  std::filesystem::path inObj(args.input);
  if (_wcsicmp(inObj.extension().wstring().c_str(), L".obj") != 0) {
    std::wcerr << L"[WARN] 输入非 .obj，仍将尝试按 OBJ 解析。\n";
  }
  ObjData obj;
  if (!ParseObjFile(inObj, &obj)) {
    std::wcerr << L"[ERROR] 无法解析 OBJ 或无三角面。\n";
    return 7;
  }
  std::wstring texPath = L"";
  if (!obj.mtlLib.empty() && std::filesystem::exists(obj.mtlLib)) {
    texPath = FindMapKdInMtl(obj.mtlLib);
  }
  std::vector<unsigned char> rgb;
  int tw = 0;
  int th = 0;
#if GIS_DESKTOP_HAVE_GDAL
  if (!texPath.empty() && std::filesystem::exists(texPath)) {
    if (ReadTextureRgbFull(texPath, 2048, &rgb, &tw, &th) != 0) {
      rgb.clear();
    }
  }
#else
  (void)texPath;
#endif
  const int step = (std::max)(1, args.mesh_spacing);
  std::vector<LasPointFlt> pts;
  pts.reserve(static_cast<size_t>((tw / step + 2) * (th / step + 2)) * (std::max)(size_t(4), obj.tris.size() / 4));

  for (const auto& tri : obj.tris) {
    auto idxV = [&](int i) -> const std::array<double, 3>& {
      const int k = tri.iv[i];
      const size_t kk = (k > 0) ? static_cast<size_t>(k - 1) : obj.v.size() - static_cast<size_t>(-k);
      return obj.v[(std::min)(kk, obj.v.size() - 1)];
    };
    double u0 = 0, v0 = 0, u1 = 0, v1 = 0, u2 = 0, v2 = 0;
    bool haveUv = tri.hasUv && !obj.vt.empty();
    if (haveUv) {
      auto idxT = [&](int i) -> const std::array<double, 2>& {
        const int k = tri.it[i];
        const size_t kk = (k > 0) ? static_cast<size_t>(k - 1) : obj.vt.size() - static_cast<size_t>(-k);
        return obj.vt[(std::min)(kk, obj.vt.size() - 1)];
      };
      u0 = idxT(0)[0];
      v0 = idxT(0)[1];
      u1 = idxT(1)[0];
      v1 = idxT(1)[1];
      u2 = idxT(2)[0];
      v2 = idxT(2)[1];
    }
    const auto& p0 = idxV(0);
    const auto& p1 = idxV(1);
    const auto& p2 = idxV(2);
    if (tw > 1 && th > 1 && haveUv && !rgb.empty()) {
      double minu = (std::min)({u0, u1, u2});
      double maxu = (std::max)({u0, u1, u2});
      double minv = (std::min)({v0, v1, v2});
      double maxv = (std::max)({v0, v1, v2});
      int i0 = static_cast<int>(std::floor(minu * tw - 0.5));
      int i1 = static_cast<int>(std::ceil(maxu * tw + 0.5));
      int j0 = static_cast<int>(std::floor((1.0 - maxv) * th - 0.5));
      int j1 = static_cast<int>(std::ceil((1.0 - minv) * th + 0.5));
      i0 = (std::max)(0, (std::min)(tw - 1, i0));
      i1 = (std::max)(0, (std::min)(tw - 1, i1));
      j0 = (std::max)(0, (std::min)(th - 1, j0));
      j1 = (std::max)(0, (std::min)(th - 1, j1));
      for (int jj = j0; jj <= j1; jj += step) {
        for (int ii = i0; ii <= i1; ii += step) {
          const double uu = (static_cast<double>(ii) + 0.5) / static_cast<double>(tw);
          const double vv = 1.0 - (static_cast<double>(jj) + 0.5) / static_cast<double>(th);
          double w0 = 0, w1 = 0, w2 = 0;
          if (!BarycentricUv(u0, v0, u1, v1, u2, v2, uu, vv, &w0, &w1, &w2)) {
            continue;
          }
          const double px = w0 * p0[0] + w1 * p1[0] + w2 * p2[0];
          const double py = w0 * p0[1] + w1 * p1[1] + w2 * p2[1];
          const double pz = w0 * p0[2] + w1 * p1[2] + w2 * p2[2];
          const size_t ti = (static_cast<size_t>(jj) * tw + ii) * 3;
          LasPointFlt lp;
          lp.x = px;
          lp.y = py;
          lp.z = pz;
          lp.r = rgb[ti + 0];
          lp.g = rgb[ti + 1];
          lp.b = rgb[ti + 2];
          pts.push_back(lp);
        }
      }
    } else {
      for (int k = 0; k < 3; ++k) {
        LasPointFlt lp;
        lp.x = idxV(k)[0];
        lp.y = idxV(k)[1];
        lp.z = idxV(k)[2];
        pts.push_back(lp);
      }
    }
  }
  if (pts.empty()) {
    return 8;
  }
  std::filesystem::path outLas = args.output;
  std::wstring ext = outLas.extension().wstring();
  for (auto& c : ext) {
    c = static_cast<wchar_t>(std::towlower(c));
  }
  if (ext == L".laz") {
    std::wcerr << L"[WARN] LAZ 压缩需 LASzip；改输出为同目录 .las。\n";
    outLas.replace_extension(L".las");
  } else if (ext != L".las") {
    outLas.replace_extension(L".las");
  }
  std::wcout << L"[OUT] LAS: " << outLas.wstring() << L" 点数=" << pts.size() << L"\n";
  return WriteLas12Pdrf2(outLas, pts);
}

static std::wstring NormalizeTextureFmtExt(const std::wstring& fmt) {
  std::wstring f = fmt;
  for (auto& c : f) {
    if (c >= L'A' && c <= L'Z') {
      c = static_cast<wchar_t>(c - L'A' + L'a');
    }
  }
  if (f.find(L"tif") != std::wstring::npos) {
    return L".tif";
  }
  if (f.find(L"bmp") != std::wstring::npos) {
    return L".bmp";
  }
  if (f.find(L"tga") != std::wstring::npos) {
    return L".tga";
  }
  return L".png";
}

static int ConvertLasToMeshObjJob(const ConvertArgs& args) {
  std::vector<LasPointFlt> pts;
  if (ReadLasPoints(args.input, &pts) != 0) {
    std::wcerr << L"[ERROR] 读取 LAS 失败。\n";
    return 7;
  }
  double minx = pts[0].x, maxx = pts[0].x, miny = pts[0].y, maxy = pts[0].y;
  for (const auto& p : pts) {
    minx = (std::min)(minx, p.x);
    maxx = (std::max)(maxx, p.x);
    miny = (std::min)(miny, p.y);
    maxy = (std::max)(maxy, p.y);
  }
  const double spanx = (std::max)(1e-12, maxx - minx);
  const double spany = (std::max)(1e-12, maxy - miny);
  int nx = static_cast<int>(std::sqrt(static_cast<double>(pts.size())));
  nx = (std::clamp)(nx, 16, 512);
  int ny = nx;
  if (spanx > spany * 1.5) {
    ny = (std::max)(16, nx * static_cast<int>(spany / spanx));
  } else if (spany > spanx * 1.5) {
    nx = (std::max)(16, ny * static_cast<int>(spanx / spany));
  }
  const int stepCell = (std::max)(1, args.mesh_spacing);
  nx = (std::max)(2, nx / stepCell);
  ny = (std::max)(2, ny / stepCell);
  const double dx = spanx / nx;
  const double dy = spany / ny;
  std::vector<int> cnt(static_cast<size_t>(nx) * ny, 0);
  std::vector<double> sz(static_cast<size_t>(nx) * ny, 0);
  std::vector<double> sr(static_cast<size_t>(nx) * ny, 0);
  std::vector<double> sg(static_cast<size_t>(nx) * ny, 0);
  std::vector<double> sb(static_cast<size_t>(nx) * ny, 0);
  for (const auto& p : pts) {
    int ix = static_cast<int>((p.x - minx) / dx);
    int iy = static_cast<int>((p.y - miny) / dy);
    ix = (std::clamp)(ix, 0, nx - 1);
    iy = (std::clamp)(iy, 0, ny - 1);
    const int idx = iy * nx + ix;
    cnt[static_cast<size_t>(idx)]++;
    sz[static_cast<size_t>(idx)] += p.z;
    sr[static_cast<size_t>(idx)] += p.r;
    sg[static_cast<size_t>(idx)] += p.g;
    sb[static_cast<size_t>(idx)] += p.b;
  }
  double meanZ = 0;
  for (const auto& p : pts) {
    meanZ += p.z;
  }
  meanZ /= static_cast<double>(pts.size());
  for (size_t i = 0; i < cnt.size(); ++i) {
    if (cnt[i] <= 0) {
      sz[i] = meanZ;
      sr[i] = sg[i] = sb[i] = 180.0;
    } else {
      const double inv = 1.0 / static_cast<double>(cnt[i]);
      sz[i] *= inv;
      sr[i] *= inv;
      sg[i] *= inv;
      sb[i] *= inv;
    }
  }
  std::vector<unsigned char> tex(static_cast<size_t>(nx) * ny * 3);
  for (int j = 0; j < ny; ++j) {
    for (int i = 0; i < nx; ++i) {
      const size_t idx = static_cast<size_t>(j) * nx + i;
      tex[idx * 3 + 0] = static_cast<unsigned char>((std::clamp)(static_cast<int>(std::lround(sr[idx])), 0, 255));
      tex[idx * 3 + 1] = static_cast<unsigned char>((std::clamp)(static_cast<int>(std::lround(sg[idx])), 0, 255));
      tex[idx * 3 + 2] = static_cast<unsigned char>((std::clamp)(static_cast<int>(std::lround(sb[idx])), 0, 255));
    }
  }
  std::filesystem::path outObj = args.output;
  if (outObj.extension().empty() || _wcsicmp(outObj.extension().wstring().c_str(), L".obj") != 0) {
    if (std::filesystem::is_directory(outObj)) {
      outObj /= L"pointcloud_mesh.obj";
    } else {
      outObj.replace_extension(L".obj");
    }
  }
  const std::wstring texExt = NormalizeTextureFmtExt(args.texture_format);
  const std::wstring texName = outObj.stem().wstring() + L"_albedo" + texExt;
  const std::filesystem::path texPath = outObj.parent_path() / texName;
  int tr = WriteRgbTextureFile(texPath, nx, ny, tex, args.texture_format);
  if (tr != 0) {
    return tr;
  }
  const std::wstring mtlName = outObj.stem().wstring() + L".mtl";
  std::wstring objText = std::wstring(kObjFileFormatBanner30);
  objText += L"# AGIS: point cloud -> mesh (grid)\n";
  objText += L"mtllib " + mtlName + L"\n";
  objText += L"usemtl pcMat\n";
  objText += L"o pointcloud_mesh\n";
  for (int j = 0; j < ny; ++j) {
    for (int i = 0; i < nx; ++i) {
      const size_t idx = static_cast<size_t>(j) * nx + i;
      const double x = minx + (i + 0.5) * dx;
      const double y = miny + (j + 0.5) * dy;
      const double z = sz[idx];
      wchar_t buf[160]{};
      swprintf_s(buf, L"v %.12g %.12g %.12g\n", x, y, z);
      objText += buf;
    }
  }
  for (int j = 0; j < ny; ++j) {
    for (int i = 0; i < nx; ++i) {
      const double u = nx > 1 ? static_cast<double>(i) / static_cast<double>(nx - 1) : 0.5;
      const double v = ny > 1 ? static_cast<double>(j) / static_cast<double>(ny - 1) : 0.5;
      wchar_t buf[96]{};
      swprintf_s(buf, L"vt %.12g %.12g\n", u, v);
      objText += buf;
    }
  }
  auto vidx = [&](int i, int j) -> int { return j * nx + i + 1; };
  auto vtidx = [&](int i, int j) -> int { return j * nx + i + 1; };
  for (int j = 0; j < ny - 1; ++j) {
    for (int i = 0; i < nx - 1; ++i) {
      const int v00 = vidx(i, j);
      const int v10 = vidx(i + 1, j);
      const int v01 = vidx(i, j + 1);
      const int v11 = vidx(i + 1, j + 1);
      const int t00 = vtidx(i, j);
      const int t10 = vtidx(i + 1, j);
      const int t01 = vtidx(i, j + 1);
      const int t11 = vtidx(i + 1, j + 1);
      wchar_t bf[256]{};
      swprintf_s(bf, L"f %d/%d %d/%d %d/%d\n", v00, t00, v10, t10, v01, t01);
      objText += bf;
      swprintf_s(bf, L"f %d/%d %d/%d %d/%d\n", v10, t10, v11, t11, v01, t01);
      objText += bf;
    }
  }
  int wr = WriteTextFile(outObj, objText);
  if (wr != 0) {
    return wr;
  }
  const std::wstring mtlText =
      L"newmtl pcMat\n"
      L"Kd 1 1 1\n"
      L"map_Kd " +
      texName + L"\n";
  return WriteTextFile(outObj.parent_path() / mtlName, mtlText);
}

int ConvertModelToModelImpl(const ConvertArgs& args) {
  const bool inPc = ModelSubtypeIsPointCloudW(args.input_subtype);
  const bool outPc = ModelSubtypeIsPointCloudW(args.output_subtype);
  const bool inMesh = ModelSubtypeIsMeshW(args.input_subtype);
  const bool outMesh = ModelSubtypeIsMeshW(args.output_subtype);
  if (inMesh && outPc) {
    return ConvertMeshObjToLasJob(args);
  }
  if (inPc && outMesh) {
    return ConvertLasToMeshObjJob(args);
  }
  std::wcerr << L"[ERROR] 模型↔模型：当前实现支持「3DMesh ↔ 点云（LAS）」子类型互转；请检查输入/输出子类型。\n";
  return 9;
}

int ConvertGisToTileImpl(const ConvertArgs& args) {
  const std::filesystem::path outDir(args.output);
  int rc = EnsureDir(outDir);
  if (rc != 0) return rc;
  rc = EnsureDir(outDir / L"0" / L"0");
  if (rc != 0) return rc;
  return WriteTextFile(outDir / L"0" / L"0" / L"0.tile.txt",
                       L"AGIS mock tile\nsource=" + args.input + L"\nsubtype=" + args.output_subtype + L"\n");
}

int ConvertModelToGisImpl(const ConvertArgs& args) {
  const std::wstring geojson =
      L"{\n"
      L"  \"type\": \"FeatureCollection\",\n"
      L"  \"name\": \"agis_model_to_gis\",\n"
      L"  \"features\": [{\"type\":\"Feature\",\"properties\":{\"source\":\"" + args.input +
      L"\"},\"geometry\":{\"type\":\"Point\",\"coordinates\":[0.0,0.0]}}]\n"
      L"}\n";
  return WriteTextFile(args.output, geojson);
}

int ConvertModelToTileImpl(const ConvertArgs& args) {
  return ConvertGisToTileImpl(args);
}

int ConvertTileToGisImpl(const ConvertArgs& args) {
  const std::wstring gpkgMock =
      L"AGIS mock GIS dataset\nfrom tile source: " + args.input + L"\noutput subtype: " + args.output_subtype + L"\n";
  return WriteTextFile(args.output, gpkgMock);
}

int ConvertTileToModelImpl(const ConvertArgs& args) {
  std::filesystem::path outPath(args.output);
  std::error_code ec;
  if (std::filesystem::is_directory(outPath, ec) || outPath.extension().empty()) {
    std::filesystem::create_directories(outPath, ec);
    outPath /= L"tile_model.obj";
  }
  std::wcout << L"[OUT] model file: " << outPath.wstring() << L"\n";
  return WriteTextFile(outPath, std::wstring(kObjFileFormatBanner30) + L"# AGIS mock model from tile source\n# source: " +
                                    args.input +
                                    L"\nmtllib tile_model.mtl\no tile_model\nv 0 0 0\nv 1 0 0\nv 0 1 0\nf 1 2 3\n");
}

}  // namespace

int ConvertGisToModel(const ConvertArgs& args) { return ConvertGisToModelImpl(args); }
int ConvertGisToTile(const ConvertArgs& args) { return ConvertGisToTileImpl(args); }
int ConvertModelToGis(const ConvertArgs& args) { return ConvertModelToGisImpl(args); }
int ConvertModelToModel(const ConvertArgs& args) { return ConvertModelToModelImpl(args); }
int ConvertModelToTile(const ConvertArgs& args) { return ConvertModelToTileImpl(args); }
int ConvertTileToGis(const ConvertArgs& args) { return ConvertTileToGisImpl(args); }
int ConvertTileToModel(const ConvertArgs& args) { return ConvertTileToModelImpl(args); }
