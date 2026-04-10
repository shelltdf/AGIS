#include "tools/convert_backend_common.h"

#include <filesystem>
#include <iostream>
#include <thread>
#include <chrono>

namespace {
bool IsHelpRequestedLocal(int argc, wchar_t** argv) {
  for (int i = 1; i < argc; ++i) {
    if (_wcsicmp(argv[i], L"--help") == 0 || _wcsicmp(argv[i], L"-h") == 0 || _wcsicmp(argv[i], L"/?") == 0) return true;
  }
  return false;
}
void PrintHelp() {
  const bool zh = IsChineseOsUi();
  if (zh) {
    std::wcout << L"简介：将三维网格或点云摘要回 GIS 侧数据（如要素 GeoJSON），便于在二维地图或矢量流程中引用模型范围与几何。\n\n"
               << L"用法:\n"
               << L"  agis_convert_model_to_gis --input <path> --output <path> [options]\n\n"
               << L"必填参数:\n"
               << L"  --input <path>               输入模型路径\n"
               << L"  --output <path>              输出 GIS 路径（如 .geojson）\n\n"
               << L"通用选项:\n"
               << L"  --help, -h, /?               显示帮助\n";
    PrintConvertCliHelpGrouped(
        std::wcout, true,
        L"  模型→GIS；主类型一般为 model。\n",
        L"  tin | dem | 3dmesh | pointcloud（默认 tin）。\n",
        L"  输出 GIS；主类型一般为 gis。\n",
        L"  auto | vector | raster | gpkg（默认 auto；本工具多为矢量 GeoJSON 场景）。\n");
    std::wcout << L"【其它选项】\n"
               << L"  --coord-system <projected|cecf>\n"
               << L"  --vector-mode <geometry|bake_texture>\n"
               << L"  --elev-horiz-ratio <num>\n"
               << L"  --target-crs <EPSG:xxxx|WKT>\n"
               << L"  --output-unit <m|km|1000km>\n"
               << L"  --mesh-spacing <1..1000000>\n"
               << L"  --texture-format <png|tif|tga|bmp|ktx2|ktx2-etc1s>\n"
               << L"  --raster-max-dim <0|64..16384>  (0=默认不降采样)\n";
    PrintConvertCliIoSection(std::wcout, true,
                             L"  --input：网格/点云模型路径；常见 .obj；点云可为 .las/.laz（视构建是否启用 LASzip）。\n"
                             L"  --output：GIS 产物路径；本工具常见为矢量 .geojson，或按实现导出 .gis/.tif 等（以实际后端为准）。\n");
  } else {
    std::wcout << L"About: Turn a mesh or point sample into GIS-friendly output (e.g. GeoJSON features) for 2D maps and downstream geoprocessing.\n\n"
               << L"Usage:\n"
               << L"  agis_convert_model_to_gis --input <path> --output <path> [options]\n\n"
               << L"Required:\n"
               << L"  --input <path>               Input model path\n"
               << L"  --output <path>              Output GIS path (e.g. .geojson)\n\n"
               << L"General:\n"
               << L"  --help, -h, /?               Show help\n";
    PrintConvertCliHelpGrouped(
        std::wcout, false,
        L"  Model → GIS; major type is usually model.\n",
        L"  tin | dem | 3dmesh | pointcloud (default: tin).\n",
        L"  GIS output; major type is usually gis.\n",
        L"  auto | vector | raster | gpkg (default: auto).\n");
    std::wcout << L"Other options:\n"
               << L"  --coord-system <projected|cecf>\n"
               << L"  --vector-mode <geometry|bake_texture>\n"
               << L"  --elev-horiz-ratio <num>\n"
               << L"  --target-crs <EPSG:xxxx|WKT>\n"
               << L"  --output-unit <m|km|1000km>\n"
               << L"  --mesh-spacing <1..1000000>\n"
               << L"  --texture-format <png|tif|tga|bmp|ktx2|ktx2-etc1s>\n"
               << L"  --raster-max-dim <0|64..16384>  (0=native default)\n";
    PrintConvertCliIoSection(std::wcout, false,
                             L"  --input: model path; commonly .obj; point cloud .las/.laz if supported by build.\n"
                             L"  --output: GIS path; often .geojson vector export (see backend for .gis/.tif, etc.).\n");
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
  const int rc = ConvertModelToGis(args);
  std::wcout << L"[3/3] write output ...\n";
  std::this_thread::sleep_for(std::chrono::milliseconds(60));
  if (rc == 0) std::wcout << L"[DONE] success\n"; else std::wcerr << L"[ERROR] conversion failed, code=" << rc << L"\n";
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
  return RunDirect(L"MODEL -> GIS", args);
}
