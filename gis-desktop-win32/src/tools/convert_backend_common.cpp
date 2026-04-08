#include "tools/convert_backend_common.h"

#include <chrono>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <sstream>
#include <thread>
#include <vector>
#include <cmath>
#include <string>
#include <windows.h>

namespace {

std::wstring ArgValue(int argc, wchar_t** argv, const wchar_t* name) {
  for (int i = 1; i + 1 < argc; ++i) {
    if (_wcsicmp(argv[i], name) == 0) {
      return argv[i + 1];
    }
  }
  return L"";
}

}  // namespace

bool ParseConvertArgs(int argc, wchar_t** argv, ConvertArgs* out) {
  if (!out) {
    return false;
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
  return !out->input.empty() && !out->output.empty();
}

void PrintConvertBanner(const wchar_t* title, const ConvertArgs& args) {
  std::wcout << L"[AGIS-CONVERT] " << (title ? title : L"convert") << L"\n";
  std::wcout << L"  input:  " << args.input << L"\n";
  std::wcout << L"  output: " << args.output << L"\n";
  std::wcout << L"  inType: " << args.input_type << L" / " << args.input_subtype << L"\n";
  std::wcout << L"  outType:" << args.output_type << L" / " << args.output_subtype << L"\n";
  std::wcout << L"  coord:  " << args.coord_system << L"\n";
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

int ConvertGisToModel(const ConvertArgs& args) {
  std::filesystem::path outPath(args.output);
  std::error_code ec;
  if (std::filesystem::is_directory(outPath, ec) || outPath.extension().empty()) {
    std::filesystem::create_directories(outPath, ec);
    outPath /= L"model.obj";
  }
  const std::wstring mtlName = outPath.stem().wstring() + L".mtl";
  std::wcout << L"[OUT] model file: " << outPath.wstring() << L"\n";
  std::wcout << L"[OUT] material file: " << (outPath.parent_path() / mtlName).wstring() << L"\n";
  // 优先从 .gis 文档提取视域信息，让 OBJ 空间范围与 GIS 工程一致。
  GisDocInfo doc;
  if (outPath.has_extension()) {
    const std::wstring inExt = std::filesystem::path(args.input).extension().wstring();
    if (_wcsicmp(inExt.c_str(), L".gis") == 0) {
      doc = ParseGisDocInfo(std::filesystem::path(args.input));
    }
  }
  constexpr int kGrid = 24;  // 24x24 网格 => 1152 三角形
  const double minx = doc.ok ? doc.minx : -1.0;
  const double miny = doc.ok ? doc.miny : -1.0;
  const double maxx = doc.ok ? doc.maxx : 1.0;
  const double maxy = doc.ok ? doc.maxy : 1.0;
  const double spanX = (std::max)(1e-6, maxx - minx);
  const double spanY = (std::max)(1e-6, maxy - miny);
  const double layerAmp = 0.02 * static_cast<double>((std::max)(1, doc.layerCount));
  std::wstringstream ss;
  ss << L"# AGIS mock model output\n"
     << L"# from GIS input: " << args.input << L"\n"
     << L"# subtype: " << args.input_subtype << L" -> " << args.output_subtype << L"\n"
     << L"# extent: [" << minx << L"," << miny << L"] - [" << maxx << L"," << maxy << L"]\n"
     << L"# layers: " << doc.layerCount << L"\n"
     << L"# coord_system: " << args.coord_system << L"\n"
     << L"mtllib " << mtlName << L"\n"
     << L"o agis_model\n";
  for (int y = 0; y <= kGrid; ++y) {
    for (int x = 0; x <= kGrid; ++x) {
      const float fx = static_cast<float>(x) / static_cast<float>(kGrid);
      const float fy = static_cast<float>(y) / static_cast<float>(kGrid);
      double px = minx + spanX * static_cast<double>(fx);
      double py = miny + spanY * static_cast<double>(fy);
      const double pz = layerAmp * std::sin(static_cast<double>(fx) * 6.2831853) *
                        std::cos(static_cast<double>(fy) * 6.2831853);
      if (_wcsicmp(args.coord_system.c_str(), L"cecf") == 0) {
        const double lon = px * 3.14159265358979323846 / 180.0;
        const double lat = py * 3.14159265358979323846 / 180.0;
        const double R = 6378137.0 + pz * 1000.0;
        const double xEcef = R * std::cos(lat) * std::cos(lon);
        const double yEcef = R * std::cos(lat) * std::sin(lon);
        const double zEcef = R * std::sin(lat);
        ss << L"v " << xEcef << L" " << yEcef << L" " << zEcef << L"\n";
      } else {
        ss << L"v " << px << L" " << py << L" " << pz << L"\n";
      }
      ss << L"vt " << fx << L" " << (1.0f - fy) << L"\n";
    }
  }
  ss << L"vn 0 0 1\n";
  ss << L"usemtl defaultMat\n";
  auto vid = [kGrid](int x, int y) { return y * (kGrid + 1) + x + 1; };
  for (int y = 0; y < kGrid; ++y) {
    for (int x = 0; x < kGrid; ++x) {
      const int v00 = vid(x, y);
      const int v10 = vid(x + 1, y);
      const int v01 = vid(x, y + 1);
      const int v11 = vid(x + 1, y + 1);
      ss << L"f " << v00 << L"/" << v00 << L"/1 " << v10 << L"/" << v10 << L"/1 " << v11 << L"/" << v11 << L"/1\n";
      ss << L"f " << v00 << L"/" << v00 << L"/1 " << v11 << L"/" << v11 << L"/1 " << v01 << L"/" << v01 << L"/1\n";
    }
  }
  int rc = WriteTextFile(outPath, ss.str());
  if (rc != 0) {
    return rc;
  }
  const std::wstring texName = outPath.stem().wstring() + L"_albedo.png";
  const std::filesystem::path texPath = outPath.parent_path() / texName;
  static const unsigned char kPng1x1Blue[] = {
      0x89, 0x50, 0x4E, 0x47, 0x0D, 0x0A, 0x1A, 0x0A, 0x00, 0x00, 0x00, 0x0D, 0x49, 0x48, 0x44, 0x52,
      0x00, 0x00, 0x00, 0x01, 0x00, 0x00, 0x00, 0x01, 0x08, 0x02, 0x00, 0x00, 0x00, 0x90, 0x77, 0x53,
      0xDE, 0x00, 0x00, 0x00, 0x0C, 0x49, 0x44, 0x41, 0x54, 0x08, 0x99, 0x63, 0x68, 0x78, 0xD8, 0x00,
      0x00, 0x03, 0x32, 0x01, 0x6D, 0xD9, 0xD4, 0x03, 0x80, 0x00, 0x00, 0x00, 0x00, 0x49, 0x45, 0x4E,
      0x44, 0xAE, 0x42, 0x60, 0x82};
  int trc = WriteBinaryFile(texPath, kPng1x1Blue, sizeof(kPng1x1Blue));
  if (trc != 0) {
    return trc;
  }
  return WriteTextFile(outPath.parent_path() / mtlName,
                       L"newmtl defaultMat\nKd 0.8 0.8 0.8\nKa 0.1 0.1 0.1\nKs 0.2 0.2 0.2\nmap_Kd " + texName + L"\n");
}

int ConvertGisToTile(const ConvertArgs& args) {
  const std::filesystem::path outDir(args.output);
  int rc = EnsureDir(outDir);
  if (rc != 0) return rc;
  rc = EnsureDir(outDir / L"0" / L"0");
  if (rc != 0) return rc;
  return WriteTextFile(outDir / L"0" / L"0" / L"0.tile.txt",
                       L"AGIS mock tile\nsource=" + args.input + L"\nsubtype=" + args.output_subtype + L"\n");
}

int ConvertModelToGis(const ConvertArgs& args) {
  const std::wstring geojson =
      L"{\n"
      L"  \"type\": \"FeatureCollection\",\n"
      L"  \"name\": \"agis_model_to_gis\",\n"
      L"  \"features\": [{\"type\":\"Feature\",\"properties\":{\"source\":\"" + args.input +
      L"\"},\"geometry\":{\"type\":\"Point\",\"coordinates\":[0.0,0.0]}}]\n"
      L"}\n";
  return WriteTextFile(args.output, geojson);
}

int ConvertModelToTile(const ConvertArgs& args) {
  return ConvertGisToTile(args);
}

int ConvertTileToGis(const ConvertArgs& args) {
  const std::wstring gpkgMock =
      L"AGIS mock GIS dataset\nfrom tile source: " + args.input + L"\noutput subtype: " + args.output_subtype + L"\n";
  return WriteTextFile(args.output, gpkgMock);
}

int ConvertTileToModel(const ConvertArgs& args) {
  std::filesystem::path outPath(args.output);
  std::error_code ec;
  if (std::filesystem::is_directory(outPath, ec) || outPath.extension().empty()) {
    std::filesystem::create_directories(outPath, ec);
    outPath /= L"tile_model.obj";
  }
  std::wcout << L"[OUT] model file: " << outPath.wstring() << L"\n";
  return WriteTextFile(outPath,
                       L"# AGIS mock model from tile source\n# source: " + args.input +
                           L"\nmtllib tile_model.mtl\no tile_model\nv 0 0 0\nv 1 0 0\nv 0 1 0\nf 1 2 3\n");
}

}  // namespace

int RunConversion(ConvertMode mode, const wchar_t* title, const ConvertArgs& args) {
  PrintConvertBanner(title, args);
  if (!std::filesystem::exists(args.input)) {
    std::wcerr << L"[ERROR] input not found: " << args.input << L"\n";
    return 2;
  }
  std::wcout << L"[1/3] validating input ...\n";
  std::this_thread::sleep_for(std::chrono::milliseconds(80));
  std::wcout << L"[2/3] converting ...\n";
  std::this_thread::sleep_for(std::chrono::milliseconds(120));
  int rc = 5;
  switch (mode) {
    case ConvertMode::kGisToModel:
      rc = ConvertGisToModel(args);
      break;
    case ConvertMode::kGisToTile:
      rc = ConvertGisToTile(args);
      break;
    case ConvertMode::kModelToGis:
      rc = ConvertModelToGis(args);
      break;
    case ConvertMode::kModelToTile:
      rc = ConvertModelToTile(args);
      break;
    case ConvertMode::kTileToGis:
      rc = ConvertTileToGis(args);
      break;
    case ConvertMode::kTileToModel:
      rc = ConvertTileToModel(args);
      break;
  }
  std::wcout << L"[3/3] write output ...\n";
  std::this_thread::sleep_for(std::chrono::milliseconds(60));
  if (rc == 0) {
    std::wcout << L"[DONE] success\n";
  } else {
    std::wcerr << L"[ERROR] conversion failed, code=" << rc << L"\n";
  }
  return rc;
}
