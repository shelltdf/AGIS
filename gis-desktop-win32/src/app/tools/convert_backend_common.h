#pragma once

#include <cstdint>
#include <iosfwd>
#include <string>

struct ConvertArgs {
  std::wstring input;
  std::wstring output;
  std::wstring input_type;
  std::wstring input_subtype;
  std::wstring output_type;
  std::wstring output_subtype;
  std::wstring coord_system;
  std::wstring vector_mode;
  /** 高程相对水平比例：水平为米、高程为米时 1.0 表示 1:1（同量级）。>1 竖向夸大。 */
  double elev_horiz_ratio = 1.0;
  /** 目标 CRS（如 EPSG:3857 或 WKT）；空表示自动：优先首栅格，其次首矢量图层，最后 WGS84。 */
  std::wstring target_crs;
  /** 模型顶点/高程单位：`m` | `km` | `1000km`（1 模型单位分别对应 1m / 1km / 1000km）。 */
  std::wstring output_unit;
  /** DEM 规则网格平面步长（**模型单位** 下的正整数，默认 1）；地面步长折合米 = mesh_spacing / output_unit_scale，且不会小于当前读入栅格一像元地面尺寸（下采样读入时按缩聚后一栅格格网计）。 */
  int mesh_spacing = 1;
  /** 模型贴图输出格式：`png`（默认）| `tif` | `tga` | `bmp`。 */
  std::wstring texture_format;
  /**
   * GIS→模型 / GIS→Tile 等：读入栅格用于 DEM/贴图/切瓦时，**长边**目标上限（像素）。`0` 为按源整幅读入。
   * 默认 0；CLI `--raster-max-dim`：省略或 `0` 不降采样；`64..16384` 限制长边以控内存（超限则等比缩小后再读）。
   */
  int raster_read_max_dim = 0;
  /** GIS->TILE 金字塔层数；-1 表示 auto（按原始图像最小边自动计算最大有意义层）。 */
  int tile_levels = -1;
  /** TILE->MODEL 合并时的内存上限（MB），超出后自动分批输出多个 mesh。默认 512。 */
  int tile_max_memory_mb = 512;
  /** OBJ 数值输出精度类型：`double`（默认）| `float`。 */
  std::wstring obj_fp_type = L"double";

  /** GIS→模型：贴图输出。`color` 仅 albedo（map_Kd）；`pbr` 额外生成 normal/roughness/metallic/AO 并在 MTL 中引用。 */
  std::wstring obj_texture_mode = L"color";
  /** GIS→模型：视觉效果。`none` | `night` | `snow`（在写出 albedo 前处理 RGB）。 */
  std::wstring obj_visual_effect = L"none";
  /** `snow` 时雪花/颗粒尺度（越大斑块越大）；范围约 0.25–8。 */
  double obj_snow_scale = 1.0;
  /** DEM 高程采样：`bilinear` | `nearest` | `cell_avg` | `average` | `dem_avg` | `median`（3×3）| `bicubic`。 */
  std::wstring gis_dem_interp = L"bilinear";
  /** 网格拓扑：`grid` | `tin`（同 grid）| `delaunay`（XY Delaunay，顶点 ≤8192 且非 cecf）。 */
  std::wstring gis_mesh_topology = L"grid";
  /**
   * 单组 OBJ+贴图预算模式（超过则按 J 向条带拆成多组 `_partN` 文件）：
   * `memory` — 估算 CPU 侧顶点/索引/纹理解码缓冲；
   * `file` — 估算未压缩 OBJ 文本 + 纹理按字节/像素的乐观上界；
   * `vram` — 估算典型 GPU 上传（顶点 + 纹理 mip≈1.33 倍）。
   */
  std::wstring model_budget_mode = L"memory";
  /** 与 `model_budget_mode` 对应的限额（MB），默认 4096。 */
  std::int64_t model_budget_mb = 4096;
  /** GIS→模型：强制沿网格 J 向拆成 N 份（每份独立 OBJ+贴图）；与预算自动拆分取较大者。默认 1。 */
  int gis_model_split_parts = 1;
};

bool ParseConvertArgs(int argc, wchar_t** argv, ConvertArgs* out);
void PrintConvertBanner(const wchar_t* title, const ConvertArgs& args);
bool IsChineseOsUi();
void EnableRealtimeConsoleFlush();
/// 将四类 CLI 轴参数在 --help 中分组成块打印；各 body 可为多行，建议行首缩进两个空格。
void PrintConvertCliHelpGrouped(std::wostream& os, bool chinese, const wchar_t* input_type_lines,
                                const wchar_t* input_subtype_lines, const wchar_t* output_type_lines,
                                const wchar_t* output_subtype_lines);
/// 各工具在 --help 末尾追加：输入/输出路径形态与推荐扩展名（多行，行首建议两个空格缩进）。
void PrintConvertCliIoSection(std::wostream& os, bool chinese, const wchar_t* lines);
int ConvertGisToModel(const ConvertArgs& args);
int ConvertGisToTile(const ConvertArgs& args);
int ConvertModelToGis(const ConvertArgs& args);
int ConvertModelToModel(const ConvertArgs& args);
int ConvertModelToTile(const ConvertArgs& args);
int ConvertTileToGis(const ConvertArgs& args);
int ConvertTileToModel(const ConvertArgs& args);
