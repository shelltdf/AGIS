#pragma once

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
   * GIS→模型：读入栅格用于 DEM/贴图时，**长边**目标上限（像素）。过大占内存；过小贴图糊。
   * 默认 4096；CLI `--raster-max-dim`（范围 64–16384）。
   */
  int raster_read_max_dim = 4096;
};

bool ParseConvertArgs(int argc, wchar_t** argv, ConvertArgs* out);
void PrintConvertBanner(const wchar_t* title, const ConvertArgs& args);
bool IsChineseOsUi();
int ConvertGisToModel(const ConvertArgs& args);
int ConvertGisToTile(const ConvertArgs& args);
int ConvertModelToGis(const ConvertArgs& args);
int ConvertModelToModel(const ConvertArgs& args);
int ConvertModelToTile(const ConvertArgs& args);
int ConvertTileToGis(const ConvertArgs& args);
int ConvertTileToModel(const ConvertArgs& args);
