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
    std::wcout << L"用法:\n"
               << L"  agis_convert_tile_to_model --input <dir|file> --output <path> [options]\n\n"
               << L"  --input：目录或单文件规则同 tile_to_gis。\n\n"
               << L"必填参数:\n"
               << L"  --input <dir|file>           瓦片目录或容器文件\n"
               << L"  --output <path>              输出模型路径（OBJ 等）\n\n"
               << L"通用选项:\n"
               << L"  --help, -h, /?               显示帮助\n";
    PrintConvertCliHelpGrouped(
        std::wcout, true,
        L"  瓦片→模型；主类型一般为 tile。\n",
        L"  xyz | tms | wmts | mbtiles | gpkg | 3dtiles（默认 xyz）。\n"
        L"  目录型子类型选文件夹；mbtiles/gpkg 选文件。\n",
        L"  输出模型；主类型一般为 model。\n",
        L"  tin | dem | 3dmesh | pointcloud（默认 tin）。\n");
    std::wcout << L"【其它选项】\n"
               << L"  --coord-system <v>           projected | cecf（默认 projected）\n"
               << L"  --vector-mode <v>            geometry | bake_texture（默认 geometry）\n"
               << L"  --elev-horiz-ratio <num>     高程/水平比，>0（默认 1）\n"
               << L"  --target-crs <text>          目标 CRS（默认自动）\n"
               << L"  --output-unit <v>            m | km | 1000km（默认 m）\n"
               << L"  --mesh-spacing <int>         1..1000000（默认 1）\n"
               << L"  --texture-format <v>         png | tif | tga | bmp | ktx2 | ktx2-etc1s（默认 png；KTX2 需 basis_universal）\n"
               << L"  --raster-max-dim <int>       0=源图全分辨率（默认）；64..16384=长边上限\n"
               << L"  --tile-max-memory-mb <int>   合并内存上限MB，64..131072（默认 512）\n";
  } else {
    std::wcout << L"Usage:\n"
               << L"  agis_convert_tile_to_model --input <dir|file> --output <path> [options]\n\n"
               << L"  --input: same directory vs file rules as tile_to_gis.\n\n"
               << L"Required:\n"
               << L"  --input <dir|file>           Tile root or container file\n"
               << L"  --output <path>              Output model path\n\n"
               << L"General:\n"
               << L"  --help, -h, /?               Show help\n";
    PrintConvertCliHelpGrouped(
        std::wcout, false,
        L"  Tiles → model; major type is usually tile.\n",
        L"  xyz | tms | wmts | mbtiles | gpkg | 3dtiles (default: xyz).\n"
        L"  Directory subtypes: folder; mbtiles/gpkg: file.\n",
        L"  Model output; major type is usually model.\n",
        L"  tin | dem | 3dmesh | pointcloud (default: tin).\n");
    std::wcout << L"Other options:\n"
               << L"  --coord-system <v>           projected | cecf (default: projected)\n"
               << L"  --vector-mode <v>            geometry | bake_texture (default: geometry)\n"
               << L"  --elev-horiz-ratio <num>     Elevation/horizontal ratio, >0 (default: 1)\n"
               << L"  --target-crs <text>          Target CRS (default: auto)\n"
               << L"  --output-unit <v>            m | km | 1000km (default: m)\n"
               << L"  --mesh-spacing <int>         1..1000000 (default: 1)\n"
               << L"  --texture-format <v>         png | tif | tga | bmp | ktx2 | ktx2-etc1s (default: png; KTX2 needs basis_universal)\n"
               << L"  --raster-max-dim <int>       0=native (default); 64..16384=cap\n"
               << L"  --tile-max-memory-mb <int>   merge memory cap in MB, 64..131072 (default: 512)\n";
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
  const int rc = ConvertTileToModel(args);
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
  return RunDirect(L"TILE -> MODEL", args);
}
