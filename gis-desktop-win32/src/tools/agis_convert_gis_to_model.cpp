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
    std::wcout << L"简介：从 .gis 或栅格/矢量构建地形网格，导出带纹理的 Wavefront OBJ。默认输出 color 贴图（仅 albedo），"
                  L"可选 PBR（normal/roughness/metallic/ao）；可选视觉特效 none/night/snow（snow 支持雪尺度）。"
                  L"支持 DEM 多插值与 Delaunay 网格；支持 file/memory/vram 三种预算模式，超预算自动拆分 _partN.obj。\n\n"
               << L"用法:\n"
               << L"  agis_convert_gis_to_model --input <path> --output <path> [options]\n\n"
               << L"必填参数:\n"
               << L"  --input <path>               输入 GIS 文件或目录\n"
               << L"  --output <path>              输出 .obj（默认）或 .las/.laz（点云模式）\n\n"
               << L"通用选项:\n"
               << L"  --help, -h, /?               显示帮助\n";
    PrintConvertCliHelpGrouped(
        std::wcout, true,
        L"  本工具固定为 GIS→模型；仅支持 `gis`（可省略 `--input-type`，传入非 gis 将报错）。\n",
        L"  auto | vector | raster | gpkg（默认 auto，与 GUI 一致）。\n",
        L"  输出为模型；主类型一般为 model。\n",
        L"  3dmesh | pointcloud（默认 3dmesh；TIN/DEM 通过算法参数控制）。\n");
    std::wcout << L"【其它选项】\n"
               << L"  --coord-system <projected|cecf>\n"
               << L"  --vector-mode <geometry|bake_texture>\n"
               << L"  --elev-horiz-ratio <num>\n"
               << L"  --target-crs <EPSG:xxxx|WKT>\n"
               << L"  --output-unit <m|km|1000km>\n"
               << L"  --mesh-spacing <1..1000000>\n"
               << L"  --texture-format <png|jpg|tga|tif|ktx|ktx2|hdr>（其中 ktx->ktx2；jpg/hdr 视后端能力回退）\n"
               << L"  --raster-max-dim <0|64..16384>  0=源图全分辨率（默认）；或长边上限\n"
               << L"  --obj-fp-type <double|float>\n"
               << L"  --obj-texture-mode <color|pbr>   默认 color（仅 albedo）；pbr 额外输出 normal/roughness/metallic/ao\n"
               << L"  --obj-visual-effect <none|night|snow>  贴图后处理；snow 配合 --obj-snow-scale <0.25..8>\n"
               << L"  --gis-dem-interp <bilinear|nearest|cell_avg|average|dem_avg|median|bicubic>  DEM 顶点高程采样\n"
               << L"  --gis-mesh-topology <grid|tin|delaunay>  默认 grid；delaunay 为 XY 三角剖分（≤8192 顶点，非 cecf）\n"
               << L"  --model-budget-mode <memory|file|vram>  单份 OBJ+贴图预算口径（与 --model-budget-mb 联用）\n"
               << L"  --model-budget-mb <256..262144>  默认 4096；超限自动按行带拆成 _partN.obj + 贴图\n"
               << L"  --gis-model-split-parts <1..512>  强制行带份数（与自动拆分取较大者）\n\n"
               << L"示例:\n"
               << L"  agis_convert_gis_to_model --input demo.gis --output demo.obj --output-subtype 3dmesh\n"
               << L"  agis_convert_gis_to_model --input demo.gis --output demo_pbr.obj --obj-texture-mode pbr --obj-visual-effect none\n"
               << L"  agis_convert_gis_to_model --input demo.gis --output demo_night.obj --obj-visual-effect night\n"
               << L"  agis_convert_gis_to_model --input demo.gis --output demo_snow.obj --obj-visual-effect snow --obj-snow-scale 2.0\n"
               << L"  agis_convert_gis_to_model --input demo.gis --output demo_delaunay.obj --gis-mesh-topology delaunay --gis-dem-interp dem_avg\n"
               << L"  agis_convert_gis_to_model --input demo.gis --output demo_4g.obj --model-budget-mode memory --model-budget-mb 4096\n"
               << L"  agis_convert_gis_to_model --input demo.gis --output demo_file_opt.obj --model-budget-mode file --model-budget-mb 4096\n"
               << L"  agis_convert_gis_to_model --input demo.gis --output demo_vram_opt.obj --model-budget-mode vram --model-budget-mb 4096\n"
               << L"  agis_convert_gis_to_model --input demo.gis --output cloud.laz --output-subtype pointcloud\n";
    PrintConvertCliIoSection(std::wcout, true,
                             L"  --input：可为 .gis 工程，或含栅格/矢量的数据路径（常见 .tif/.tiff/.flt、.shp、.gpkg、.geojson 等）。\n"
                             L"  --output（网格）：主模型后缀支持 .obj/.flt（当前网格主文件按 OBJ 语义写出）；贴图后缀支持 .png/.jpg/.tga/.tif/.ktx/.ktx2/.hdr（由 --texture-format 指定，按后端能力落盘）。\n"
                             L"  --output（点云）：`--output-subtype pointcloud` 或输出扩展名为 .las/.laz 时，写出 LAS 1.2（RGB）；LAZ 需构建编入 LASzip。采样步长同 `--mesh-spacing`（与 agis_convert_model_to_model 网格→点云一致）。\n"
                             L"  网格模式统一使用 3dmesh；TIN/DEM 通过 `--gis-dem-interp` 与 `--gis-mesh-topology` 表达。"
                             L"  仅 pointcloud（或 .las/.laz 路径）走点云分支。\n");
  } else {
    std::wcout << L"About: Build a textured terrain mesh from .gis or raster/vector; export Wavefront OBJ. Default is color texture (albedo only), "
                  L"optional PBR maps (normal/roughness/metallic/ao), plus visual effects none/night/snow (snow scale configurable). "
                  L"Supports DEM interpolation modes, Delaunay topology, and budget modes file/memory/vram with automatic _partN OBJ split.\n\n"
               << L"Usage:\n"
               << L"  agis_convert_gis_to_model --input <path> --output <path> [options]\n\n"
               << L"Required:\n"
               << L"  --input <path>               Input GIS file or directory\n"
               << L"  --output <path>              Output .obj (default) or .las/.laz (point cloud mode)\n\n"
               << L"General:\n"
               << L"  --help, -h, /?               Show help\n";
    PrintConvertCliHelpGrouped(
        std::wcout, false,
        L"  GIS → model tool; only `gis` input type is supported (omitting `--input-type` is allowed; non-gis is rejected).\n",
        L"  auto | vector | raster | gpkg (default: auto).\n",
        L"  Output is model; major type is usually model.\n",
        L"  3dmesh | pointcloud (default: 3dmesh; TIN/DEM are controlled by algorithm options).\n");
    std::wcout << L"Other options:\n"
               << L"  --coord-system <projected|cecf>\n"
               << L"  --vector-mode <geometry|bake_texture>\n"
               << L"  --elev-horiz-ratio <num>\n"
               << L"  --target-crs <EPSG:xxxx|WKT>\n"
               << L"  --output-unit <m|km|1000km>\n"
               << L"  --mesh-spacing <1..1000000>\n"
               << L"  --texture-format <png|jpg|tga|tif|ktx|ktx2|hdr> (ktx->ktx2; jpg/hdr may fallback by backend capability)\n"
               << L"  --raster-max-dim <0|64..16384>  0=native (default); or long-side cap\n"
               << L"  --obj-fp-type <double|float>\n"
               << L"  --obj-texture-mode <color|pbr>   default color (albedo only); pbr adds normal/roughness/metallic/ao maps\n"
               << L"  --obj-visual-effect <none|night|snow>  post-process albedo; snow uses --obj-snow-scale <0.25..8>\n"
               << L"  --gis-dem-interp <bilinear|nearest|cell_avg|average|dem_avg|median|bicubic>  DEM height sampling\n"
               << L"  --gis-mesh-topology <grid|tin|delaunay>  default grid; delaunay = XY triangulation (<=8192 verts, not cecf)\n"
               << L"  --model-budget-mode <memory|file|vram>  budget metric for one OBJ+textures chunk\n"
               << L"  --model-budget-mb <256..262144>  default 4096; auto row-strip split to _partN.obj when exceeded\n"
               << L"  --gis-model-split-parts <1..512>  force row-strip count (max with auto split)\n\n"
               << L"Example:\n"
               << L"  agis_convert_gis_to_model --input demo.gis --output demo.obj --output-subtype 3dmesh\n"
               << L"  agis_convert_gis_to_model --input demo.gis --output demo_pbr.obj --obj-texture-mode pbr --obj-visual-effect none\n"
               << L"  agis_convert_gis_to_model --input demo.gis --output demo_night.obj --obj-visual-effect night\n"
               << L"  agis_convert_gis_to_model --input demo.gis --output demo_snow.obj --obj-visual-effect snow --obj-snow-scale 2.0\n"
               << L"  agis_convert_gis_to_model --input demo.gis --output demo_delaunay.obj --gis-mesh-topology delaunay --gis-dem-interp dem_avg\n"
               << L"  agis_convert_gis_to_model --input demo.gis --output demo_4g.obj --model-budget-mode memory --model-budget-mb 4096\n"
               << L"  agis_convert_gis_to_model --input demo.gis --output demo_file_opt.obj --model-budget-mode file --model-budget-mb 4096\n"
               << L"  agis_convert_gis_to_model --input demo.gis --output demo_vram_opt.obj --model-budget-mode vram --model-budget-mb 4096\n"
               << L"  agis_convert_gis_to_model --input demo.gis --output cloud.laz --output-subtype pointcloud\n";
    PrintConvertCliIoSection(std::wcout, false,
                             L"  --input: a .gis project or a path to raster/vector data (.tif/.tiff/.flt, .shp, .gpkg, .geojson, ...).\n"
                             L"  --output (mesh): model suffix supports .obj/.flt (mesh is currently written with OBJ semantics); texture suffix supports .png/.jpg/.tga/.tif/.ktx/.ktx2/.hdr via --texture-format (subject to backend capability).\n"
                             L"  --output (point cloud): with `--output-subtype pointcloud` or .las/.laz path, writes LAS 1.2 RGB; LAZ needs LASzip in build. "
                             L"Sampling step is `--mesh-spacing` (same as model_to_model mesh→cloud).\n"
                             L"  Mesh mode uses 3dmesh; TIN/DEM behavior is controlled by `--gis-dem-interp` and "
                             L"`--gis-mesh-topology`. pointcloud or .las/.laz selects the LAS/LAZ path.\n");
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
  if (!args.input_type.empty()) {
    std::wstring t = args.input_type;
    for (auto& c : t) {
      if (c >= L'A' && c <= L'Z') c = static_cast<wchar_t>(c - L'A' + L'a');
    }
    if (t != L"gis") {
      std::wcerr << L"[ERROR] agis_convert_gis_to_model only supports --input-type gis.\n";
      return 1;
    }
  }
  return RunDirect(L"GIS -> MODEL", args);
}
