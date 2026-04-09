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
               << L"  agis_convert_model_to_model --input <path> --output <path> [options]\n\n"
               << L"必填参数:\n"
               << L"  --input <path>               输入模型路径（OBJ/LAS）\n"
               << L"  --output <path>              输出模型路径（OBJ/LAS）\n\n"
               << L"通用选项:\n"
               << L"  --help, -h, /?               显示帮助\n";
    PrintConvertCliHelpGrouped(
        std::wcout, true,
        L"  模型→模型；主类型一般为 model。\n",
        L"  tin | dem | 3dmesh | pointcloud（常用：3dmesh ↔ pointcloud）。\n",
        L"  仍为模型输出；主类型一般为 model。\n",
        L"  tin | dem | 3dmesh | pointcloud（与输入子类型组合对应 UI）。\n");
    std::wcout << L"【其它选项】\n"
               << L"  --coord-system <v>           projected | cecf（默认 projected）\n"
               << L"  --vector-mode <v>            geometry | bake_texture（默认 geometry）\n"
               << L"  --elev-horiz-ratio <num>     高程/水平比，>0（默认 1）\n"
               << L"  --target-crs <text>          目标 CRS（默认自动）\n"
               << L"  --output-unit <v>            m | km | 1000km（默认 m）\n"
               << L"  --mesh-spacing <int>         网格步长 1..1000000（默认 1）\n"
               << L"  --texture-format <v>         png | tif | tga | bmp | ktx2 | ktx2-etc1s（默认 png；KTX2 需 basis_universal）\n"
               << L"  --raster-max-dim <int>       0=源图全分辨率（默认）；64..16384=长边上限\n\n"
               << L"示例:\n"
               << L"  agis_convert_model_to_model --input in.obj --output out.las --input-subtype 3dmesh --output-subtype pointcloud\n";
  } else {
    std::wcout << L"Usage:\n"
               << L"  agis_convert_model_to_model --input <path> --output <path> [options]\n\n"
               << L"Required:\n"
               << L"  --input <path>               Input model path (OBJ/LAS)\n"
               << L"  --output <path>              Output model path (OBJ/LAS)\n\n"
               << L"General:\n"
               << L"  --help, -h, /?               Show help\n";
    PrintConvertCliHelpGrouped(
        std::wcout, false,
        L"  Model → model; major type is usually model.\n",
        L"  tin | dem | 3dmesh | pointcloud (common: 3dmesh ↔ pointcloud).\n",
        L"  Model output; major type is usually model.\n",
        L"  tin | dem | 3dmesh | pointcloud (pair with input subtype per UI).\n");
    std::wcout << L"Other options:\n"
               << L"  --coord-system <v>           projected | cecf (default: projected)\n"
               << L"  --vector-mode <v>            geometry | bake_texture (default: geometry)\n"
               << L"  --elev-horiz-ratio <num>     Elevation/horizontal ratio, >0 (default: 1)\n"
               << L"  --target-crs <text>          Target CRS (default: auto)\n"
               << L"  --output-unit <v>            m | km | 1000km (default: m)\n"
               << L"  --mesh-spacing <int>         1..1000000 (default: 1)\n"
               << L"  --texture-format <v>         png | tif | tga | bmp | ktx2 | ktx2-etc1s (default: png; KTX2 needs basis_universal)\n"
               << L"  --raster-max-dim <int>       0=native (default); 64..16384=cap\n\n"
               << L"Example:\n"
               << L"  agis_convert_model_to_model --input in.obj --output out.las --input-subtype 3dmesh --output-subtype pointcloud\n";
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
  const int rc = ConvertModelToModel(args);
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
  return RunDirect(L"MODEL -> MODEL", args);
}
