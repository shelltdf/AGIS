#include "tools/convert_backend_common.h"

#include "utils/agis_gdal_runtime_env.h"

#include <algorithm>
#include <chrono>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <sstream>
#include <thread>
#include <vector>
#include <array>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <cwctype>
#include <string>
#include <regex>
#include <windows.h>
#if defined(AGIS_HAVE_LASZIP) && AGIS_HAVE_LASZIP
#include <laszip_api.h>
#endif
#if GIS_DESKTOP_HAVE_GDAL
#include <gdal.h>
#include <gdal_utils.h>
#include <cpl_conv.h>
#include <ogr_api.h>
#include <ogr_spatialref.h>
#endif
#if defined(AGIS_HAVE_BASISU) && AGIS_HAVE_BASISU
#include "tools/ktx2_basis_encode.h"
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
      L"--texture-format", L"--raster-max-dim", L"--tile-levels", L"--obj-fp-type", L"--tile-max-memory-mb",
      L"--obj-texture-mode", L"--obj-visual-effect", L"--obj-snow-scale", L"--gis-dem-interp", L"--gis-mesh-topology",
      L"--model-budget-mode", L"--model-budget-mb", L"--gis-model-split-parts",
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

void EnableRealtimeConsoleFlush() {
  std::wcout.setf(std::ios::unitbuf);
  std::wcerr.setf(std::ios::unitbuf);
  setvbuf(stdout, nullptr, _IONBF, 0);
  setvbuf(stderr, nullptr, _IONBF, 0);
}

void PrintConvertCliHelpGrouped(std::wostream& os, bool chinese, const wchar_t* input_type_lines,
                                const wchar_t* input_subtype_lines, const wchar_t* output_type_lines,
                                const wchar_t* output_subtype_lines) {
  const wchar_t* it = input_type_lines ? input_type_lines : L"";
  const wchar_t* ist = input_subtype_lines ? input_subtype_lines : L"";
  const wchar_t* ot = output_type_lines ? output_type_lines : L"";
  const wchar_t* ost = output_subtype_lines ? output_subtype_lines : L"";
  if (chinese) {
    os << L"\n【1 输入类型】（桌面数据转换窗口 ① / --input-type）\n"
       << L"  --input-type <token>\n"
       << L"      主类型标识（本工具语义见下）。\n"
       << it << L"\n【2 输入子类型】（桌面 ② / --input-subtype）\n"
       << L"  --input-subtype <token>\n"
       << L"      与输入数据形态/格式相关的子分类。\n"
       << ist << L"\n【3 输出类型】（桌面 ③ / --output-type）\n"
       << L"  --output-type <token>\n"
       << L"      输出主类型标识。\n"
       << ot << L"\n【4 输出子类型】（桌面 ④ / --output-subtype）\n"
       << L"  --output-subtype <token>\n"
       << L"      与输出形态/格式相关的子分类。\n"
       << ost << L"\n";
  } else {
    os << L"\n[1] Input type (desktop group ① / --input-type)\n"
       << L"  --input-type <token>\n"
       << L"      Major input category (see below for this tool).\n"
       << it << L"\n[2] Input subtype (desktop ② / --input-subtype)\n"
       << L"  --input-subtype <token>\n"
       << L"      Input format/shape subtype.\n"
       << ist << L"\n[3] Output type (desktop ③ / --output-type)\n"
       << L"  --output-type <token>\n"
       << L"      Major output category.\n"
       << ot << L"\n[4] Output subtype (desktop ④ / --output-subtype)\n"
       << L"  --output-subtype <token>\n"
       << L"      Output format/shape subtype.\n"
       << ost << L"\n";
  }
}

void PrintConvertCliIoSection(std::wostream& os, bool chinese, const wchar_t* lines) {
  if (!lines || !*lines) {
    return;
  }
  if (chinese) {
    os << L"\n【输入/输出文件（路径形态与扩展名）】\n" << lines << L"\n";
  } else {
    os << L"\n[Input/output files (path shape and extensions)]\n" << lines << L"\n";
  }
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
  out->raster_read_max_dim = 0;
  const std::wstring rmax = ArgValue(argc, argv, L"--raster-max-dim");
  if (!rmax.empty()) {
    wchar_t* end = nullptr;
    const long v = wcstol(rmax.c_str(), &end, 10);
    if (end == rmax.c_str()) {
      std::wcerr << L"[ERROR] invalid --raster-max-dim.\n";
      return false;
    }
    if (v == 0) {
      out->raster_read_max_dim = 0;
    } else if (v >= 64 && v <= 16384) {
      out->raster_read_max_dim = static_cast<int>(v);
    } else {
      std::wcerr << L"[ERROR] invalid --raster-max-dim, expected 0|64..16384.\n";
      return false;
    }
  }
  out->tile_levels = -1;
  const std::wstring zlvl = ArgValue(argc, argv, L"--tile-levels");
  if (!zlvl.empty()) {
    if (_wcsicmp(zlvl.c_str(), L"auto") == 0) {
      out->tile_levels = -1;
    } else {
      const long v = wcstol(zlvl.c_str(), nullptr, 10);
      if (v >= 1 && v <= 23) {
        out->tile_levels = static_cast<int>(v);
      } else {
        std::wcerr << L"[ERROR] invalid --tile-levels, expected auto|1..23.\n";
        return false;
      }
    }
  }
  out->tile_max_memory_mb = 512;
  const std::wstring memLimit = ArgValue(argc, argv, L"--tile-max-memory-mb");
  if (!memLimit.empty()) {
    const long v = wcstol(memLimit.c_str(), nullptr, 10);
    if (v >= 64 && v <= 131072) {
      out->tile_max_memory_mb = static_cast<int>(v);
    }
  }
  out->obj_fp_type = ArgValue(argc, argv, L"--obj-fp-type");
  if (out->obj_fp_type.empty()) {
    out->obj_fp_type = L"double";
  } else {
    for (auto& c : out->obj_fp_type) {
      if (c >= L'A' && c <= L'Z') {
        c = static_cast<wchar_t>(c - L'A' + L'a');
      }
    }
    if (out->obj_fp_type != L"float" && out->obj_fp_type != L"double") {
      std::wcerr << L"[ERROR] invalid --obj-fp-type, expected float|double.\n";
      return false;
    }
  }
  out->obj_texture_mode = ArgValue(argc, argv, L"--obj-texture-mode");
  if (out->obj_texture_mode.empty()) {
    out->obj_texture_mode = L"color";
  }
  for (auto& c : out->obj_texture_mode) {
    if (c >= L'A' && c <= L'Z') {
      c = static_cast<wchar_t>(c - L'A' + L'a');
    }
  }
  if (out->obj_texture_mode != L"color" && out->obj_texture_mode != L"pbr") {
    std::wcerr << L"[ERROR] invalid --obj-texture-mode, expected color|pbr.\n";
    return false;
  }
  out->obj_visual_effect = ArgValue(argc, argv, L"--obj-visual-effect");
  if (out->obj_visual_effect.empty()) {
    out->obj_visual_effect = L"none";
  }
  for (auto& c : out->obj_visual_effect) {
    if (c >= L'A' && c <= L'Z') {
      c = static_cast<wchar_t>(c - L'A' + L'a');
    }
  }
  if (out->obj_visual_effect != L"none" && out->obj_visual_effect != L"night" && out->obj_visual_effect != L"snow") {
    std::wcerr << L"[ERROR] invalid --obj-visual-effect, expected none|night|snow.\n";
    return false;
  }
  out->obj_snow_scale = 1.0;
  const std::wstring snowStr = ArgValue(argc, argv, L"--obj-snow-scale");
  if (!snowStr.empty()) {
    const double s = _wtof(snowStr.c_str());
    if (std::isfinite(s) && s >= 0.25 && s <= 8.0) {
      out->obj_snow_scale = s;
    } else {
      std::wcerr << L"[ERROR] invalid --obj-snow-scale, expected 0.25..8.\n";
      return false;
    }
  }
  out->gis_dem_interp = ArgValue(argc, argv, L"--gis-dem-interp");
  if (out->gis_dem_interp.empty()) {
    out->gis_dem_interp = L"bilinear";
  }
  for (auto& c : out->gis_dem_interp) {
    if (c >= L'A' && c <= L'Z') {
      c = static_cast<wchar_t>(c - L'A' + L'a');
    }
  }
  if (out->gis_dem_interp != L"bilinear" && out->gis_dem_interp != L"nearest" && out->gis_dem_interp != L"cell_avg" &&
      out->gis_dem_interp != L"bicubic" && out->gis_dem_interp != L"median" && out->gis_dem_interp != L"average" &&
      out->gis_dem_interp != L"dem_avg") {
    std::wcerr << L"[ERROR] invalid --gis-dem-interp, expected bilinear|nearest|cell_avg|average|dem_avg|median|bicubic.\n";
    return false;
  }
  out->gis_mesh_topology = ArgValue(argc, argv, L"--gis-mesh-topology");
  if (out->gis_mesh_topology.empty()) {
    out->gis_mesh_topology = L"grid";
  }
  for (auto& c : out->gis_mesh_topology) {
    if (c >= L'A' && c <= L'Z') {
      c = static_cast<wchar_t>(c - L'A' + L'a');
    }
  }
  if (out->gis_mesh_topology == L"tin") {
    out->gis_mesh_topology = L"grid";
  }
  if (out->gis_mesh_topology != L"grid" && out->gis_mesh_topology != L"delaunay") {
    std::wcerr << L"[ERROR] invalid --gis-mesh-topology, expected grid|tin|delaunay.\n";
    return false;
  }
  out->model_budget_mode = ArgValue(argc, argv, L"--model-budget-mode");
  if (out->model_budget_mode.empty()) {
    out->model_budget_mode = L"memory";
  }
  for (auto& c : out->model_budget_mode) {
    if (c >= L'A' && c <= L'Z') {
      c = static_cast<wchar_t>(c - L'A' + L'a');
    }
  }
  if (out->model_budget_mode != L"memory" && out->model_budget_mode != L"file" && out->model_budget_mode != L"vram") {
    std::wcerr << L"[ERROR] invalid --model-budget-mode, expected memory|file|vram.\n";
    return false;
  }
  out->model_budget_mb = 4096;
  const std::wstring budMb = ArgValue(argc, argv, L"--model-budget-mb");
  if (!budMb.empty()) {
    wchar_t* end = nullptr;
    const long long v = wcstoll(budMb.c_str(), &end, 10);
    if (end == budMb.c_str() || v < 256 || v > 262144) {
      std::wcerr << L"[ERROR] invalid --model-budget-mb, expected 256..262144 (MB).\n";
      return false;
    }
    out->model_budget_mb = v;
  }
  out->gis_model_split_parts = 1;
  const std::wstring splitPartsStr = ArgValue(argc, argv, L"--gis-model-split-parts");
  if (!splitPartsStr.empty()) {
    const long v = wcstol(splitPartsStr.c_str(), nullptr, 10);
    if (v >= 1 && v <= 512) {
      out->gis_model_split_parts = static_cast<int>(v);
    } else {
      std::wcerr << L"[ERROR] invalid --gis-model-split-parts, expected 1..512.\n";
      return false;
    }
  }
  const std::wstring fields[] = {
      out->input_type,      out->input_subtype, out->output_type, out->output_subtype,
      out->coord_system,    out->vector_mode,   out->target_crs,  out->output_unit,
      out->texture_format,  out->obj_fp_type,   out->obj_texture_mode, out->obj_visual_effect,
      out->gis_dem_interp,  out->gis_mesh_topology, out->model_budget_mode,
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
  std::wcout << L"  raster max read dim: "
             << (args.raster_read_max_dim <= 0 ? L"0 (native, no cap)" : std::to_wstring(args.raster_read_max_dim))
             << L"\n";
  std::wcout << L"  tile levels: " << (args.tile_levels < 0 ? L"auto" : std::to_wstring(args.tile_levels)) << L"\n";
  std::wcout << L"  tile merge memory limit (MB): " << args.tile_max_memory_mb << L"\n";
  std::wcout << L"  obj fp type: " << args.obj_fp_type << L"\n";
  std::wcout << L"  obj texture mode: " << args.obj_texture_mode << L"\n";
  std::wcout << L"  obj visual effect: " << args.obj_visual_effect;
  if (args.obj_visual_effect == L"snow") {
    std::wcout << L" (snow_scale=" << args.obj_snow_scale << L")";
  }
  std::wcout << L"\n";
  std::wcout << L"  gis dem interp: " << args.gis_dem_interp << L"\n";
  std::wcout << L"  gis mesh topology: " << args.gis_mesh_topology << L"\n";
  std::wcout << L"  model budget: " << args.model_budget_mode << L" " << args.model_budget_mb << L" MB\n";
  std::wcout << L"  gis model split parts: " << args.gis_model_split_parts << L"\n";
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

void AppendLe32(std::vector<unsigned char>* out, uint32_t v) {
  out->push_back(static_cast<unsigned char>(v & 0xff));
  out->push_back(static_cast<unsigned char>((v >> 8) & 0xff));
  out->push_back(static_cast<unsigned char>((v >> 16) & 0xff));
  out->push_back(static_cast<unsigned char>((v >> 24) & 0xff));
}

std::vector<unsigned char> BuildMinimalGlbV2() {
  const std::string json = "{\"asset\":{\"version\":\"2.0\"},\"scene\":0,\"scenes\":[{\"nodes\":[]}]}";
  std::vector<unsigned char> jsonChunk(json.begin(), json.end());
  while ((jsonChunk.size() % 4) != 0) {
    jsonChunk.push_back(' ');
  }
  const uint32_t jsonLen = static_cast<uint32_t>(jsonChunk.size());
  const uint32_t glbLen = 12u + 8u + jsonLen;
  std::vector<unsigned char> out;
  out.reserve(glbLen);
  // GLB header: magic, version, length
  AppendLe32(&out, 0x46546C67u);  // "glTF"
  AppendLe32(&out, 2u);
  AppendLe32(&out, glbLen);
  // JSON chunk
  AppendLe32(&out, jsonLen);
  AppendLe32(&out, 0x4E4F534Au);  // "JSON"
  out.insert(out.end(), jsonChunk.begin(), jsonChunk.end());
  return out;
}

std::vector<unsigned char> WrapGlbAsB3dm(const std::vector<unsigned char>& glb) {
  std::string featureJson = "{\"BATCH_LENGTH\":0}";
  while ((featureJson.size() % 8) != 0) {
    featureJson.push_back(' ');
  }
  const uint32_t ftJsonLen = static_cast<uint32_t>(featureJson.size());
  const uint32_t byteLength = 28u + ftJsonLen + static_cast<uint32_t>(glb.size());
  std::vector<unsigned char> out;
  out.reserve(byteLength);
  out.push_back('b');
  out.push_back('3');
  out.push_back('d');
  out.push_back('m');
  AppendLe32(&out, 1u);
  AppendLe32(&out, byteLength);
  AppendLe32(&out, ftJsonLen);
  AppendLe32(&out, 0u);
  AppendLe32(&out, 0u);
  AppendLe32(&out, 0u);
  out.insert(out.end(), featureJson.begin(), featureJson.end());
  out.insert(out.end(), glb.begin(), glb.end());
  return out;
}

std::vector<unsigned char> BuildMinimalB3dmWithGlb() {
  return WrapGlbAsB3dm(BuildMinimalGlbV2());
}

static void Wgs84GeodeticToEcef(double lonDeg, double latDeg, double hMeters, double out[3]) {
  constexpr double a = 6378137.0;
  constexpr double invF = 298.257223563;
  const double f = 1.0 / invF;
  const double e2 = 2.0 * f - f * f;
  const double lon = lonDeg * 3.14159265358979323846 / 180.0;
  const double lat = latDeg * 3.14159265358979323846 / 180.0;
  const double sinl = std::sin(lat);
  const double cosl = std::cos(lat);
  const double coso = std::cos(lon);
  const double sino = std::sin(lon);
  const double N = a / std::sqrt(1.0 - e2 * sinl * sinl);
  out[0] = (N + hMeters) * cosl * coso;
  out[1] = (N + hMeters) * cosl * sino;
  out[2] = (N * (1.0 - e2) + hMeters) * sinl;
}

static void EnuBasisWgs84(double lonDeg, double latDeg, double east[3], double north[3], double up[3]) {
  const double lat = latDeg * 3.14159265358979323846 / 180.0;
  const double lon = lonDeg * 3.14159265358979323846 / 180.0;
  up[0] = std::cos(lat) * std::cos(lon);
  up[1] = std::cos(lat) * std::sin(lon);
  up[2] = std::sin(lat);
  east[0] = -std::sin(lon);
  east[1] = std::cos(lon);
  east[2] = 0.0;
  north[0] = -std::sin(lat) * std::cos(lon);
  north[1] = -std::sin(lat) * std::sin(lon);
  north[2] = std::cos(lat);
}

static double Dot3d(const double a[3], const double b[3]) {
  return a[0] * b[0] + a[1] * b[1] + a[2] * b[2];
}

static void Sub3d(const double a[3], const double b[3], double o[3]) {
  o[0] = a[0] - b[0];
  o[1] = a[1] - b[1];
  o[2] = a[2] - b[2];
}

static void Cross3d(const double a[3], const double b[3], double o[3]) {
  o[0] = a[1] * b[2] - a[2] * b[1];
  o[1] = a[2] * b[0] - a[0] * b[2];
  o[2] = a[0] * b[1] - a[1] * b[0];
}

static double Len3d(const double v[3]) {
  return std::sqrt(Dot3d(v, v));
}

static void Normalize3d(double v[3]) {
  const double l = Len3d(v);
  if (l > 1e-30) {
    v[0] /= l;
    v[1] /= l;
    v[2] /= l;
  }
}


int EnsureDir(const std::filesystem::path& p) {
  std::error_code ec;
  std::filesystem::create_directories(p, ec);
  return ec ? 4 : 0;
}

#if GIS_DESKTOP_HAVE_GDAL
void EnsureGdalRegisteredOnce() {
  static bool s_registered = false;
  if (!s_registered) {
    AgisEnsureGdalDataPath();
    GDALAllRegister();
    s_registered = true;
  }
}
#endif

std::string ReadTextFileUtf8(const std::filesystem::path& p) {
  std::ifstream ifs(p, std::ios::binary);
  if (!ifs.is_open()) {
    return {};
  }
  std::ostringstream oss;
  oss << ifs.rdbuf();
  return oss.str();
}

std::vector<unsigned char> ReadBinaryFile(const std::filesystem::path& p) {
  std::ifstream ifs(p, std::ios::binary);
  if (!ifs.is_open()) {
    return {};
  }
  ifs.seekg(0, std::ios::end);
  const std::streamoff n = ifs.tellg();
  if (n <= 0) {
    return {};
  }
  ifs.seekg(0, std::ios::beg);
  std::vector<unsigned char> out(static_cast<size_t>(n));
  ifs.read(reinterpret_cast<char*>(out.data()), n);
  if (!ifs.good() && !ifs.eof()) {
    return {};
  }
  return out;
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
  /** GDAL 读入时的波段数（TryReadRaster 填充）。 */
  int bandCount = 0;
  /** 是否将 band1 浮点缓冲当作高程用于地形（单通道/双通道栅格通常为 DEM+掩膜）。 */
  bool useElevAsHeight = false;
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
  if (maxReadDim > 0) {
    int kMax = maxReadDim;
    if (kMax < 64) {
      kMax = 64;
    }
    if (kMax > 16384) {
      kMax = 16384;
    }
    if (w > kMax || h > kMax) {
      const double s = (std::max)(static_cast<double>(w) / kMax, static_cast<double>(h) / kMax);
      w = (std::max)(2, static_cast<int>(w / s));
      h = (std::max)(2, static_cast<int>(h / s));
      std::wcout << L"[RASTER] downsample read buffer: " << srcW << L"x" << srcH << L" -> " << w << L"x" << h
                 << L" (max side " << kMax << L" px)\n";
    }
  }
  out->w = w;
  out->h = h;
  out->bandCount = bands;
  out->useElevAsHeight = (bands <= 2);
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
#if GIS_DESKTOP_HAVE_GDAL
  EnsureGdalRegisteredOnce();
#endif
  GDALDriverH memDrv = GDALGetDriverByName("MEM");
  GDALDriverH pngDrv = GDALGetDriverByName("PNG");
  if (!memDrv || !pngDrv) {
    std::wcerr << L"[ERROR] PNG write driver missing (MEM/PNG).\n";
    return 7;
  }
  GDALDatasetH mem = GDALCreate(memDrv, "", w, h, 3, GDT_Byte, nullptr);
  if (!mem) {
    std::wcerr << L"[ERROR] PNG write temp dataset create failed.\n";
    return 7;
  }
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
  if (!outDs) {
    std::wcerr << L"[ERROR] PNG write create-copy failed: " << path.wstring() << L"\n";
    return 7;
  }
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
  if (f.find(L"ktx2") != std::wstring::npos || f.find(L"basis") != std::wstring::npos ||
      f.find(L"uastc") != std::wstring::npos) {
#if defined(AGIS_HAVE_BASISU) && AGIS_HAVE_BASISU
    const bool etc1s = (f.find(L"etc1s") != std::wstring::npos);
    return AgisWriteRgbToKtx2Basis(path, w, h, rgb, etc1s);
#else
    std::wcerr << L"[ERROR] KTX2/Basis 需要 3rdparty/basis_universal（见 3rdparty/README-BASIS-KTX2.md）。\n";
    return 7;
#endif
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

static float SampleNearestF(const std::vector<float>& e, int W, int H, double col, double row) {
  if (W <= 0 || H <= 0 || e.size() < static_cast<size_t>(W * H)) {
    return 0.f;
  }
  col = (std::clamp)(col, 0.0, static_cast<double>(W - 1));
  row = (std::clamp)(row, 0.0, static_cast<double>(H - 1));
  const int xi = static_cast<int>(std::lround(col));
  const int yi = static_cast<int>(std::lround(row));
  return e[static_cast<size_t>((std::clamp)(yi, 0, H - 1)) * W + (std::clamp)(xi, 0, W - 1)];
}

static float SampleCellAvgF(const std::vector<float>& e, int W, int H, double col, double row) {
  if (W <= 0 || H <= 0 || e.size() < static_cast<size_t>(W * H)) {
    return 0.f;
  }
  const int cx = static_cast<int>(std::floor(col + 0.5));
  const int cy = static_cast<int>(std::floor(row + 0.5));
  double sum = 0.0;
  int n = 0;
  for (int dy = -1; dy <= 1; ++dy) {
    for (int dx = -1; dx <= 1; ++dx) {
      const int x = (std::clamp)(cx + dx, 0, W - 1);
      const int y = (std::clamp)(cy + dy, 0, H - 1);
      sum += static_cast<double>(e[static_cast<size_t>(y) * W + x]);
      ++n;
    }
  }
  return static_cast<float>(sum / static_cast<double>((std::max)(1, n)));
}

static float SampleMedian3x3F(const std::vector<float>& e, int W, int H, double col, double row) {
  if (W <= 0 || H <= 0 || e.size() < static_cast<size_t>(W * H)) {
    return 0.f;
  }
  const int cx = static_cast<int>(std::floor(col + 0.5));
  const int cy = static_cast<int>(std::floor(row + 0.5));
  std::array<float, 9> buf{};
  int k = 0;
  for (int dy = -1; dy <= 1; ++dy) {
    for (int dx = -1; dx <= 1; ++dx) {
      const int x = (std::clamp)(cx + dx, 0, W - 1);
      const int y = (std::clamp)(cy + dy, 0, H - 1);
      buf[static_cast<size_t>(k++)] = e[static_cast<size_t>(y) * W + x];
    }
  }
  std::sort(buf.begin(), buf.begin() + k);
  return buf[static_cast<size_t>(k / 2)];
}

static double CubicHermite(double p0, double p1, double p2, double p3, double t) {
  const double a = -0.5 * p0 + 1.5 * p1 - 1.5 * p2 + 0.5 * p3;
  const double b = p0 - 2.5 * p1 + 2.0 * p2 - 0.5 * p3;
  const double c = -0.5 * p0 + 0.5 * p2;
  const double d = p1;
  return ((a * t + b) * t + c) * t + d;
}

static float SampleBicubicF(const std::vector<float>& e, int W, int H, double col, double row) {
  if (W <= 0 || H <= 0 || e.size() < static_cast<size_t>(W * H)) {
    return 0.f;
  }
  col = (std::clamp)(col, 0.0, static_cast<double>(W - 1));
  row = (std::clamp)(row, 0.0, static_cast<double>(H - 1));
  const int x1 = static_cast<int>(std::floor(col));
  const int y1 = static_cast<int>(std::floor(row));
  const double tx = col - x1;
  const double ty = row - y1;
  auto at = [&](int x, int y) -> double {
    x = (std::clamp)(x, 0, W - 1);
    y = (std::clamp)(y, 0, H - 1);
    return static_cast<double>(e[static_cast<size_t>(y) * W + x]);
  };
  const int xm1 = x1 - 1;
  const int xp2 = x1 + 2;
  const int ym1 = y1 - 1;
  const int yp2 = y1 + 2;
  double c0 = CubicHermite(at(xm1, ym1), at(x1, ym1), at(x1 + 1, ym1), at(xp2, ym1), tx);
  double c1 = CubicHermite(at(xm1, y1), at(x1, y1), at(x1 + 1, y1), at(xp2, y1), tx);
  double c2 = CubicHermite(at(xm1, y1 + 1), at(x1, y1 + 1), at(x1 + 1, y1 + 1), at(xp2, y1 + 1), tx);
  double c3 = CubicHermite(at(xm1, yp2), at(x1, yp2), at(x1 + 1, yp2), at(xp2, yp2), tx);
  return static_cast<float>(CubicHermite(c0, c1, c2, c3, ty));
}

static float SampleDemElevForMesh(const std::vector<float>& elev, int rw, int rh, double col, double row,
                                  const std::wstring& mode) {
  if (_wcsicmp(mode.c_str(), L"nearest") == 0) {
    return SampleNearestF(elev, rw, rh, col, row);
  }
  if (_wcsicmp(mode.c_str(), L"cell_avg") == 0 || _wcsicmp(mode.c_str(), L"average") == 0 ||
      _wcsicmp(mode.c_str(), L"dem_avg") == 0) {
    return SampleCellAvgF(elev, rw, rh, col, row);
  }
  if (_wcsicmp(mode.c_str(), L"median") == 0) {
    return SampleMedian3x3F(elev, rw, rh, col, row);
  }
  if (_wcsicmp(mode.c_str(), L"bicubic") == 0) {
    return SampleBicubicF(elev, rw, rh, col, row);
  }
  return SampleBilinearF(elev, rw, rh, col, row);
}

static std::int64_t EstimateGisModelChunkBytes(const std::wstring& budgetMode, int ni, int nj, int texW, int texH,
                                               bool wantPbr, bool objFloat) {
  if (ni < 2 || nj < 2 || texW < 1 || texH < 1) {
    return 0;
  }
  const std::int64_t verts = static_cast<std::int64_t>(ni) * static_cast<std::int64_t>(nj);
  const std::int64_t faces = static_cast<std::int64_t>(ni - 1) * static_cast<std::int64_t>(nj - 1) * 2;
  const int pbrMaps = wantPbr ? 5 : 1;
  const int bytesPerVertLine = objFloat ? 48 : 80;
  const std::int64_t objText = verts * bytesPerVertLine + faces * 72 + 4096;
  const std::int64_t texBytes = static_cast<std::int64_t>(texW) * static_cast<std::int64_t>(texH) * 4 * pbrMaps;
  const std::int64_t cpuMesh = verts * 40 + faces * 72;
  const std::int64_t vramTex = static_cast<std::int64_t>(static_cast<double>(texW) * texH * 4.0 * static_cast<double>(pbrMaps) * 1.33);
  const std::int64_t vramMesh = verts * 32 + faces * 36;
  if (_wcsicmp(budgetMode.c_str(), L"file") == 0) {
    const std::int64_t pngGuess = static_cast<std::int64_t>(texW) * texH * 3 * pbrMaps / 2 + objText;
    return (std::max)(objText + texBytes / 4, pngGuess);
  }
  if (_wcsicmp(budgetMode.c_str(), L"vram") == 0) {
    return vramTex + vramMesh;
  }
  return cpuMesh + texBytes;
}

#if GIS_DESKTOP_HAVE_GDAL
static void TerrainSampleRasterAtLonLat(const RasterExtract& r, bool useElevAsHeight, double lonDeg, double latDeg,
                                          double bLonMin, double bLonMax, double bLatMin, double bLatMax,
                                          OGRCoordinateTransformationH wgsToRaster, double* hMeters,
                                          unsigned char rgbOut[3]) {
  double col = 0.0;
  double row = 0.0;
  bool havePx = false;
  if (r.ok && r.w > 0 && r.h > 0) {
    double x = lonDeg, y = latDeg, z = 0.0;
    if (wgsToRaster && OCTTransform(wgsToRaster, 1, &x, &y, &z)) {
      PixelFromGeo(r.gt, x, y, &col, &row);
      havePx = true;
    } else {
      const double du = (lonDeg - bLonMin) / (std::max)(1e-12, bLonMax - bLonMin);
      const double dv = (latDeg - bLatMin) / (std::max)(1e-12, bLatMax - bLatMin);
      const double u = (std::clamp)(du, 0.0, 1.0);
      const double v = (std::clamp)(dv, 0.0, 1.0);
      col = u * static_cast<double>(r.w - 1);
      row = v * static_cast<double>(r.h - 1);
      havePx = true;
    }
  }
  if (havePx && r.ok) {
    if (useElevAsHeight && !r.elev.empty()) {
      *hMeters = static_cast<double>(SampleBilinearF(r.elev, r.w, r.h, col, row));
    } else {
      *hMeters = 0.0;
    }
    if (!r.rgb.empty() && r.rgb.size() >= static_cast<size_t>(r.w * r.h * 3)) {
      rgbOut[0] = SampleBilinearRgb(r.rgb, r.w, r.h, 0, col, row);
      rgbOut[1] = SampleBilinearRgb(r.rgb, r.w, r.h, 1, col, row);
      rgbOut[2] = SampleBilinearRgb(r.rgb, r.w, r.h, 2, col, row);
    } else {
      rgbOut[0] = rgbOut[1] = rgbOut[2] = 200;
    }
  } else {
    *hMeters = 0.0;
    rgbOut[0] = 180;
    rgbOut[1] = 185;
    rgbOut[2] = 200;
  }
}

static std::vector<unsigned char> BuildTerrainGlbEcefRtc(const RasterExtract& rex, bool haveRaster, bool useElevAsHeight,
                                                         double bLonMin, double bLonMax, double bLatMin, double bLatMax,
                                                         int nx, int ny, double* outMinH, double* outMaxH) {
  *outMinH = 0.0;
  *outMaxH = 0.0;
  if (nx < 2 || ny < 2) {
    return {};
  }
  const double lonC = (bLonMin + bLonMax) * 0.5;
  const double latC = (std::clamp)((bLatMin + bLatMax) * 0.5, -85.0, 85.0);
  OGRCoordinateTransformationH wgsToRaster = nullptr;
  OGRSpatialReferenceH srWgs = nullptr;
  OGRSpatialReferenceH srRaster = nullptr;
  if (haveRaster && !rex.wkt.empty()) {
    srWgs = OSRNewSpatialReference(nullptr);
    srRaster = OSRNewSpatialReference(rex.wkt.c_str());
    if (srWgs && srRaster && OSRImportFromEPSG(srWgs, 4326) == OGRERR_NONE) {
      wgsToRaster = OCTNewCoordinateTransformation(srWgs, srRaster);
    }
  }
  double rtc[3]{};
  Wgs84GeodeticToEcef(lonC, latC, 0.0, rtc);
  double east[3], north[3], up[3];
  EnuBasisWgs84(lonC, latC, east, north, up);

  const int nv = nx * ny;
  std::vector<float> pos(static_cast<size_t>(nv) * 3u);
  std::vector<float> nom(static_cast<size_t>(nv) * 3u);
  std::vector<unsigned char> vcol(static_cast<size_t>(nv) * 4u);
  double hMin = 1e300;
  double hMax = -1e300;

  for (int j = 0; j < ny; ++j) {
    for (int i = 0; i < nx; ++i) {
      const double u = nx > 1 ? static_cast<double>(i) / static_cast<double>(nx - 1) : 0.0;
      const double v = ny > 1 ? static_cast<double>(j) / static_cast<double>(ny - 1) : 0.0;
      const double lon = bLonMin + u * (bLonMax - bLonMin);
      const double lat = bLatMin + v * (bLatMax - bLatMin);
      double h = 0.0;
      unsigned char rgb[3]{};
      TerrainSampleRasterAtLonLat(rex, useElevAsHeight && haveRaster, lon, lat, bLonMin, bLonMax, bLatMin, bLatMax,
                                  wgsToRaster, &h, rgb);
      hMin = (std::min)(hMin, h);
      hMax = (std::max)(hMax, h);
      double ecef[3]{};
      Wgs84GeodeticToEcef(lon, lat, h, ecef);
      double d[3]{};
      Sub3d(ecef, rtc, d);
      const double pe = Dot3d(d, east);
      const double pn = Dot3d(d, north);
      const double pu = Dot3d(d, up);
      const int vi = j * nx + i;
      pos[static_cast<size_t>(vi) * 3 + 0] = static_cast<float>(pe);
      pos[static_cast<size_t>(vi) * 3 + 1] = static_cast<float>(pu);
      pos[static_cast<size_t>(vi) * 3 + 2] = static_cast<float>(-pn);
      vcol[static_cast<size_t>(vi) * 4 + 0] = rgb[0];
      vcol[static_cast<size_t>(vi) * 4 + 1] = rgb[1];
      vcol[static_cast<size_t>(vi) * 4 + 2] = rgb[2];
      vcol[static_cast<size_t>(vi) * 4 + 3] = 255;
    }
  }
  if (wgsToRaster) {
    OCTDestroyCoordinateTransformation(wgsToRaster);
  }
  if (srRaster) {
    OSRDestroySpatialReference(srRaster);
  }
  if (srWgs) {
    OSRDestroySpatialReference(srWgs);
  }
  if (!(hMin <= hMax) || !std::isfinite(hMin) || !std::isfinite(hMax)) {
    hMin = 0.0;
    hMax = 0.0;
  }
  *outMinH = hMin;
  *outMaxH = hMax;

  const auto vidx = [&](int i, int j) -> int { return j * nx + i; };

  for (int i = 0; i < nv; ++i) {
    nom[static_cast<size_t>(i) * 3 + 0] = 0.f;
    nom[static_cast<size_t>(i) * 3 + 1] = 0.f;
    nom[static_cast<size_t>(i) * 3 + 2] = 0.f;
  }
  for (int j = 0; j < ny - 1; ++j) {
    for (int i = 0; i < nx - 1; ++i) {
      const int i00 = vidx(i, j);
      const int i10 = vidx(i + 1, j);
      const int i01 = vidx(i, j + 1);
      const int i11 = vidx(i + 1, j + 1);
      double p00[3] = {pos[static_cast<size_t>(i00) * 3 + 0], pos[static_cast<size_t>(i00) * 3 + 1], pos[static_cast<size_t>(i00) * 3 + 2]};
      double p10[3] = {pos[static_cast<size_t>(i10) * 3 + 0], pos[static_cast<size_t>(i10) * 3 + 1], pos[static_cast<size_t>(i10) * 3 + 2]};
      double p01[3] = {pos[static_cast<size_t>(i01) * 3 + 0], pos[static_cast<size_t>(i01) * 3 + 1], pos[static_cast<size_t>(i01) * 3 + 2]};
      double p11[3] = {pos[static_cast<size_t>(i11) * 3 + 0], pos[static_cast<size_t>(i11) * 3 + 1], pos[static_cast<size_t>(i11) * 3 + 2]};
      double e1[3], e2[3], n1[3], n2[3];
      Sub3d(p10, p00, e1);
      Sub3d(p01, p00, e2);
      Cross3d(e1, e2, n1);
      Normalize3d(n1);
      Sub3d(p11, p10, e1);
      Sub3d(p01, p10, e2);
      Cross3d(e1, e2, n2);
      Normalize3d(n2);
      for (int k = 0; k < 3; ++k) {
        nom[static_cast<size_t>(i00) * 3 + k] += static_cast<float>(n1[k]);
        nom[static_cast<size_t>(i10) * 3 + k] += static_cast<float>(n1[k]);
        nom[static_cast<size_t>(i01) * 3 + k] += static_cast<float>(n1[k]);
        nom[static_cast<size_t>(i10) * 3 + k] += static_cast<float>(n2[k]);
        nom[static_cast<size_t>(i11) * 3 + k] += static_cast<float>(n2[k]);
        nom[static_cast<size_t>(i01) * 3 + k] += static_cast<float>(n2[k]);
      }
    }
  }
  for (int i = 0; i < nv; ++i) {
    double nn[3] = {nom[static_cast<size_t>(i) * 3 + 0], nom[static_cast<size_t>(i) * 3 + 1], nom[static_cast<size_t>(i) * 3 + 2]};
    Normalize3d(nn);
    if (!(Len3d(nn) > 1e-12)) {
      nn[0] = 0.0;
      nn[1] = 1.0;
      nn[2] = 0.0;
    }
    nom[static_cast<size_t>(i) * 3 + 0] = static_cast<float>(nn[0]);
    nom[static_cast<size_t>(i) * 3 + 1] = static_cast<float>(nn[1]);
    nom[static_cast<size_t>(i) * 3 + 2] = static_cast<float>(nn[2]);
  }

  const int nTri = (nx - 1) * (ny - 1) * 2;
  const bool use32 = nv > 65535;
  std::vector<unsigned char> idxBlob;
  idxBlob.resize(static_cast<size_t>(nTri * 3 * (use32 ? 4 : 2)));
  size_t iq = 0;
  for (int j = 0; j < ny - 1; ++j) {
    for (int i = 0; i < nx - 1; ++i) {
      const uint32_t a = static_cast<uint32_t>(vidx(i, j));
      const uint32_t b = static_cast<uint32_t>(vidx(i + 1, j));
      const uint32_t c = static_cast<uint32_t>(vidx(i, j + 1));
      const uint32_t d = static_cast<uint32_t>(vidx(i + 1, j + 1));
      const uint32_t tri[2][3] = {{a, c, b}, {b, c, d}};
      for (int ti = 0; ti < 2; ++ti) {
        for (int k = 0; k < 3; ++k) {
          const uint32_t ix = tri[ti][k];
          if (use32) {
            idxBlob[iq++] = static_cast<unsigned char>(ix & 0xff);
            idxBlob[iq++] = static_cast<unsigned char>((ix >> 8) & 0xff);
            idxBlob[iq++] = static_cast<unsigned char>((ix >> 16) & 0xff);
            idxBlob[iq++] = static_cast<unsigned char>((ix >> 24) & 0xff);
          } else {
            const uint16_t s = static_cast<uint16_t>(ix);
            idxBlob[iq++] = static_cast<unsigned char>(s & 0xff);
            idxBlob[iq++] = static_cast<unsigned char>((s >> 8) & 0xff);
          }
        }
      }
    }
  }

  const size_t posLen = static_cast<size_t>(nv) * 3u * sizeof(float);
  const size_t nomLen = static_cast<size_t>(nv) * 3u * sizeof(float);
  const size_t colLen = static_cast<size_t>(nv) * 4u;
  const size_t idxLen = idxBlob.size();
  const size_t binPad = (4u - ((posLen + nomLen + colLen + idxLen) % 4u)) % 4u;
  const size_t binLen = posLen + nomLen + colLen + idxLen + binPad;

  double pMin[3] = {1e300, 1e300, 1e300};
  double pMax[3] = {-1e300, -1e300, -1e300};
  for (int i = 0; i < nv; ++i) {
    for (int k = 0; k < 3; ++k) {
      const double vxf = pos[static_cast<size_t>(i) * 3 + k];
      pMin[k] = (std::min)(pMin[k], vxf);
      pMax[k] = (std::max)(pMax[k], vxf);
    }
  }

  std::vector<unsigned char> bin;
  bin.resize(binLen);
  size_t off = 0;
  std::memcpy(bin.data() + off, pos.data(), posLen);
  off += posLen;
  std::memcpy(bin.data() + off, nom.data(), nomLen);
  off += nomLen;
  std::memcpy(bin.data() + off, vcol.data(), colLen);
  off += colLen;
  std::memcpy(bin.data() + off, idxBlob.data(), idxLen);
  off += idxLen;
  for (size_t pd = 0; pd < binPad; ++pd) {
    bin[off + pd] = 0;
  }

  std::ostringstream js;
  js << std::setprecision(9);
  js << "{\"asset\":{\"version\":\"2.0\",\"generator\":\"AGIS\"},\"scene\":0,\"scenes\":[{\"nodes\":[0]}],\"nodes\":[{\"mesh\":0}],"
        "\"meshes\":[{\"primitives\":[{\"attributes\":{\"POSITION\":0,\"NORMAL\":1,\"COLOR_0\":2},\"indices\":3,\"material\":0}]}],"
        "\"materials\":[{\"pbrMetallicRoughness\":{\"baseColorFactor\":[1,1,1,1],\"metallicFactor\":0.0,\"roughnessFactor\":0.9},"
        "\"doubleSided\":true}],"
        "\"accessors\":["
        "{\"bufferView\":0,\"componentType\":5126,\"count\":" << nv
     << ",\"type\":\"VEC3\",\"min\":[" << pMin[0] << "," << pMin[1] << "," << pMin[2] << "],\"max\":[" << pMax[0] << "," << pMax[1]
     << "," << pMax[2] << "]},"
        "{\"bufferView\":1,\"componentType\":5126,\"count\":" << nv << ",\"type\":\"VEC3\"},"
        "{\"bufferView\":2,\"componentType\":5121,\"count\":" << nv << ",\"type\":\"VEC3\",\"normalized\":true},"
        "{\"bufferView\":3,\"componentType\":" << (use32 ? 5125 : 5123) << ",\"count\":" << (nTri * 3) << ",\"type\":\"SCALAR\"}],"
        "\"bufferViews\":["
        "{\"buffer\":0,\"byteOffset\":0,\"byteLength\":" << posLen << "},"
        "{\"buffer\":0,\"byteOffset\":" << posLen << ",\"byteLength\":" << nomLen << "},"
        "{\"buffer\":0,\"byteOffset\":" << (posLen + nomLen) << ",\"byteLength\":" << colLen << "},"
        "{\"buffer\":0,\"byteOffset\":" << (posLen + nomLen + colLen) << ",\"byteLength\":" << idxLen << "}],"
        "\"buffers\":[{\"byteLength\":" << binLen << "}]}";

  std::string jsonStr = js.str();
  while ((jsonStr.size() % 4) != 0) {
    jsonStr.push_back(' ');
  }
  const uint32_t jsonLen = static_cast<uint32_t>(jsonStr.size());
  const uint32_t binChunkLen = static_cast<uint32_t>(binLen);
  const uint32_t glbLen = 12u + 8u + jsonLen + 8u + binChunkLen;
  std::vector<unsigned char> glb;
  glb.reserve(glbLen);
  AppendLe32(&glb, 0x46546C67u);
  AppendLe32(&glb, 2u);
  AppendLe32(&glb, glbLen);
  AppendLe32(&glb, jsonLen);
  AppendLe32(&glb, 0x4E4F534Au);
  glb.insert(glb.end(), jsonStr.begin(), jsonStr.end());
  AppendLe32(&glb, binChunkLen);
  AppendLe32(&glb, 0x004E4942u);
  glb.insert(glb.end(), bin.begin(), bin.end());
  return glb;
}
#endif  // GIS_DESKTOP_HAVE_GDAL

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

struct AgisMeshDelaunayTri {
  int a = 0;
  int b = 0;
  int c = 0;
};

static bool AgisMeshInCircumcircleXY(double ax, double ay, double bx, double by, double cx, double cy, double px, double py) {
  const double a11 = bx - ax, a12 = by - ay, a13 = (bx - ax) * (bx - ax) + (by - ay) * (by - ay);
  const double a21 = cx - ax, a22 = cy - ay, a23 = (cx - ax) * (cx - ax) + (cy - ay) * (cy - ay);
  const double a31 = px - ax, a32 = py - ay, a33 = (px - ax) * (px - ax) + (py - ay) * (py - ay);
  const double det = a11 * (a22 * a33 - a23 * a32) - a12 * (a21 * a33 - a23 * a31) + a13 * (a21 * a32 - a22 * a31);
  return det > 1e-12;
}

static std::vector<AgisMeshDelaunayTri> AgisMeshDelaunayTriangulate2D(const std::vector<std::array<double, 2>>& pts) {
  std::vector<AgisMeshDelaunayTri> out;
  const int n = static_cast<int>(pts.size());
  if (n < 3) {
    return out;
  }
  double minx = pts[0][0], maxx = pts[0][0], miny = pts[0][1], maxy = pts[0][1];
  for (int i = 1; i < n; ++i) {
    minx = (std::min)(minx, pts[i][0]);
    maxx = (std::max)(maxx, pts[i][0]);
    miny = (std::min)(miny, pts[i][1]);
    maxy = (std::max)(maxy, pts[i][1]);
  }
  const double dx = (std::max)(maxx - minx, 1e-6);
  const double dy = (std::max)(maxy - miny, 1e-6);
  const double m = (std::max)(dx, dy) * 4.0;
  const int s0 = n;
  const int s1 = n + 1;
  const int s2 = n + 2;
  std::vector<std::array<double, 2>> work = pts;
  work.push_back({minx - m, miny - m});
  work.push_back({minx + 2.0 * m, miny - m});
  work.push_back({minx + 0.5 * m, miny + 2.0 * m});
  std::vector<AgisMeshDelaunayTri> tris;
  tris.push_back({s0, s1, s2});
  for (int pi = 0; pi < n; ++pi) {
    const auto& p = work[static_cast<size_t>(pi)];
    std::vector<int> bad;
    for (int ti = 0; ti < static_cast<int>(tris.size()); ++ti) {
      const AgisMeshDelaunayTri& t = tris[static_cast<size_t>(ti)];
      const auto& pa = work[static_cast<size_t>(t.a)];
      const auto& pb = work[static_cast<size_t>(t.b)];
      const auto& pc = work[static_cast<size_t>(t.c)];
      if (AgisMeshInCircumcircleXY(pa[0], pa[1], pb[0], pb[1], pc[0], pc[1], p[0], p[1])) {
        bad.push_back(ti);
      }
    }
    if (bad.empty()) {
      continue;
    }
    std::vector<std::pair<int, int>> edges;
    auto addEdge = [&](int u, int v) {
      if (u > v) {
        std::swap(u, v);
      }
      edges.emplace_back(u, v);
    };
    for (int idx : bad) {
      const AgisMeshDelaunayTri& t = tris[static_cast<size_t>(idx)];
      addEdge(t.a, t.b);
      addEdge(t.b, t.c);
      addEdge(t.c, t.a);
    }
    std::sort(edges.begin(), edges.end());
    std::vector<std::pair<int, int>> boundary;
    for (size_t i = 0; i < edges.size(); ++i) {
      size_t cnt = 1;
      while (i + 1 < edges.size() && edges[i + 1] == edges[i]) {
        ++cnt;
        ++i;
      }
      if (cnt % 2 == 1) {
        boundary.push_back(edges[i]);
      }
    }
    std::vector<AgisMeshDelaunayTri> next;
    for (int ti = 0; ti < static_cast<int>(tris.size()); ++ti) {
      if (std::find(bad.begin(), bad.end(), ti) == bad.end()) {
        next.push_back(tris[static_cast<size_t>(ti)]);
      }
    }
    for (const auto& e : boundary) {
      next.push_back({e.first, e.second, pi});
    }
    tris.swap(next);
  }
  for (const AgisMeshDelaunayTri& t : tris) {
    if (t.a < n && t.b < n && t.c < n) {
      out.push_back(t);
    }
  }
  return out;
}

static std::uint32_t AgisObjFxHashPixel(std::uint32_t x, std::uint32_t y) {
  std::uint32_t h = x * 374761393u + y * 668265263u;
  h = (h ^ (h >> 13)) * 1274126177u;
  return h ^ (h >> 16);
}

static void ApplyObjVisualEffectOnAlbedo(std::vector<unsigned char>* rgb, int texW, int texH, const std::wstring& effect,
                                         double snowScale) {
  if (!rgb || texW <= 0 || texH <= 0 ||
      rgb->size() < static_cast<size_t>(texW) * static_cast<size_t>(texH) * 3) {
    return;
  }
  if (_wcsicmp(effect.c_str(), L"none") == 0) {
    return;
  }
  if (_wcsicmp(effect.c_str(), L"night") == 0) {
    for (size_t i = 0; i + 2 < rgb->size(); i += 3) {
      const int r = (*rgb)[i + 0];
      const int g = (*rgb)[i + 1];
      const int b = (*rgb)[i + 2];
      (*rgb)[i + 0] = static_cast<unsigned char>((std::clamp)(static_cast<int>(r * 0.35 + 8), 0, 255));
      (*rgb)[i + 1] = static_cast<unsigned char>((std::clamp)(static_cast<int>(g * 0.28 + 4), 0, 255));
      (*rgb)[i + 2] = static_cast<unsigned char>((std::clamp)(static_cast<int>(b * 0.42 + 28), 0, 255));
    }
    return;
  }
  if (_wcsicmp(effect.c_str(), L"snow") == 0) {
    const double invS = 1.0 / (std::max)(0.25, snowScale);
    const std::uint32_t thresh = static_cast<std::uint32_t>((std::clamp)(60.0 * invS, 5.0, 120.0));
    for (int y = 0; y < texH; ++y) {
      for (int x = 0; x < texW; ++x) {
        const size_t i = (static_cast<size_t>(y) * static_cast<size_t>(texW) + static_cast<size_t>(x)) * 3;
        std::uint32_t h = AgisObjFxHashPixel(static_cast<std::uint32_t>(x), static_cast<std::uint32_t>(y));
        const bool flake = (h % 256u) < thresh;
        int r = (*rgb)[i + 0];
        int g = (*rgb)[i + 1];
        int b = (*rgb)[i + 2];
        const int lum = (r * 2126 + g * 7152 + b * 722) / 10000;
        const int lift = flake ? 55 : (lum > 180 ? 35 : 12);
        (*rgb)[i + 0] = static_cast<unsigned char>((std::clamp)(r + lift, 0, 255));
        (*rgb)[i + 1] = static_cast<unsigned char>((std::clamp)(g + lift, 0, 255));
        (*rgb)[i + 2] = static_cast<unsigned char>((std::clamp)(b + lift, 0, 255));
      }
    }
  }
}

bool ModelSubtypeIsPointCloudW(const std::wstring& s);
static int ConvertMeshObjToLasJob(const ConvertArgs& args);
static std::wstring NormalizeTextureFmtExt(const std::wstring& fmt);

/// GIS→模型 默认产出 OBJ；`--output-subtype pointcloud` 或输出扩展名为 .las/.laz 时产出点云。瓦片→模型仍为 OBJ；误将 .las/.laz 用于非点云模式时会改为 .obj。
static void AgisCoerceModelOutputPathFromLasLazToObj(std::filesystem::path* path, const wchar_t* toolLabel) {
  if (!path || path->empty()) {
    return;
  }
  std::error_code ec;
  if (std::filesystem::is_directory(*path, ec)) {
    return;
  }
  std::wstring ext = path->extension().wstring();
  for (auto& c : ext) {
    c = static_cast<wchar_t>(std::towlower(c));
  }
  if (ext == L".laz" || ext == L".las") {
    std::wcerr << L"[WARN] " << toolLabel
               << L" 输出为 Wavefront OBJ，不能使用 .las/.laz 扩展名（否则内容仍为 OBJ，与点云格式不符）。已改为 .obj。\n";
    path->replace_extension(L".obj");
  }
}

int ConvertGisToModelImpl(const ConvertArgs& args) {
  int lastProgressPct = 0;
  auto reportProgress = [&](int pct, const wchar_t* text) {
    const int p = (std::max)(lastProgressPct, (std::clamp)(pct, 0, 100));
    lastProgressPct = p;
    std::wcout << L"[PROGRESS " << p << L"] " << (text ? text : L"") << L"\n";
  };
  auto reportProgressMeta = [](const std::wstring& stage, int fileCur, int fileTotal, const std::wstring& io,
                               const std::filesystem::path& filePath) {
    std::wstring f = filePath.wstring();
    for (auto& c : f) {
      if (c == L'|') c = L'/';
    }
    std::wcout << L"[PROGRESS_META] stage=" << stage << L"|files=" << fileCur << L"/" << fileTotal << L"|io=" << io
               << L"|file=" << f << L"\n";
  };
  reportProgress(3, L"初始化输出路径");
  std::filesystem::path outPath(args.output);
  bool wantPointCloud = ModelSubtypeIsPointCloudW(args.output_subtype);
  {
    std::wstring ext0 = outPath.extension().wstring();
    for (auto& c : ext0) {
      c = static_cast<wchar_t>(std::towlower(c));
    }
    if (ext0 == L".las" || ext0 == L".laz") {
      wantPointCloud = true;
    }
  }
  std::error_code ec;
  if (std::filesystem::is_directory(outPath, ec) || outPath.extension().empty()) {
    std::filesystem::create_directories(outPath, ec);
    if (wantPointCloud) {
      outPath /= L"points.las";
    } else {
      outPath /= L"model.obj";
    }
  } else if (!wantPointCloud) {
    AgisCoerceModelOutputPathFromLasLazToObj(&outPath, L"GIS→模型");
  }
  reportProgressMeta(L"初始化输出", 1, 1, L"write", outPath);

  std::filesystem::path tmpPcDir;
  std::filesystem::path meshWritePath = outPath;
  if (wantPointCloud) {
    std::error_code ecTmp;
    std::filesystem::path tbase = std::filesystem::temp_directory_path(ecTmp);
    if (ecTmp) {
      tbase = outPath.parent_path();
    }
    tmpPcDir = tbase / (L"agis_gis2pc_" + std::to_wstring(GetCurrentProcessId()) + L"_" + std::to_wstring(GetTickCount64()));
    std::filesystem::create_directories(tmpPcDir, ecTmp);
    if (ecTmp) {
      std::wcerr << L"[ERROR] 无法创建 GIS→点云临时目录。\n";
      return 6;
    }
    meshWritePath = tmpPcDir / L"stage.obj";
  }

  const std::wstring mtlName = meshWritePath.stem().wstring() + L".mtl";
  if (wantPointCloud) {
    std::wcout << L"[OUT] point cloud: " << outPath.wstring() << L" (mesh+texture sampling; staging OBJ in temp)\n";
  } else {
    std::wcout << L"[OUT] model file: " << outPath.wstring() << L"\n";
    std::wcout << L"[OUT] material file: " << (meshWritePath.parent_path() / mtlName).wstring() << L"\n";
  }
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
  OGRSpatialReferenceH cecfGeoSr = nullptr;
  OGRCoordinateTransformationH cecfFromRasterToGeo = nullptr;
  OGRCoordinateTransformationH cecfFromTargetToGeo = nullptr;
  /** 已为顶点写出「栅格像元」UV；为 false 时 _albedo 仍用网格 ni×nj 与 vt 一致（如 cecf）。 */
  bool meshAlbedoUseFullRasterUv = false;
#endif
  const std::filesystem::path inPath(args.input);
  const bool isGis = _wcsicmp(inPath.extension().wstring().c_str(), L".gis") == 0;
  reportProgress(8, L"解析输入源");
  if (isGis) {
    doc = ParseGisDocInfo(inPath);
    sources = ParseGisLayerSources(inPath);
  } else {
    sources.push_back(inPath.wstring());
  }
#if GIS_DESKTOP_HAVE_GDAL
  AgisEnsureGdalDataPath();
  GDALAllRegister();
  reportProgress(15, L"加载栅格/矢量数据");
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
  if (_wcsicmp(args.coord_system.c_str(), L"cecf") == 0 && args.target_crs.empty()) {
    // CECF 需要经纬度语义；未显式指定时默认锁定 WGS84，避免 auto 选到投影 CRS 后生成“扇面”。
    OGRSpatialReferenceH wgs84 = OSRNewSpatialReference(nullptr);
    if (wgs84 && OSRImportFromEPSG(wgs84, 4326) == OGRERR_NONE) {
      if (targetSr) {
        OSRDestroySpatialReference(targetSr);
      }
      targetSr = wgs84;
      std::wcout << L"[CECF] target CRS defaulted to EPSG:4326 (WGS84).\n";
    } else if (wgs84) {
      OSRDestroySpatialReference(wgs84);
    }
  }
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
  const int srcW = (raster.ok && raster.srcW > 0) ? raster.srcW : rw;
  const int srcH = (raster.ok && raster.srcH > 0) ? raster.srcH : rh;
  const double readToSrcX = (rw > 0) ? static_cast<double>(srcW) / static_cast<double>(rw) : 1.0;
  const double readToSrcY = (rh > 0) ? static_cast<double>(srcH) / static_cast<double>(rh) : 1.0;
  int mw = rw;
  int mh = rh;
  if (_wcsicmp(args.coord_system.c_str(), L"cecf") == 0) {
    // cecf 全局球面场景限制面数，防止超大 OBJ 导致预览抽稀后看起来“缺块”。
    constexpr int64_t kMaxFacesBudget = 1800000;
    constexpr int64_t kMaxCells = kMaxFacesBudget / 2;
    int64_t cells = static_cast<int64_t>(mw - 1) * static_cast<int64_t>(mh - 1);
    if (cells > kMaxCells) {
      const double s = std::sqrt(static_cast<double>(kMaxCells) / static_cast<double>(cells));
      mw = (std::max)(2, 1 + static_cast<int>(std::floor((mw - 1) * s)));
      mh = (std::max)(2, 1 + static_cast<int>(std::floor((mh - 1) * s)));
      std::wcout << L"[MESH] cecf face budget clamp: " << mw << L"x" << mh << L"\n";
    }
  }
  std::vector<float> meshElevBuf;
  std::vector<unsigned char> meshRgbBuf;
  double meshTminX = 0.0;
  double meshTmaxX = 0.0;
  double meshTminY = 0.0;
  double meshTmaxY = 0.0;
  bool haveTargetMesh = false;
  int meshJRowOffset = 0;
  int meshNjFull = 0;
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
    reportProgress(28, L"构建目标规则网格");
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
    {
      const int64_t maxFacesBudget = (_wcsicmp(args.coord_system.c_str(), L"cecf") == 0) ? 1800000LL : 3000000LL;
      const int64_t maxCells = (std::max)(1LL, maxFacesBudget / 2);
      int64_t cells = static_cast<int64_t>(ni - 1) * static_cast<int64_t>(nj - 1);
      if (cells > maxCells) {
        const double s = std::sqrt(static_cast<double>(maxCells) / static_cast<double>(cells));
        ni = (std::max)(2, 1 + static_cast<int>(std::floor((ni - 1) * s)));
        nj = (std::max)(2, 1 + static_cast<int>(std::floor((nj - 1) * s)));
        cells = static_cast<int64_t>(ni - 1) * static_cast<int64_t>(nj - 1);
        std::wcout << L"[MESH] face budget clamp: cells -> " << cells << L", maxCells=" << maxCells << L"\n";
      }
    }
    std::wcout << L"[MESH] grid (ni x nj) " << ni << L" x " << nj << L" interior cells ~ 2:1, stepK ~ " << stepK
               << L"\n";
    meshTminX = bminx;
    meshTmaxX = bmaxx;
    meshTminY = bminy;
    meshTmaxY = bmaxy;
    const int niFull = ni;
    const int njFull = nj;
    int texOutH = (std::max)(2, njFull);
    int texOutW = 2 * texOutH;
    constexpr int kTexMaxEst = 8192;
    if (texOutW > kTexMaxEst) {
      texOutW = kTexMaxEst;
      texOutH = (std::max)(2, texOutW / 2);
    }
    const bool wantPbrEst = (_wcsicmp(args.obj_texture_mode.c_str(), L"pbr") == 0);
    const std::int64_t budgetB = args.model_budget_mb * 1024LL * 1024LL;
    const std::int64_t est = EstimateGisModelChunkBytes(args.model_budget_mode, niFull, njFull, texOutW, texOutH, wantPbrEst,
                                                         _wcsicmp(args.obj_fp_type.c_str(), L"float") == 0);
    int autoStripes = 1;
    if (!wantPointCloud && _wcsicmp(args.coord_system.c_str(), L"cecf") != 0 && njFull >= 4 && est > budgetB) {
      autoStripes = static_cast<int>(std::min<int64_t>(static_cast<int64_t>(njFull - 1),
                                                        std::max<int64_t>(2LL, (est + budgetB - 1) / budgetB)));
      std::wcout << L"[BUDGET] estimate ~" << est << L" B vs budget " << budgetB << L" B (" << args.model_budget_mode
                 << L") -> auto stripes=" << autoStripes << L"\n";
    } else if (est > budgetB) {
      std::wcout << L"[BUDGET] estimate ~" << est << L" B exceeds budget " << budgetB << L" B (" << args.model_budget_mode
                 << L"); single-file (cecf/pointcloud or small grid).\n";
    }
    int gisMeshStripeCount = (std::max)(1, (std::max)(args.gis_model_split_parts, autoStripes));
    gisMeshStripeCount = (std::min)(gisMeshStripeCount, (std::max)(1, njFull - 1));
    // 对 file 口径使用保守系数，避免 OBJ 文本体积估算偏小导致超预算明显。
    const bool strictFileBudget = (_wcsicmp(args.model_budget_mode.c_str(), L"file") == 0);
    const double budgetSafety = strictFileBudget ? 1.35 : 1.0;
    auto estimateStripeWorstCaseBytes = [&](int stripes) -> std::int64_t {
      stripes = (std::max)(1, stripes);
      const int cellsTotalY = (std::max)(1, njFull - 1);
      const int cellsPerStripe = (cellsTotalY + stripes - 1) / stripes;
      const int njPartWorst = cellsPerStripe + 1;
      int texPartH = (std::max)(2, njPartWorst);
      int texPartW = 2 * texPartH;
      constexpr int kTexMaxEstPart = 8192;
      if (texPartW > kTexMaxEstPart) {
        texPartW = kTexMaxEstPart;
        texPartH = (std::max)(2, texPartW / 2);
      }
      return EstimateGisModelChunkBytes(args.model_budget_mode, niFull, njPartWorst, texPartW, texPartH, wantPbrEst,
                                        _wcsicmp(args.obj_fp_type.c_str(), L"float") == 0);
    };
    if (!wantPointCloud && _wcsicmp(args.coord_system.c_str(), L"cecf") != 0 && njFull >= 4) {
      const std::int64_t targetBudget = static_cast<std::int64_t>(static_cast<double>(budgetB) / budgetSafety);
      while (gisMeshStripeCount < (std::max)(1, njFull - 1) && estimateStripeWorstCaseBytes(gisMeshStripeCount) > targetBudget) {
        ++gisMeshStripeCount;
      }
    }
    if (gisMeshStripeCount > 1) {
      std::wcout << L"[BUDGET] output split into " << gisMeshStripeCount << L" row-stripes (_partN.obj), worst-stripe est ~"
                 << estimateStripeWorstCaseBytes(gisMeshStripeCount) << L" B.\n";
    }
    OGRCoordinateTransformationH holdTargetToRasterMesh = OCTNewCoordinateTransformation(targetSr, rasterSr);
    for (int gisStripeIdx = 0; gisStripeIdx < gisMeshStripeCount; ++gisStripeIdx) {
      if (holdTargetToRasterMesh) {
        const int j0 = gisStripeIdx * (njFull - 1) / gisMeshStripeCount;
        int j1 = (gisStripeIdx + 1) * (njFull - 1) / gisMeshStripeCount;
        if (gisStripeIdx == gisMeshStripeCount - 1) {
          j1 = njFull - 1;
        }
        const int njPart = j1 - j0 + 1;
        meshElevBuf.assign(static_cast<size_t>(niFull) * static_cast<size_t>(njPart), 0.f);
        meshRgbBuf.assign(static_cast<size_t>(niFull) * static_cast<size_t>(njPart) * 3, 0);
        for (int jj = 0; jj < njPart; ++jj) {
          if ((jj & 31) == 0 || jj + 1 == njPart) {
            const int p = 30 + static_cast<int>((static_cast<double>(jj + 1) / static_cast<double>((std::max)(1, njPart))) * 20.0);
            reportProgress(p, L"构建规则网格采样");
          }
          const int jGlobal = j0 + jj;
          const double fj = (njFull > 1) ? static_cast<double>(jGlobal) / static_cast<double>(njFull - 1) : 0.0;
          const double tyv = meshTminY + fj * (meshTmaxY - meshTminY);
          for (int i = 0; i < niFull; ++i) {
            const double fi = (niFull > 1) ? static_cast<double>(i) / static_cast<double>(niFull - 1) : 0.0;
            const double txv = meshTminX + fi * (meshTmaxX - meshTminX);
            double ox = txv;
            double oy = tyv;
            double oz = 0.0;
            const size_t idx = static_cast<size_t>(jj) * static_cast<size_t>(niFull) + static_cast<size_t>(i);
            if (!OCTTransform(holdTargetToRasterMesh, 1, &ox, &oy, &oz)) {
              meshElevBuf[idx] = 0.f;
              meshRgbBuf[idx * 3 + 0] = 128;
              meshRgbBuf[idx * 3 + 1] = 128;
              meshRgbBuf[idx * 3 + 2] = 128;
              continue;
            }
            double col = 0.0;
            double row = 0.0;
            PixelFromGeo(raster.gt, ox, oy, &col, &row);
            meshElevBuf[idx] = SampleDemElevForMesh(raster.elev, rw, rh, col, row, args.gis_dem_interp);
            meshRgbBuf[idx * 3 + 0] = SampleBilinearRgb(raster.rgb, rw, rh, 0, col, row);
            meshRgbBuf[idx * 3 + 1] = SampleBilinearRgb(raster.rgb, rw, rh, 1, col, row);
            meshRgbBuf[idx * 3 + 2] = SampleBilinearRgb(raster.rgb, rw, rh, 2, col, row);
          }
        }
        mw = niFull;
        mh = njPart;
        meshJRowOffset = j0;
        meshNjFull = njFull;
        haveTargetMesh = true;
        if (gisMeshStripeCount > 1) {
          const std::wstring stem = outPath.stem().wstring() + L"_part" + std::to_wstring(gisStripeIdx + 1);
          meshWritePath = outPath.parent_path() / (stem + L".obj");
        }
      } else if (gisStripeIdx > 0) {
        break;
      }
    }
  }
#endif
  reportProgress(62, L"写出几何与纹理坐标");
  reportProgressMeta(L"写 OBJ 顶点", 1, 1, L"write", meshWritePath);
  const int w = mw;
  const int h = mh;
  const int gridW = w - 1;
  const int gridH = h - 1;
  const bool isCecf = (_wcsicmp(args.coord_system.c_str(), L"cecf") == 0);
  std::vector<std::array<double, 3>> gridVerts;
  gridVerts.reserve(static_cast<size_t>(w) * h);
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
  bool cecfHasLonLatBBox = false;
  double cecfLonMin = -180.0;
  double cecfLonMax = 180.0;
  double cecfLatMin = -90.0;
  double cecfLatMax = 90.0;
  bool cecfIsGlobalLike = false;
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
  if (_wcsicmp(args.coord_system.c_str(), L"cecf") == 0 && !raster.ok && !vEnvsTarget.empty()) {
    double minxT = 1e300, maxxT = -1e300, minyT = 1e300, maxyT = -1e300;
    for (const auto& e : vEnvsTarget) {
      minxT = (std::min)(minxT, e.minx);
      maxxT = (std::max)(maxxT, e.maxx);
      minyT = (std::min)(minyT, e.miny);
      maxyT = (std::max)(maxyT, e.maxy);
    }
    if (minxT <= maxxT && minyT <= maxyT) {
      if (targetIsGeographic) {
        cecfLonMin = minxT;
        cecfLonMax = maxxT;
        cecfLatMin = minyT;
        cecfLatMax = maxyT;
        cecfHasLonLatBBox = true;
      } else if (cecfFromTargetToGeo) {
        double lonMin = 1e300, lonMax = -1e300, latMin = 1e300, latMax = -1e300;
        for (double xx : {minxT, maxxT}) {
          for (double yy : {minyT, maxyT}) {
            double lx = xx, ly = yy, lz = 0.0;
            if (!OCTTransform(cecfFromTargetToGeo, 1, &lx, &ly, &lz)) continue;
            lonMin = (std::min)(lonMin, lx);
            lonMax = (std::max)(lonMax, lx);
            latMin = (std::min)(latMin, ly);
            latMax = (std::max)(latMax, ly);
          }
        }
        if (lonMin <= lonMax && latMin <= latMax) {
          cecfLonMin = lonMin;
          cecfLonMax = lonMax;
          cecfLatMin = latMin;
          cecfLatMax = latMax;
          cecfHasLonLatBBox = true;
          cecfIsGlobalLike = ((cecfLonMax - cecfLonMin) >= 340.0);
        }
      }
    }
  }
  double rasterPlaneSpanX = 0.0;
  double rasterPlaneSpanY = 0.0;
  if (raster.ok && _wcsicmp(args.coord_system.c_str(), L"cecf") != 0) {
    double bx0 = 1e100, bx1 = -1e100, by0 = 1e100, by1 = -1e100;
    for (int ix : {0, rw - 1}) {
      for (int iy : {0, rh - 1}) {
        const double sx = static_cast<double>(ix) * readToSrcX;
        const double sy = static_cast<double>(iy) * readToSrcY;
        const double px = raster.gt[0] + sx * raster.gt[1] + sy * raster.gt[2];
        const double py = raster.gt[3] + sx * raster.gt[4] + sy * raster.gt[5];
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
  const bool useFloatObj = (_wcsicmp(args.obj_fp_type.c_str(), L"float") == 0);
  std::wstringstream ss;
  ss.setf(std::ios::fixed, std::ios::floatfield);
  ss << std::setprecision(useFloatObj ? 7 : 15);
  ss << kObjFileFormatBanner30 << L"# AGIS model output\n"
     << L"# from GIS input: " << args.input << L"\n"
     << L"# subtype: " << args.input_subtype << L" -> " << args.output_subtype << L"\n"
     << L"# raster_source: " << (raster.ok ? raster.sourcePath : L"<none>") << L"\n"
     << L"# obj_fp_type: " << (useFloatObj ? L"float" : L"double") << L"\n";
  if (doc.ok) {
    ss << L"# gis_document_viewport (2D map <display> from .gis — last saved map view, NOT raster footprint): ["
       << minx << L"," << miny << L"] - [" << maxx << L"," << maxy << L"]\n";
  }
  if (raster.ok && rw >= 1 && rh >= 1) {
    double rbMinX = 1e300;
    double rbMaxX = -1e300;
    double rbMinY = 1e300;
    double rbMaxY = -1e300;
    for (int ix : {0, srcW - 1}) {
      for (int iy : {0, srcH - 1}) {
        const double px = raster.gt[0] + static_cast<double>(ix) * raster.gt[1] + static_cast<double>(iy) * raster.gt[2];
        const double py = raster.gt[3] + static_cast<double>(ix) * raster.gt[4] + static_cast<double>(iy) * raster.gt[5];
        rbMinX = (std::min)(rbMinX, px);
        rbMaxX = (std::max)(rbMaxX, px);
        rbMinY = (std::min)(rbMinY, py);
        rbMaxY = (std::max)(rbMaxY, py);
      }
    }
    ss << L"# raster_bbox_native (GeoTransform axis-aligned hull in raster source CRS; for global EPSG:4326 "
          L"expect ~lon [-180,180], lat [-90,90]): ["
       << rbMinX << L"," << rbMinY << L"] - [" << rbMaxX << L"," << rbMaxY << L"]\n";
    if (_wcsicmp(args.coord_system.c_str(), L"cecf") == 0) {
      std::wcout << L"[CECF-DEBUG] source CRS bbox: [" << rbMinX << L"," << rbMinY << L"] - [" << rbMaxX << L"," << rbMaxY
                 << L"]\n";
#if GIS_DESKTOP_HAVE_GDAL
      if (cecfFromRasterToGeo) {
        double lonMin = 1e300, lonMax = -1e300, latMin = 1e300, latMax = -1e300;
        for (int ix : {0, srcW - 1}) {
          for (int iy : {0, srcH - 1}) {
            double px = raster.gt[0] + static_cast<double>(ix) * raster.gt[1] + static_cast<double>(iy) * raster.gt[2];
            double py = raster.gt[3] + static_cast<double>(ix) * raster.gt[4] + static_cast<double>(iy) * raster.gt[5];
            double pz = 0.0;
            if (!OCTTransform(cecfFromRasterToGeo, 1, &px, &py, &pz)) {
              continue;
            }
            lonMin = (std::min)(lonMin, px);
            lonMax = (std::max)(lonMax, px);
            latMin = (std::min)(latMin, py);
            latMax = (std::max)(latMax, py);
          }
        }
        if (lonMin <= lonMax && latMin <= latMax) {
          const double lonSpan = lonMax - lonMin;
          const double latSpan = latMax - latMin;
          const bool nearGlobalLon = lonSpan >= 340.0;
          const bool nearGlobalLat = latSpan >= 150.0;
          const wchar_t* coverage = (nearGlobalLon && nearGlobalLat) ? L"GLOBAL_LIKE" : L"REGIONAL_LIKE";
          std::wcout << L"[CECF-DEBUG] WGS84 bbox: [" << lonMin << L"," << latMin << L"] - [" << lonMax << L"," << latMax
                     << L"], span(lon,lat)=(" << lonSpan << L"," << latSpan << L"), coverage=" << coverage << L"\n";
          ss << L"# cecf_wgs84_bbox: [" << lonMin << L"," << latMin << L"] - [" << lonMax << L"," << latMax << L"]\n";
          ss << L"# cecf_wgs84_span: lon=" << lonSpan << L", lat=" << latSpan << L", coverage=" << coverage << L"\n";
        } else {
          std::wcout << L"[CECF-DEBUG] WGS84 bbox: <transform failed>\n";
          ss << L"# cecf_wgs84_bbox: <transform failed>\n";
        }
      } else
#endif
      {
        const double lonSpan = rbMaxX - rbMinX;
        const double latSpan = rbMaxY - rbMinY;
        const bool nearGlobalLon = lonSpan >= 340.0;
        const bool nearGlobalLat = latSpan >= 150.0;
        const wchar_t* coverage = (nearGlobalLon && nearGlobalLat) ? L"GLOBAL_LIKE" : L"REGIONAL_LIKE";
        std::wcout << L"[CECF-DEBUG] WGS84 bbox (fallback=native): [" << rbMinX << L"," << rbMinY << L"] - [" << rbMaxX << L","
                   << rbMaxY << L"], span(lon,lat)=(" << lonSpan << L"," << latSpan << L"), coverage=" << coverage << L"\n";
        ss << L"# cecf_wgs84_bbox (fallback=native): [" << rbMinX << L"," << rbMinY << L"] - [" << rbMaxX << L"," << rbMaxY
           << L"]\n";
        ss << L"# cecf_wgs84_span: lon=" << lonSpan << L", lat=" << latSpan << L", coverage=" << coverage << L"\n";
      }
    }
  }
  if (_wcsicmp(args.coord_system.c_str(), L"cecf") == 0 && !raster.ok) {
    if (cecfHasLonLatBBox) {
      const double lonSpan = cecfLonMax - cecfLonMin;
      const double latSpan = cecfLatMax - cecfLatMin;
      const bool nearGlobalLon = lonSpan >= 340.0;
      const bool nearGlobalLat = latSpan >= 150.0;
      const wchar_t* coverage = (nearGlobalLon && nearGlobalLat) ? L"GLOBAL_LIKE" : L"REGIONAL_LIKE";
      cecfIsGlobalLike = nearGlobalLon;
      std::wcout << L"[CECF-DEBUG] source CRS bbox: <raster missing; from vector envelopes>\n";
      std::wcout << L"[CECF-DEBUG] WGS84 bbox: [" << cecfLonMin << L"," << cecfLatMin << L"] - [" << cecfLonMax << L","
                 << cecfLatMax << L"], span(lon,lat)=(" << lonSpan << L"," << latSpan << L"), coverage=" << coverage << L"\n";
      ss << L"# cecf_wgs84_bbox: [" << cecfLonMin << L"," << cecfLatMin << L"] - [" << cecfLonMax << L"," << cecfLatMax
         << L"]\n";
      ss << L"# cecf_wgs84_span: lon=" << lonSpan << L", lat=" << latSpan << L", coverage=" << coverage << L"\n";
    } else {
      std::wcout << L"[CECF-DEBUG] source CRS bbox: <raster missing>\n";
      std::wcout << L"[CECF-DEBUG] WGS84 bbox: <unavailable>\n";
      ss << L"# cecf_wgs84_bbox: <unavailable>\n";
    }
  }
  ss << L"# layers: " << doc.layerCount << L"\n"
     << L"# coord_system: " << args.coord_system << L"\n"
     << L"# target_crs: " << (args.target_crs.empty() ? L"<auto>" : args.target_crs) << L"\n"
     << L"# output_unit: " << NormalizeOutputUnitW(args.output_unit) << L" (scale=" << unitS << L")\n"
     << L"# mesh_spacing: " << args.mesh_spacing << L" (model units)\n"
     << L"# obj_texture_mode: " << args.obj_texture_mode << L" (color=albedo only; pbr=normal/roughness/metallic/ao maps)\n"
     << L"# obj_visual_effect: " << args.obj_visual_effect;
  if (_wcsicmp(args.obj_visual_effect.c_str(), L"snow") == 0) {
    ss << L" snow_scale=" << args.obj_snow_scale;
  }
  ss << L"\n"
     << L"# gis_dem_interpolation: " << args.gis_dem_interp << L"\n"
     << L"# gis_mesh_topology: " << args.gis_mesh_topology << L"\n"
     << L"# model_budget: mode=" << args.model_budget_mode << L" limit_mb=" << args.model_budget_mb << L"\n";
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
  if (_wcsicmp(args.coord_system.c_str(), L"cecf") == 0) {
    cecfGeoSr = OSRNewSpatialReference(nullptr);
    if (cecfGeoSr) {
      OSRSetWellKnownGeogCS(cecfGeoSr, "WGS84");
      if (rasterSr) {
        cecfFromRasterToGeo = OCTNewCoordinateTransformation(rasterSr, cecfGeoSr);
      }
      if (targetSr) {
        cecfFromTargetToGeo = OCTNewCoordinateTransformation(targetSr, cecfGeoSr);
      }
    }
  }
#endif
  for (int y = 0; y < h; ++y) {
    if ((y & 31) == 0 || y + 1 == h) {
      const int p = 62 + static_cast<int>((static_cast<double>(y + 1) / static_cast<double>((std::max)(1, h))) * 12.0);
      reportProgress(p, L"生成顶点/纹理坐标");
    }
    for (int x = 0; x < w; ++x) {
      const float fx = (gridW > 0) ? static_cast<float>(x) / static_cast<float>(gridW) : 0.0f;
      const float fy = (gridH > 0) ? static_cast<float>(y) / static_cast<float>(gridH) : 0.0f;
#if GIS_DESKTOP_HAVE_GDAL
      if (haveTargetMesh && _wcsicmp(args.coord_system.c_str(), L"cecf") == 0) {
        const double txv = meshTminX + static_cast<double>(fx) * (meshTmaxX - meshTminX);
        const double tyv = meshTminY + static_cast<double>(fy) * (meshTmaxY - meshTminY);
        const float pzf = meshElevBuf[static_cast<size_t>(y) * w + x];
        double lonDeg = txv;
        double latDeg = tyv;
        if (cecfFromTargetToGeo) {
          double tz = 0.0;
          if (OCTTransform(cecfFromTargetToGeo, 1, &lonDeg, &latDeg, &tz)) {
            // lonDeg/latDeg already converted to WGS84.
          }
        }
        latDeg = (std::clamp)(latDeg, -89.999999, 89.999999);
        const double lon = lonDeg * kPi / 180.0;
        const double lat = latDeg * kPi / 180.0;
        const double R = 6378137.0 + static_cast<double>(pzf) * elevRatio;
        const double xEcef = R * std::cos(lat) * std::cos(lon) * unitS;
        const double yEcef = R * std::cos(lat) * std::sin(lon) * unitS;
        const double zEcef = R * std::sin(lat) * unitS;
        ss << L"v " << xEcef << L" " << yEcef << L" " << zEcef << L"\n";
        gridVerts.push_back({xEcef, yEcef, zEcef});
        const bool pole = (isCecf && (y == 0 || y == h - 1));
        const float tu = pole ? 0.5f : fx;
        float tvOut = 1.0f - fy;
        if (pole) {
          const float epsV = 0.5f / static_cast<float>((std::max)(2, h));
          tvOut = (y == 0) ? (1.0f - epsV) : epsV;
        }
        ss << L"vt " << tu << L" " << tvOut << L"\n";
        continue;
      }
#endif
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
        gridVerts.push_back({xo, yo, zo});
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
        const double colRead = static_cast<double>(fx) * static_cast<double>((std::max)(1, rw - 1));
        const double rowRead = static_cast<double>(fy) * static_cast<double>((std::max)(1, rh - 1));
        const double colGeo = static_cast<double>(fx) * static_cast<double>((std::max)(1, srcW - 1));
        const double rowGeo = static_cast<double>(fy) * static_cast<double>((std::max)(1, srcH - 1));
        px = raster.gt[0] + colGeo * raster.gt[1] + rowGeo * raster.gt[2];
        py = raster.gt[3] + colGeo * raster.gt[4] + rowGeo * raster.gt[5];
        pz = SampleDemElevForMesh(raster.elev, rw, rh, colRead, rowRead, args.gis_dem_interp);
      } else if (_wcsicmp(args.coord_system.c_str(), L"cecf") == 0 && cecfHasLonLatBBox) {
        const double fxC = cecfIsGlobalLike ? ((static_cast<double>(x) + 0.5) / static_cast<double>((std::max)(1, w)))
                                            : static_cast<double>(fx);
        const double fyC = (static_cast<double>(y) + 0.5) / static_cast<double>((std::max)(1, h));
        const double lonSpan = cecfLonMax - cecfLonMin;
        px = cecfLonMin + lonSpan * fxC;
        py = cecfLatMin + (cecfLatMax - cecfLatMin) * fyC;
      } else if (_wcsicmp(args.coord_system.c_str(), L"cecf") == 0 && !cecfHasLonLatBBox) {
        std::wcerr << L"[ERROR] CECF requires valid geographic extent; cannot derive lon/lat bbox from input.\n";
        return 7;
      }
      if (_wcsicmp(args.coord_system.c_str(), L"cecf") == 0) {
        double lonDeg = px;
        double latDeg = py;
#if GIS_DESKTOP_HAVE_GDAL
        if (raster.ok && cecfFromRasterToGeo) {
          double tz = 0.0;
          OCTTransform(cecfFromRasterToGeo, 1, &lonDeg, &latDeg, &tz);
        }
#endif
        latDeg = (std::clamp)(latDeg, -89.999999, 89.999999);
        const double lon = lonDeg * kPi / 180.0;
        const double lat = latDeg * kPi / 180.0;
        const double R = 6378137.0 + pz * elevRatio;
        const double xEcef = R * std::cos(lat) * std::cos(lon) * unitS;
        const double yEcef = R * std::cos(lat) * std::sin(lon) * unitS;
        const double zEcef = R * std::sin(lat) * unitS;
        ss << L"v " << xEcef << L" " << yEcef << L" " << zEcef << L"\n";
        gridVerts.push_back({xEcef, yEcef, zEcef});
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
        gridVerts.push_back({xo, yo, zo});
      }
      const bool pole = (isCecf && (y == 0 || y == h - 1));
      const float tu = pole ? 0.5f : fx;
      float tvOut = 1.0f - fy;
      if (pole) {
        const float epsV = 0.5f / static_cast<float>((std::max)(2, h));
        tvOut = (y == 0) ? (1.0f - epsV) : epsV;
      }
      ss << L"vt " << tu << L" " << tvOut << L"\n";
    }
  }
  ss << L"vn 0 0 1\n";
  ss << L"usemtl defaultMat\n";
  auto vid = [w](int x, int y) { return y * w + x + 1; };
  auto emitTriIfValid = [&](int a, int b, int c) {
    const auto& A = gridVerts[static_cast<size_t>(a - 1)];
    const auto& B = gridVerts[static_cast<size_t>(b - 1)];
    const auto& C = gridVerts[static_cast<size_t>(c - 1)];
    const double abx = B[0] - A[0], aby = B[1] - A[1], abz = B[2] - A[2];
    const double acx = C[0] - A[0], acy = C[1] - A[1], acz = C[2] - A[2];
    const double cx = aby * acz - abz * acy;
    const double cy = abz * acx - abx * acz;
    const double cz = abx * acy - aby * acx;
    const double area2 = cx * cx + cy * cy + cz * cz;
    const double e1 = abx * abx + aby * aby + abz * abz;
    const double e2 = acx * acx + acy * acy + acz * acz;
    const double maxE = (std::max)(e1, e2);
    // 不再按 area2 剔除：规则 DEM/TIN 网格上三角常近乎共面，area2 相对 maxE 极小会被误删，OBJ 呈破碎三角。
    if (maxE <= 0.0 || !std::isfinite(maxE) || !std::isfinite(area2)) {
      return;
    }
    int ib = b;
    int ic = c;
    if (isCecf) {
      // CECF 球面：按“从地心指向外侧”为正，统一三角绕序，避免法线里外翻转。
      const double mx = (A[0] + B[0] + C[0]) / 3.0;
      const double my = (A[1] + B[1] + C[1]) / 3.0;
      const double mz = (A[2] + B[2] + C[2]) / 3.0;
      const double outward = cx * mx + cy * my + cz * mz;
      if (outward < 0.0) {
        ib = c;
        ic = b;
      }
    } else {
      // projected/其它平面模型：统一 +Z 朝上，避免法线整体反向导致明暗颠倒。
      if (cz < 0.0) {
        ib = c;
        ic = b;
      }
    }
    ss << L"f " << a << L"/" << a << L"/1 " << ib << L"/" << ib << L"/1 " << ic << L"/" << ic << L"/1\n";
  };
  int northPoleIdx = 0;
  int southPoleIdx = 0;
  if (isCecf && h >= 4 && w >= 3) {
    // 极帽单顶点重建：追加北极/南极单顶点，后续用扇形连接次极圈，规避极区退化与叠片。
    std::array<double, 3> north{0.0, 0.0, 0.0};
    std::array<double, 3> south{0.0, 0.0, 0.0};
    for (int x = 0; x < w; ++x) {
      const auto& vn = gridVerts[static_cast<size_t>(vid(x, 0) - 1)];
      const auto& vs = gridVerts[static_cast<size_t>(vid(x, h - 1) - 1)];
      north[0] += vn[0];
      north[1] += vn[1];
      north[2] += vn[2];
      south[0] += vs[0];
      south[1] += vs[1];
      south[2] += vs[2];
    }
    const double invW = 1.0 / static_cast<double>(w);
    north[0] *= invW;
    north[1] *= invW;
    north[2] *= invW;
    south[0] *= invW;
    south[1] *= invW;
    south[2] *= invW;

    northPoleIdx = static_cast<int>(gridVerts.size()) + 1;
    ss << L"v " << north[0] << L" " << north[1] << L" " << north[2] << L"\n";
    ss << L"vt 0.5 " << (1.0f - 0.5f / static_cast<float>((std::max)(2, h))) << L"\n";
    gridVerts.push_back(north);

    southPoleIdx = static_cast<int>(gridVerts.size()) + 1;
    ss << L"v " << south[0] << L" " << south[1] << L" " << south[2] << L"\n";
    ss << L"vt 0.5 " << (0.5f / static_cast<float>((std::max)(2, h))) << L"\n";
    gridVerts.push_back(south);

    for (int y = 1; y < gridH - 1; ++y) {
      for (int x = 0; x < gridW; ++x) {
        const int v00 = vid(x, y);
        const int v10 = vid(x + 1, y);
        const int v01 = vid(x, y + 1);
        const int v11 = vid(x + 1, y + 1);
        emitTriIfValid(v00, v10, v11);
        emitTriIfValid(v00, v11, v01);
      }
    }
    for (int x = 0; x < gridW; ++x) {
      emitTriIfValid(northPoleIdx, vid(x + 1, 1), vid(x, 1));
      emitTriIfValid(southPoleIdx, vid(x, h - 2), vid(x + 1, h - 2));
    }
  } else {
    const int64_t nVert = static_cast<int64_t>(w) * static_cast<int64_t>(h);
    const bool wantDelaunay = !isCecf && _wcsicmp(args.gis_mesh_topology.c_str(), L"delaunay") == 0 && gridW >= 1 &&
                              gridH >= 1 && nVert >= 3 && nVert <= 8192;
    if (_wcsicmp(args.gis_mesh_topology.c_str(), L"delaunay") == 0 && !wantDelaunay) {
      if (isCecf) {
        std::wcout << L"[MESH] delaunay: skipped (cecf uses structured grid).\n";
      } else if (nVert > 8192) {
        std::wcout << L"[MESH] delaunay: skipped (vertices " << nVert << L" > 8192), using grid.\n";
      } else if (gridW < 1 || gridH < 1) {
        std::wcout << L"[MESH] delaunay: skipped (degenerate grid).\n";
      }
    }
    if (wantDelaunay) {
      std::vector<std::array<double, 2>> pts2d;
      pts2d.reserve(static_cast<size_t>(nVert));
      for (int yi = 0; yi < h; ++yi) {
        for (int xi = 0; xi < w; ++xi) {
          const auto& v = gridVerts[static_cast<size_t>(vid(xi, yi) - 1)];
          pts2d.push_back({v[0], v[1]});
        }
      }
      const std::vector<AgisMeshDelaunayTri> dtris = AgisMeshDelaunayTriangulate2D(pts2d);
      std::wcout << L"[MESH] delaunay: " << dtris.size() << L" triangles, " << nVert << L" vertices\n";
      for (const auto& t : dtris) {
        emitTriIfValid(t.a + 1, t.b + 1, t.c + 1);
      }
    } else {
      for (int y = 0; y < gridH; ++y) {
        if ((y & 31) == 0 || y + 1 == gridH) {
          const int p = 74 + static_cast<int>((static_cast<double>(y + 1) / static_cast<double>((std::max)(1, gridH))) * 8.0);
          reportProgress(p, L"生成网格三角面");
        }
        for (int x = 0; x < gridW; ++x) {
          const int v00 = vid(x, y);
          const int v10 = vid(x + 1, y);
          const int v01 = vid(x, y + 1);
          const int v11 = vid(x + 1, y + 1);
          emitTriIfValid(v00, v10, v11);
          emitTriIfValid(v00, v11, v01);
        }
      }
    }
  }
  int baseVertex = static_cast<int>(gridVerts.size()) + 1;
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
  if (cecfFromRasterToGeo) {
    OCTDestroyCoordinateTransformation(cecfFromRasterToGeo);
    cecfFromRasterToGeo = nullptr;
  }
  if (cecfFromTargetToGeo) {
    OCTDestroyCoordinateTransformation(cecfFromTargetToGeo);
    cecfFromTargetToGeo = nullptr;
  }
  if (cecfGeoSr) {
    OSRDestroySpatialReference(cecfGeoSr);
    cecfGeoSr = nullptr;
  }
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
  reportProgressMeta(L"写 OBJ 主文件", 1, 1, L"write", meshWritePath);
  int rc = WriteTextFile(meshWritePath, ss.str());
  if (rc != 0) {
    return rc;
  }
  const bool wantPbrTextures = !wantPointCloud && (_wcsicmp(args.obj_texture_mode.c_str(), L"pbr") == 0);
  if (wantPointCloud) {
    reportProgress(85, L"生成点云采样用 albedo 贴图");
  } else if (wantPbrTextures) {
    reportProgress(85, L"生成 albedo 与 PBR 贴图");
  } else {
    reportProgress(85, L"生成 albedo 贴图");
  }
  const std::wstring texExt = NormalizeTextureFmtExt(args.texture_format);
  const std::wstring texName = meshWritePath.stem().wstring() + L"_albedo" + texExt;
  const std::wstring normalName = meshWritePath.stem().wstring() + L"_normal" + texExt;
  const std::wstring roughName = meshWritePath.stem().wstring() + L"_roughness" + texExt;
  const std::wstring metalName = meshWritePath.stem().wstring() + L"_metallic" + texExt;
  const std::wstring aoName = meshWritePath.stem().wstring() + L"_ao" + texExt;
  const std::filesystem::path texPath = meshWritePath.parent_path() / texName;
  const std::filesystem::path normalPath = meshWritePath.parent_path() / normalName;
  const std::filesystem::path roughPath = meshWritePath.parent_path() / roughName;
  const std::filesystem::path metalPath = meshWritePath.parent_path() / metalName;
  const std::filesystem::path aoPath = meshWritePath.parent_path() / aoName;
  reportProgressMeta(L"写贴图", 1, wantPbrTextures ? 5 : 1, L"write", texPath);
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
    ApplyObjVisualEffectOnAlbedo(&texForMaps.rgb, texForMaps.w, texForMaps.h, args.obj_visual_effect, args.obj_snow_scale);
    trc = WriteRgbTextureFile(texPath, texForMaps.w, texForMaps.h, texForMaps.rgb, args.texture_format);
    if (trc == 0 && wantPbrTextures) {
      const PbrMaps p = BuildPbrFromElevation(texForMaps);
      if (!p.normalRgb.empty()) {
        reportProgressMeta(L"写贴图", 2, 5, L"write", normalPath);
        trc = WriteRgbTextureFile(normalPath, texForMaps.w, texForMaps.h, p.normalRgb, args.texture_format);
      }
      std::vector<unsigned char> grayAsRgb;
      auto writeGrayByTextureFormat = [&](const std::filesystem::path& outPath, const std::vector<unsigned char>& gray) -> int {
        if (gray.empty()) {
          return 0;
        }
        grayAsRgb.resize(gray.size() * 3);
        for (size_t i = 0; i < gray.size(); ++i) {
          const unsigned char g = gray[i];
          grayAsRgb[i * 3 + 0] = g;
          grayAsRgb[i * 3 + 1] = g;
          grayAsRgb[i * 3 + 2] = g;
        }
        return WriteRgbTextureFile(outPath, texForMaps.w, texForMaps.h, grayAsRgb, args.texture_format);
      };
      if (trc == 0) {
        reportProgressMeta(L"写贴图", 3, 5, L"write", roughPath);
        trc = writeGrayByTextureFormat(roughPath, p.roughness);
      }
      if (trc == 0) {
        reportProgressMeta(L"写贴图", 4, 5, L"write", metalPath);
        trc = writeGrayByTextureFormat(metalPath, p.metallic);
      }
      if (trc == 0) {
        reportProgressMeta(L"写贴图", 5, 5, L"write", aoPath);
        trc = writeGrayByTextureFormat(aoPath, p.ao);
      }
    }
  } else {
    const std::vector<unsigned char> kFallbackRgb = {104, 120, 216};
    trc = WriteRgbTextureFile(texPath, 1, 1, kFallbackRgb, args.texture_format);
    if (trc == 0 && wantPbrTextures) {
      if (trc == 0) trc = WriteRgbTextureFile(normalPath, 1, 1, kFallbackRgb, args.texture_format);
      if (trc == 0) trc = WriteRgbTextureFile(roughPath, 1, 1, kFallbackRgb, args.texture_format);
      if (trc == 0) trc = WriteRgbTextureFile(metalPath, 1, 1, kFallbackRgb, args.texture_format);
      if (trc == 0) trc = WriteRgbTextureFile(aoPath, 1, 1, kFallbackRgb, args.texture_format);
    }
  }
#else
  const std::vector<unsigned char> kFallbackRgb = {104, 120, 216};
  trc = WriteRgbTextureFile(texPath, 1, 1, kFallbackRgb, args.texture_format);
  if (trc == 0 && wantPbrTextures) {
    if (trc == 0) trc = WriteRgbTextureFile(normalPath, 1, 1, kFallbackRgb, args.texture_format);
    if (trc == 0) trc = WriteRgbTextureFile(roughPath, 1, 1, kFallbackRgb, args.texture_format);
    if (trc == 0) trc = WriteRgbTextureFile(metalPath, 1, 1, kFallbackRgb, args.texture_format);
    if (trc == 0) trc = WriteRgbTextureFile(aoPath, 1, 1, kFallbackRgb, args.texture_format);
  }
#endif
  if (trc != 0) {
    if (wantPointCloud && !tmpPcDir.empty()) {
      std::error_code ex;
      std::filesystem::remove_all(tmpPcDir, ex);
    }
    return trc;
  }

  if (wantPointCloud) {
    const std::wstring mtlSlim = L"newmtl defaultMat\nKd 0.8 0.8 0.8\nmap_Kd " + texName + L"\n";
    const int mtlPc = WriteTextFile(meshWritePath.parent_path() / mtlName, mtlSlim);
    if (mtlPc != 0) {
      if (!tmpPcDir.empty()) {
        std::error_code ex;
        std::filesystem::remove_all(tmpPcDir, ex);
      }
      return mtlPc;
    }
    reportProgress(92, L"按三角网与贴图像素采样，写出 LAS/LAZ");
    ConvertArgs lasArgs = args;
    lasArgs.input = meshWritePath.wstring();
    lasArgs.output = outPath.wstring();
    const int lasRc = ConvertMeshObjToLasJob(lasArgs);
    if (!tmpPcDir.empty()) {
      std::error_code ex;
      std::filesystem::remove_all(tmpPcDir, ex);
    }
    if (lasRc == 0) {
      reportProgress(100, L"点云转换完成");
    }
    return lasRc;
  }

  const std::wstring mtlText =
      wantPbrTextures ? (L"newmtl defaultMat\n"
                         L"Kd 0.8 0.8 0.8\n"
                         L"Ka 0.1 0.1 0.1\n"
                         L"Ks 0.2 0.2 0.2\n"
                         L"map_Kd " + texName + L"\n"
                         L"map_Bump " + normalName + L"\n"
                         L"map_Pr " + roughName + L"\n"
                         L"map_Pm " + metalName + L"\n"
                         L"map_AO " + aoName + L"\n")
                      : (L"newmtl defaultMat\n"
                         L"Kd 0.8 0.8 0.8\n"
                         L"Ka 0.05 0.05 0.05\n"
                         L"map_Kd " + texName + L"\n");
  const int mtlRc = WriteTextFile(meshWritePath.parent_path() / mtlName, mtlText);
  if (mtlRc == 0) {
    reportProgress(100, L"转换完成");
  }
  return mtlRc;
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

static std::string AgisWideToUtf8ForCApi(const std::wstring& ws) {
  if (ws.empty()) {
    return {};
  }
  const int n =
      WideCharToMultiByte(CP_UTF8, 0, ws.c_str(), static_cast<int>(ws.size()), nullptr, 0, nullptr, nullptr);
  if (n <= 0) {
    return {};
  }
  std::string out(static_cast<size_t>(n), '\0');
  WideCharToMultiByte(CP_UTF8, 0, ws.c_str(), static_cast<int>(ws.size()), out.data(), n, nullptr, nullptr);
  return out;
}

#if defined(AGIS_HAVE_LASZIP) && AGIS_HAVE_LASZIP
/// 与 WriteLas12Pdrf2 相同的 LAS 1.2 PDRF 2（RGB）语义，经 LASzip 压缩为 LAZ。
static int WriteLas12Pdrf2Laz(const std::filesystem::path& path, const std::vector<LasPointFlt>& pts) {
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
  const std::uint32_t nPts = static_cast<std::uint32_t>(pts.size());
  const std::string utf8Path = AgisWideToUtf8ForCApi(path.wstring());
  if (utf8Path.empty() && !path.empty()) {
    std::wcerr << L"[ERROR] LAZ：无法将输出路径转为 UTF-8。\n";
    return 3;
  }
  laszip_POINTER laszip = nullptr;
  if (laszip_create(&laszip) != 0 || !laszip) {
    std::wcerr << L"[ERROR] LAZ：laszip_create 失败。\n";
    return 3;
  }
  laszip_header_struct* header = nullptr;
  if (laszip_get_header_pointer(laszip, &header) != 0 || !header) {
    std::wcerr << L"[ERROR] LAZ：无法取得 header 指针。\n";
    laszip_destroy(laszip);
    return 3;
  }
  std::memset(header, 0, sizeof(laszip_header_struct));
  header->header_size = 227;
  header->offset_to_point_data = 227;
  header->number_of_variable_length_records = 0;
  header->start_of_waveform_data_packet_record = 0;
  header->start_of_first_extended_variable_length_record = 0;
  header->number_of_extended_variable_length_records = 0;
  header->extended_number_of_point_records = 0;
  header->vlrs = nullptr;
  header->user_data_in_header = nullptr;
  header->user_data_after_header = nullptr;
  header->version_major = 1;
  header->version_minor = 2;
  {
    const char* sys = "AGIS";
    const char* gen = "agis_convert_model_to_model LAZ";
    std::memcpy(header->system_identifier, sys, (std::min)(std::strlen(sys), size_t(31)));
    std::memcpy(header->generating_software, gen, (std::min)(std::strlen(gen), size_t(31)));
  }
  header->point_data_format = 2;
  header->point_data_record_length = 26;
  header->number_of_point_records = nPts;
  header->number_of_points_by_return[0] = nPts;
  header->x_scale_factor = xscale;
  header->y_scale_factor = yscale;
  header->z_scale_factor = zscale;
  header->x_offset = xoff;
  header->y_offset = yoff;
  header->z_offset = zoff;
  header->max_x = maxx;
  header->min_x = minx;
  header->max_y = maxy;
  header->min_y = miny;
  header->max_z = maxz;
  header->min_z = minz;
  if (laszip_open_writer(laszip, utf8Path.c_str(), 1) != 0) {
    laszip_CHAR* err = nullptr;
    if (laszip_get_error(laszip, &err) == 0 && err && err[0]) {
      std::wcerr << L"[ERROR] LAZ：open_writer: ";
      std::cerr << err;
      std::wcerr << L"\n";
    } else {
      std::wcerr << L"[ERROR] LAZ：laszip_open_writer 失败。\n";
    }
    laszip_destroy(laszip);
    return 3;
  }
  laszip_point_struct* point = nullptr;
  if (laszip_get_point_pointer(laszip, &point) != 0 || !point) {
    std::wcerr << L"[ERROR] LAZ：无法取得 point 指针。\n";
    laszip_close_writer(laszip);
    laszip_destroy(laszip);
    return 3;
  }
  laszip_F64 coords[3]{};
  for (const auto& p : pts) {
    coords[0] = p.x;
    coords[1] = p.y;
    coords[2] = p.z;
    if (laszip_set_coordinates(laszip, coords) != 0) {
      std::wcerr << L"[ERROR] LAZ：laszip_set_coordinates 失败。\n";
      laszip_close_writer(laszip);
      laszip_destroy(laszip);
      return 6;
    }
    point->intensity = 0;
    point->return_number = 1;
    point->number_of_returns = 1;
    point->classification = 1;
    point->scan_angle_rank = 0;
    point->user_data = 0;
    point->point_source_ID = 1;
    const std::uint16_t R = static_cast<std::uint16_t>(p.r * 257);
    const std::uint16_t G = static_cast<std::uint16_t>(p.g * 257);
    const std::uint16_t B = static_cast<std::uint16_t>(p.b * 257);
    point->gps_time = 0;
    point->rgb[0] = R;
    point->rgb[1] = G;
    point->rgb[2] = B;
    point->rgb[3] = 0;
    if (laszip_write_point(laszip) != 0) {
      laszip_CHAR* err = nullptr;
      if (laszip_get_error(laszip, &err) == 0 && err && err[0]) {
        std::wcerr << L"[ERROR] LAZ：write_point: ";
        std::cerr << err;
        std::wcerr << L"\n";
      } else {
        std::wcerr << L"[ERROR] LAZ：laszip_write_point 失败。\n";
      }
      laszip_close_writer(laszip);
      laszip_destroy(laszip);
      return 6;
    }
  }
  if (laszip_close_writer(laszip) != 0) {
    std::wcerr << L"[ERROR] LAZ：laszip_close_writer 失败。\n";
    laszip_destroy(laszip);
    return 6;
  }
  if (laszip_destroy(laszip) != 0) {
    return 6;
  }
  return 0;
}
#endif  // AGIS_HAVE_LASZIP

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
    AgisEnsureGdalDataPath();
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
  constexpr size_t kMaxLasPointsTotal = 80000000;
  constexpr int64_t kMaxRasterCellsPerTri = 400000;
  try {
    const size_t hint = (std::min)(obj.tris.size() * 32 + 4096, kMaxLasPointsTotal);
    pts.reserve(hint);
  } catch (...) {
    pts.clear();
  }
  bool warnedTexSubsample = false;

  for (const auto& tri : obj.tris) {
    if (pts.size() >= kMaxLasPointsTotal) {
      std::wcerr << L"[WARN] 已达点数上限 " << kMaxLasPointsTotal
                 << L"，提前结束；可增大 --mesh-spacing 或降低贴图分辨率。\n";
      break;
    }
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
      int triStep = step;
      if (i0 <= i1 && j0 <= j1) {
        auto cellCount = [&](int s) -> int64_t {
          if (s < 1) {
            return 0;
          }
          const int64_t nw = static_cast<int64_t>(i1 - i0) / s + 1;
          const int64_t nh = static_cast<int64_t>(j1 - j0) / s + 1;
          return nw * nh;
        };
        while (cellCount(triStep) > kMaxRasterCellsPerTri && triStep < 65536) {
          triStep *= 2;
        }
        if (triStep > step && !warnedTexSubsample) {
          std::wcout << L"[WARN] 贴图按三角包围像素过多，已自动加大采样步长（单三角约 " << kMaxRasterCellsPerTri
                     << L" 像素上限）；可调 --mesh-spacing。\n";
          warnedTexSubsample = true;
        }
      }
      for (int jj = j0; jj <= j1; jj += triStep) {
        for (int ii = i0; ii <= i1; ii += triStep) {
          if (pts.size() >= kMaxLasPointsTotal) {
            break;
          }
          const double uu = (static_cast<double>(ii) + 0.5) / static_cast<double>(tw);
          const double vv = 1.0 - (static_cast<double>(jj) + 0.5) / static_cast<double>(th);
          double w0 = 0, w1 = 0, w2 = 0;
          if (!BarycentricUv(u0, v0, u1, v1, u2, v2, uu, vv, &w0, &w1, &w2)) {
            continue;
          }
          const double px = w0 * p0[0] + w1 * p1[0] + w2 * p2[0];
          const double py = w0 * p0[1] + w1 * p1[1] + w2 * p2[1];
          const double pz = w0 * p0[2] + w1 * p1[2] + w2 * p2[2];
          const size_t ti = (static_cast<size_t>(jj) * static_cast<size_t>(tw) + static_cast<size_t>(ii)) * 3;
          if (ti + 2 >= rgb.size()) {
            continue;
          }
          LasPointFlt lp;
          lp.x = px;
          lp.y = py;
          lp.z = pz;
          lp.r = rgb[ti + 0];
          lp.g = rgb[ti + 1];
          lp.b = rgb[ti + 2];
          pts.push_back(lp);
        }
        if (pts.size() >= kMaxLasPointsTotal) {
          break;
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
#if defined(AGIS_HAVE_LASZIP) && AGIS_HAVE_LASZIP
    std::wcout << L"[OUT] LAZ (LASzip): " << outLas.wstring() << L" 点数=" << pts.size() << L"\n";
    return WriteLas12Pdrf2Laz(outLas, pts);
#else
    std::wcerr << L"[WARN] 构建未编入 bundled LASzip，无法写入 LAZ；改输出为同目录 .las。\n";
    outLas.replace_extension(L".las");
#endif
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
  if (f.find(L"ktx2") != std::wstring::npos || f.find(L"basis") != std::wstring::npos ||
      f.find(L"uastc") != std::wstring::npos) {
    return L".ktx2";
  }
  return L".png";
}

static int ConvertLasToMeshObjJob(const ConvertArgs& args) {
  const bool useFloatObj = (_wcsicmp(args.obj_fp_type.c_str(), L"float") == 0);
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
  objText += L"# obj_fp_type: ";
  objText += useFloatObj ? L"float\n" : L"double\n";
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
      swprintf_s(buf, useFloatObj ? L"v %.7g %.7g %.7g\n" : L"v %.15g %.15g %.15g\n", x, y, z);
      objText += buf;
    }
  }
  for (int j = 0; j < ny; ++j) {
    for (int i = 0; i < nx; ++i) {
      const double u = nx > 1 ? static_cast<double>(i) / static_cast<double>(nx - 1) : 0.5;
      const double v = ny > 1 ? static_cast<double>(j) / static_cast<double>(ny - 1) : 0.5;
      wchar_t buf[96]{};
      swprintf_s(buf, useFloatObj ? L"vt %.7g %.7g\n" : L"vt %.15g %.15g\n", u, v);
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
#if GIS_DESKTOP_HAVE_GDAL
  EnsureGdalRegisteredOnce();
#endif
  const std::filesystem::path outDir(args.output);
  std::wstring subtype = args.output_subtype;
  for (auto& c : subtype) {
    if (c >= L'A' && c <= L'Z') {
      c = static_cast<wchar_t>(c - L'A' + L'a');
    }
  }
  if (subtype != L"xyz" && subtype != L"tms" && subtype != L"wmts" && subtype != L"mbtiles" && subtype != L"gpkg" &&
      subtype != L"3dtiles") {
    subtype = L"xyz";
  }
  // 单文件容器只建父目录；目录型协议建输出根目录。
  if (subtype == L"mbtiles" || subtype == L"gpkg") {
    EnsureParent(outDir);
  } else {
    const int erc = EnsureDir(outDir);
    if (erc != 0) {
      return erc;
    }
  }
  std::wstring texFmt = args.texture_format;
  for (auto& c : texFmt) {
    if (c >= L'A' && c <= L'Z') {
      c = static_cast<wchar_t>(c - L'A' + L'a');
    }
  }
  std::wstring ext = L".png";
  if (texFmt.find(L"tif") != std::wstring::npos) {
    ext = L".tif";
  } else if (texFmt.find(L"bmp") != std::wstring::npos) {
    ext = L".bmp";
  } else if (texFmt.find(L"tga") != std::wstring::npos) {
    ext = L".tga";
  } else {
    texFmt = L"png";
    ext = L".png";
  }
  int rc = 0;
  std::wcout << L"[TILE] protocol: " << subtype << L", format: " << texFmt << L"\n";

  RasterExtract raster;
  bool haveRaster = false;
  GisDocInfo doc;
  const std::filesystem::path inPath(args.input);
  const bool isGis = _wcsicmp(inPath.extension().wstring().c_str(), L".gis") == 0;
  if (isGis) {
    doc = ParseGisDocInfo(inPath);
    const auto sources = ParseGisLayerSources(inPath);
    for (const auto& s : sources) {
      if (TryReadRaster(s, &raster, args.raster_read_max_dim)) {
        haveRaster = true;
        break;
      }
    }
  } else {
    haveRaster = TryReadRaster(args.input, &raster, args.raster_read_max_dim);
  }
  // 输出元数据范围：优先栅格真实范围 -> .gis 视口 -> 全球默认。
  double bLonMin = -180.0, bLonMax = 180.0, bLatMin = -85.05112878, bLatMax = 85.05112878;
  double bMxMin = -20037508.3427892, bMxMax = 20037508.3427892;
  double bMyMin = -20037508.3427892, bMyMax = 20037508.3427892;
  bool haveBounds = false;
#if GIS_DESKTOP_HAVE_GDAL
  if (haveRaster && !raster.wkt.empty()) {
    OGRSpatialReferenceH srcSr = OSRNewSpatialReference(raster.wkt.c_str());
    OGRSpatialReferenceH wgs84 = OSRNewSpatialReference(nullptr);
    OGRSpatialReferenceH merc = OSRNewSpatialReference(nullptr);
    if (srcSr && wgs84 && merc && OSRImportFromEPSG(wgs84, 4326) == OGRERR_NONE && OSRImportFromEPSG(merc, 3857) == OGRERR_NONE) {
      OGRCoordinateTransformationH toWgs84 = OCTNewCoordinateTransformation(srcSr, wgs84);
      OGRCoordinateTransformationH toMerc = OCTNewCoordinateTransformation(srcSr, merc);
      (void)toMerc;
      if (toWgs84) {
        const int rw = (std::max)(1, raster.srcW > 0 ? raster.srcW : raster.w);
        const int rh = (std::max)(1, raster.srcH > 0 ? raster.srcH : raster.h);
        double lonMin = 1e300, lonMax = -1e300, latMin = 1e300, latMax = -1e300;
        double mxMin = 1e300, mxMax = -1e300, myMin = 1e300, myMax = -1e300;
        constexpr double kEarthRadius = 6378137.0;
        constexpr double kPi = 3.14159265358979323846;
        for (int ix : {0, rw - 1}) {
          for (int iy : {0, rh - 1}) {
            const double px = raster.gt[0] + static_cast<double>(ix) * raster.gt[1] + static_cast<double>(iy) * raster.gt[2];
            const double py = raster.gt[3] + static_cast<double>(ix) * raster.gt[4] + static_cast<double>(iy) * raster.gt[5];
            double x1 = px, y1 = py, z1 = 0.0;
            if (OCTTransform(toWgs84, 1, &x1, &y1, &z1)) {
              lonMin = (std::min)(lonMin, x1);
              lonMax = (std::max)(lonMax, x1);
              latMin = (std::min)(latMin, y1);
              latMax = (std::max)(latMax, y1);
              const double lonDeg = (std::clamp)(x1, -180.0, 180.0);
              const double latDeg = (std::clamp)(y1, -85.05112878, 85.05112878);
              const double lonRad = lonDeg * kPi / 180.0;
              const double latRad = latDeg * kPi / 180.0;
              const double mx = kEarthRadius * lonRad;
              const double my = kEarthRadius * std::log(std::tan(kPi * 0.25 + latRad * 0.5));
              mxMin = (std::min)(mxMin, mx);
              mxMax = (std::max)(mxMax, mx);
              myMin = (std::min)(myMin, my);
              myMax = (std::max)(myMax, my);
            }
          }
        }
        if (lonMin <= lonMax && latMin <= latMax && mxMin <= mxMax && myMin <= myMax) {
          bLonMin = (std::clamp)(lonMin, -180.0, 180.0);
          bLonMax = (std::clamp)(lonMax, -180.0, 180.0);
          bLatMin = (std::clamp)(latMin, -85.05112878, 85.05112878);
          bLatMax = (std::clamp)(latMax, -85.05112878, 85.05112878);
          bMxMin = mxMin;
          bMxMax = mxMax;
          bMyMin = myMin;
          bMyMax = myMax;
          haveBounds = true;
        }
      }
      if (toWgs84) OCTDestroyCoordinateTransformation(toWgs84);
      if (toMerc) OCTDestroyCoordinateTransformation(toMerc);
    }
    if (srcSr) OSRDestroySpatialReference(srcSr);
    if (wgs84) OSRDestroySpatialReference(wgs84);
    if (merc) OSRDestroySpatialReference(merc);
  }
#endif
  if (!haveBounds && doc.ok) {
    bLonMin = (std::min)(doc.minx, doc.maxx);
    bLonMax = (std::max)(doc.minx, doc.maxx);
    bLatMin = (std::min)(doc.miny, doc.maxy);
    bLatMax = (std::max)(doc.miny, doc.maxy);
    bLonMin = (std::clamp)(bLonMin, -180.0, 180.0);
    bLonMax = (std::clamp)(bLonMax, -180.0, 180.0);
    bLatMin = (std::clamp)(bLatMin, -85.05112878, 85.05112878);
    bLatMax = (std::clamp)(bLatMax, -85.05112878, 85.05112878);
  }

  constexpr int kTileSize = 256;
  std::vector<unsigned char> tileRgb(static_cast<size_t>(kTileSize) * kTileSize * 3);
  auto buildTileRgb = [&](int z, int x, int yXyz) {
    if (haveRaster && raster.w > 1 && raster.h > 1 && raster.rgb.size() >= static_cast<size_t>(raster.w * raster.h * 3)) {
      const int dim = 1 << z;
      for (int py = 0; py < kTileSize; ++py) {
        for (int px = 0; px < kTileSize; ++px) {
          const double gx = (static_cast<double>(x) + (static_cast<double>(px) + 0.5) / static_cast<double>(kTileSize)) /
                            static_cast<double>(dim);
          const double gy = (static_cast<double>(yXyz) + (static_cast<double>(py) + 0.5) / static_cast<double>(kTileSize)) /
                            static_cast<double>(dim);
          const double sx = gx * static_cast<double>(raster.w - 1);
          const double sy = gy * static_cast<double>(raster.h - 1);
          const size_t di = (static_cast<size_t>(py) * kTileSize + px) * 3;
          tileRgb[di + 0] = SampleBilinearRgb(raster.rgb, raster.w, raster.h, 0, sx, sy);
          tileRgb[di + 1] = SampleBilinearRgb(raster.rgb, raster.w, raster.h, 1, sx, sy);
          tileRgb[di + 2] = SampleBilinearRgb(raster.rgb, raster.w, raster.h, 2, sx, sy);
        }
      }
      return;
    }
    // 无栅格时输出可辨识的占位图块（而不是 txt）。
    for (int py = 0; py < kTileSize; ++py) {
      for (int px = 0; px < kTileSize; ++px) {
        const size_t di = (static_cast<size_t>(py) * kTileSize + px) * 3;
        const unsigned char r = static_cast<unsigned char>((x * 53 + z * 41 + px) & 0xff);
        const unsigned char g = static_cast<unsigned char>((yXyz * 67 + z * 19 + py) & 0xff);
        const unsigned char b = static_cast<unsigned char>((z * 85 + (px ^ py)) & 0xff);
        tileRgb[di + 0] = r;
        tileRgb[di + 1] = g;
        tileRgb[di + 2] = b;
      }
    }
  };

  constexpr int kMinZ = 0;
  int kMaxZ = 2;
  if (args.tile_levels > 0) {
    kMaxZ = args.tile_levels - 1;
  } else if (haveRaster) {
    const int srcW = (std::max)(1, raster.srcW > 0 ? raster.srcW : raster.w);
    const int srcH = (std::max)(1, raster.srcH > 0 ? raster.srcH : raster.h);
    const int minSide = (std::max)(1, (std::min)(srcW, srcH));
    if (minSide <= kTileSize) {
      kMaxZ = 0;
    } else {
      const double ratio = static_cast<double>(minSide) / static_cast<double>(kTileSize);
      kMaxZ = static_cast<int>(std::floor(std::log2(ratio)));
    }
  }
  kMaxZ = (std::clamp)(kMaxZ, 0, 22);
  std::wcout << L"[TILE] levels: " << (kMaxZ - kMinZ + 1) << L" (z=" << kMinZ << L".." << kMaxZ
             << (args.tile_levels > 0 ? L", manual" : L", auto") << L")\n";
  if (subtype == L"mbtiles" || subtype == L"gpkg") {
#if GIS_DESKTOP_HAVE_GDAL
    std::filesystem::path outPath = outDir;
    // 仅当 --output 为「已存在的目录」时在其下生成默认文件名；无扩展名的非目录路径按「单文件主名」处理，
    // 避免把 D:\foo 误当成目录而写成 D:\foo\tiles.mbtiles（用户意图多为 D:\foo.mbtiles）。
    if (std::filesystem::is_directory(outPath)) {
      outPath /= (subtype == L"mbtiles" ? L"tiles.mbtiles" : L"tiles.gpkg");
    } else if (outPath.extension().empty()) {
      outPath.replace_extension(subtype == L"mbtiles" ? L".mbtiles" : L".gpkg");
    }
    if (subtype == L"mbtiles" && _wcsicmp(outPath.extension().wstring().c_str(), L".mbtiles") != 0) {
      outPath.replace_extension(L".mbtiles");
    }
    if (subtype == L"gpkg" && _wcsicmp(outPath.extension().wstring().c_str(), L".gpkg") != 0) {
      outPath.replace_extension(L".gpkg");
    }
    EnsureParent(outPath);
    GDALDriverH drv = GDALGetDriverByName(subtype == L"mbtiles" ? "MBTiles" : "GPKG");
    if (!drv) {
      std::wcerr << L"[ERROR] GDAL driver not available: " << subtype << L"\n";
      return 7;
    }
    const int rasterW = kTileSize * (1 << kMaxZ);
    const int rasterH = kTileSize * (1 << kMaxZ);
    char** createOpts = nullptr;
    // MBTiles：像素尺寸须与标准 Web Mercator 某级 zoom 完全一致（见 GDAL MBTilesDataset::SetGeoTransform）。
    // 使用全球范围仿射 + BOUNDS 写入实际数据经纬度范围；勿用 MINZOOM/MAXZOOM（非本驱动 CreationOption）。
    if (subtype == L"mbtiles") {
      const char* tileFmtOpt = "PNG";
      if (texFmt.find(L"jpeg") != std::wstring::npos || texFmt.find(L"jpg") != std::wstring::npos) {
        tileFmtOpt = "JPEG";
      } else if (texFmt.find(L"webp") != std::wstring::npos) {
        tileFmtOpt = "WEBP";
      }
      createOpts = CSLSetNameValue(createOpts, "TILE_FORMAT", tileFmtOpt);
      std::ostringstream bnd;
      bnd << std::setprecision(17) << bLonMin << ',' << bLatMin << ',' << bLonMax << ',' << bLatMax;
      createOpts = CSLSetNameValue(createOpts, "BOUNDS", bnd.str().c_str());
    }
    GDALDatasetH ds = GDALCreate(drv, WideToUtf8(outPath.wstring()).c_str(), rasterW, rasterH, 3, GDT_Byte, createOpts);
    if (createOpts) {
      CSLDestroy(createOpts);
    }
    if (!ds) {
      std::wcerr << L"[ERROR] cannot create output container: " << outPath.wstring() << L"\n";
      return 7;
    }
    OGRSpatialReferenceH merc = OSRNewSpatialReference(nullptr);
    if (merc && OSRImportFromEPSG(merc, 3857) == OGRERR_NONE) {
      char* wkt = nullptr;
      if (OSRExportToWkt(merc, &wkt) == OGRERR_NONE && wkt) {
        GDALSetProjection(ds, wkt);
        CPLFree(wkt);
      }
    }
    if (merc) {
      OSRDestroySpatialReference(merc);
    }
    // 与 GDAL frmts/mbtiles TMS_ORIGIN 一致：左上 (-MAX_GM, MAX_GM)，全球 256·2^z 像素。
    constexpr double kSphericalRadius = 6378137.0;
    constexpr double kPi = 3.14159265358979323846;
    const double kMaxGm = kSphericalRadius * kPi;
    const double resX = (2.0 * kMaxGm) / static_cast<double>(rasterW);
    const double resY = (2.0 * kMaxGm) / static_cast<double>(rasterH);
    const double gt[6] = {-kMaxGm, resX, 0.0, kMaxGm, 0.0, -resY};
    if (GDALSetGeoTransform(ds, const_cast<double*>(gt)) != CE_None) {
      std::wcerr << L"[ERROR] " << subtype
                 << L": SetGeoTransform failed (need global Web Mercator grid; check GDAL errors).\n";
      GDALClose(ds);
      return 7;
    }
    std::vector<unsigned char> fullRgb(static_cast<size_t>(rasterW) * rasterH * 3);
    for (int y = 0; y < rasterH; ++y) {
      for (int x = 0; x < rasterW; ++x) {
        const double gx = (static_cast<double>(x) + 0.5) / static_cast<double>(rasterW);
        const double gy = (static_cast<double>(y) + 0.5) / static_cast<double>(rasterH);
        const size_t di = (static_cast<size_t>(y) * rasterW + x) * 3;
        if (haveRaster && raster.w > 1 && raster.h > 1 && raster.rgb.size() >= static_cast<size_t>(raster.w * raster.h * 3)) {
          const double sx = gx * static_cast<double>(raster.w - 1);
          const double sy = gy * static_cast<double>(raster.h - 1);
          fullRgb[di + 0] = SampleBilinearRgb(raster.rgb, raster.w, raster.h, 0, sx, sy);
          fullRgb[di + 1] = SampleBilinearRgb(raster.rgb, raster.w, raster.h, 1, sx, sy);
          fullRgb[di + 2] = SampleBilinearRgb(raster.rgb, raster.w, raster.h, 2, sx, sy);
        } else {
          fullRgb[di + 0] = static_cast<unsigned char>((x + 31) & 0xff);
          fullRgb[di + 1] = static_cast<unsigned char>((y + 67) & 0xff);
          fullRgb[di + 2] = static_cast<unsigned char>(((x ^ y) + 13) & 0xff);
        }
      }
    }
    for (int b = 0; b < 3; ++b) {
      std::vector<unsigned char> band(static_cast<size_t>(rasterW) * rasterH);
      for (int i = 0; i < rasterW * rasterH; ++i) {
        band[static_cast<size_t>(i)] = fullRgb[static_cast<size_t>(i) * 3 + b];
      }
      GDALRasterBandH rb = GDALGetRasterBand(ds, b + 1);
      if (!rb || GDALRasterIO(rb, GF_Write, 0, 0, rasterW, rasterH, band.data(), rasterW, rasterH, GDT_Byte, 0, 0) != CE_None) {
        GDALClose(ds);
        return 7;
      }
    }
    GDALClose(ds);
    return 0;
#else
    std::wcerr << L"[ERROR] mbtiles/gpkg output requires GDAL build.\n";
    return 7;
#endif
  }
  if (subtype == L"3dtiles") {
    const std::filesystem::path b3dmPath = outDir / L"root.b3dm";
    double regionZMin = -100.0;
    double regionZMax = 15000.0;
#if GIS_DESKTOP_HAVE_GDAL
    const bool useDem = haveRaster && raster.useElevAsHeight;
    const int rwGrid = haveRaster ? raster.w : 48;
    const int rhGrid = haveRaster ? raster.h : 48;
    constexpr int kMaxSide = 160;
    const int maxWH = (std::max)(rwGrid, rhGrid);
    const double gridScale = (maxWH > kMaxSide) ? static_cast<double>(kMaxSide) / static_cast<double>(maxWH) : 1.0;
    const int nx3d = (std::max)(2, static_cast<int>(std::lround(rwGrid * gridScale)));
    const int ny3d = (std::max)(2, static_cast<int>(std::lround(rhGrid * gridScale)));
    double demHMin = 0.0;
    double demHMax = 0.0;
    std::vector<unsigned char> glb3d =
        BuildTerrainGlbEcefRtc(raster, haveRaster, useDem, bLonMin, bLonMax, bLatMin, bLatMax, nx3d, ny3d, &demHMin, &demHMax);
    if (glb3d.empty()) {
      glb3d = BuildMinimalGlbV2();
      demHMin = 0.0;
      demHMax = 0.0;
    }
    std::wcout << L"[TILE] 3dtiles: mesh " << nx3d << L"x" << ny3d << (useDem ? L" (DEM heights)\n" : L" (WGS84 ellipsoid, z=0)\n");
    const std::vector<unsigned char> b3dm = WrapGlbAsB3dm(glb3d);
    regionZMin = demHMin - 80.0;
    regionZMax = demHMax + 220.0;
    if (!(regionZMax > regionZMin + 5.0) || !std::isfinite(regionZMin) || !std::isfinite(regionZMax)) {
      regionZMin = -100.0;
      regionZMax = 1200.0;
    }
#else
    const std::vector<unsigned char> b3dm = BuildMinimalB3dmWithGlb();
#endif
    rc = WriteBinaryFile(b3dmPath, b3dm.data(), b3dm.size());
    if (rc != 0) {
      std::wcerr << L"[ERROR] 3dtiles b3dm write failed: " << b3dmPath.wstring() << L", rc=" << rc << L"\n";
      return rc;
    }
    const double degToRad = 3.14159265358979323846 / 180.0;
    const double west = bLonMin * degToRad;
    const double south = bLatMin * degToRad;
    const double east = bLonMax * degToRad;
    const double north = bLatMax * degToRad;
    std::wostringstream ts;
    ts << std::setprecision(12);
    ts << L"{\n"
       << L"  \"asset\": {\"version\": \"1.0\", \"generator\": \"AGIS\"},\n"
       << L"  \"geometricError\": 500,\n"
       << L"  \"root\": {\n"
       << L"    \"boundingVolume\": {\n"
       << L"      \"region\": [" << west << L", " << south << L", " << east << L", " << north << L", " << regionZMin << L", "
       << regionZMax << L"]\n"
       << L"    },\n"
       << L"    \"geometricError\": 0,\n"
       << L"    \"refine\": \"ADD\",\n"
       << L"    \"content\": {\"uri\": \"root.b3dm\"}\n"
       << L"  }\n"
       << L"}\n";
    rc = WriteTextFile(outDir / L"tileset.json", ts.str());
    if (rc != 0) {
      return rc;
    }
    rc = WriteTextFile(outDir / L"README_3DTILES.txt",
                       L"AGIS 3D Tiles output\n"
                       L"files:\n"
                       L"  tileset.json\n"
                       L"  root.b3dm\n"
                       L"note:\n"
                       L"  root.b3dm embeds glTF 2.0: regular lon/lat grid, heights from DEM when input uses 1–2 bands;\n"
                       L"  otherwise geometry on WGS84 ellipsoid (h=0). Vertex colors sample RGB when available.\n"
                       L"  Model space: RTC meters in local ENU at tile center. region[4],region[5] are ellipsoid heights (m).\n");
    if (rc != 0) {
      return rc;
    }
    return 0;
  }
  for (int z = kMinZ; z <= kMaxZ; ++z) {
    const int dim = 1 << z;
    for (int x = 0; x < dim; ++x) {
      rc = EnsureDir(outDir / std::to_wstring(z) / std::to_wstring(x));
      if (rc != 0) {
        return rc;
      }
      for (int yXyz = 0; yXyz < dim; ++yXyz) {
        const int yOut = (subtype == L"tms") ? (dim - 1 - yXyz) : yXyz;
        buildTileRgb(z, x, yXyz);
        const std::filesystem::path tilePath = outDir / std::to_wstring(z) / std::to_wstring(x) / (std::to_wstring(yOut) + ext);
        rc = WriteRgbTextureFile(tilePath, kTileSize, kTileSize, tileRgb, texFmt);
        if (rc != 0) {
          std::wcerr << L"[ERROR] tile write failed: " << tilePath.wstring() << L", rc=" << rc << L"\n";
          return rc;
        }
      }
    }
  }
  const std::wstring readme = L"AGIS tile pyramid output\n"
                              L"source=" +
                              args.input + L"\n"
                                           L"protocol=" +
                              subtype + L"\n"
                                        L"layout=/{z}/{x}/{y}" +
                              ext +
                              L"\n"
                              L"tile_size=256\n"
                                        L"note=tms uses flipped y from xyz: y_tms=(2^z-1-y_xyz)\n";
  rc = WriteTextFile(outDir / L"README.txt", readme);
  if (rc != 0) {
    return rc;
  }
  if (subtype == L"wmts") {
    const std::wstring formatMime = (ext == L".tif" ? L"image/tiff"
                                  : (ext == L".tga" ? L"image/tga"
                                  : (ext == L".bmp" ? L"image/bmp" : L"image/png")));
    std::wostringstream wmts;
    wmts << L"<?xml version=\"1.0\" encoding=\"UTF-8\"?>\n"
         << L"<Capabilities xmlns=\"http://www.opengis.net/wmts/1.0\"\n"
         << L"              xmlns:ows=\"http://www.opengis.net/ows/1.1\"\n"
         << L"              xmlns:xlink=\"http://www.w3.org/1999/xlink\"\n"
         << L"              version=\"1.0.0\">\n"
         << L"  <ows:ServiceIdentification>\n"
         << L"    <ows:Title>AGIS WMTS</ows:Title>\n"
         << L"    <ows:Abstract>AGIS generated WMTS tile pyramid</ows:Abstract>\n"
         << L"    <ows:ServiceType>OGC WMTS</ows:ServiceType>\n"
         << L"    <ows:ServiceTypeVersion>1.0.0</ows:ServiceTypeVersion>\n"
         << L"  </ows:ServiceIdentification>\n"
         << L"  <Contents>\n"
         << L"    <Layer>\n"
         << L"      <ows:Identifier>agis</ows:Identifier>\n"
         << L"      <ows:Title>AGIS Tiles</ows:Title>\n"
         << L"      <Format>" << formatMime << L"</Format>\n"
         << L"      <TileMatrixSetLink><TileMatrixSet>agis-global-3857</TileMatrixSet></TileMatrixSetLink>\n"
         << L"      <ResourceURL format=\"" << formatMime << L"\" resourceType=\"tile\" template=\"./{TileMatrix}/{TileCol}/{TileRow}"
         << ext << L"\"/>\n"
         << L"    </Layer>\n"
         << L"    <TileMatrixSet>\n"
         << L"      <ows:Identifier>agis-global-3857</ows:Identifier>\n"
         << L"      <ows:SupportedCRS>urn:ogc:def:crs:EPSG::3857</ows:SupportedCRS>\n";
    for (int z = kMinZ; z <= kMaxZ; ++z) {
      const int dim = 1 << z;
      const double pixelSpan = 0.00028;
      const double scaleDen = (156543.03392804097 / static_cast<double>(dim)) / pixelSpan;
      wmts << L"      <TileMatrix>\n"
           << L"        <ows:Identifier>" << z << L"</ows:Identifier>\n"
           << L"        <ScaleDenominator>" << scaleDen << L"</ScaleDenominator>\n"
           << L"        <TopLeftCorner>-20037508.3427892 20037508.3427892</TopLeftCorner>\n"
           << L"        <TileWidth>256</TileWidth>\n"
           << L"        <TileHeight>256</TileHeight>\n"
           << L"        <MatrixWidth>" << dim << L"</MatrixWidth>\n"
           << L"        <MatrixHeight>" << dim << L"</MatrixHeight>\n"
           << L"      </TileMatrix>\n";
    }
    wmts << L"    </TileMatrixSet>\n"
         << L"  </Contents>\n"
         << L"</Capabilities>\n";
    rc = WriteTextFile(outDir / L"wmts_capabilities.xml", wmts.str());
    if (rc != 0) {
      return rc;
    }
  }
  if (subtype == L"xyz") {
    const std::wstring extNoDot = ext.empty() ? L"png" : ext.substr(1);
    std::wstring format = extNoDot;
    if (format == L"tif") {
      format = L"tiff";
    }
    std::wostringstream tj;
    tj << L"{\n"
       << L"  \"tilejson\": \"2.2.0\",\n"
       << L"  \"name\": \"AGIS XYZ Tiles\",\n"
       << L"  \"description\": \"AGIS generated XYZ tile pyramid\",\n"
       << L"  \"scheme\": \"xyz\",\n"
       << L"  \"format\": \"" << format << L"\",\n"
       << L"  \"minzoom\": " << kMinZ << L",\n"
       << L"  \"maxzoom\": " << kMaxZ << L",\n"
       << L"  \"bounds\": [" << bLonMin << L", " << bLatMin << L", " << bLonMax << L", " << bLatMax << L"],\n"
       << L"  \"tiles\": [\"./{z}/{x}/{y}" << ext << L"\"]\n"
       << L"}\n";
    rc = WriteTextFile(outDir / L"tilejson.json", tj.str());
    if (rc != 0) {
      return rc;
    }
  }
  if (subtype == L"tms") {
    const std::wstring profile = L"global-mercator";
    const std::wstring extNoDot = ext.empty() ? L"png" : ext.substr(1);
    std::wostringstream xml;
    xml << L"<?xml version=\"1.0\" encoding=\"UTF-8\"?>\n"
        << L"<TileMap version=\"1.0.0\" tilemapservice=\"http://tms.osgeo.org/1.0.0\">\n"
        << L"  <Title>AGIS TMS TileMap</Title>\n"
        << L"  <Abstract>AGIS generated TMS pyramid</Abstract>\n"
        << L"  <SRS>EPSG:3857</SRS>\n"
        << L"  <BoundingBox minx=\"" << bMxMin << L"\" miny=\"" << bMyMin << L"\" maxx=\"" << bMxMax << L"\" maxy=\"" << bMyMax
        << L"\"/>\n"
        << L"  <Origin x=\"" << bMxMin << L"\" y=\"" << bMyMin << L"\"/>\n"
        << L"  <TileFormat width=\"256\" height=\"256\" mime-type=\"image/"
        << (extNoDot == L"tif" ? L"tiff" : extNoDot) << L"\" extension=\"" << extNoDot << L"\"/>\n"
        << L"  <TileSets profile=\"" << profile << L"\">\n";
    for (int z = kMinZ; z <= kMaxZ; ++z) {
      const double unitsPerPixel = 156543.03392804097 / static_cast<double>(1 << z);
      xml << L"    <TileSet href=\"" << z << L"\" units-per-pixel=\"" << unitsPerPixel << L"\" order=\"" << z << L"\"/>\n";
    }
    xml << L"  </TileSets>\n"
        << L"</TileMap>\n";
    rc = WriteTextFile(outDir / L"tms.xml", xml.str());
    if (rc != 0) {
      return rc;
    }
  }
  return 0;
}

std::wstring ReplaceBackslashWithSlash(std::wstring s) {
  for (auto& ch : s) {
    if (ch == L'\\') ch = L'/';
  }
  return s;
}

std::wstring ObjPrecisionFormat(const ConvertArgs& args) {
  return (args.obj_fp_type == L"float") ? L"%.7g" : L"%.15g";
}

int ConvertModelToGisImpl(const ConvertArgs& args) {
  ObjData obj;
  if (!ParseObjFile(args.input, &obj)) {
    std::wcerr << L"[ERROR] model->gis currently expects a valid OBJ mesh input.\n";
    return 7;
  }
  if (obj.v.empty()) {
    return 7;
  }
  double minx = obj.v[0][0], maxx = obj.v[0][0];
  double miny = obj.v[0][1], maxy = obj.v[0][1];
  double minz = obj.v[0][2], maxz = obj.v[0][2];
  for (const auto& p : obj.v) {
    minx = (std::min)(minx, p[0]);
    maxx = (std::max)(maxx, p[0]);
    miny = (std::min)(miny, p[1]);
    maxy = (std::max)(maxy, p[1]);
    minz = (std::min)(minz, p[2]);
    maxz = (std::max)(maxz, p[2]);
  }
  const double cx = (minx + maxx) * 0.5;
  const double cy = (miny + maxy) * 0.5;
  const double elev = (minz + maxz) * 0.5;
  std::wostringstream gj;
  gj << L"{\n"
     << L"  \"type\": \"FeatureCollection\",\n"
     << L"  \"name\": \"agis_model_to_gis\",\n"
     << L"  \"crs\": {\"type\": \"name\", \"properties\": {\"name\": \""
     << (args.target_crs.empty() ? L"EPSG:4326" : args.target_crs) << L"\"}},\n"
     << L"  \"features\": [\n"
     << L"    {\n"
     << L"      \"type\": \"Feature\",\n"
     << L"      \"properties\": {\n"
     << L"        \"source\": \"" << ReplaceBackslashWithSlash(args.input) << L"\",\n"
     << L"        \"vertex_count\": " << obj.v.size() << L",\n"
     << L"        \"triangle_count\": " << obj.tris.size() << L",\n"
     << L"        \"z_min\": " << minz << L",\n"
     << L"        \"z_max\": " << maxz << L",\n"
     << L"        \"z_center\": " << elev << L"\n"
     << L"      },\n"
     << L"      \"geometry\": {\n"
     << L"        \"type\": \"Polygon\",\n"
     << L"        \"coordinates\": [[["
     << minx << L"," << miny << L"],[" << maxx << L"," << miny << L"],[" << maxx << L"," << maxy << L"],[" << minx
     << L"," << maxy << L"],[" << minx << L"," << miny << L"]]]\n"
     << L"      }\n"
     << L"    },\n"
     << L"    {\n"
     << L"      \"type\": \"Feature\",\n"
     << L"      \"properties\": {\"kind\": \"model_centroid\", \"z\": " << elev << L"},\n"
     << L"      \"geometry\": {\"type\": \"Point\", \"coordinates\": [" << cx << L"," << cy << L"]}\n"
     << L"    }\n"
     << L"  ]\n"
     << L"}\n";
  std::filesystem::path outPath(args.output);
  if (outPath.extension().empty()) {
    outPath.replace_extension(L".geojson");
  }
  return WriteTextFile(outPath, gj.str());
}

int ConvertModelToTileImpl(const ConvertArgs& args) {
  const std::filesystem::path inPath(args.input);
  const bool maybeObj = _wcsicmp(inPath.extension().wstring().c_str(), L".obj") == 0;
  if (!maybeObj) {
    return ConvertGisToTileImpl(args);
  }
  const std::filesystem::path tempDir = std::filesystem::temp_directory_path() / L"agis_model_to_tile";
  std::error_code ec;
  std::filesystem::create_directories(tempDir, ec);
  const std::filesystem::path tempGeojson = tempDir / L"model_to_gis.geojson";
  ConvertArgs gArgs = args;
  gArgs.output = tempGeojson.wstring();
  const int rc = ConvertModelToGisImpl(gArgs);
  if (rc != 0) {
    return rc;
  }
  ConvertArgs tArgs = args;
  tArgs.input = tempGeojson.wstring();
  return ConvertGisToTileImpl(tArgs);
}

struct TileImageInfo {
  std::filesystem::path path;
  int z = 0;
  int x = 0;
  int y = 0;
};

bool ParseXyzTilePath(const std::filesystem::path& p, TileImageInfo* out) {
  if (!out || p.extension().empty()) return false;
  const auto yName = p.stem().wstring();
  const auto xName = p.parent_path().filename().wstring();
  const auto zName = p.parent_path().parent_path().filename().wstring();
  if (zName.empty() || xName.empty() || yName.empty()) return false;
  wchar_t* e1 = nullptr;
  wchar_t* e2 = nullptr;
  wchar_t* e3 = nullptr;
  long z = wcstol(zName.c_str(), &e1, 10);
  long x = wcstol(xName.c_str(), &e2, 10);
  long y = wcstol(yName.c_str(), &e3, 10);
  if (!e1 || !e2 || !e3 || *e1 || *e2 || *e3) return false;
  out->path = p;
  out->z = static_cast<int>(z);
  out->x = static_cast<int>(x);
  out->y = static_cast<int>(y);
  return true;
}

int ConvertTileToGisImpl(const ConvertArgs& args) {
  std::vector<TileImageInfo> tiles;
  const std::filesystem::path inPath(args.input);
  if (std::filesystem::is_regular_file(inPath)) {
    TileImageInfo t;
    if (ParseXyzTilePath(inPath, &t)) {
      tiles.push_back(t);
    }
  } else {
    for (const auto& e : std::filesystem::recursive_directory_iterator(inPath)) {
      if (!e.is_regular_file()) continue;
      TileImageInfo t;
      if (ParseXyzTilePath(e.path(), &t)) {
        tiles.push_back(t);
      }
    }
  }
  if (tiles.empty()) {
    std::wcerr << L"[ERROR] no xyz-like tile images found under input.\n";
    return 7;
  }
  int maxZ = tiles.front().z;
  for (const auto& t : tiles) maxZ = (std::max)(maxZ, t.z);
  std::vector<TileImageInfo> level;
  for (const auto& t : tiles) {
    if (t.z == maxZ) level.push_back(t);
  }
  int minX = level.front().x, maxX = level.front().x;
  int minY = level.front().y, maxY = level.front().y;
  for (const auto& t : level) {
    minX = (std::min)(minX, t.x);
    maxX = (std::max)(maxX, t.x);
    minY = (std::min)(minY, t.y);
    maxY = (std::max)(maxY, t.y);
  }
  RasterExtract sample;
  if (!TryReadRaster(level.front().path.wstring(), &sample, 4096) || sample.w < 2 || sample.h < 2) {
    std::wcerr << L"[ERROR] unable to read tile image pixels.\n";
    return 7;
  }
  const int tw = sample.w;
  const int th = sample.h;
  const int gridW = (maxX - minX + 1) * tw;
  const int gridH = (maxY - minY + 1) * th;
  std::vector<unsigned char> mosaic(static_cast<size_t>(gridW) * gridH * 3, 0);
  for (const auto& t : level) {
    RasterExtract r;
    if (!TryReadRaster(t.path.wstring(), &r, 4096) || r.w <= 0 || r.h <= 0) continue;
    for (int y = 0; y < th; ++y) {
      for (int x = 0; x < tw; ++x) {
        const int dx = (t.x - minX) * tw + x;
        const int dy = (t.y - minY) * th + y;
        if (dx < 0 || dy < 0 || dx >= gridW || dy >= gridH) continue;
        const size_t si = (static_cast<size_t>(y) * r.w + x) * 3;
        const size_t di = (static_cast<size_t>(dy) * gridW + dx) * 3;
        mosaic[di + 0] = r.rgb[si + 0];
        mosaic[di + 1] = r.rgb[si + 1];
        mosaic[di + 2] = r.rgb[si + 2];
      }
    }
  }
  std::filesystem::path outPath(args.output);
  if (outPath.extension().empty()) {
    outPath.replace_extension(L".tif");
  }
  return WriteRgbTextureFile(outPath, gridW, gridH, mosaic, L"tif");
}

int ConvertTileToModelImpl(const ConvertArgs& args) {
  std::vector<TileImageInfo> tiles;
  const std::filesystem::path inPath(args.input);
  if (std::filesystem::is_regular_file(inPath)) {
    TileImageInfo t;
    if (ParseXyzTilePath(inPath, &t)) tiles.push_back(t);
  } else {
    for (const auto& e : std::filesystem::recursive_directory_iterator(inPath)) {
      if (!e.is_regular_file()) continue;
      TileImageInfo t;
      if (ParseXyzTilePath(e.path(), &t)) tiles.push_back(t);
    }
  }
  if (tiles.empty()) {
    std::filesystem::path outPath(args.output);
    std::error_code ec;
    if (std::filesystem::is_directory(outPath, ec) || outPath.extension().empty()) {
      std::filesystem::create_directories(outPath, ec);
      outPath /= L"tile_model_000.obj";
    } else {
      AgisCoerceModelOutputPathFromLasLazToObj(&outPath, L"瓦片→模型");
    }
    const std::wstring msg = std::wstring(kObjFileFormatBanner30) + L"o tile_model\nv 0 0 0\nv 1 0 0\nv 0 1 0\nf 1 2 3\n";
    return WriteTextFile(outPath, msg);
  }
  int maxZ = tiles.front().z;
  for (const auto& t : tiles) maxZ = (std::max)(maxZ, t.z);
  std::vector<TileImageInfo> level;
  for (const auto& t : tiles) if (t.z == maxZ) level.push_back(t);
  const size_t tileCount = level.size();
  const size_t bytesPerTile = static_cast<size_t>(4 * 3) * sizeof(double) + static_cast<size_t>(2 * 4) * sizeof(double);
  const size_t memLimitBytes = static_cast<size_t>((std::max)(64, args.tile_max_memory_mb)) * 1024ull * 1024ull;
  size_t tilesPerBatch = memLimitBytes / (std::max)(size_t(1), bytesPerTile);
  tilesPerBatch = (std::max)(size_t(1), tilesPerBatch);

  std::filesystem::path outBase(args.output);
  std::error_code ec;
  bool outputIsDir = std::filesystem::is_directory(outBase, ec) || outBase.extension().empty();
  if (!outputIsDir) {
    AgisCoerceModelOutputPathFromLasLazToObj(&outBase, L"瓦片→模型");
  }
  if (outputIsDir) {
    std::filesystem::create_directories(outBase, ec);
  } else {
    std::filesystem::create_directories(outBase.parent_path(), ec);
  }
  const std::wstring fpFmt = ObjPrecisionFormat(args);
  size_t batchIdx = 0;
  for (size_t b = 0; b < tileCount; b += tilesPerBatch, ++batchIdx) {
    const size_t e = (std::min)(tileCount, b + tilesPerBatch);
    std::filesystem::path outObj;
    if (outputIsDir) {
      wchar_t name[64]{};
      swprintf_s(name, L"tile_model_%03zu.obj", batchIdx);
      outObj = outBase / name;
    } else {
      if (batchIdx == 0 && e == tileCount) {
        outObj = outBase;
      } else {
        wchar_t name[64]{};
        swprintf_s(name, L"%ls_%03zu%ls", outBase.stem().wstring().c_str(), batchIdx, outBase.extension().wstring().c_str());
        outObj = outBase.parent_path() / name;
      }
    }
    std::wstring obj = kObjFileFormatBanner30;
    obj += L"# source tile set: " + args.input + L"\n";
    obj += L"o tile_batch_" + std::to_wstring(batchIdx) + L"\n";
    int vBase = 1;
    for (size_t i = b; i < e; ++i) {
      const auto& t = level[i];
      const double s = 1.0 / static_cast<double>(1 << t.z);
      const double x0 = t.x * s;
      const double y0 = t.y * s;
      const double x1 = (t.x + 1) * s;
      const double y1 = (t.y + 1) * s;
      wchar_t line[256]{};
      swprintf_s(line, (L"v " + fpFmt + L" " + fpFmt + L" " + fpFmt + L"\n").c_str(), x0, y0, 0.0);
      obj += line;
      swprintf_s(line, (L"v " + fpFmt + L" " + fpFmt + L" " + fpFmt + L"\n").c_str(), x1, y0, 0.0);
      obj += line;
      swprintf_s(line, (L"v " + fpFmt + L" " + fpFmt + L" " + fpFmt + L"\n").c_str(), x1, y1, 0.0);
      obj += line;
      swprintf_s(line, (L"v " + fpFmt + L" " + fpFmt + L" " + fpFmt + L"\n").c_str(), x0, y1, 0.0);
      obj += line;
      obj += L"vt 0 0\nvt 1 0\nvt 1 1\nvt 0 1\n";
      wchar_t f[128]{};
      swprintf_s(f, L"f %d/%d %d/%d %d/%d\n", vBase, vBase, vBase + 1, vBase + 1, vBase + 2, vBase + 2);
      obj += f;
      swprintf_s(f, L"f %d/%d %d/%d %d/%d\n", vBase, vBase, vBase + 2, vBase + 2, vBase + 3, vBase + 3);
      obj += f;
      vBase += 4;
    }
    const int wr = WriteTextFile(outObj, obj);
    if (wr != 0) return wr;
    std::wcout << L"[OUT] model batch: " << outObj.wstring() << L" tiles=" << (e - b) << L"\n";
  }
  return 0;
}

}  // namespace

int ConvertGisToModel(const ConvertArgs& args) { return ConvertGisToModelImpl(args); }
int ConvertGisToTile(const ConvertArgs& args) { return ConvertGisToTileImpl(args); }
int ConvertModelToGis(const ConvertArgs& args) { return ConvertModelToGisImpl(args); }
int ConvertModelToModel(const ConvertArgs& args) { return ConvertModelToModelImpl(args); }
int ConvertModelToTile(const ConvertArgs& args) { return ConvertModelToTileImpl(args); }
int ConvertTileToGis(const ConvertArgs& args) { return ConvertTileToGisImpl(args); }
int ConvertTileToModel(const ConvertArgs& args) { return ConvertTileToModelImpl(args); }
