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
    std::wcout << L"简介：把 GIS 工程或栅格裁成 Web Mercator 等瓦片金字塔，输出 XYZ/TMS/WMTS 目录、MBTiles、GeoPackage 或 3D Tiles 等，用于发布或离线浏览。\n\n"
               << L"用法:\n"
               << L"  agis_convert_gis_to_tile --input <path> --output <dir|file> [options]\n\n"
               << L"  --output 形态取决于 --output-subtype：xyz/tms/wmts/3dtiles 为目录；mbtiles、gpkg 为单文件路径。\n\n"
               << L"必填参数:\n"
               << L"  --input <path>               输入 GIS 文件或目录\n"
               << L"  --output <dir|file>          瓦片根目录，或 .mbtiles / .gpkg 文件路径\n\n"
               << L"通用选项:\n"
               << L"  --help, -h, /?               显示帮助\n";
    PrintConvertCliHelpGrouped(
        std::wcout, true,
        L"  GIS→瓦片；主类型一般为 gis。\n",
        L"  auto | vector | raster | gpkg（默认 auto）。\n",
        L"  输出瓦片集；主类型一般为 tile。\n",
        L"  xyz | tms | wmts | mbtiles | gpkg | 3dtiles（默认 xyz）。\n"
        L"  mbtiles/gpkg：--output 指向单文件；其余子类型：--output 为目录。\n");
    std::wcout << L"【其它选项】\n"
               << L"  --coord-system <v>           projected | cecf（默认 projected）\n"
               << L"  --vector-mode <v>            geometry | bake_texture（默认 geometry）\n"
               << L"  --elev-horiz-ratio <num>     高程/水平比，默认 1\n"
               << L"  --target-crs <text>          目标 CRS（默认自动）\n"
               << L"  --output-unit <v>            m | km | 1000km（默认 m）\n"
               << L"  --mesh-spacing <int>         1..1000000（默认 1）\n"
               << L"  --texture-format <v>         png | tif | tga | bmp | ktx2 | ktx2-etc1s（默认 png；KTX2 需 basis_universal）\n"
               << L"  --raster-max-dim <int>       0=源图全分辨率（默认）；64..16384=长边上限控内存\n"
               << L"  --tile-levels <auto|1..23>   瓦片层数（默认 auto）\n";
    PrintConvertCliIoSection(std::wcout, true,
                             L"  --input：同 gis_to_model（.gis 或栅格/矢量路径）。\n"
                             L"  --output：xyz/tms/wmts/3dtiles 为**目录**（内含 tileset.json、{z}/{x}/{y}.png 等）；mbtiles→*.mbtiles；gpkg→*.gpkg。\n");
  } else {
    std::wcout << L"About: Slice GIS/raster into a Web Mercator (etc.) tile pyramid as XYZ/TMS/WMTS folders, MBTiles, GPKG, or 3D Tiles for serving or offline use.\n\n"
               << L"Usage:\n"
               << L"  agis_convert_gis_to_tile --input <path> --output <dir|file> [options]\n\n"
               << L"  --output is a directory for xyz/tms/wmts/3dtiles, or a single file for mbtiles/gpkg.\n\n"
               << L"Required:\n"
               << L"  --input <path>               Input GIS file or directory\n"
               << L"  --output <dir|file>          Tile root dir, or .mbtiles / .gpkg path\n\n"
               << L"General:\n"
               << L"  --help, -h, /?               Show help\n";
    PrintConvertCliHelpGrouped(
        std::wcout, false,
        L"  GIS → tiles; major type is usually gis.\n",
        L"  auto | vector | raster | gpkg (default: auto).\n",
        L"  Tile output; major type is usually tile.\n",
        L"  xyz | tms | wmts | mbtiles | gpkg | 3dtiles (default: xyz).\n"
        L"  mbtiles/gpkg: --output is a file path; others: --output is a directory.\n");
    std::wcout << L"Other options:\n"
               << L"  --coord-system <v>           projected | cecf (default: projected)\n"
               << L"  --vector-mode <v>            geometry | bake_texture (default: geometry)\n"
               << L"  --elev-horiz-ratio <num>     Elevation/horizontal ratio (default: 1)\n"
               << L"  --target-crs <text>          Target CRS (default: auto)\n"
               << L"  --output-unit <v>            m | km | 1000km (default: m)\n"
               << L"  --mesh-spacing <int>         1..1000000 (default: 1)\n"
               << L"  --texture-format <v>         png | tif | tga | bmp | ktx2 | ktx2-etc1s (default: png; KTX2 needs basis_universal)\n"
               << L"  --raster-max-dim <int>       0=native full read (default); 64..16384=cap long side\n"
               << L"  --tile-levels <auto|1..23>   tile levels (default: auto)\n";
    PrintConvertCliIoSection(std::wcout, false,
                             L"  --input: same as gis_to_model (.gis or raster/vector paths).\n"
                             L"  --output: xyz/tms/wmts/3dtiles = **directory** (tileset.json, z/x/y.png, ...); mbtiles = *.mbtiles; gpkg = *.gpkg.\n");
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
  const int rc = ConvertGisToTile(args);
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
  return RunDirect(L"GIS -> TILE", args);
}
