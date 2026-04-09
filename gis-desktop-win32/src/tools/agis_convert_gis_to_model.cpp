#include "tools/convert_backend_common.h"

#include <filesystem>
#include <iostream>
#include <thread>
#include <chrono>

namespace {

bool IsHelpRequestedLocal(int argc, wchar_t** argv) {
  for (int i = 1; i < argc; ++i) {
    if (_wcsicmp(argv[i], L"--help") == 0 || _wcsicmp(argv[i], L"-h") == 0 || _wcsicmp(argv[i], L"/?") == 0) {
      return true;
    }
  }
  return false;
}

void PrintHelp() {
  if (IsChineseOsUi()) {
    std::wcout << L"用法:\n"
               << L"  agis_convert_gis_to_model --input <path> --output <path> [options]\n\n"
               << L"必填参数:\n"
               << L"  --input <path>               输入 GIS 文件/目录\n"
               << L"  --output <path>              输出模型文件路径（例如 .obj）\n\n"
               << L"可选参数:\n"
               << L"  --help, -h, /?               显示帮助\n"
               << L"  --input-type <text>\n"
               << L"  --input-subtype <text>\n"
               << L"  --output-type <text>\n"
               << L"  --output-subtype <text>\n"
               << L"  --coord-system <projected|cecf>\n"
               << L"  --vector-mode <geometry|bake_texture>\n"
               << L"  --elev-horiz-ratio <num>\n"
               << L"  --target-crs <EPSG:xxxx|WKT>\n"
               << L"  --output-unit <m|km|1000km>\n"
               << L"  --mesh-spacing <1..1000000>\n"
               << L"  --texture-format <png|tif|tga|bmp>\n"
               << L"  --raster-max-dim <64..16384>\n\n"
               << L"示例:\n"
               << L"  agis_convert_gis_to_model --input demo.gis --output demo.obj --output-subtype obj\n";
  } else {
    std::wcout << L"Usage:\n"
               << L"  agis_convert_gis_to_model --input <path> --output <path> [options]\n\n"
               << L"Required:\n"
               << L"  --input <path>               Input GIS file/dir\n"
               << L"  --output <path>              Output model path (e.g. .obj)\n\n"
               << L"Options:\n"
               << L"  --help, -h, /?               Show help\n"
               << L"  --input-type <text>\n"
               << L"  --input-subtype <text>\n"
               << L"  --output-type <text>\n"
               << L"  --output-subtype <text>\n"
               << L"  --coord-system <projected|cecf>\n"
               << L"  --vector-mode <geometry|bake_texture>\n"
               << L"  --elev-horiz-ratio <num>\n"
               << L"  --target-crs <EPSG:xxxx|WKT>\n"
               << L"  --output-unit <m|km|1000km>\n"
               << L"  --mesh-spacing <1..1000000>\n"
               << L"  --texture-format <png|tif|tga|bmp>\n"
               << L"  --raster-max-dim <64..16384>\n\n"
               << L"Example:\n"
               << L"  agis_convert_gis_to_model --input demo.gis --output demo.obj --output-subtype obj\n";
  }
}

int RunDirect(const wchar_t* title, const ConvertArgs& args) {
  PrintConvertBanner(title, args);
  if (!std::filesystem::exists(args.input)) {
    std::wcerr << L"[ERROR] input not found: " << args.input << L"\n";
    return 2;
  }
  std::wcout << L"[1/3] validating input ...\n";
  std::this_thread::sleep_for(std::chrono::milliseconds(80));
  std::wcout << L"[2/3] converting ...\n";
  std::this_thread::sleep_for(std::chrono::milliseconds(120));
  const int rc = ConvertGisToModel(args);
  std::wcout << L"[3/3] write output ...\n";
  std::this_thread::sleep_for(std::chrono::milliseconds(60));
  if (rc == 0) {
    std::wcout << L"[DONE] success\n";
  } else {
    std::wcerr << L"[ERROR] conversion failed, code=" << rc << L"\n";
  }
  return rc;
}

}  // namespace

int wmain(int argc, wchar_t** argv) {
  if (IsHelpRequestedLocal(argc, argv)) {
    PrintHelp();
    return 0;
  }
  ConvertArgs args;
  if (!ParseConvertArgs(argc, argv, &args)) {
    PrintHelp();
    return 1;
  }
  return RunDirect(L"GIS -> MODEL", args);
}
