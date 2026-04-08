#include "tools/convert_backend_common.h"

#include <chrono>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <sstream>
#include <thread>
#include <vector>
#include <cmath>
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
  return !out->input.empty() && !out->output.empty();
}

void PrintConvertBanner(const wchar_t* title, const ConvertArgs& args) {
  std::wcout << L"[AGIS-CONVERT] " << (title ? title : L"convert") << L"\n";
  std::wcout << L"  input:  " << args.input << L"\n";
  std::wcout << L"  output: " << args.output << L"\n";
  std::wcout << L"  inType: " << args.input_type << L" / " << args.input_subtype << L"\n";
  std::wcout << L"  outType:" << args.output_type << L" / " << args.output_subtype << L"\n";
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

int EnsureDir(const std::filesystem::path& p) {
  std::error_code ec;
  std::filesystem::create_directories(p, ec);
  return ec ? 4 : 0;
}

int ConvertGisToModel(const ConvertArgs& args) {
  std::filesystem::path outPath(args.output);
  std::error_code ec;
  if (std::filesystem::is_directory(outPath, ec) || outPath.extension().empty()) {
    std::filesystem::create_directories(outPath, ec);
    outPath /= L"model.obj";
  }
  std::wcout << L"[OUT] model file: " << outPath.wstring() << L"\n";
  // 生成规则网格，避免只输出单三角形导致“看起来是空模型”。
  constexpr int kGrid = 24;  // 24x24 网格 => 1152 三角形
  std::wstringstream ss;
  ss << L"# AGIS mock model output\n"
     << L"# from GIS input: " << args.input << L"\n"
     << L"# subtype: " << args.input_subtype << L" -> " << args.output_subtype << L"\n"
     << L"mtllib model.mtl\n"
     << L"o agis_model\n";
  for (int y = 0; y <= kGrid; ++y) {
    for (int x = 0; x <= kGrid; ++x) {
      const float fx = static_cast<float>(x) / static_cast<float>(kGrid);
      const float fy = static_cast<float>(y) / static_cast<float>(kGrid);
      const float px = fx * 2.0f - 1.0f;
      const float py = fy * 2.0f - 1.0f;
      const float pz = 0.15f * std::sin(fx * 6.2831853f) * std::cos(fy * 6.2831853f);
      ss << L"v " << px << L" " << py << L" " << pz << L"\n";
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
  return WriteTextFile(outPath.parent_path() / L"model.mtl",
                       L"newmtl defaultMat\nKd 0.8 0.8 0.8\nKa 0.1 0.1 0.1\nKs 0.2 0.2 0.2\n");
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
