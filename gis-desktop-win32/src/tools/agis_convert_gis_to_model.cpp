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
  const bool zh = IsChineseOsUi();
  if (zh) {
    std::wcout << L"用法:\n"
               << L"  agis_convert_gis_to_model --input <path> --output <path> [options]\n\n"
               << L"必填参数:\n"
               << L"  --input <path>               输入 GIS 文件或目录\n"
               << L"  --output <path>              输出模型文件（如 .obj）\n\n"
               << L"通用选项:\n"
               << L"  --help, -h, /?               显示帮助\n";
    PrintConvertCliHelpGrouped(
        std::wcout, true,
        L"  本工具为 GIS→模型；主类型一般为 gis，可省略或由实现推断。\n",
        L"  auto | vector | raster | gpkg（默认 auto，与 GUI 一致）。\n",
        L"  输出为模型；主类型一般为 model。\n",
        L"  tin | dem | 3dmesh | pointcloud（默认 tin）。\n");
    std::wcout << L"【其它选项】\n"
               << L"  --coord-system <projected|cecf>\n"
               << L"  --vector-mode <geometry|bake_texture>\n"
               << L"  --elev-horiz-ratio <num>\n"
               << L"  --target-crs <EPSG:xxxx|WKT>\n"
               << L"  --output-unit <m|km|1000km>\n"
               << L"  --mesh-spacing <1..1000000>\n"
               << L"  --texture-format <png|tif|tga|bmp>\n"
               << L"  --raster-max-dim <0|64..16384>  0=源图全分辨率（默认）；或长边上限\n"
               << L"  --obj-fp-type <double|float>\n\n"
               << L"示例:\n"
               << L"  agis_convert_gis_to_model --input demo.gis --output demo.obj --output-subtype 3dmesh\n";
  } else {
    std::wcout << L"Usage:\n"
               << L"  agis_convert_gis_to_model --input <path> --output <path> [options]\n\n"
               << L"Required:\n"
               << L"  --input <path>               Input GIS file or directory\n"
               << L"  --output <path>              Output model file (e.g. .obj)\n\n"
               << L"General:\n"
               << L"  --help, -h, /?               Show help\n";
    PrintConvertCliHelpGrouped(
        std::wcout, false,
        L"  GIS → model tool; major type is usually gis (optional if implied).\n",
        L"  auto | vector | raster | gpkg (default: auto).\n",
        L"  Output is model; major type is usually model.\n",
        L"  tin | dem | 3dmesh | pointcloud (default: tin).\n");
    std::wcout << L"Other options:\n"
               << L"  --coord-system <projected|cecf>\n"
               << L"  --vector-mode <geometry|bake_texture>\n"
               << L"  --elev-horiz-ratio <num>\n"
               << L"  --target-crs <EPSG:xxxx|WKT>\n"
               << L"  --output-unit <m|km|1000km>\n"
               << L"  --mesh-spacing <1..1000000>\n"
               << L"  --texture-format <png|tif|tga|bmp>\n"
               << L"  --raster-max-dim <0|64..16384>  0=native (default); or long-side cap\n"
               << L"  --obj-fp-type <double|float>\n\n"
               << L"Example:\n"
               << L"  agis_convert_gis_to_model --input demo.gis --output demo.obj --output-subtype 3dmesh\n";
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
  EnableRealtimeConsoleFlush();
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
