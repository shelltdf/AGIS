#include <algorithm>
#include <cctype>
#include <cmath>
#include <cwctype>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <string>
#include <vector>

#include <windows.h>
#include <windowsx.h>
#include <commctrl.h>
#include <shellapi.h>
#include <shlobj.h>

#include <cstring>

#ifndef GIS_DESKTOP_HAVE_GDAL
#define GIS_DESKTOP_HAVE_GDAL 0
#endif

#if GIS_DESKTOP_HAVE_GDAL
#include <cpl_conv.h>
#include <gdal.h>
#include <gdal_priv.h>
#include <ogr_api.h>
#endif

#include "app/resource.h"
#include "app/ui_font.h"
#include "app/ui_theme.h"
#include "main_app.h"
#include "main_globals.h"
#include "main_gis_xml.h"
#include "map_engine/map_engine.h"
#include "map_engine/map_projection.h"

namespace {
PROCESS_INFORMATION g_convertPi{};
bool g_convertRunning = false;
constexpr UINT_PTR kConvertPollTimerId = 2;
}  // namespace

void WriteConvertLog(HWND hwnd, const wchar_t* line) {
  HWND hLog = GetDlgItem(hwnd, IDC_CONV_LOG);
  if (!hLog || !line) {
    return;
  }
  const int len = GetWindowTextLengthW(hLog);
  SendMessageW(hLog, EM_SETSEL, static_cast<WPARAM>(len), static_cast<LPARAM>(len));
  std::wstring s = line;
  s += L"\r\n";
  SendMessageW(hLog, EM_REPLACESEL, FALSE, reinterpret_cast<LPARAM>(s.c_str()));
}
void LayoutConvertWindow(HWND hwnd) {
  RECT rc{};
  GetClientRect(hwnd, &rc);
  const int w = rc.right - rc.left;
  const int h = rc.bottom - rc.top;
  const int m = 10;
  const int topH = std::max(180, h / 2);
  const int colW = (w - m * 4) / 3;

  MoveWindow(GetDlgItem(hwnd, IDC_CONV_INPUT_TYPE), m, m + 20, colW - 28, 220, TRUE);
  MoveWindow(GetDlgItem(hwnd, IDC_CONV_INPUT_TYPE_HELP), m + colW - 24, m + 20, 24, 24, TRUE);
  MoveWindow(GetDlgItem(hwnd, IDC_CONV_INPUT_SUBTYPE), m, m + 48, colW - 28, 220, TRUE);
  MoveWindow(GetDlgItem(hwnd, IDC_CONV_INPUT_SUBTYPE_HELP), m + colW - 24, m + 48, 24, 24, TRUE);
  MoveWindow(GetDlgItem(hwnd, IDC_CONV_INPUT_PATH), m, m + 76, colW - 146, 24, TRUE);
  MoveWindow(GetDlgItem(hwnd, IDC_CONV_INPUT_BROWSE), m + colW - 142, m + 76, 68, 24, TRUE);
  MoveWindow(GetDlgItem(hwnd, IDC_CONV_INPUT_PREVIEW), m + colW - 70, m + 76, 70, 24, TRUE);
  MoveWindow(GetDlgItem(hwnd, IDC_CONV_INPUT_INFO), m, m + 106, colW, topH - 110, TRUE);

  const int midColX = m * 2 + colW;
  const int midStackBottom = 324;
  const int settingTotalH = (std::max)(88, topH - m - 20 - midStackBottom);
  const int inSettingH = (std::max)(36, settingTotalH / 2 - 12);
  const int outSettingH = (std::max)(36, settingTotalH - inSettingH - 28);
  MoveWindow(GetDlgItem(hwnd, IDC_CONV_INPUT_SETTING_LBL), midColX, m + 8, colW, 18, TRUE);
  MoveWindow(GetDlgItem(hwnd, IDC_CONV_INPUT_SETTING), midColX, m + 24, colW, inSettingH, TRUE);
  MoveWindow(GetDlgItem(hwnd, IDC_CONV_OUTPUT_SETTING_LBL), midColX, m + 28 + inSettingH, colW, 18, TRUE);
  MoveWindow(GetDlgItem(hwnd, IDC_CONV_OUTPUT_SETTING), midColX, m + 44 + inSettingH, colW, outSettingH, TRUE);
  MoveWindow(GetDlgItem(hwnd, IDC_CONV_ELEV_HORIZ_LBL), midColX, topH - 258, 168, 18, TRUE);
  MoveWindow(GetDlgItem(hwnd, IDC_CONV_ELEV_HORIZ_RATIO), midColX + 172, topH - 260, (std::max)(72, colW - 178), 24,
             TRUE);
  MoveWindow(GetDlgItem(hwnd, IDC_CONV_TARGET_CRS_LBL), midColX, topH - 228, colW, 16, TRUE);
  MoveWindow(GetDlgItem(hwnd, IDC_CONV_TARGET_CRS), midColX, topH - 210, colW, 22, TRUE);
  MoveWindow(GetDlgItem(hwnd, IDC_CONV_OUTPUT_UNIT_LBL), midColX, topH - 180, 132, 18, TRUE);
  MoveWindow(GetDlgItem(hwnd, IDC_CONV_OUTPUT_UNIT), midColX + 136, topH - 182, (std::max)(88, colW - 140), 120, TRUE);
  MoveWindow(GetDlgItem(hwnd, IDC_CONV_MESH_SPACING_LBL), midColX, topH - 152, 168, 18, TRUE);
  MoveWindow(GetDlgItem(hwnd, IDC_CONV_MESH_SPACING), midColX + 172, topH - 154, (std::max)(72, colW - 178), 24, TRUE);
  MoveWindow(GetDlgItem(hwnd, IDC_CONV_RASTER_MAX_LBL), midColX, topH - 124, 168, 18, TRUE);
  MoveWindow(GetDlgItem(hwnd, IDC_CONV_RASTER_MAX), midColX + 172, topH - 126, (std::max)(72, colW - 178), 24, TRUE);
  MoveWindow(GetDlgItem(hwnd, IDC_CONV_TEXTURE_FMT_LBL), midColX, topH - 96, 168, 18, TRUE);
  MoveWindow(GetDlgItem(hwnd, IDC_CONV_TEXTURE_FORMAT), midColX + 172, topH - 98, (std::max)(72, colW - 178), 120, TRUE);
  MoveWindow(GetDlgItem(hwnd, IDC_CONV_MODEL_COORD), midColX, topH - 68, colW, 24, TRUE);
  MoveWindow(GetDlgItem(hwnd, IDC_CONV_VECTOR_MODE), midColX, topH - 40, colW, 24, TRUE);

  MoveWindow(GetDlgItem(hwnd, IDC_CONV_OUTPUT_TYPE), m * 3 + colW * 2, m + 20, colW - 28, 220, TRUE);
  MoveWindow(GetDlgItem(hwnd, IDC_CONV_OUTPUT_TYPE_HELP), m * 3 + colW * 2 + colW - 24, m + 20, 24, 24, TRUE);
  MoveWindow(GetDlgItem(hwnd, IDC_CONV_OUTPUT_SUBTYPE), m * 3 + colW * 2, m + 48, colW - 28, 220, TRUE);
  MoveWindow(GetDlgItem(hwnd, IDC_CONV_OUTPUT_SUBTYPE_HELP), m * 3 + colW * 2 + colW - 24, m + 48, 24, 24, TRUE);
  MoveWindow(GetDlgItem(hwnd, IDC_CONV_OUTPUT_PATH), m * 3 + colW * 2, m + 76, colW - 146, 24, TRUE);
  MoveWindow(GetDlgItem(hwnd, IDC_CONV_OUTPUT_BROWSE), m * 3 + colW * 2 + colW - 142, m + 76, 68, 24, TRUE);
  MoveWindow(GetDlgItem(hwnd, IDC_CONV_OUTPUT_PREVIEW), m * 3 + colW * 2 + colW - 70, m + 76, 70, 24, TRUE);
  MoveWindow(GetDlgItem(hwnd, IDC_CONV_OUTPUT_INFO), m * 3 + colW * 2, m + 106, colW, topH - 110, TRUE);

  const int y1 = m + topH + 8;
  MoveWindow(GetDlgItem(hwnd, IDC_CONV_CMDLINE), m, y1, w - m * 2 - 110, 88, TRUE);
  MoveWindow(GetDlgItem(hwnd, IDC_CONV_COPY_CMD), w - m - 100, y1, 100, 26, TRUE);
  MoveWindow(GetDlgItem(hwnd, IDC_CONV_RUN), w - m - 100, y1 + 32, 100, 26, TRUE);
  MoveWindow(GetDlgItem(hwnd, IDC_CONV_PROGRESS), m, y1 + 96, w - m * 2, 22, TRUE);
  MoveWindow(GetDlgItem(hwnd, IDC_CONV_MSG), m, y1 + 122, w - m * 2, 24, TRUE);
  MoveWindow(GetDlgItem(hwnd, IDC_CONV_LOG), m, y1 + 150, w - m * 2, h - (y1 + 150) - m, TRUE);
}

void FillConvertTypeCombo(HWND combo) {
  SendMessageW(combo, CB_RESETCONTENT, 0, 0);
  SendMessageW(combo, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(L"GIS数据（矢量/栅格）"));
  SendMessageW(combo, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(L"模型数据（TIN/DEM/3DMesh）"));
  SendMessageW(combo, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(L"瓦片数据（XYZ/TMS/WMTS）"));
  SendMessageW(combo, CB_SETCURSEL, 0, 0);
}

void FillConvertSubtypeCombo(HWND combo, int majorType) {
  SendMessageW(combo, CB_RESETCONTENT, 0, 0);
  switch (majorType) {
    case 0:
      SendMessageW(combo, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(L"全部（自动识别）"));
      SendMessageW(combo, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(L"矢量（Shapefile/GeoJSON）"));
      SendMessageW(combo, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(L"栅格（GeoTIFF）"));
      SendMessageW(combo, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(L"空间数据库（GPKG）"));
      break;
    case 1:
      SendMessageW(combo, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(L"TIN（三角网）"));
      SendMessageW(combo, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(L"DEM（高程栅格）"));
      SendMessageW(combo, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(L"3DMesh（网格模型）"));
      SendMessageW(combo, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(L"点云（LAS；LAZ 输出为同路径 LAS + 提示）"));
      break;
    default:
      SendMessageW(combo, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(L"XYZ（金字塔瓦片）"));
      SendMessageW(combo, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(L"TMS（倒序行号）"));
      SendMessageW(combo, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(L"WMTS（服务化瓦片）"));
      break;
  }
  SendMessageW(combo, CB_SETCURSEL, 0, 0);
}

const wchar_t* ConvertTypeTooltipByMajor(int major) {
  switch (major) {
    case 0:
      return L"GIS数据：面向地图生产与分析。\n"
             L"- 矢量：点/线/面要素，属性字段丰富，适合查询与编辑。\n"
             L"- 栅格：像元网格（影像/高程等），适合连续场分析。\n"
             L"- 空间数据库：统一封装多图层与索引（如 GPKG）。";
    case 1:
      return L"模型数据：面向三维/地形表达。\n"
             L"- TIN：不规则三角网，地形边界表达精细。\n"
             L"- DEM：规则高程栅格，适合分析与重采样。\n"
             L"- 3DMesh：三维网格，适合渲染与发布。\n"
             L"- 点云：LAS（颜色 PDRF 2）；可与 3DMesh 互转。";
    default:
      return L"瓦片数据：面向快速显示与分发。\n"
             L"- XYZ：常用 Web 瓦片行列规则。\n"
             L"- TMS：与 XYZ 行号方向不同（Y 轴翻转）。\n"
             L"- WMTS：标准化服务接口，便于平台互操作。";
  }
}

const wchar_t* ConvertSubtypeTooltipByMajorSubtype(int major, int sub) {
  if (major == 0) {
    switch (sub) {
      case 0:
        return L"全部（自动识别）\n"
               L"策略：按输入路径与数据内容自动识别矢量/栅格/容器数据。\n"
               L"适用：来源不确定或批处理场景。";
      case 1:
        return L"矢量（Shapefile/GeoJSON）\n"
               L"文件格式：.shp/.dbf/.shx 或 .geojson\n"
               L"细节：保留要素与属性；注意编码、坐标系与字段长度。";
      case 2:
        return L"栅格（GeoTIFF）\n"
               L"文件格式：.tif/.tiff\n"
               L"细节：支持分辨率、NoData、压缩与金字塔。";
      default:
        return L"空间数据库（GPKG）\n"
               L"文件格式：.gpkg（SQLite 容器）\n"
               L"细节：可存多图层、索引与元数据，便于工程归档。";
    }
  }
  if (major == 1) {
    switch (sub) {
      case 0:
        return L"TIN（三角网）\n"
               L"常见格式：.tin/.obj/.ply（实现可扩展）\n"
               L"细节：保地形突变更好，适合地表重建。";
      case 1:
        return L"DEM（高程栅格）\n"
               L"常见格式：GeoTIFF/ASCII Grid\n"
               L"细节：规则网格，适合坡度/流域等分析。";
      case 2:
        return L"3DMesh（网格模型）\n"
               L"常见格式：Wavefront OBJ 3.0 多边形子集等\n"
               L"细节：可与点云互转；贴图参与 LAS 着色。";
      case 3:
        return L"点云（LAS）\n"
               L"文件格式：.las（PDRF 2 带 RGB）；选 .laz 时输出 .las 并提示需 LASzip 压缩。\n"
               L"细节：可由带 UV 与 map_Kd 的 OBJ+MTL 按贴图像素采样生成。";
      default:
        return L"（未知模型子类型）";
    }
  }
  switch (sub) {
    case 0:
      return L"XYZ（金字塔瓦片）\n"
             L"组织：/{z}/{x}/{y}\n"
             L"细节：与绝大多数 Web 地图兼容。";
    case 1:
      return L"TMS（倒序行号）\n"
             L"组织：/{z}/{x}/{y}，但 Y 轴方向与 XYZ 相反\n"
             L"细节：与部分历史服务兼容时常用。";
    default:
      return L"WMTS（服务化瓦片）\n"
             L"协议：OGC WMTS（KVP/REST）\n"
             L"细节：可声明 TileMatrixSet、样式和能力文档。";
  }
}

const wchar_t* GetConvertTooltipText(HWND dlg, UINT_PTR ctrlId) {
  const int inMajor = static_cast<int>(SendMessageW(GetDlgItem(dlg, IDC_CONV_INPUT_TYPE), CB_GETCURSEL, 0, 0));
  const int outMajor = static_cast<int>(SendMessageW(GetDlgItem(dlg, IDC_CONV_OUTPUT_TYPE), CB_GETCURSEL, 0, 0));
  const int inSub = static_cast<int>(SendMessageW(GetDlgItem(dlg, IDC_CONV_INPUT_SUBTYPE), CB_GETCURSEL, 0, 0));
  const int outSub = static_cast<int>(SendMessageW(GetDlgItem(dlg, IDC_CONV_OUTPUT_SUBTYPE), CB_GETCURSEL, 0, 0));
  switch (ctrlId) {
    case IDC_CONV_INPUT_TYPE:
      return ConvertTypeTooltipByMajor(inMajor < 0 ? 0 : inMajor);
    case IDC_CONV_OUTPUT_TYPE:
      return ConvertTypeTooltipByMajor(outMajor < 0 ? 0 : outMajor);
    case IDC_CONV_INPUT_SUBTYPE:
      return ConvertSubtypeTooltipByMajorSubtype(inMajor < 0 ? 0 : inMajor, inSub < 0 ? 0 : inSub);
    case IDC_CONV_OUTPUT_SUBTYPE:
      return ConvertSubtypeTooltipByMajorSubtype(outMajor < 0 ? 0 : outMajor, outSub < 0 ? 0 : outSub);
    default:
      return L"";
  }
}

void AttachConvertTooltip(HWND dlg, HWND tip, int ctrlId) {
  HWND h = GetDlgItem(dlg, ctrlId);
  if (!h || !tip) {
    return;
  }
  TOOLINFOW ti{};
  ti.cbSize = sizeof(ti);
  ti.uFlags = TTF_IDISHWND | TTF_SUBCLASS;
  ti.hwnd = dlg;
  ti.uId = reinterpret_cast<UINT_PTR>(h);
  ti.lpszText = LPSTR_TEXTCALLBACKW;
  SendMessageW(tip, TTM_ADDTOOLW, 0, reinterpret_cast<LPARAM>(&ti));
}

static std::wstring BuildConvertSideInfoExtras(HWND hwnd, bool inputSide, int majorType);

void ShowConvertHelpDialog(HWND hwnd, bool inputSide, bool typeHelp) {
  const int major = static_cast<int>(SendMessageW(GetDlgItem(hwnd, inputSide ? IDC_CONV_INPUT_TYPE : IDC_CONV_OUTPUT_TYPE),
                                                  CB_GETCURSEL, 0, 0));
  const int sub = static_cast<int>(
      SendMessageW(GetDlgItem(hwnd, inputSide ? IDC_CONV_INPUT_SUBTYPE : IDC_CONV_OUTPUT_SUBTYPE), CB_GETCURSEL, 0, 0));
  const wchar_t* body = typeHelp ? ConvertTypeTooltipByMajor(major < 0 ? 0 : major)
                                 : ConvertSubtypeTooltipByMajorSubtype(major < 0 ? 0 : major, sub < 0 ? 0 : sub);
  const wchar_t* title = nullptr;
  if (inputSide) {
    title = typeHelp ? L"输入类型说明" : L"输入子类型说明";
  } else {
    title = typeHelp ? L"输出类型说明" : L"输出子类型说明";
  }
  MessageBoxW(hwnd, body, title, MB_OK | MB_ICONINFORMATION);
}

void SyncConvertInfoByType(HWND hwnd, bool inputSide) {
  const int typeId = inputSide ? IDC_CONV_INPUT_TYPE : IDC_CONV_OUTPUT_TYPE;
  const int subtypeId = inputSide ? IDC_CONV_INPUT_SUBTYPE : IDC_CONV_OUTPUT_SUBTYPE;
  const int infoId = inputSide ? IDC_CONV_INPUT_INFO : IDC_CONV_OUTPUT_INFO;
  HWND hType = GetDlgItem(hwnd, typeId);
  HWND hSubtype = GetDlgItem(hwnd, subtypeId);
  HWND hInfo = GetDlgItem(hwnd, infoId);
  if (!hType || !hSubtype || !hInfo) {
    return;
  }
  const int sel = static_cast<int>(SendMessageW(hType, CB_GETCURSEL, 0, 0));
  const int t = sel < 0 ? 0 : sel;
  FillConvertSubtypeCombo(hSubtype, t);
  const wchar_t* preset = L"";
  if (t == 0) {
    preset = inputSide ? L"输入路径：\r\n编码：UTF-8\r\n几何类型：自动检测\r\nCRS：自动识别"
                       : L"输出路径：\r\n目标CRS：WebMercator\r\n输出格式：GPKG\r\n精度：中";
  } else if (t == 1) {
    preset = inputSide ? L"输入路径：\r\n高程基准：\r\n网格密度：\r\n缺失值策略：插值"
                       : L"输出路径：\r\n模型格式：3DMesh\r\nLOD：2\r\n压缩：开启";
  } else {
    preset = inputSide ? L"输入源：\r\n级别范围：0-14\r\n切片规则：XYZ/TMS\r\n并发读取：4"
                       : L"输出目录：\r\n切片方案：XYZ\r\n压缩：PNG/JPEG\r\n并发写入：4";
  }
  std::wstring combined = preset;
  combined += BuildConvertSideInfoExtras(hwnd, inputSide, t);
  SetWindowTextW(hInfo, combined.c_str());
}

std::wstring GetComboSelectedText(HWND combo) {
  if (!combo) {
    return L"";
  }
  const int sel = static_cast<int>(SendMessageW(combo, CB_GETCURSEL, 0, 0));
  if (sel < 0) {
    return L"";
  }
  wchar_t buf[256]{};
  SendMessageW(combo, CB_GETLBTEXT, static_cast<WPARAM>(sel), reinterpret_cast<LPARAM>(buf));
  return buf;
}

std::wstring GetConvertMajorTypeArg(HWND hwnd, bool inputSide) {
  const int id = inputSide ? IDC_CONV_INPUT_TYPE : IDC_CONV_OUTPUT_TYPE;
  const int sel = static_cast<int>(SendMessageW(GetDlgItem(hwnd, id), CB_GETCURSEL, 0, 0));
  switch (sel) {
    case 1:
      return L"model";
    case 2:
      return L"tile";
    default:
      return L"gis";
  }
}

std::wstring GetConvertSubtypeArg(HWND hwnd, bool inputSide) {
  const int majorId = inputSide ? IDC_CONV_INPUT_TYPE : IDC_CONV_OUTPUT_TYPE;
  const int subId = inputSide ? IDC_CONV_INPUT_SUBTYPE : IDC_CONV_OUTPUT_SUBTYPE;
  const int major = static_cast<int>(SendMessageW(GetDlgItem(hwnd, majorId), CB_GETCURSEL, 0, 0));
  const int sub = static_cast<int>(SendMessageW(GetDlgItem(hwnd, subId), CB_GETCURSEL, 0, 0));
  if (major == 0) {
    switch (sub) {
      case 1:
        return L"vector";
      case 2:
        return L"raster";
      case 3:
        return L"gpkg";
      default:
        return L"auto";
    }
  }
  if (major == 1) {
    switch (sub) {
      case 1:
        return L"dem";
      case 2:
        return L"3dmesh";
      case 3:
        return L"pointcloud";
      default:
        return L"tin";
    }
  }
  switch (sub) {
    case 1:
      return L"tms";
    case 2:
      return L"wmts";
    default:
      return L"xyz";
  }
}

std::wstring GetModelCoordArg(HWND hwnd) {
  const std::wstring raw = GetComboSelectedText(GetDlgItem(hwnd, IDC_CONV_MODEL_COORD));
  if (raw.find(L"cecf") != std::wstring::npos || raw.find(L"CECF") != std::wstring::npos) {
    return L"cecf";
  }
  return L"projected";
}

std::wstring GetVectorModeArg(HWND hwnd) {
  const std::wstring raw = GetComboSelectedText(GetDlgItem(hwnd, IDC_CONV_VECTOR_MODE));
  if (raw.find(L"贴图") != std::wstring::npos || raw.find(L"texture") != std::wstring::npos) {
    return L"bake_texture";
  }
  return L"geometry";
}

double GetElevHorizRatioArg(HWND hwnd) {
  wchar_t buf[64]{};
  GetWindowTextW(GetDlgItem(hwnd, IDC_CONV_ELEV_HORIZ_RATIO), buf, 64);
  const double v = _wtof(buf);
  if (!std::isfinite(v) || v <= 0.0) {
    return 1.0;
  }
  return v;
}

std::wstring GetTargetCrsArg(HWND hwnd) {
  wchar_t buf[1024]{};
  GetWindowTextW(GetDlgItem(hwnd, IDC_CONV_TARGET_CRS), buf, 1024);
  std::wstring s = buf;
  while (!s.empty() && (s.back() == L' ' || s.back() == L'\t' || s.back() == L'\r' || s.back() == L'\n')) {
    s.pop_back();
  }
  size_t i = 0;
  while (i < s.size() && (s[i] == L' ' || s[i] == L'\t')) {
    ++i;
  }
  if (i > 0) {
    s.erase(0, i);
  }
  return s;
}

std::wstring GetOutputUnitArg(HWND hwnd) {
  const HWND u = GetDlgItem(hwnd, IDC_CONV_OUTPUT_UNIT);
  const int sel = static_cast<int>(SendMessageW(u, CB_GETCURSEL, 0, 0));
  if (sel == 1) {
    return L"km";
  }
  if (sel == 2) {
    return L"1000km";
  }
  return L"m";
}

int GetMeshSpacingArg(HWND hwnd);
std::wstring GetTextureFormatArg(HWND hwnd);
int GetRasterReadMaxDimArg(HWND hwnd);

void RefreshConvertSettingPanels(HWND hwnd) {
  const std::wstring inType = GetConvertMajorTypeArg(hwnd, true);
  const std::wstring inSub = GetConvertSubtypeArg(hwnd, true);
  const std::wstring outType = GetConvertMajorTypeArg(hwnd, false);
  const std::wstring outSub = GetConvertSubtypeArg(hwnd, false);
  wchar_t inPath[512]{};
  wchar_t outPath[512]{};
  GetWindowTextW(GetDlgItem(hwnd, IDC_CONV_INPUT_PATH), inPath, 512);
  GetWindowTextW(GetDlgItem(hwnd, IDC_CONV_OUTPUT_PATH), outPath, 512);

  const std::wstring inputText =
      L"input-type=" + inType + L"\r\n"
      L"input-subtype=" + inSub + L"\r\n"
      L"input-path=" + std::wstring(inPath) + L"\r\n";

  const std::wstring outputText =
      L"output-type=" + outType + L"\r\n"
      L"output-subtype=" + outSub + L"\r\n"
      L"coord-system=" + GetModelCoordArg(hwnd) + L"\r\n"
      L"vector-mode=" + GetVectorModeArg(hwnd) + L"\r\n"
      L"elev-horiz-ratio=" + std::to_wstring(GetElevHorizRatioArg(hwnd)) + L"\r\n"
      L"target-crs=" + GetTargetCrsArg(hwnd) + L"\r\n"
      L"output-unit=" + GetOutputUnitArg(hwnd) + L"\r\n"
      L"mesh-spacing=" + std::to_wstring(GetMeshSpacingArg(hwnd)) + L"\r\n"
      L"texture-format=" + GetTextureFormatArg(hwnd) + L"\r\n"
      L"raster-max-dim=" + std::to_wstring(GetRasterReadMaxDimArg(hwnd)) + L"\r\n"
      L"output-path=" + std::wstring(outPath) + L"\r\n";

  SetWindowTextW(GetDlgItem(hwnd, IDC_CONV_INPUT_SETTING), inputText.c_str());
  SetWindowTextW(GetDlgItem(hwnd, IDC_CONV_OUTPUT_SETTING), outputText.c_str());
}

#if GIS_DESKTOP_HAVE_GDAL
static std::string NarrowUtf8FromWide(const std::wstring& w) {
  if (w.empty()) return {};
  const int n =
      WideCharToMultiByte(CP_UTF8, 0, w.c_str(), static_cast<int>(w.size()), nullptr, 0, nullptr, nullptr);
  if (n <= 0) return {};
  std::string s(static_cast<size_t>(n), '\0');
  WideCharToMultiByte(CP_UTF8, 0, w.c_str(), static_cast<int>(w.size()), s.data(), n, nullptr, nullptr);
  return s;
}

static std::wstring WidePrefixFromUtf8(const char* u8, size_t maxLen) {
  if (!u8 || !u8[0]) return L"";
  const int wn = MultiByteToWideChar(CP_UTF8, 0, u8, -1, nullptr, 0);
  if (wn <= 0) return L"";
  std::wstring ws(static_cast<size_t>(wn - 1), L'\0');
  MultiByteToWideChar(CP_UTF8, 0, u8, -1, ws.data(), wn);
  if (ws.size() > maxLen) {
    return ws.substr(0, maxLen) + L"…";
  }
  return ws;
}
#endif

static void AppendAgisGisSummaryForConvert(std::wstring* s, const std::wstring& path) {
  std::wifstream ifs(path);
  if (!ifs) {
    *s += L"\r\n\r\n【.gis】无法读取文件。";
    return;
  }
  std::wstringstream wss;
  wss << ifs.rdbuf();
  const std::wstring xml = wss.str();
  if (xml.find(L"<agis-gis") == std::wstring::npos) {
    *s += L"\r\n\r\n【.gis】根节点不是 <agis-gis>。";
    return;
  }
  int projIdx = ParseIntAttr(xml, L"projection", 0);
  if (projIdx < 0 || projIdx >= static_cast<int>(MapDisplayProjection::kCount)) {
    projIdx = 0;
  }
  const auto mp = static_cast<MapDisplayProjection>(projIdx);
  const bool grid = ParseBoolAttr(xml, L"showGrid", true);
  const double mnX = ParseDoubleAttr(xml, L"viewMinX", 0);
  const double mnY = ParseDoubleAttr(xml, L"viewMinY", 0);
  const double mxX = ParseDoubleAttr(xml, L"viewMaxX", 0);
  const double mxY = ParseDoubleAttr(xml, L"viewMaxY", 0);

  int layerCount = 0;
  size_t scan = 0;
  while ((scan = xml.find(L"<layer ", scan)) != std::wstring::npos) {
    ++layerCount;
    scan += 7;
  }

  *s += L"\r\n\r\n【.gis 文档摘要】\r\n";
  *s += L"显示投影（2D 视图）：";
  *s += MapProj_MenuLabel(mp);
  *s += L"\r\n经纬网：";
  *s += grid ? L"显示" : L"隐藏";
  wchar_t vb[384]{};
  swprintf_s(vb, L"\r\n视口范围：[%.8g , %.8g] — [%.8g , %.8g]", mnX, mnY, mxX, mxY);
  *s += vb;
  *s += L"\r\n图层数量：";
  *s += std::to_wstring(layerCount);

  scan = 0;
  int listed = 0;
  while ((scan = xml.find(L"<layer ", scan)) != std::wstring::npos && listed < 10) {
    const size_t end = xml.find(L"/>", scan);
    if (end == std::wstring::npos) break;
    const std::wstring line = xml.substr(scan, end - scan + 2);
    scan = end + 2;
    ++listed;
    std::wstring name = GetXmlAttr(line, L"name");
    std::wstring driver = GetXmlAttr(line, L"driver");
    std::wstring src = GetXmlAttr(line, L"source");
    if (src.size() > 56) {
      src = src.substr(0, 53) + L"...";
    }
    *s += L"\r\n  · ";
    *s += name.empty() ? L"（未命名）" : name;
    *s += L"  |  ";
    *s += driver.empty() ? L"?" : driver;
    *s += L"\r\n    ";
    *s += src.empty() ? L"（无 source）" : src;
  }
  if (layerCount > listed) {
    *s += L"\r\n  … 另有 ";
    *s += std::to_wstring(layerCount - listed);
    *s += L" 个图层";
  }
}

#if GIS_DESKTOP_HAVE_GDAL
static void AppendGdalSummaryForConvert(std::wstring* s, const std::wstring& path) {
  static bool registered = false;
  if (!registered) {
    GDALAllRegister();
    registered = true;
  }
  const std::string u8 = NarrowUtf8FromWide(path);
  if (u8.empty()) {
    return;
  }
  GDALDatasetH ds = GDALOpenEx(u8.c_str(), GDAL_OF_READONLY | GDAL_OF_RASTER | GDAL_OF_VECTOR, nullptr, nullptr, nullptr);
  if (!ds) {
    *s += L"\r\n\r\n【GDAL】无法打开该路径（格式、驱动或权限）。";
    return;
  }
  *s += L"\r\n\r\n【GDAL 数据源摘要】\r\n";
  const int rx = GDALGetRasterXSize(ds);
  const int ry = GDALGetRasterYSize(ds);
  if (rx > 0 && ry > 0) {
    *s += L"栅格尺寸：";
    *s += std::to_wstring(rx);
    *s += L" × ";
    *s += std::to_wstring(ry);
    const int bands = GDALGetRasterCount(ds);
    if (bands > 0) {
      *s += L"  波段数：";
      *s += std::to_wstring(bands);
    }
    *s += L"\r\n";
  }
  const char* wkt = GDALGetProjectionRef(ds);
  if (wkt && wkt[0]) {
    *s += L"数据集投影(WKT 前缀)：";
    *s += WidePrefixFromUtf8(wkt, 220);
    *s += L"\r\n";
  } else {
    *s += L"数据集投影：未设置或未知\r\n";
  }
  const int nVec = GDALDatasetGetLayerCount(ds);
  if (nVec > 0) {
    *s += L"矢量图层数：";
    *s += std::to_wstring(nVec);
    *s += L"\r\n";
    for (int li = 0; li < nVec && li < 8; ++li) {
      OGRLayerH lyr = GDALDatasetGetLayer(ds, li);
      if (!lyr) continue;
      *s += L"  · ";
      const char* nm = OGR_L_GetName(lyr);
      *s += nm ? WidePrefixFromUtf8(nm, 120) : L"（未命名）";
      *s += L"\r\n";
    }
    if (nVec > 8) {
      *s += L"  … 另有 ";
      *s += std::to_wstring(nVec - 8);
      *s += L" 个矢量图层\r\n";
    }
  }
  GDALClose(ds);
}
#endif

static std::wstring BuildConvertSideInfoExtras(HWND hwnd, bool inputSide, int majorType) {
  (void)majorType;
  std::wstring x;
  const int pathCtl = inputSide ? IDC_CONV_INPUT_PATH : IDC_CONV_OUTPUT_PATH;
  wchar_t pathBuf[4096]{};
  GetWindowTextW(GetDlgItem(hwnd, pathCtl), pathBuf, 4096);
  std::wstring path = pathBuf;
  while (!path.empty() && (path.back() == L' ' || path.back() == L'\t' || path.back() == L'\r' || path.back() == L'\n')) {
    path.pop_back();
  }

  if (inputSide) {
    x += L"\r\n────────\r\n【当前输入路径】\r\n";
    x += path.empty() ? L"（未选择文件或数据源）" : path;
    if (path.empty()) {
      return x;
    }
    std::wstring ext = std::filesystem::path(path).extension().wstring();
    for (auto& ch : ext) ch = static_cast<wchar_t>(std::towlower(ch));
    if (ext == L".gis") {
      AppendAgisGisSummaryForConvert(&x, path);
    } else {
#if GIS_DESKTOP_HAVE_GDAL
      AppendGdalSummaryForConvert(&x, path);
#else
      x += L"\r\n\r\n（未启用 GDAL：无法自动读取单层文件的 CRS/尺寸；扩展名：";
      x += ext.empty() ? L"无" : ext;
      x += L"）\r\n";
#endif
    }
    return x;
  }

  x += L"\r\n────────\r\n【输出路径】\r\n";
  x += path.empty() ? L"（未指定）" : path;
  x += L"\r\n\r\n【中间区转换参数】\r\n目标 CRS（空表示自动）：";
  {
    const std::wstring crs = GetTargetCrsArg(hwnd);
    x += crs.empty() ? L"（空）" : crs;
  }
  x += L"\r\n模型顶点坐标语义：";
  x += GetComboSelectedText(GetDlgItem(hwnd, IDC_CONV_MODEL_COORD));
  x += L"\r\n矢量参与转换策略：";
  x += GetComboSelectedText(GetDlgItem(hwnd, IDC_CONV_VECTOR_MODE));
  x += L"\r\n输出模型单位：";
  x += GetOutputUnitArg(hwnd);
  wchar_t ms[64]{};
  GetWindowTextW(GetDlgItem(hwnd, IDC_CONV_MESH_SPACING), ms, 64);
  x += L"\r\nMesh 间距（模型单位）：";
  x += ms;
  wchar_t eh[64]{};
  GetWindowTextW(GetDlgItem(hwnd, IDC_CONV_ELEV_HORIZ_RATIO), eh, 64);
  x += L"\r\n高程/水平比例：";
  x += eh;
  x += L"\r\n模型贴图格式（点云→模型等）：";
  x += GetComboSelectedText(GetDlgItem(hwnd, IDC_CONV_TEXTURE_FORMAT));
  wchar_t rzb[32]{};
  GetWindowTextW(GetDlgItem(hwnd, IDC_CONV_RASTER_MAX), rzb, 32);
  x += L"\r\n栅格读入最大边(px)（GIS→模型贴图源）：";
  x += (rzb[0] ? rzb : L"4096");
  return x;
}

int GetMeshSpacingArg(HWND hwnd) {
  wchar_t buf[32]{};
  GetWindowTextW(GetDlgItem(hwnd, IDC_CONV_MESH_SPACING), buf, 32);
  const long v = wcstol(buf, nullptr, 10);
  if (v < 1) {
    return 1;
  }
  if (v > 1000000) {
    return 1000000;
  }
  return static_cast<int>(v);
}

std::wstring GetTextureFormatArg(HWND hwnd) {
  const std::wstring t = GetComboSelectedText(GetDlgItem(hwnd, IDC_CONV_TEXTURE_FORMAT));
  if (t.find(L"TIFF") != std::wstring::npos || t.find(L"tiff") != std::wstring::npos ||
      t.find(L"Tif") != std::wstring::npos) {
    return L"tif";
  }
  if (t.find(L"TGA") != std::wstring::npos || t.find(L"tga") != std::wstring::npos) {
    return L"tga";
  }
  if (t.find(L"BMP") != std::wstring::npos || t.find(L"bmp") != std::wstring::npos) {
    return L"bmp";
  }
  return L"png";
}

void FillConvertTextureFormatCombo(HWND combo) {
  if (!combo) {
    return;
  }
  SendMessageW(combo, CB_RESETCONTENT, 0, 0);
  SendMessageW(combo, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(L"PNG（默认）"));
  SendMessageW(combo, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(L"TIFF / GeoTIFF"));
  SendMessageW(combo, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(L"TGA"));
  SendMessageW(combo, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(L"BMP"));
  SendMessageW(combo, CB_SETCURSEL, 0, 0);
}

int GetRasterReadMaxDimArg(HWND hwnd) {
  wchar_t buf[32]{};
  if (HWND e = GetDlgItem(hwnd, IDC_CONV_RASTER_MAX)) {
    GetWindowTextW(e, buf, 32);
  }
  const long v = wcstol(buf, nullptr, 10);
  if (v < 64 || v > 16384) {
    return 4096;
  }
  return static_cast<int>(v);
}

std::wstring PromptOpenInputPath(HWND owner) {
  wchar_t path[MAX_PATH]{};
  OPENFILENAMEW ofn{};
  ofn.lStructSize = sizeof(ofn);
  ofn.hwndOwner = owner;
  ofn.lpstrFilter = L"所有文件 (*.*)\0*.*\0";
  ofn.lpstrFile = path;
  ofn.nMaxFile = MAX_PATH;
  ofn.Flags = OFN_FILEMUSTEXIST | OFN_PATHMUSTEXIST;
  return GetOpenFileNameW(&ofn) ? std::wstring(path) : L"";
}

std::wstring PromptSaveOutputPath(HWND owner) {
  wchar_t path[MAX_PATH]{};
  OPENFILENAMEW ofn{};
  ofn.lStructSize = sizeof(ofn);
  ofn.hwndOwner = owner;
  ofn.lpstrFilter = L"所有文件 (*.*)\0*.*\0";
  ofn.lpstrFile = path;
  ofn.nMaxFile = MAX_PATH;
  ofn.Flags = OFN_OVERWRITEPROMPT | OFN_PATHMUSTEXIST;
  return GetSaveFileNameW(&ofn) ? std::wstring(path) : L"";
}

std::wstring PromptSelectOutputFolder(HWND owner) {
  BROWSEINFOW bi{};
  bi.hwndOwner = owner;
  bi.lpszTitle = L"选择输出目录";
  bi.ulFlags = BIF_RETURNONLYFSDIRS | BIF_NEWDIALOGSTYLE;
  PIDLIST_ABSOLUTE pidl = SHBrowseForFolderW(&bi);
  if (!pidl) {
    return L"";
  }
  wchar_t folder[MAX_PATH]{};
  const BOOL ok = SHGetPathFromIDListW(pidl, folder);
  CoTaskMemFree(pidl);
  return ok ? std::wstring(folder) : L"";
}

const wchar_t* ConvertToolExeName(int inMajor, int outMajor) {
  if (inMajor == 1 && outMajor == 1) return L"agis_convert_model_to_model.exe";
  if (inMajor == 0 && outMajor == 1) return L"agis_convert_gis_to_model.exe";
  if (inMajor == 0 && outMajor == 2) return L"agis_convert_gis_to_tile.exe";
  if (inMajor == 1 && outMajor == 0) return L"agis_convert_model_to_gis.exe";
  if (inMajor == 1 && outMajor == 2) return L"agis_convert_model_to_tile.exe";
  if (inMajor == 2 && outMajor == 0) return L"agis_convert_tile_to_gis.exe";
  if (inMajor == 2 && outMajor == 1) return L"agis_convert_tile_to_model.exe";
  return nullptr;
}

std::wstring QuoteArg(const std::wstring& s) {
  std::wstring out = L"\"";
  for (wchar_t ch : s) {
    if (ch == L'"') {
      out += L"\\\"";
    } else {
      out.push_back(ch);
    }
  }
  out += L"\"";
  return out;
}

std::wstring BuildConvertCommandLine(HWND hwnd) {
  const int inMajor = static_cast<int>(SendMessageW(GetDlgItem(hwnd, IDC_CONV_INPUT_TYPE), CB_GETCURSEL, 0, 0));
  const int outMajor = static_cast<int>(SendMessageW(GetDlgItem(hwnd, IDC_CONV_OUTPUT_TYPE), CB_GETCURSEL, 0, 0));
  const wchar_t* exeName = ConvertToolExeName(inMajor, outMajor);
  wchar_t modulePath[MAX_PATH]{};
  GetModuleFileNameW(nullptr, modulePath, MAX_PATH);
  std::wstring exeDir = modulePath;
  const size_t slash = exeDir.find_last_of(L"\\/");
  if (slash != std::wstring::npos) {
    exeDir.resize(slash + 1);
  }
  const std::wstring exePath = exeName ? (exeDir + exeName) : L"<请选择不同输入/输出类型>";
  wchar_t inPath[1024]{};
  wchar_t outPath[1024]{};
  GetWindowTextW(GetDlgItem(hwnd, IDC_CONV_INPUT_PATH), inPath, 1024);
  GetWindowTextW(GetDlgItem(hwnd, IDC_CONV_OUTPUT_PATH), outPath, 1024);
  const std::wstring inType = GetConvertMajorTypeArg(hwnd, true);
  const std::wstring inSub = GetConvertSubtypeArg(hwnd, true);
  const std::wstring outType = GetConvertMajorTypeArg(hwnd, false);
  const std::wstring outSub = GetConvertSubtypeArg(hwnd, false);
  const std::wstring coord = GetModelCoordArg(hwnd);
  const std::wstring vectorMode = GetVectorModeArg(hwnd);
  const double elevRatio = GetElevHorizRatioArg(hwnd);
  wchar_t ratioStr[64]{};
  swprintf_s(ratioStr, L"%.12g", elevRatio);
  const std::wstring outUnit = GetOutputUnitArg(hwnd);
  const std::wstring tcr = GetTargetCrsArg(hwnd);
  const int meshSp = GetMeshSpacingArg(hwnd);
  wchar_t meshStr[32]{};
  swprintf_s(meshStr, L"%d", meshSp);
  const std::wstring texFmt = GetTextureFormatArg(hwnd);
  const int rmax = GetRasterReadMaxDimArg(hwnd);
  wchar_t rmaxStr[32]{};
  swprintf_s(rmaxStr, L"%d", rmax);
  std::wstring tail = L"\r\n"
                      L"  --coord-system " + QuoteArg(coord) + L"\r\n"
                      L"  --vector-mode " + QuoteArg(vectorMode) + L"\r\n"
                      L"  --elev-horiz-ratio " + QuoteArg(ratioStr) + L"\r\n"
                      L"  --output-unit " + QuoteArg(outUnit) + L"\r\n"
                      L"  --mesh-spacing " + QuoteArg(meshStr) + L"\r\n"
                      L"  --texture-format " + QuoteArg(texFmt) + L"\r\n"
                      L"  --raster-max-dim " + QuoteArg(rmaxStr);
  if (!tcr.empty()) {
    tail += L"\r\n  --target-crs " + QuoteArg(tcr);
  }
  return L"命令行:\r\n" + QuoteArg(exePath) + L"\r\n"
         L"  --input " + QuoteArg(inPath) + L"\r\n"
         L"  --output " + QuoteArg(outPath) + L"\r\n"
         L"  --input-type " + QuoteArg(inType) + L"\r\n"
         L"  --input-subtype " + QuoteArg(inSub) + L"\r\n"
         L"  --output-type " + QuoteArg(outType) + L"\r\n"
         L"  --output-subtype " + QuoteArg(outSub) + tail;
}

void UpdateConvertCmdlinePreview(HWND hwnd) {
  SetWindowTextW(GetDlgItem(hwnd, IDC_CONV_CMDLINE), BuildConvertCommandLine(hwnd).c_str());
}
bool RunConvertBackendAsync(HWND hwnd) {
  const int inMajor = static_cast<int>(SendMessageW(GetDlgItem(hwnd, IDC_CONV_INPUT_TYPE), CB_GETCURSEL, 0, 0));
  const int outMajor = static_cast<int>(SendMessageW(GetDlgItem(hwnd, IDC_CONV_OUTPUT_TYPE), CB_GETCURSEL, 0, 0));
  if (inMajor < 0 || outMajor < 0) {
    MessageBoxW(hwnd, L"请先选择输入与输出类型。", L"数据转换", MB_OK | MB_ICONWARNING);
    return false;
  }
  if (inMajor == outMajor) {
    if (inMajor != 1) {
      MessageBoxW(hwnd, L"同类型转换当前仅支持「模型数据」下 3DMesh 与点云子类型互转。", L"数据转换",
                  MB_OK | MB_ICONWARNING);
      return false;
    }
    const std::wstring inSu = GetConvertSubtypeArg(hwnd, true);
    const std::wstring outSu = GetConvertSubtypeArg(hwnd, false);
    const auto isPc = [](const std::wstring& s) { return s == L"pointcloud" || s == L"las" || s == L"laz"; };
    const auto isM3 = [](const std::wstring& s) { return s == L"3dmesh" || s == L"mesh"; };
    if (!((isM3(inSu) && isPc(outSu)) || (isPc(inSu) && isM3(outSu)))) {
      MessageBoxW(hwnd, L"请选择：一侧为「3DMesh（网格模型）」、另一侧为「点云（LAS…）」。", L"数据转换",
                  MB_OK | MB_ICONWARNING);
      return false;
    }
  }
  wchar_t inPath[1024]{};
  wchar_t outPath[1024]{};
  GetWindowTextW(GetDlgItem(hwnd, IDC_CONV_INPUT_PATH), inPath, 1024);
  GetWindowTextW(GetDlgItem(hwnd, IDC_CONV_OUTPUT_PATH), outPath, 1024);
  if (inPath[0] == L'\0' || outPath[0] == L'\0') {
    MessageBoxW(hwnd, L"请先设置输入和输出路径。", L"数据转换", MB_OK | MB_ICONWARNING);
    return false;
  }
  const wchar_t* exeName = ConvertToolExeName(inMajor, outMajor);
  if (!exeName) {
    return false;
  }
  wchar_t modulePath[MAX_PATH]{};
  GetModuleFileNameW(nullptr, modulePath, MAX_PATH);
  std::wstring exeDir = modulePath;
  const size_t slash = exeDir.find_last_of(L"\\/");
  if (slash != std::wstring::npos) {
    exeDir.resize(slash + 1);
  }
  const std::wstring exePath = exeDir + exeName;
  const std::wstring inType = GetConvertMajorTypeArg(hwnd, true);
  const std::wstring inSub = GetConvertSubtypeArg(hwnd, true);
  const std::wstring outType = GetConvertMajorTypeArg(hwnd, false);
  const std::wstring outSub = GetConvertSubtypeArg(hwnd, false);
  const std::wstring coord = GetModelCoordArg(hwnd);
  const std::wstring vectorMode = GetVectorModeArg(hwnd);
  const double elevRatio = GetElevHorizRatioArg(hwnd);
  wchar_t ratioStr[64]{};
  swprintf_s(ratioStr, L"%.12g", elevRatio);
  const std::wstring outUnit = GetOutputUnitArg(hwnd);
  const std::wstring tcr = GetTargetCrsArg(hwnd);
  const int meshSp = GetMeshSpacingArg(hwnd);
  wchar_t meshStr[32]{};
  swprintf_s(meshStr, L"%d", meshSp);
  const std::wstring texFmt = GetTextureFormatArg(hwnd);
  const int rmax = GetRasterReadMaxDimArg(hwnd);
  wchar_t rmaxStr[32]{};
  swprintf_s(rmaxStr, L"%d", rmax);
  std::wstring cmd = QuoteArg(exePath) + L" --input " + QuoteArg(inPath) + L" --output " + QuoteArg(outPath) +
                     L" --input-type " + QuoteArg(inType) + L" --input-subtype " + QuoteArg(inSub) +
                     L" --output-type " + QuoteArg(outType) + L" --output-subtype " + QuoteArg(outSub) +
                     L" --coord-system " + QuoteArg(coord) + L" --vector-mode " + QuoteArg(vectorMode) +
                     L" --elev-horiz-ratio " + QuoteArg(ratioStr) + L" --output-unit " + QuoteArg(outUnit) +
                     L" --mesh-spacing " + QuoteArg(meshStr) + L" --texture-format " + QuoteArg(texFmt) +
                     L" --raster-max-dim " + QuoteArg(rmaxStr);
  if (!tcr.empty()) {
    cmd += L" --target-crs " + QuoteArg(tcr);
  }
  UpdateConvertCmdlinePreview(hwnd);
  WriteConvertLog(hwnd, (std::wstring(L"[命令] ") + cmd).c_str());
  std::vector<wchar_t> cmdBuf(cmd.begin(), cmd.end());
  cmdBuf.push_back(L'\0');
  STARTUPINFOW si{};
  si.cb = sizeof(si);
  PROCESS_INFORMATION pi{};
  if (!CreateProcessW(nullptr, cmdBuf.data(), nullptr, nullptr, FALSE, CREATE_NO_WINDOW, nullptr, nullptr, &si, &pi)) {
    WriteConvertLog(hwnd, (std::wstring(L"[错误] 无法启动：") + exeName).c_str());
    return false;
  }
  g_convertPi = pi;
  g_convertRunning = true;
  SetTimer(hwnd, kConvertPollTimerId, 120, nullptr);
  return true;
}

void PreviewPath(HWND hwnd, bool inputSide) {
  const int ctrl = inputSide ? IDC_CONV_INPUT_PATH : IDC_CONV_OUTPUT_PATH;
  const int majorCtrl = inputSide ? IDC_CONV_INPUT_TYPE : IDC_CONV_OUTPUT_TYPE;
  wchar_t pathBuf[1024]{};
  GetWindowTextW(GetDlgItem(hwnd, ctrl), pathBuf, 1024);
  std::wstring path = pathBuf;
  if (path.empty()) {
    MessageBoxW(hwnd, L"请先设置路径。", L"预览", MB_OK | MB_ICONINFORMATION);
    return;
  }
  DWORD attr = GetFileAttributesW(path.c_str());
  if (attr == INVALID_FILE_ATTRIBUTES) {
    const std::wstring msg = std::wstring(L"路径不存在：\n") + path;
    MessageBoxW(hwnd, msg.c_str(), L"预览", MB_OK | MB_ICONWARNING);
    return;
  }
  const int major = static_cast<int>(SendMessageW(GetDlgItem(hwnd, majorCtrl), CB_GETCURSEL, 0, 0));
  HMENU menu = CreatePopupMenu();
  AppendMenuW(menu, MF_STRING, 1, L"内置预览（默认）");
  AppendMenuW(menu, MF_STRING, 2, L"系统默认打开");
  AppendMenuW(menu, MF_STRING, 3, L"选择其他应用...");
  SetMenuDefaultItem(menu, 1, FALSE);
  POINT pt{};
  GetCursorPos(&pt);
  const UINT chosen = TrackPopupMenu(menu, TPM_RETURNCMD | TPM_RIGHTBUTTON, pt.x, pt.y, 0, hwnd, nullptr);
  DestroyMenu(menu);
  if (chosen == 0) {
    return;
  }
  if (chosen == 1) {
    if (major == 1 && (attr & FILE_ATTRIBUTE_DIRECTORY) == 0) {
      OpenModelPreviewWindow(hwnd, path);
      WriteConvertLog(hwnd, (std::wstring(inputSide ? L"[预览] 输入(内置3D)：" : L"[预览] 输出(内置3D)：") + path).c_str());
      return;
    }
    HINSTANCE h = ShellExecuteW(hwnd, L"open", path.c_str(), nullptr, nullptr, SW_SHOWNORMAL);
    if (reinterpret_cast<INT_PTR>(h) <= 32) {
      MessageBoxW(hwnd, L"无法打开预览目标。", L"预览", MB_OK | MB_ICONWARNING);
      return;
    }
  } else if (chosen == 2) {
    HINSTANCE h = ShellExecuteW(hwnd, L"open", path.c_str(), nullptr, nullptr, SW_SHOWNORMAL);
    if (reinterpret_cast<INT_PTR>(h) <= 32) {
      MessageBoxW(hwnd, L"无法用系统默认方式打开。", L"预览", MB_OK | MB_ICONWARNING);
      return;
    }
  } else {
    const std::wstring params = L"shell32.dll,OpenAs_RunDLL " + QuoteArg(path);
    HINSTANCE h = ShellExecuteW(hwnd, L"open", L"rundll32.exe", params.c_str(), nullptr, SW_SHOWNORMAL);
    if (reinterpret_cast<INT_PTR>(h) <= 32) {
      MessageBoxW(hwnd, L"无法打开“选择其他应用”。", L"预览", MB_OK | MB_ICONWARNING);
      return;
    }
  }
  WriteConvertLog(hwnd, (std::wstring(inputSide ? L"[预览] 输入：" : L"[预览] 输出：") + path).c_str());
}

void ShowDataConvertWindow(HWND owner) {
  if (g_hwndConvertDlg && IsWindow(g_hwndConvertDlg)) {
    ShowWindow(g_hwndConvertDlg, SW_MAXIMIZE);
    SetForegroundWindow(g_hwndConvertDlg);
    return;
  }
  g_hwndConvertDlg = CreateWindowExW(WS_EX_DLGMODALFRAME, kConvertClass, L"数据转换",
                                     WS_OVERLAPPEDWINDOW | WS_VISIBLE, CW_USEDEFAULT, CW_USEDEFAULT, 920, 620, owner,
                                     nullptr, GetModuleHandleW(nullptr), nullptr);
  if (g_hwndConvertDlg) {
    ShowWindow(g_hwndConvertDlg, SW_MAXIMIZE);
  }
}

LRESULT CALLBACK ConvertWndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) {
  static HBRUSH s_bgLight = nullptr;
  static HBRUSH s_editLight = nullptr;
  static HBRUSH s_bgDark = nullptr;
  static HBRUSH s_editDark = nullptr;
  static HWND s_tip = nullptr;
  switch (msg) {
    case WM_CREATE: {
      SendMessageW(hwnd, WM_SETFONT, reinterpret_cast<WPARAM>(UiGetAppFont()), TRUE);
      CreateWindowW(L"STATIC", L"输入数据", WS_CHILD | WS_VISIBLE, 10, 8, 120, 16, hwnd, nullptr,
                    GetModuleHandleW(nullptr), nullptr);
      HWND inType = CreateWindowW(L"COMBOBOX", L"", WS_CHILD | WS_VISIBLE | CBS_DROPDOWNLIST | WS_VSCROLL, 10, 30, 240,
                                  240, hwnd, reinterpret_cast<HMENU>(static_cast<INT_PTR>(IDC_CONV_INPUT_TYPE)),
                                  GetModuleHandleW(nullptr), nullptr);
      CreateWindowW(L"BUTTON", L"?", WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON, 226, 30, 24, 24, hwnd,
                    reinterpret_cast<HMENU>(static_cast<INT_PTR>(IDC_CONV_INPUT_TYPE_HELP)), GetModuleHandleW(nullptr),
                    nullptr);
      CreateWindowW(L"COMBOBOX", L"", WS_CHILD | WS_VISIBLE | CBS_DROPDOWNLIST | WS_VSCROLL, 10, 58, 240, 220, hwnd,
                    reinterpret_cast<HMENU>(static_cast<INT_PTR>(IDC_CONV_INPUT_SUBTYPE)),
                    GetModuleHandleW(nullptr), nullptr);
      CreateWindowW(L"BUTTON", L"?", WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON, 226, 58, 24, 24, hwnd,
                    reinterpret_cast<HMENU>(static_cast<INT_PTR>(IDC_CONV_INPUT_SUBTYPE_HELP)),
                    GetModuleHandleW(nullptr), nullptr);
      CreateWindowW(L"EDIT", L"", WS_CHILD | WS_VISIBLE | WS_BORDER | ES_AUTOHSCROLL, 10, 86, 172, 24, hwnd,
                    reinterpret_cast<HMENU>(static_cast<INT_PTR>(IDC_CONV_INPUT_PATH)), GetModuleHandleW(nullptr),
                    nullptr);
      CreateWindowW(L"BUTTON", L"浏览", WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON, 186, 86, 68, 24, hwnd,
                    reinterpret_cast<HMENU>(static_cast<INT_PTR>(IDC_CONV_INPUT_BROWSE)), GetModuleHandleW(nullptr),
                    nullptr);
      CreateWindowW(L"BUTTON", L"预览", WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON, 258, 86, 70, 24, hwnd,
                    reinterpret_cast<HMENU>(static_cast<INT_PTR>(IDC_CONV_INPUT_PREVIEW)), GetModuleHandleW(nullptr),
                    nullptr);
      CreateWindowW(L"EDIT", L"", WS_CHILD | WS_VISIBLE | WS_BORDER | ES_MULTILINE | ES_AUTOVSCROLL | WS_VSCROLL, 10,
                    116, 240, 200, hwnd, reinterpret_cast<HMENU>(static_cast<INT_PTR>(IDC_CONV_INPUT_INFO)),
                    GetModuleHandleW(nullptr), nullptr);

      CreateWindowW(L"STATIC", L"输入参数（来源）", WS_CHILD | WS_VISIBLE, 280, 8, 140, 16, hwnd,
                    reinterpret_cast<HMENU>(static_cast<INT_PTR>(IDC_CONV_INPUT_SETTING_LBL)),
                    GetModuleHandleW(nullptr), nullptr);
      CreateWindowW(L"EDIT", L"",
                    WS_CHILD | WS_VISIBLE | WS_BORDER | ES_MULTILINE | ES_AUTOVSCROLL | WS_VSCROLL | ES_READONLY, 280,
                    30, 240, 90, hwnd, reinterpret_cast<HMENU>(static_cast<INT_PTR>(IDC_CONV_INPUT_SETTING)),
                    GetModuleHandleW(nullptr), nullptr);
      CreateWindowW(L"STATIC", L"输出参数（目标）", WS_CHILD | WS_VISIBLE, 280, 126, 140, 16, hwnd,
                    reinterpret_cast<HMENU>(static_cast<INT_PTR>(IDC_CONV_OUTPUT_SETTING_LBL)),
                    GetModuleHandleW(nullptr), nullptr);
      CreateWindowW(L"EDIT", L"",
                    WS_CHILD | WS_VISIBLE | WS_BORDER | ES_MULTILINE | ES_AUTOVSCROLL | WS_VSCROLL | ES_READONLY, 280,
                    146, 240, 90, hwnd, reinterpret_cast<HMENU>(static_cast<INT_PTR>(IDC_CONV_OUTPUT_SETTING)),
                    GetModuleHandleW(nullptr), nullptr);
      CreateWindowW(L"STATIC", L"高程/水平比（1 = 1:1）：", WS_CHILD | WS_VISIBLE, 280, 238, 200, 18, hwnd,
                    reinterpret_cast<HMENU>(static_cast<INT_PTR>(IDC_CONV_ELEV_HORIZ_LBL)), GetModuleHandleW(nullptr),
                    nullptr);
      CreateWindowW(L"EDIT", L"1", WS_CHILD | WS_VISIBLE | WS_BORDER | ES_AUTOHSCROLL, 420, 236, 98, 22, hwnd,
                    reinterpret_cast<HMENU>(static_cast<INT_PTR>(IDC_CONV_ELEV_HORIZ_RATIO)), GetModuleHandleW(nullptr),
                    nullptr);
      CreateWindowW(L"STATIC", L"目标坐标系（留空=自动）：", WS_CHILD | WS_VISIBLE, 280, 264, 240, 16, hwnd,
                    reinterpret_cast<HMENU>(static_cast<INT_PTR>(IDC_CONV_TARGET_CRS_LBL)), GetModuleHandleW(nullptr),
                    nullptr);
      CreateWindowW(L"EDIT", L"", WS_CHILD | WS_VISIBLE | WS_BORDER | ES_AUTOHSCROLL, 280, 282, 240, 22, hwnd,
                    reinterpret_cast<HMENU>(static_cast<INT_PTR>(IDC_CONV_TARGET_CRS)), GetModuleHandleW(nullptr),
                    nullptr);
      CreateWindowW(L"STATIC", L"输出模型单位：", WS_CHILD | WS_VISIBLE, 280, 310, 88, 18, hwnd,
                    reinterpret_cast<HMENU>(static_cast<INT_PTR>(IDC_CONV_OUTPUT_UNIT_LBL)), GetModuleHandleW(nullptr),
                    nullptr);
      HWND outUnitCombo =
          CreateWindowW(L"COMBOBOX", L"", WS_CHILD | WS_VISIBLE | CBS_DROPDOWNLIST | WS_VSCROLL, 368, 308, 152, 120,
                        hwnd, reinterpret_cast<HMENU>(static_cast<INT_PTR>(IDC_CONV_OUTPUT_UNIT)),
                        GetModuleHandleW(nullptr), nullptr);
      SendMessageW(outUnitCombo, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(L"米（m）"));
      SendMessageW(outUnitCombo, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(L"千米（km）"));
      SendMessageW(outUnitCombo, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(L"千千米（1000 km）"));
      SendMessageW(outUnitCombo, CB_SETCURSEL, 0, 0);
      CreateWindowW(L"STATIC", L"Mesh 间距（模型单位）：", WS_CHILD | WS_VISIBLE, 280, 338, 200, 18, hwnd,
                    reinterpret_cast<HMENU>(static_cast<INT_PTR>(IDC_CONV_MESH_SPACING_LBL)), GetModuleHandleW(nullptr),
                    nullptr);
      CreateWindowW(L"EDIT", L"1", WS_CHILD | WS_VISIBLE | WS_BORDER | ES_AUTOHSCROLL | ES_NUMBER, 420, 336, 98, 22,
                    hwnd, reinterpret_cast<HMENU>(static_cast<INT_PTR>(IDC_CONV_MESH_SPACING)), GetModuleHandleW(nullptr),
                    nullptr);
      CreateWindowW(L"STATIC", L"栅格读取最大边（px）：", WS_CHILD | WS_VISIBLE, 280, 364, 178, 18, hwnd,
                    reinterpret_cast<HMENU>(static_cast<INT_PTR>(IDC_CONV_RASTER_MAX_LBL)), GetModuleHandleW(nullptr),
                    nullptr);
      CreateWindowW(L"EDIT", L"4096", WS_CHILD | WS_VISIBLE | WS_BORDER | ES_AUTOHSCROLL | ES_NUMBER, 420, 362, 98, 22,
                    hwnd, reinterpret_cast<HMENU>(static_cast<INT_PTR>(IDC_CONV_RASTER_MAX)), GetModuleHandleW(nullptr),
                    nullptr);
      CreateWindowW(L"STATIC", L"模型贴图格式：", WS_CHILD | WS_VISIBLE, 280, 392, 168, 18, hwnd,
                    reinterpret_cast<HMENU>(static_cast<INT_PTR>(IDC_CONV_TEXTURE_FMT_LBL)), GetModuleHandleW(nullptr),
                    nullptr);
      HWND texFmtCombo =
          CreateWindowW(L"COMBOBOX", L"", WS_CHILD | WS_VISIBLE | CBS_DROPDOWNLIST | WS_VSCROLL, 420, 390, 98, 120, hwnd,
                        reinterpret_cast<HMENU>(static_cast<INT_PTR>(IDC_CONV_TEXTURE_FORMAT)),
                        GetModuleHandleW(nullptr), nullptr);
      FillConvertTextureFormatCombo(texFmtCombo);
      HWND coord = CreateWindowW(L"COMBOBOX", L"", WS_CHILD | WS_VISIBLE | CBS_DROPDOWNLIST | WS_VSCROLL, 280, 420, 240,
                                 120, hwnd, reinterpret_cast<HMENU>(static_cast<INT_PTR>(IDC_CONV_MODEL_COORD)),
                                 GetModuleHandleW(nullptr), nullptr);
      SendMessageW(coord, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(L"projected（投影坐标，单位随 CRS）"));
      SendMessageW(coord, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(L"cecf（地心地固 XYZ）"));
      SendMessageW(coord, CB_SETCURSEL, 0, 0);
      HWND vmode = CreateWindowW(L"COMBOBOX", L"", WS_CHILD | WS_VISIBLE | CBS_DROPDOWNLIST | WS_VSCROLL, 280, 448, 240,
                                 120, hwnd, reinterpret_cast<HMENU>(static_cast<INT_PTR>(IDC_CONV_VECTOR_MODE)),
                                 GetModuleHandleW(nullptr), nullptr);
      SendMessageW(vmode, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(L"geometry（矢量转几何）"));
      SendMessageW(vmode, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(L"bake_texture（矢量烘焙到贴图）"));
      SendMessageW(vmode, CB_SETCURSEL, 0, 0);

      CreateWindowW(L"STATIC", L"输出数据", WS_CHILD | WS_VISIBLE, 550, 8, 120, 16, hwnd, nullptr,
                    GetModuleHandleW(nullptr), nullptr);
      HWND outType =
          CreateWindowW(L"COMBOBOX", L"", WS_CHILD | WS_VISIBLE | CBS_DROPDOWNLIST | WS_VSCROLL, 550, 30, 240, 240,
                        hwnd, reinterpret_cast<HMENU>(static_cast<INT_PTR>(IDC_CONV_OUTPUT_TYPE)),
                        GetModuleHandleW(nullptr), nullptr);
      CreateWindowW(L"BUTTON", L"?", WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON, 766, 30, 24, 24, hwnd,
                    reinterpret_cast<HMENU>(static_cast<INT_PTR>(IDC_CONV_OUTPUT_TYPE_HELP)),
                    GetModuleHandleW(nullptr), nullptr);
      CreateWindowW(L"COMBOBOX", L"", WS_CHILD | WS_VISIBLE | CBS_DROPDOWNLIST | WS_VSCROLL, 550, 58, 240, 220, hwnd,
                    reinterpret_cast<HMENU>(static_cast<INT_PTR>(IDC_CONV_OUTPUT_SUBTYPE)),
                    GetModuleHandleW(nullptr), nullptr);
      CreateWindowW(L"BUTTON", L"?", WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON, 766, 58, 24, 24, hwnd,
                    reinterpret_cast<HMENU>(static_cast<INT_PTR>(IDC_CONV_OUTPUT_SUBTYPE_HELP)),
                    GetModuleHandleW(nullptr), nullptr);
      CreateWindowW(L"EDIT", L"", WS_CHILD | WS_VISIBLE | WS_BORDER | ES_AUTOHSCROLL, 550, 86, 172, 24, hwnd,
                    reinterpret_cast<HMENU>(static_cast<INT_PTR>(IDC_CONV_OUTPUT_PATH)), GetModuleHandleW(nullptr),
                    nullptr);
      CreateWindowW(L"BUTTON", L"浏览", WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON, 726, 86, 68, 24, hwnd,
                    reinterpret_cast<HMENU>(static_cast<INT_PTR>(IDC_CONV_OUTPUT_BROWSE)), GetModuleHandleW(nullptr),
                    nullptr);
      CreateWindowW(L"BUTTON", L"预览", WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON, 798, 86, 70, 24, hwnd,
                    reinterpret_cast<HMENU>(static_cast<INT_PTR>(IDC_CONV_OUTPUT_PREVIEW)),
                    GetModuleHandleW(nullptr), nullptr);
      CreateWindowW(L"EDIT", L"", WS_CHILD | WS_VISIBLE | WS_BORDER | ES_MULTILINE | ES_AUTOVSCROLL | WS_VSCROLL, 550,
                    116, 240, 200, hwnd, reinterpret_cast<HMENU>(static_cast<INT_PTR>(IDC_CONV_OUTPUT_INFO)),
                    GetModuleHandleW(nullptr), nullptr);

      CreateWindowW(PROGRESS_CLASSW, L"", WS_CHILD | WS_VISIBLE, 10, 280, 740, 22, hwnd,
                    reinterpret_cast<HMENU>(static_cast<INT_PTR>(IDC_CONV_PROGRESS)), GetModuleHandleW(nullptr),
                    nullptr);
      CreateWindowW(L"BUTTON", L"开始转换", WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON, 760, 278, 100, 26, hwnd,
                    reinterpret_cast<HMENU>(static_cast<INT_PTR>(IDC_CONV_RUN)), GetModuleHandleW(nullptr), nullptr);
      CreateWindowW(L"BUTTON", L"复制命令", WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON, 760, 248, 100, 26, hwnd,
                    reinterpret_cast<HMENU>(static_cast<INT_PTR>(IDC_CONV_COPY_CMD)), GetModuleHandleW(nullptr), nullptr);
      CreateWindowW(L"EDIT", L"命令预览：\r\n（尚未执行）", WS_CHILD | WS_VISIBLE | WS_BORDER | ES_MULTILINE | ES_AUTOVSCROLL |
                                              WS_VSCROLL | ES_READONLY,
                    10, 280, 760, 92, hwnd, reinterpret_cast<HMENU>(static_cast<INT_PTR>(IDC_CONV_CMDLINE)),
                    GetModuleHandleW(nullptr), nullptr);
      CreateWindowW(L"STATIC", L"就绪：请先选择输入与输出数据。", WS_CHILD | WS_VISIBLE, 10, 308, 860, 20, hwnd,
                    reinterpret_cast<HMENU>(static_cast<INT_PTR>(IDC_CONV_MSG)), GetModuleHandleW(nullptr), nullptr);
      CreateWindowW(L"EDIT", L"", WS_CHILD | WS_VISIBLE | WS_BORDER | ES_MULTILINE | ES_AUTOVSCROLL | WS_VSCROLL, 10,
                    336, 860, 220, hwnd, reinterpret_cast<HMENU>(static_cast<INT_PTR>(IDC_CONV_LOG)),
                    GetModuleHandleW(nullptr), nullptr);

      FillConvertTypeCombo(inType);
      FillConvertTypeCombo(outType);
      SyncConvertInfoByType(hwnd, true);
      SyncConvertInfoByType(hwnd, false);
      for (int cid : {IDC_CONV_INPUT_TYPE, IDC_CONV_INPUT_TYPE_HELP, IDC_CONV_INPUT_SUBTYPE, IDC_CONV_INPUT_SUBTYPE_HELP,
                      IDC_CONV_INPUT_PATH, IDC_CONV_INPUT_BROWSE, IDC_CONV_INPUT_PREVIEW, IDC_CONV_INPUT_INFO,
                      IDC_CONV_INPUT_SETTING_LBL, IDC_CONV_INPUT_SETTING, IDC_CONV_OUTPUT_SETTING_LBL,
                      IDC_CONV_OUTPUT_SETTING,
                      IDC_CONV_ELEV_HORIZ_LBL, IDC_CONV_ELEV_HORIZ_RATIO, IDC_CONV_TARGET_CRS_LBL, IDC_CONV_TARGET_CRS,
                      IDC_CONV_OUTPUT_UNIT_LBL, IDC_CONV_OUTPUT_UNIT, IDC_CONV_MESH_SPACING_LBL, IDC_CONV_MESH_SPACING,
                      IDC_CONV_RASTER_MAX_LBL, IDC_CONV_RASTER_MAX, IDC_CONV_TEXTURE_FMT_LBL, IDC_CONV_TEXTURE_FORMAT,
                      IDC_CONV_OUTPUT_TYPE, IDC_CONV_OUTPUT_TYPE_HELP,
                      IDC_CONV_OUTPUT_SUBTYPE,
                      IDC_CONV_OUTPUT_SUBTYPE_HELP, IDC_CONV_OUTPUT_PATH, IDC_CONV_OUTPUT_BROWSE, IDC_CONV_OUTPUT_PREVIEW,
                      IDC_CONV_OUTPUT_INFO, IDC_CONV_MODEL_COORD, IDC_CONV_VECTOR_MODE, IDC_CONV_COPY_CMD,
                      IDC_CONV_PROGRESS,
                      IDC_CONV_RUN, IDC_CONV_MSG}) {
        if (HWND c = GetDlgItem(hwnd, cid)) {
          SendMessageW(c, WM_SETFONT, reinterpret_cast<WPARAM>(UiGetAppFont()), TRUE);
        }
      }
      if (HWND h = GetDlgItem(hwnd, IDC_CONV_CMDLINE)) {
        SendMessageW(h, WM_SETFONT, reinterpret_cast<WPARAM>(UiGetLogFont()), TRUE);
      }
      if (HWND h = GetDlgItem(hwnd, IDC_CONV_LOG)) {
        SendMessageW(h, WM_SETFONT, reinterpret_cast<WPARAM>(UiGetLogFont()), TRUE);
      }
      SendMessageW(GetDlgItem(hwnd, IDC_CONV_PROGRESS), PBM_SETRANGE, 0, MAKELPARAM(0, 100));
      SendMessageW(GetDlgItem(hwnd, IDC_CONV_PROGRESS), PBM_SETPOS, 0, 0);
      s_tip = CreateWindowExW(WS_EX_TOPMOST, TOOLTIPS_CLASSW, nullptr,
                              WS_POPUP | TTS_ALWAYSTIP | TTS_NOPREFIX, CW_USEDEFAULT, CW_USEDEFAULT, CW_USEDEFAULT,
                              CW_USEDEFAULT, hwnd, nullptr, GetModuleHandleW(nullptr), nullptr);
      if (s_tip) {
        SendMessageW(s_tip, TTM_SETMAXTIPWIDTH, 0, 480);
        AttachConvertTooltip(hwnd, s_tip, IDC_CONV_INPUT_TYPE);
        AttachConvertTooltip(hwnd, s_tip, IDC_CONV_INPUT_SUBTYPE);
        AttachConvertTooltip(hwnd, s_tip, IDC_CONV_OUTPUT_TYPE);
        AttachConvertTooltip(hwnd, s_tip, IDC_CONV_OUTPUT_SUBTYPE);
      }
      WriteConvertLog(hwnd, L"[转换] 窗口已打开。");
      WriteConvertLog(hwnd, L"[转换] 支持：GIS数据 / 模型数据 / 瓦片数据（含子类型）。");
      RefreshConvertSettingPanels(hwnd);
      UpdateConvertCmdlinePreview(hwnd);
      LayoutConvertWindow(hwnd);
      return 0;
    }
    case WM_CTLCOLORDLG: {
      if (AgisEffectiveUiDark()) {
        if (!s_bgDark) {
          s_bgDark = CreateSolidBrush(RGB(40, 42, 48));
        }
        return reinterpret_cast<INT_PTR>(s_bgDark);
      }
      if (!s_bgLight) {
        s_bgLight = CreateSolidBrush(RGB(245, 248, 252));
      }
      return reinterpret_cast<INT_PTR>(s_bgLight);
    }
    case WM_CTLCOLORSTATIC:
    case WM_CTLCOLORLISTBOX: {
      HDC hdc = reinterpret_cast<HDC>(wParam);
      SetBkMode(hdc, OPAQUE);
      if (AgisEffectiveUiDark()) {
        if (!s_bgDark) {
          s_bgDark = CreateSolidBrush(RGB(40, 42, 48));
        }
        SetBkColor(hdc, RGB(40, 42, 48));
        SetTextColor(hdc, RGB(220, 222, 228));
        return reinterpret_cast<INT_PTR>(s_bgDark);
      }
      if (!s_bgLight) {
        s_bgLight = CreateSolidBrush(RGB(245, 248, 252));
      }
      SetBkColor(hdc, RGB(245, 248, 252));
      SetTextColor(hdc, RGB(30, 42, 62));
      return reinterpret_cast<INT_PTR>(s_bgLight);
    }
    case WM_CTLCOLOREDIT: {
      HDC hdc = reinterpret_cast<HDC>(wParam);
      SetBkMode(hdc, OPAQUE);
      if (AgisEffectiveUiDark()) {
        if (!s_editDark) {
          s_editDark = CreateSolidBrush(RGB(52, 54, 60));
        }
        SetBkColor(hdc, RGB(52, 54, 60));
        SetTextColor(hdc, RGB(230, 230, 235));
        return reinterpret_cast<INT_PTR>(s_editDark);
      }
      if (!s_editLight) {
        s_editLight = CreateSolidBrush(RGB(255, 255, 255));
      }
      SetBkColor(hdc, RGB(255, 255, 255));
      SetTextColor(hdc, RGB(28, 36, 52));
      return reinterpret_cast<INT_PTR>(s_editLight);
    }
    case WM_SIZE:
      LayoutConvertWindow(hwnd);
      return 0;
    case WM_NOTIFY: {
      if (!lParam) {
        break;
      }
      const NMHDR* hdr = reinterpret_cast<const NMHDR*>(lParam);
      if (hdr->code == TTN_GETDISPINFOW) {
        auto* di = reinterpret_cast<NMTTDISPINFOW*>(lParam);
        di->lpszText = const_cast<LPWSTR>(GetConvertTooltipText(hwnd, hdr->idFrom));
        return 0;
      }
      break;
    }
    case WM_COMMAND:
      if (HIWORD(wParam) == CBN_SELCHANGE && LOWORD(wParam) == IDC_CONV_INPUT_TYPE) {
        SyncConvertInfoByType(hwnd, true);
        RefreshConvertSettingPanels(hwnd);
        UpdateConvertCmdlinePreview(hwnd);
        return 0;
      }
      if (HIWORD(wParam) == CBN_SELCHANGE && LOWORD(wParam) == IDC_CONV_OUTPUT_TYPE) {
        SyncConvertInfoByType(hwnd, false);
        RefreshConvertSettingPanels(hwnd);
        UpdateConvertCmdlinePreview(hwnd);
        return 0;
      }
      if (HIWORD(wParam) == CBN_SELCHANGE && (LOWORD(wParam) == IDC_CONV_INPUT_SUBTYPE || LOWORD(wParam) == IDC_CONV_OUTPUT_SUBTYPE)) {
        RefreshConvertSettingPanels(hwnd);
        UpdateConvertCmdlinePreview(hwnd);
        return 0;
      }
      if (HIWORD(wParam) == CBN_SELCHANGE && LOWORD(wParam) == IDC_CONV_MODEL_COORD) {
        SyncConvertInfoByType(hwnd, false);
        RefreshConvertSettingPanels(hwnd);
        UpdateConvertCmdlinePreview(hwnd);
        return 0;
      }
      if (HIWORD(wParam) == CBN_SELCHANGE && LOWORD(wParam) == IDC_CONV_VECTOR_MODE) {
        SyncConvertInfoByType(hwnd, false);
        RefreshConvertSettingPanels(hwnd);
        UpdateConvertCmdlinePreview(hwnd);
        return 0;
      }
      if (HIWORD(wParam) == CBN_SELCHANGE && LOWORD(wParam) == IDC_CONV_TEXTURE_FORMAT) {
        RefreshConvertSettingPanels(hwnd);
        UpdateConvertCmdlinePreview(hwnd);
        SyncConvertInfoByType(hwnd, false);
        break;
      }
      if (HIWORD(wParam) == CBN_SELCHANGE && LOWORD(wParam) == IDC_CONV_OUTPUT_UNIT) {
        SyncConvertInfoByType(hwnd, false);
        RefreshConvertSettingPanels(hwnd);
        UpdateConvertCmdlinePreview(hwnd);
        return 0;
      }
      if (HIWORD(wParam) == EN_CHANGE &&
          (LOWORD(wParam) == IDC_CONV_INPUT_PATH || LOWORD(wParam) == IDC_CONV_OUTPUT_PATH ||
           LOWORD(wParam) == IDC_CONV_ELEV_HORIZ_RATIO || LOWORD(wParam) == IDC_CONV_TARGET_CRS ||
           LOWORD(wParam) == IDC_CONV_MESH_SPACING || LOWORD(wParam) == IDC_CONV_RASTER_MAX)) {
        if (LOWORD(wParam) == IDC_CONV_INPUT_PATH) {
          SyncConvertInfoByType(hwnd, true);
        } else if (LOWORD(wParam) == IDC_CONV_OUTPUT_PATH) {
          SyncConvertInfoByType(hwnd, false);
        } else {
          SyncConvertInfoByType(hwnd, false);
        }
        RefreshConvertSettingPanels(hwnd);
        UpdateConvertCmdlinePreview(hwnd);
        return 0;
      }
      if (LOWORD(wParam) == IDC_CONV_INPUT_BROWSE) {
        const std::wstring p = PromptOpenInputPath(hwnd);
        if (!p.empty()) {
          SetWindowTextW(GetDlgItem(hwnd, IDC_CONV_INPUT_PATH), p.c_str());
          WriteConvertLog(hwnd, (std::wstring(L"[路径] 输入：") + p).c_str());
          SyncConvertInfoByType(hwnd, true);
          RefreshConvertSettingPanels(hwnd);
          UpdateConvertCmdlinePreview(hwnd);
        }
        return 0;
      }
      if (LOWORD(wParam) == IDC_CONV_INPUT_PREVIEW) {
        PreviewPath(hwnd, true);
        return 0;
      }
      if (LOWORD(wParam) == IDC_CONV_OUTPUT_BROWSE) {
        const int outMajor = static_cast<int>(SendMessageW(GetDlgItem(hwnd, IDC_CONV_OUTPUT_TYPE), CB_GETCURSEL, 0, 0));
        std::wstring p;
        if (outMajor == 2) {
          p = PromptSelectOutputFolder(hwnd);
        } else {
          p = PromptSaveOutputPath(hwnd);
        }
        if (!p.empty()) {
          SetWindowTextW(GetDlgItem(hwnd, IDC_CONV_OUTPUT_PATH), p.c_str());
          WriteConvertLog(hwnd, (std::wstring(L"[路径] 输出：") + p).c_str());
          SyncConvertInfoByType(hwnd, false);
          RefreshConvertSettingPanels(hwnd);
          UpdateConvertCmdlinePreview(hwnd);
        }
        return 0;
      }
      if (LOWORD(wParam) == IDC_CONV_OUTPUT_PREVIEW) {
        PreviewPath(hwnd, false);
        return 0;
      }
      if (LOWORD(wParam) == IDC_CONV_INPUT_TYPE_HELP) {
        ShowConvertHelpDialog(hwnd, true, true);
        return 0;
      }
      if (LOWORD(wParam) == IDC_CONV_INPUT_SUBTYPE_HELP) {
        ShowConvertHelpDialog(hwnd, true, false);
        return 0;
      }
      if (LOWORD(wParam) == IDC_CONV_OUTPUT_TYPE_HELP) {
        ShowConvertHelpDialog(hwnd, false, true);
        return 0;
      }
      if (LOWORD(wParam) == IDC_CONV_OUTPUT_SUBTYPE_HELP) {
        ShowConvertHelpDialog(hwnd, false, false);
        return 0;
      }
      if (LOWORD(wParam) == IDC_CONV_COPY_CMD) {
        std::wstring cmd = BuildConvertCommandLine(hwnd);
        if (OpenClipboard(hwnd)) {
          EmptyClipboard();
          const size_t bytes = (cmd.size() + 1) * sizeof(wchar_t);
          HGLOBAL h = GlobalAlloc(GMEM_MOVEABLE, bytes);
          if (h) {
            void* p = GlobalLock(h);
            if (p) {
              memcpy(p, cmd.c_str(), bytes);
              GlobalUnlock(h);
              SetClipboardData(CF_UNICODETEXT, h);
              h = nullptr;
              WriteConvertLog(hwnd, L"[命令] 已复制到剪贴板。");
            }
          }
          if (h) GlobalFree(h);
          CloseClipboard();
        }
        return 0;
      }
      if (LOWORD(wParam) == IDC_CONV_RUN) {
        if (g_convertRunning) {
          MessageBoxW(hwnd, L"已有转换任务在运行，请等待完成。", L"数据转换", MB_OK | MB_ICONINFORMATION);
          return 0;
        }
        const std::wstring inType = GetComboSelectedText(GetDlgItem(hwnd, IDC_CONV_INPUT_TYPE));
        const std::wstring inSub = GetComboSelectedText(GetDlgItem(hwnd, IDC_CONV_INPUT_SUBTYPE));
        const std::wstring outType = GetComboSelectedText(GetDlgItem(hwnd, IDC_CONV_OUTPUT_TYPE));
        const std::wstring outSub = GetComboSelectedText(GetDlgItem(hwnd, IDC_CONV_OUTPUT_SUBTYPE));
        const std::wstring inLine = std::wstring(L"[任务] 输入：") + inType + L" / " + inSub;
        const std::wstring outLine = std::wstring(L"[任务] 输出：") + outType + L" / " + outSub;
        WriteConvertLog(hwnd, inLine.c_str());
        WriteConvertLog(hwnd, outLine.c_str());
        SendMessageW(GetDlgItem(hwnd, IDC_CONV_PROGRESS), PBM_SETPOS, 15, 0);
        SetWindowTextW(GetDlgItem(hwnd, IDC_CONV_MSG), L"处理中：正在启动后端命令行程序...");
        if (!RunConvertBackendAsync(hwnd)) {
          SendMessageW(GetDlgItem(hwnd, IDC_CONV_PROGRESS), PBM_SETPOS, 0, 0);
          SetWindowTextW(GetDlgItem(hwnd, IDC_CONV_MSG), L"失败：后端未能启动。");
        } else {
          SendMessageW(GetDlgItem(hwnd, IDC_CONV_PROGRESS), PBM_SETPOS, 35, 0);
          SetWindowTextW(GetDlgItem(hwnd, IDC_CONV_MSG), L"处理中：转换任务后台运行中...");
        }
        return 0;
      }
      break;
    case WM_TIMER:
      if (wParam == kConvertPollTimerId && g_convertRunning && g_convertPi.hProcess) {
        const DWORD wait = WaitForSingleObject(g_convertPi.hProcess, 0);
        if (wait == WAIT_TIMEOUT) {
          return 0;
        }
        KillTimer(hwnd, kConvertPollTimerId);
        DWORD code = 1;
        GetExitCodeProcess(g_convertPi.hProcess, &code);
        CloseHandle(g_convertPi.hThread);
        CloseHandle(g_convertPi.hProcess);
        ZeroMemory(&g_convertPi, sizeof(g_convertPi));
        g_convertRunning = false;
        WriteConvertLog(hwnd, (std::wstring(L"[后端] 退出码：") + std::to_wstring(code)).c_str());
        const bool ok = (code == 0);
        SendMessageW(GetDlgItem(hwnd, IDC_CONV_PROGRESS), PBM_SETPOS, ok ? 100 : 0, 0);
        SetWindowTextW(GetDlgItem(hwnd, IDC_CONV_MSG), ok ? L"完成：转换成功。" : L"失败：后端执行出错。");
        return 0;
      }
      break;
    case WM_CLOSE:
      DestroyWindow(hwnd);
      return 0;
    case WM_DESTROY:
      if (g_convertRunning) {
        KillTimer(hwnd, kConvertPollTimerId);
        if (g_convertPi.hThread) CloseHandle(g_convertPi.hThread);
        if (g_convertPi.hProcess) CloseHandle(g_convertPi.hProcess);
        ZeroMemory(&g_convertPi, sizeof(g_convertPi));
        g_convertRunning = false;
      }
      g_hwndConvertDlg = nullptr;
      s_tip = nullptr;
      if (s_bgLight) {
        DeleteObject(s_bgLight);
        s_bgLight = nullptr;
      }
      if (s_bgDark) {
        DeleteObject(s_bgDark);
        s_bgDark = nullptr;
      }
      if (s_editLight) {
        DeleteObject(s_editLight);
        s_editLight = nullptr;
      }
      if (s_editDark) {
        DeleteObject(s_editDark);
        s_editDark = nullptr;
      }
      return 0;
    default:
      break;
  }
  return DefWindowProcW(hwnd, msg, wParam, lParam);
}
