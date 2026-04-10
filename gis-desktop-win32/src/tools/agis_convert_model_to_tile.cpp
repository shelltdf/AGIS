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
    std::wcout << L"简介：以模型（OBJ/点云等）为纹理与高程来源，生成与 gis_to_tile 相同形态的瓦片集，用于把三维成果发布为二维瓦片服务。\n\n"
               << L"用法:\n"
               << L"  agis_convert_model_to_tile --input <path> --output <dir|file> [options]\n\n"
               << L"  --output 形态取决于 --output-subtype：目录或单文件，与 gis_to_tile 相同。\n\n"
               << L"必填参数:\n"
               << L"  --input <path>               输入模型路径（OBJ/LAS 等）\n"
               << L"  --output <dir|file>          瓦片根目录或 .mbtiles/.gpkg\n\n"
               << L"通用选项:\n"
               << L"  --help, -h, /?               显示帮助\n";
    PrintConvertCliHelpGrouped(
        std::wcout, true,
        L"  模型→瓦片；主类型一般为 model。\n",
        L"  tin | dem | 3dmesh | pointcloud（默认 tin，与 GUI 一致）。\n",
        L"  输出瓦片；主类型一般为 tile。\n",
        L"  xyz | tms | wmts | mbtiles | gpkg | 3dtiles（默认 xyz）。\n"
        L"  mbtiles/gpkg：单文件；其余：目录。\n");
    std::wcout << L"【其它选项】\n"
               << L"  --coord-system <v>           projected | cecf（默认 projected）\n"
               << L"  --vector-mode <v>            geometry | bake_texture（默认 geometry）\n"
               << L"  --elev-horiz-ratio <num>     高程/水平比，>0（默认 1）\n"
               << L"  --target-crs <text>          目标 CRS（默认自动）\n"
               << L"  --output-unit <v>            m | km | 1000km（默认 m）\n"
               << L"  --mesh-spacing <int>         1..1000000（默认 1）\n"
               << L"  --texture-format <v>         png | tif | tga | bmp | ktx2 | ktx2-etc1s（默认 png；KTX2 需 basis_universal）\n"
               << L"  --raster-max-dim <int>       0=源图全分辨率（默认）；64..16384=长边上限\n"
               << L"  --tile-levels <auto|1..23>   瓦片层数（默认 auto）\n";
    PrintConvertCliIoSection(std::wcout, true,
                             L"  --input：.obj 或 .las/.laz 等模型/点云路径。\n"
                             L"  --output：与 gis_to_tile 相同（目录 或 .mbtiles / .gpkg）。\n");
  } else {
    std::wcout << L"About: Rasterize a model (OBJ/point cloud, etc.) into the same tile products as gis_to_tile for 2D tile services.\n\n"
               << L"Usage:\n"
               << L" agis_convert_model_to_tile --input <path> --output <dir|file> [options]\n\n"
               << L"  --output is a directory or single file depending on --output-subtype.\n\n"
               << L"Required:\n"
               << L"  --input <path>               Input model path (OBJ/LAS, etc.)\n"
               << L"  --output <dir|file>          Tile root or .mbtiles/.gpkg\n\n"
               << L"General:\n"
               << L"  --help, -h, /?               Show help\n";
    PrintConvertCliHelpGrouped(
        std::wcout, false,
        L"  Model → tiles; major type is usually model.\n",
        L"  tin | dem | 3dmesh | pointcloud (default: tin).\n",
        L"  Tile output; major type is usually tile.\n",
        L"  xyz | tms | wmts | mbtiles | gpkg | 3dtiles (default: xyz).\n"
        L"  mbtiles/gpkg: file; others: directory.\n");
    std::wcout << L"Other options:\n"
               << L"  --coord-system <v>           projected | cecf (default: projected)\n"
               << L"  --vector-mode <v>            geometry | bake_texture (default: geometry)\n"
               << L"  --elev-horiz-ratio <num>     Elevation/horizontal ratio, >0 (default: 1)\n"
               << L"  --target-crs <text>          Target CRS (default: auto)\n"
               << L"  --output-unit <v>            m | km | 1000km (default: m)\n"
               << L"  --mesh-spacing <int>         1..1000000 (default: 1)\n"
               << L"  --texture-format <v>         png | tif | tga | bmp | ktx2 | ktx2-etc1s (default: png; KTX2 needs basis_universal)\n"
               << L"  --raster-max-dim <int>       0=native full read (default); 64..16384=cap\n"
               << L"  --tile-levels <auto|1..23>   tile levels (default: auto)\n";
    PrintConvertCliIoSection(std::wcout, false,
                             L"  --input: .obj or .las/.laz (model/point cloud).\n"
                             L"  --output: same as gis_to_tile (directory or .mbtiles / .gpkg).\n");
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
  const int rc = ConvertModelToTile(args);
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
  return RunDirect(L"MODEL -> TILE", args);
}
