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
  if (IsChineseOsUi()) {
    std::wcout << L"用法:\n"
               << L"  agis_convert_gis_to_tile --input <path> --output <dir> [options]\n\n"
               << L"必填参数:\n"
               << L"  --input <path>               输入 GIS 文件或目录\n"
               << L"  --output <dir>               输出瓦片目录\n\n"
               << L"可选参数:\n"
               << L"  --help, -h, /?               显示帮助\n"
               << L"  --input-type <text>          输入主类型（gis/model/tile）\n"
               << L"  --input-subtype <text>       输入子类型\n"
               << L"  --output-type <text>         输出主类型\n"
               << L"  --output-subtype <xyz|tms|wmts|mbtiles|gpkg|3dtiles> 输出子类型（默认 xyz）\n"
               << L"  --coord-system <v>           projected | cecf（默认 projected）\n"
               << L"  --vector-mode <v>            geometry | bake_texture（默认 geometry）\n"
               << L"  --elev-horiz-ratio <num>     高程/水平比，默认 1\n"
               << L"  --target-crs <text>          目标 CRS（默认自动）\n"
               << L"  --output-unit <v>            m | km | 1000km（默认 m）\n"
               << L"  --mesh-spacing <int>         1..1000000（默认 1）\n"
               << L"  --texture-format <v>         png | tif | tga | bmp（默认 png）\n"
               << L"  --raster-max-dim <int>       64..16384（默认 4096）\n";
  } else {
    std::wcout << L"Usage:\n"
               << L"  agis_convert_gis_to_tile --input <path> --output <dir> [options]\n\n"
               << L"Required:\n"
               << L"  --input <path>               Input GIS file or directory\n"
               << L"  --output <dir>               Output tile directory\n\n"
               << L"Options:\n"
               << L"  --help, -h, /?               Show help\n"
               << L"  --input-type <text>          Input major type (gis/model/tile)\n"
               << L"  --input-subtype <text>       Input subtype\n"
               << L"  --output-type <text>         Output major type\n"
               << L"  --output-subtype <xyz|tms|wmts|mbtiles|gpkg|3dtiles> Output subtype (default: xyz)\n"
               << L"  --coord-system <v>           projected | cecf (default: projected)\n"
               << L"  --vector-mode <v>            geometry | bake_texture (default: geometry)\n"
               << L"  --elev-horiz-ratio <num>     Elevation/horizontal ratio (default: 1)\n"
               << L"  --target-crs <text>          Target CRS (default: auto)\n"
               << L"  --output-unit <v>            m | km | 1000km (default: m)\n"
               << L"  --mesh-spacing <int>         1..1000000 (default: 1)\n"
               << L"  --texture-format <v>         png | tif | tga | bmp (default: png)\n"
               << L"  --raster-max-dim <int>       64..16384 (default: 4096)\n";
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
