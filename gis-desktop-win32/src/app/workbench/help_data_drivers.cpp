#include "workbench/help_data_drivers.h"

#include "utils/agis_ui_l10n.h"
#include "utils/ui_font.h"
#include "utils/ui_theme.h"
#include "core/main_globals.h"
#include "utils/utf8_wide.h"

#include <algorithm>
#include <cwctype>
#include <string>
#include <utility>
#include <vector>

#ifndef GIS_DESKTOP_HAVE_GDAL
#define GIS_DESKTOP_HAVE_GDAL 0
#endif

#if GIS_DESKTOP_HAVE_GDAL
#include "utils/agis_gdal_runtime_env.h"
#include "gdal_priv.h"
#endif

namespace {

constexpr int kIdHelpEdit = 70001;
constexpr int kIdHelpOk = 70002;
const wchar_t kHelpDlgClass[] = L"AGISHelpDataDriversDlg";

#if GIS_DESKTOP_HAVE_GDAL
static void AppendRegisteredGdalDrivers(std::wstring* s) {
  if (!s) {
    return;
  }
  const bool en = AgisGetUiLanguage() == AgisUiLanguage::kEn;
  *s += en ? L"4) GDAL drivers registered in this build (runtime enum, sorted by short name)\r\n"
           : L"四、本构建已注册的 GDAL 驱动（运行时枚举，按短名排序）\r\n";
  AgisEnsureGdalDataPath();
  GDALAllRegister();
  GDALDriverManager* dm = GetGDALDriverManager();
  const int n = dm ? dm->GetDriverCount() : 0;
  if (n <= 0) {
    *s += AgisPickUiLang(L"  （未能枚举驱动。）\r\n", L"  (Could not enumerate drivers.)\r\n");
    return;
  }
  struct Row {
    std::wstring sortKey;
    std::wstring line;
  };
  std::vector<Row> rows;
  rows.reserve(static_cast<size_t>(n));
  for (int i = 0; i < n; ++i) {
    GDALDriver* drv = dm->GetDriver(i);
    if (!drv) {
      continue;
    }
    const char* shortName = drv->GetDescription();
    const char* longName = drv->GetMetadataItem(GDAL_DMD_LONGNAME);
    const bool hasVector = drv->GetMetadataItem(GDAL_DCAP_VECTOR) != nullptr;
    const bool hasRaster = drv->GetMetadataItem(GDAL_DCAP_RASTER) != nullptr;
    const bool hasMultidim = drv->GetMetadataItem(GDAL_DCAP_MULTIDIM_RASTER) != nullptr;
    std::wstring wShort = WideFromUtf8(shortName ? shortName : "");
    std::wstring wLong = WideFromUtf8(longName ? longName : "");
    std::wstring tags;
    if (hasRaster) {
      tags += AgisPickUiLang(L"栅格 ", L"Raster ");
    }
    if (hasVector) {
      tags += AgisPickUiLang(L"矢量 ", L"Vector ");
    }
    if (hasMultidim) {
      tags += AgisPickUiLang(L"多维栅格 ", L"Multidim ");
    }
    if (tags.empty()) {
      tags = AgisPickUiLang(L"其它 ", L"Other ");
    }
    std::wstring line = L"  ";
    line += wShort;
    line += L" — ";
    line += wLong.empty() ? wShort : wLong;
    line += L"  [";
    line += tags;
    line += L"]\r\n";
    std::wstring key = wShort;
    std::transform(key.begin(), key.end(), key.begin(), [](wchar_t c) { return std::towlower(c); });
    rows.push_back({std::move(key), std::move(line)});
  }
  std::sort(rows.begin(), rows.end(), [](const Row& a, const Row& b) { return a.sortKey < b.sortKey; });
  wchar_t head[64]{};
  _snwprintf_s(head, _TRUNCATE, en ? L"  (%d drivers)\r\n" : L"  （共 %d 个驱动）\r\n", n);
  *s += head;
  for (const auto& r : rows) {
    *s += r.line;
  }
}
#endif

static HBRUSH g_helpDlgBgLight = nullptr;
static HBRUSH g_helpDlgBgDark = nullptr;
static HBRUSH g_helpEditBgLight = nullptr;
static HBRUSH g_helpEditBgDark = nullptr;
static HBRUSH g_helpBtnBgLight = nullptr;
static HBRUSH g_helpBtnBgDark = nullptr;

std::wstring BuildDataDriversHelpText() {
  if (AgisGetUiLanguage() == AgisUiLanguage::kEn) {
    std::wstring s;
    s +=
        L"[Data drivers]\r\n"
        L"\r\n"
        L"1) AGIS layer types (application)\r\n"
        L"  Name (as in Add Layer)     Description\r\n"
        L"  ─────────────────────────────────────────────────────────\r\n"
        L"  GDAL (local file)   Open a local path or GDAL VSI virtual path.\r\n"
        L"  TMS / XYZ          Standard tile URL with {z}, {x}, {y} (GDAL XYZ mini-driver).\r\n"
        L"  WMTS               OGC WMTS — paste GetCapabilities or service URL (GDAL WMTS).\r\n"
        L"  ArcGIS REST JSON   Esri MapServer / ImageServer REST endpoint (GDAL JSON tiles).\r\n"
        L"  SOAP (placeholder) Not implemented.\r\n"
        L"  WMS (placeholder)  Classic KVP WMS not implemented.\r\n"
        L"\r\n"
        L"2) How to supply a data source\r\n"
        L"  1) Layers → Add Data Layer… or the toolbar Add Layer button.\r\n"
        L"  2) Pick a type in the dialog, then OK.\r\n"
        L"  3) Local file (GDAL): file picker; examples: .tif .tiff .png .jpg .jp2 .img .shp .geojson .json "
        L".gpkg .kml .vrt .osm .pbf …\r\n"
        L"     Put sidecar files (e.g. .shp + .dbf) in the same folder when required.\r\n"
        L"  4) Network URL (TMS / WMTS / ArcGIS REST): paste into the URL box, e.g.\r\n"
        L"     TMS:  https://tile.openstreetmap.org/{z}/{x}/{y}.png\r\n"
        L"     WMTS: (service GetCapabilities or base URL)\r\n"
        L"     ArcGIS: https://services.arcgisonline.com/arcgis/rest/services/World_Imagery/MapServer\r\n"
        L"  5) Replace source: use the layer list or properties panel (same rules as add).\r\n"
        L"\r\n"
        L"3) GDAL and format support\r\n"
        L"  Upstream GDAL supports many raster, vector, and network drivers; what you get depends on CMake "
        L"options\r\n"
        L"  and dependencies (SQLite, curl, PROJ, …). Drivers not built in will not appear in the list below.\r\n"
        L"  Common: raster (GeoTIFF, JPEG2000, VRT…), vector (Shapefile, GeoPackage, KML, OSM…), network "
        L"(XYZ, WMTS…).\r\n"
        L"  If a format is not recognized: verify the file and whether this build includes the driver.\r\n"
        L"  OpenStreetMap: .osm.pbf needs OGR OSM + SQLite3 (see table). .osm XML may need Expat; if Expat is "
        L"disabled,\r\n"
        L"  use .pbf or convert with ogr2ogr.\r\n"
        L"\r\n";
#if GIS_DESKTOP_HAVE_GDAL
    AppendRegisteredGdalDrivers(&s);
#else
    s += L"4) GDAL\r\n"
         L"  This build has GIS_DESKTOP_HAVE_GDAL disabled; drivers cannot be enumerated. Rebuild with GDAL "
         L"enabled.\r\n";
#endif
    s += L"\r\n— End —\r\n";
    return s;
  }

  std::wstring s;
  s +=
      L"【数据驱动说明】\r\n"
      L"\r\n"
      L"一、AGIS 图层类型（应用层）\r\n"
      L"  类型名（添加图层对话框中的选项）     说明\r\n"
      L"  ─────────────────────────────────────────────────────────\r\n"
      L"  GDAL（本地文件）   通过 GDAL 打开本机路径或 GDAL 虚拟文件系统（VSI）路径。\r\n"
      L"  TMS / XYZ          标准瓦片 URL，模板中含 {z}、{x}、{y}（GDAL XYZ 迷你驱动）。\r\n"
      L"  WMTS               OGC WMTS，填写 GetCapabilities 或服务 URL（GDAL WMTS）。\r\n"
      L"  ArcGIS REST JSON   Esri MapServer / ImageServer 的 REST 端点（GDAL 解析 JSON）。\r\n"
      L"  SOAP（占位）       尚未接入。\r\n"
      L"  WMS（占位）        经典 KVP WMS 尚未接入。\r\n"
      L"\r\n"
      L"二、如何输入数据源\r\n"
      L"  1）菜单「图层 → 添加数据图层」或工具栏「添加图层」。\r\n"
      L"  2）在对话框中选择上表类型后点「确定」。\r\n"
      L"  3）本地文件（GDAL）：弹出文件选择框，可选扩展名示例：\r\n"
      L"     .tif .tiff .png .jpg .jp2 .img .shp .geojson .json .gpkg .kml .vrt .osm .pbf 等。\r\n"
      L"     若格式需辅助文件（如 .shp 与 .dbf），请放在同一目录。\r\n"
      L"  4）网络 URL（TMS / WMTS / ArcGIS REST）：在「URL」编辑框粘贴完整地址，例如：\r\n"
      L"     TMS：  https://tile.openstreetmap.org/{z}/{x}/{y}.png\r\n"
      L"     WMTS： （服务提供的 GetCapabilities 或 WMTS 基 URL）\r\n"
      L"     ArcGIS： https://services.arcgisonline.com/arcgis/rest/services/World_Imagery/MapServer\r\n"
      L"  5）更换数据源：在图层列表或属性面板中按提示操作（规则与添加时相同）。\r\n"
      L"\r\n"
      L"三、GDAL 与格式支持\r\n"
      L"  GDAL 上游可支持多种栅格、矢量及网络驱动；是否编入本程序取决于构建 GDAL 时的 CMake 选项与依赖库\r\n"
      L"  （如 SQLite、curl、PROJ 等）。未编入的驱动不会出现在下文「本构建已注册」列表中。\r\n"
      L"  常见：栅格（GeoTIFF、JPEG2000、VRT…）、矢量（Shapefile、GeoPackage、KML、OSM…）、\r\n"
      L"  网络迷你驱动（XYZ、WMTS 等）。若提示无法识别格式：请检查文件是否有效，以及本构建是否包含对应驱动。\r\n"
      L"  OpenStreetMap：.osm.pbf 需 OGR OSM 驱动与 SQLite3（见下表）。.osm（XML）另需 Expat；若构建未启用 Expat，\r\n"
      L"  打开 XML 时会提示解析器不可用，可改用 .pbf 或对区域数据使用 ogr2ogr 转换。\r\n"
      L"\r\n";

#if GIS_DESKTOP_HAVE_GDAL
  AppendRegisteredGdalDrivers(&s);
#else
  s +=
      L"四、GDAL\r\n"
      L"  本构建未启用 GIS_DESKTOP_HAVE_GDAL，无法枚举驱动。请使用默认启用 GDAL 的构建并重新编译后查看列表。\r\n";
#endif

  s += L"\r\n— 结束 —\r\n";
  return s;
}

LRESULT CALLBACK HelpDataDriversDlgProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp) {
  switch (msg) {
    case WM_NCCREATE: {
      auto* cs = reinterpret_cast<CREATESTRUCTW*>(lp);
      SetWindowLongPtrW(hwnd, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(cs->lpCreateParams));
      return TRUE;
    }
    case WM_CREATE: {
      auto* body = reinterpret_cast<std::wstring*>(GetWindowLongPtrW(hwnd, GWLP_USERDATA));
      HINSTANCE inst = GetModuleHandleW(nullptr);
      HWND hEdit =
          CreateWindowExW(WS_EX_CLIENTEDGE, L"EDIT", body && !body->empty() ? body->c_str() : L"",
                          WS_CHILD | WS_VISIBLE | ES_MULTILINE | ES_READONLY | ES_AUTOVSCROLL | WS_VSCROLL |
                              WS_TABSTOP | WS_HSCROLL | ES_AUTOHSCROLL,
                          12, 12, 100, 100, hwnd, reinterpret_cast<HMENU>(static_cast<INT_PTR>(kIdHelpEdit)), inst,
                          nullptr);
      CreateWindowW(L"BUTTON", AgisTr(AgisUiStr::HelpBtnClose), WS_CHILD | WS_VISIBLE | BS_DEFPUSHBUTTON | WS_TABSTOP,
                    12, 12, 100, 28, hwnd, reinterpret_cast<HMENU>(static_cast<INT_PTR>(kIdHelpOk)), inst, nullptr);
      if (hEdit) {
        SendMessageW(hEdit, EM_SETLIMITTEXT, 0, 0);
      }
      HFONT fnt = UiGetAppFont();
      if (HWND ok = GetDlgItem(hwnd, kIdHelpOk)) {
        SendMessageW(ok, WM_SETFONT, reinterpret_cast<WPARAM>(fnt), TRUE);
      }
      if (hEdit) {
        SendMessageW(hEdit, WM_SETFONT, reinterpret_cast<WPARAM>(fnt), TRUE);
      }
      AgisApplyDwmDark(hwnd, AgisEffectiveUiDark());
      return 0;
    }
    case WM_CTLCOLORDLG: {
      if (AgisEffectiveUiDark()) {
        if (!g_helpDlgBgDark) {
          g_helpDlgBgDark = CreateSolidBrush(RGB(40, 42, 48));
        }
        return reinterpret_cast<INT_PTR>(g_helpDlgBgDark);
      }
      if (!g_helpDlgBgLight) {
        g_helpDlgBgLight = CreateSolidBrush(GetSysColor(COLOR_WINDOW));
      }
      return reinterpret_cast<INT_PTR>(g_helpDlgBgLight);
    }
    case WM_CTLCOLORSTATIC: {
      HDC hdc = reinterpret_cast<HDC>(wp);
      SetBkMode(hdc, OPAQUE);
      if (AgisEffectiveUiDark()) {
        if (!g_helpDlgBgDark) {
          g_helpDlgBgDark = CreateSolidBrush(RGB(40, 42, 48));
        }
        SetBkColor(hdc, RGB(40, 42, 48));
        SetTextColor(hdc, RGB(220, 222, 228));
        return reinterpret_cast<INT_PTR>(g_helpDlgBgDark);
      }
      if (!g_helpDlgBgLight) {
        g_helpDlgBgLight = CreateSolidBrush(GetSysColor(COLOR_WINDOW));
      }
      SetBkColor(hdc, GetSysColor(COLOR_WINDOW));
      SetTextColor(hdc, GetSysColor(COLOR_WINDOWTEXT));
      return reinterpret_cast<INT_PTR>(g_helpDlgBgLight);
    }
    case WM_CTLCOLOREDIT: {
      HDC hdc = reinterpret_cast<HDC>(wp);
      SetBkMode(hdc, OPAQUE);
      if (AgisEffectiveUiDark()) {
        if (!g_helpEditBgDark) {
          g_helpEditBgDark = CreateSolidBrush(RGB(52, 54, 60));
        }
        SetBkColor(hdc, RGB(52, 54, 60));
        SetTextColor(hdc, RGB(230, 230, 235));
        return reinterpret_cast<INT_PTR>(g_helpEditBgDark);
      }
      if (!g_helpEditBgLight) {
        g_helpEditBgLight = CreateSolidBrush(RGB(255, 255, 255));
      }
      SetBkColor(hdc, RGB(255, 255, 255));
      SetTextColor(hdc, RGB(28, 36, 52));
      return reinterpret_cast<INT_PTR>(g_helpEditBgLight);
    }
    case WM_CTLCOLORBTN: {
      HDC hdc = reinterpret_cast<HDC>(wp);
      SetBkMode(hdc, OPAQUE);
      if (AgisEffectiveUiDark()) {
        if (!g_helpBtnBgDark) {
          g_helpBtnBgDark = CreateSolidBrush(RGB(56, 58, 64));
        }
        SetBkColor(hdc, RGB(56, 58, 64));
        SetTextColor(hdc, RGB(230, 230, 235));
        return reinterpret_cast<INT_PTR>(g_helpBtnBgDark);
      }
      if (!g_helpBtnBgLight) {
        g_helpBtnBgLight = CreateSolidBrush(GetSysColor(COLOR_BTNFACE));
      }
      SetBkColor(hdc, GetSysColor(COLOR_BTNFACE));
      SetTextColor(hdc, GetSysColor(COLOR_BTNTEXT));
      return reinterpret_cast<INT_PTR>(g_helpBtnBgLight);
    }
    case WM_SIZE: {
      const int w = LOWORD(lp);
      const int h = HIWORD(lp);
      const int btnH = 30;
      const int gap = 10;
      const int margin = 10;
      if (HWND ok = GetDlgItem(hwnd, kIdHelpOk)) {
        MoveWindow(ok, std::max(10, w - margin - 100), std::max(10, h - margin - btnH), 100, btnH, TRUE);
      }
      if (HWND ed = GetDlgItem(hwnd, kIdHelpEdit)) {
        const int edH = std::max(80, h - 2 * margin - btnH - gap);
        MoveWindow(ed, margin, margin, std::max(80, w - 2 * margin), edH, TRUE);
      }
      return 0;
    }
    case WM_COMMAND: {
      const int id = LOWORD(wp);
      if (id == kIdHelpOk) {
        DestroyWindow(hwnd);
        return 0;
      }
      break;
    }
    case WM_CLOSE:
      DestroyWindow(hwnd);
      return 0;
    case WM_DESTROY:
      if (g_hwndHelpDataDriversDlg == hwnd) {
        g_hwndHelpDataDriversDlg = nullptr;
      }
      if (auto* body = reinterpret_cast<std::wstring*>(GetWindowLongPtrW(hwnd, GWLP_USERDATA))) {
        delete body;
        SetWindowLongPtrW(hwnd, GWLP_USERDATA, 0);
      }
      return 0;
    default:
      break;
  }
  return DefWindowProcW(hwnd, msg, wp, lp);
}

void EnsureHelpDlgClass() {
  static bool registered = false;
  if (registered) {
    return;
  }
  WNDCLASSW wc{};
  wc.lpfnWndProc = HelpDataDriversDlgProc;
  wc.hInstance = GetModuleHandleW(nullptr);
  wc.lpszClassName = kHelpDlgClass;
  wc.hbrBackground = reinterpret_cast<HBRUSH>(COLOR_WINDOW + 1);
  wc.hCursor = LoadCursor(nullptr, IDC_ARROW);
  if (RegisterClassW(&wc) != 0 || GetLastError() == ERROR_CLASS_ALREADY_EXISTS) {
    registered = true;
  }
}

}  // namespace

void ShowDataDriversHelp(HWND owner) {
  EnsureHelpDlgClass();
  auto* body = new std::wstring(BuildDataDriversHelpText());

  constexpr int kW = 720;
  constexpr int kH = 520;
  int x = CW_USEDEFAULT;
  int y = CW_USEDEFAULT;
  if (owner) {
    RECT wr{};
    if (GetWindowRect(owner, &wr)) {
      x = wr.left + ((wr.right - wr.left) - kW) / 2;
      y = wr.top + ((wr.bottom - wr.top) - kH) / 2;
    }
  }

  DWORD style = WS_POPUP | WS_CAPTION | WS_SYSMENU | WS_THICKFRAME | WS_MINIMIZEBOX;
  DWORD ex = WS_EX_DLGMODALFRAME | WS_EX_WINDOWEDGE;
  RECT wr{0, 0, kW, kH};
  AdjustWindowRectEx(&wr, style, FALSE, ex);
  const int dw = wr.right - wr.left;
  const int dh = wr.bottom - wr.top;

  HWND dlg = CreateWindowExW(ex, kHelpDlgClass, AgisTr(AgisUiStr::HelpDlgTitle), style, x, y, dw, dh, owner, nullptr,
                             GetModuleHandleW(nullptr), body);
  if (!dlg) {
    delete body;
    return;
  }
  g_hwndHelpDataDriversDlg = dlg;

  if (owner) {
    EnableWindow(owner, FALSE);
  }
  ShowWindow(dlg, SW_SHOW);
  UpdateWindow(dlg);

  MSG msg{};
  while (GetMessageW(&msg, nullptr, 0, 0)) {
    if (!IsDialogMessageW(dlg, &msg)) {
      TranslateMessage(&msg);
      DispatchMessageW(&msg);
    }
    if (!IsWindow(dlg)) {
      break;
    }
  }

  if (owner) {
    EnableWindow(owner, TRUE);
    SetForegroundWindow(owner);
  }
}
