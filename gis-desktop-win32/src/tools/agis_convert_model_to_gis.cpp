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
    std::wcout << L"用法:\n  agis_convert_model_to_gis --input <path> --output <path> [options]\n\n"
               << L"必填参数: --input, --output\n"
               << L"可选参数: --help, --input-type, --input-subtype, --output-type, --output-subtype,\n"
               << L"         --coord-system, --vector-mode, --elev-horiz-ratio, --target-crs,\n"
               << L"         --output-unit, --mesh-spacing, --texture-format, --raster-max-dim\n";
  } else {
    std::wcout << L"Usage:\n  agis_convert_model_to_gis --input <path> --output <path> [options]\n\n"
               << L"Required: --input, --output\n"
               << L"Options : --help, --input-type, --input-subtype, --output-type, --output-subtype,\n"
               << L"          --coord-system, --vector-mode, --elev-horiz-ratio, --target-crs,\n"
               << L"          --output-unit, --mesh-spacing, --texture-format, --raster-max-dim\n";
  }
  std::wcout << L"\nDetailed options:\n"
             << L"  --coord-system <projected|cecf>\n"
             << L"  --vector-mode <geometry|bake_texture>\n"
             << L"  --elev-horiz-ratio <num>\n"
             << L"  --target-crs <EPSG:xxxx|WKT>\n"
             << L"  --output-unit <m|km|1000km>\n"
             << L"  --mesh-spacing <1..1000000>\n"
             << L"  --texture-format <png|tif|tga|bmp>\n"
             << L"  --raster-max-dim <64..16384>\n";
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
