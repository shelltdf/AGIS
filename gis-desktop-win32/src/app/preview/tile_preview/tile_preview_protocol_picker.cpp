#include "app/preview/tile_preview/tile_preview_protocol_picker.h"

#include "utils/agis_ui_l10n.h"

#include <commdlg.h>
#include <objbase.h>
#include <shlobj.h>
#include <shobjidl.h>
#include <windowsx.h>

#include <algorithm>
#include <filesystem>
#include <string>

namespace {

constexpr wchar_t kPickerClassName[] = L"AgisTilePreviewProtocolPickerDlg";

static const wchar_t* PickerWindowTitleW() {
  return AgisPickUiLang(L"本地瓦片预览 — 选择数据源类型", L"Local tile preview — choose data source");
}

// OPENFILENAME / 内嵌 \0，按语言整段选用（勿用 wstring 拼接）。
static const wchar_t kFilterTilesetZh[] =
    L"tileset.json\0tileset.json\0JSON\0*.json\0所有文件\0*.*\0\0";
static const wchar_t kFilterTilesetEn[] =
    L"tileset.json\0tileset.json\0JSON\0*.json\0All files\0*.*\0\0";
static const wchar_t kFilterMbtilesZh[] = L"MBTiles\0*.mbtiles\0所有文件\0*.*\0\0";
static const wchar_t kFilterMbtilesEn[] = L"MBTiles\0*.mbtiles\0All files\0*.*\0\0";
static const wchar_t kFilterGpkgZh[] = L"GeoPackage\0*.gpkg\0所有文件\0*.*\0\0";
static const wchar_t kFilterGpkgEn[] = L"GeoPackage\0*.gpkg\0All files\0*.*\0\0";
constexpr int kIdList = 1001;
constexpr int kIdOk = 1002;
constexpr int kIdCancel = 1003;
constexpr int kIdDetail = 1004;

struct PickerUiCtx {
  HWND owner = nullptr;
  HWND list = nullptr;
  HWND detail = nullptr;
  bool done = false;
  bool accepted = false;
  std::wstring path;
  AgisTilePreviewProtocol protocol = AgisTilePreviewProtocol::kXyzTmsFolder;
  std::wstring error;
};

static HINSTANCE PickerModuleInstance(HWND owner) {
  if (owner) {
    if (HINSTANCE hi = reinterpret_cast<HINSTANCE>(GetWindowLongPtrW(owner, GWLP_HINSTANCE))) {
      return hi;
    }
  }
  return GetModuleHandleW(nullptr);
}

static bool PickFolderWithIFileOrBrowse(HWND owner, std::wstring* out, std::wstring* err) {
  if (err) {
    err->clear();
  }
  *out = L"";
  HRESULT hrInit = CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
  const bool needUninit = (hrInit == S_OK);
  IFileOpenDialog* pfd = nullptr;
  HRESULT hr = CoCreateInstance(CLSID_FileOpenDialog, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&pfd));
  if (SUCCEEDED(hr) && pfd) {
    DWORD opts = 0;
    if (SUCCEEDED(pfd->GetOptions(&opts))) {
      pfd->SetOptions(opts | FOS_PICKFOLDERS | FOS_FORCEFILESYSTEM | FOS_PATHMUSTEXIST);
    }
    pfd->SetTitle(AgisPickUiLang(L"选择本地 XYZ / TMS 瓦片根目录", L"Choose local XYZ / TMS tile root folder"));
    hr = pfd->Show(owner);
    if (hr == HRESULT_FROM_WIN32(ERROR_CANCELLED)) {
      pfd->Release();
      if (needUninit) {
        CoUninitialize();
      }
      return false;
    }
    if (SUCCEEDED(hr)) {
      IShellItem* psi = nullptr;
      if (SUCCEEDED(pfd->GetResult(&psi)) && psi) {
        PWSTR psz = nullptr;
        if (SUCCEEDED(psi->GetDisplayName(SIGDN_FILESYSPATH, &psz)) && psz) {
          *out = psz;
          CoTaskMemFree(psz);
        }
        psi->Release();
      }
      pfd->Release();
      if (needUninit) {
        CoUninitialize();
      }
      return !out->empty();
    }
    pfd->Release();
  }
  if (needUninit) {
    CoUninitialize();
  }

  wchar_t display[MAX_PATH * 2]{};
  BROWSEINFOW bi{};
  bi.hwndOwner = owner;
  bi.lpszTitle = AgisPickUiLang(L"选择本地 XYZ / TMS 瓦片根目录（备用文件夹浏览器）",
                                L"Choose local XYZ / TMS tile root (fallback folder browser)");
  bi.pszDisplayName = display;
  bi.ulFlags = BIF_RETURNONLYFSDIRS | BIF_NEWDIALOGSTYLE | BIF_USENEWUI;
  PIDLIST_ABSOLUTE pidl = SHBrowseForFolderW(&bi);
  if (!pidl) {
    return false;
  }
  wchar_t pathBuf[MAX_PATH * 4]{};
  const BOOL gp = SHGetPathFromIDListW(pidl, pathBuf);
  CoTaskMemFree(pidl);
  if (!gp) {
    if (err) {
      *err = AgisPickUiLang(L"无法解析所选文件夹路径（SHGetPathFromIDList）。",
                            L"Could not resolve the selected folder path (SHGetPathFromIDList).");
    }
    return false;
  }
  *out = pathBuf;
  return true;
}

static bool PickOpenFile(HWND owner, const wchar_t* title, const wchar_t* filter, std::wstring* out) {
  wchar_t buf[32768]{};
  OPENFILENAMEW ofn{};
  ofn.lStructSize = sizeof(ofn);
  ofn.hwndOwner = owner;
  ofn.lpstrFile = buf;
  ofn.nMaxFile = static_cast<DWORD>(std::size(buf));
  ofn.lpstrFilter = filter;
  ofn.nFilterIndex = 1;
  ofn.lpstrTitle = title;
  ofn.Flags = OFN_FILEMUSTEXIST | OFN_PATHMUSTEXIST | OFN_EXPLORER | OFN_HIDEREADONLY;
  if (!GetOpenFileNameW(&ofn)) {
    return false;
  }
  *out = buf;
  return true;
}

static bool PickPathForProtocol(HWND owner, AgisTilePreviewProtocol proto, std::wstring* pathOut, std::wstring* err) {
  if (err) {
    err->clear();
  }
  switch (proto) {
  case AgisTilePreviewProtocol::kXyzTmsFolder:
    return PickFolderWithIFileOrBrowse(owner, pathOut, err);
  case AgisTilePreviewProtocol::kThreeDTilesJson:
    return PickOpenFile(owner,
                        AgisPickUiLang(L"选择 3D Tiles 的 tileset.json", L"Choose 3D Tiles tileset.json"),
                        AgisGetUiLanguage() == AgisUiLanguage::kEn ? kFilterTilesetEn : kFilterTilesetZh, pathOut);
  case AgisTilePreviewProtocol::kMBTilesFile:
    return PickOpenFile(owner, AgisPickUiLang(L"选择 MBTiles 文件", L"Choose MBTiles file"),
                        AgisGetUiLanguage() == AgisUiLanguage::kEn ? kFilterMbtilesEn : kFilterMbtilesZh, pathOut);
  case AgisTilePreviewProtocol::kGeoPackageFile:
    return PickOpenFile(owner, AgisPickUiLang(L"选择 GeoPackage 文件", L"Choose GeoPackage file"),
                        AgisGetUiLanguage() == AgisUiLanguage::kEn ? kFilterGpkgEn : kFilterGpkgZh, pathOut);
  default:
    if (err) {
      *err = AgisPickUiLang(L"内部错误：未知瓦片协议。", L"Internal error: unknown tile protocol.");
    }
    return false;
  }
}

static void FillProtocolList(HWND lb) {
  SendMessageW(lb, LB_RESETCONTENT, 0, 0);
  SendMessageW(lb, LB_ADDSTRING, 0,
               reinterpret_cast<LPARAM>(AgisPickUiLang(L"XYZ / TMS 瓦片目录", L"XYZ / TMS tile folder")));
  SendMessageW(lb, LB_ADDSTRING, 0, reinterpret_cast<LPARAM>(AgisPickUiLang(L"3D Tiles", L"3D Tiles")));
  SendMessageW(lb, LB_ADDSTRING, 0, reinterpret_cast<LPARAM>(AgisPickUiLang(L"MBTiles", L"MBTiles")));
  SendMessageW(lb, LB_ADDSTRING, 0, reinterpret_cast<LPARAM>(AgisPickUiLang(L"GeoPackage", L"GeoPackage")));
  SendMessageW(lb, LB_SETCURSEL, 0, 0);
}

/// 与 `AgisTilePreviewProtocol` 枚举顺序一致（列表框索引相同）。
static const wchar_t* ProtocolLongDescription(AgisTilePreviewProtocol p) {
  switch (p) {
  case AgisTilePreviewProtocol::kXyzTmsFolder:
    return AgisPickUiLang(
        L"【打开方式】\r\n点「确定」后弹出文件夹选择框，请选择「瓦片根目录」文件夹（不要选单个瓦片文件）。\r\n\r\n"
        L"【目录与格式】\r\n根目录下应为按缩放级组织的子目录与栅格瓦片（常见结构类似 z/x/y 与 .png/.jpg 等）。若磁盘上为 "
        L"TMS 行号文件名，且存在 tms.xml 或 README 标明 protocol=tms（与 AGIS 瓦片导出约定一致），预览会映射为北向上的 XYZ 方式拼图。\r\n\r\n"
        L"【范围】\r\n仅本机路径；不支持 http(s)、WMTS、网络 URL。",
        L"[How to open]\r\nClick OK, then pick the tile root folder (not a single tile file).\r\n\r\n"
        L"[Layout]\r\nExpect zoom subfolders and raster tiles (often z/x/y with .png/.jpg). If filenames use TMS row order "
        L"and tms.xml/README says protocol=tms (same as AGIS tile export), the preview maps to north-up XYZ.\r\n\r\n"
        L"[Scope]\r\nLocal paths only; no http(s), WMTS, or network URLs.");
  case AgisTilePreviewProtocol::kThreeDTilesJson:
    return AgisPickUiLang(
        L"【打开方式】\r\n点「确定」后弹出文件选择框，请选择本地「tileset.json」。\r\n\r\n"
        L"【文件说明】\r\n3D Tiles 切片集的入口描述文件。本预览窗主要展示元数据、包围体与统计信息（非「数据转换里子类型 3dtiles」时的三维网格绘制，三维网格在模型预览窗）。\r\n\r\n"
        L"【范围】\r\n仅解析磁盘上的相对路径资源；纯 http(s) 外链内容无法在此加载。",
        L"[How to open]\r\nClick OK, then choose local tileset.json.\r\n\r\n"
        L"[About]\r\nEntry JSON for a 3D Tiles tileset. This window shows metadata, bounds, and stats (not the meshed 3D "
        L"view used when subtype 3dtiles in Data conversion—that is Model preview).\r\n\r\n"
        L"[Scope]\r\nResolves relative paths on disk only; pure http(s) externals cannot load here.");
  case AgisTilePreviewProtocol::kMBTilesFile:
    return AgisPickUiLang(
        L"【打开方式】\r\n点「确定」后选择本地「.mbtiles」单文件。\r\n\r\n"
        L"【文件说明】\r\nSQLite 封装的瓦片库。本预览依赖 GDAL：将栅格层读出并下采样为一张全球墨卡托缩略拼图（约不超过 2048px），用于快速浏览内容概况。\r\n\r\n"
        L"【限制】\r\n当前不按 zoom 在容器内逐瓦交互；若构建未启用 GDAL，此项不可用。",
        L"[How to open]\r\nClick OK, then pick a local .mbtiles file.\r\n\r\n"
        L"[About]\r\nSQLite tile archive. Preview uses GDAL to read the raster layer and downsample to one global Mercator "
        L"thumbnail (≤~2048 px).\r\n\r\n"
        L"[Limits]\r\nNo per-zoom interactive tiles inside the container; unavailable without GDAL in the build.");
  case AgisTilePreviewProtocol::kGeoPackageFile:
    return AgisPickUiLang(
        L"【打开方式】\r\n点「确定」后选择本地「.gpkg」单文件。\r\n\r\n"
        L"【文件说明】\r\nOGC GeoPackage。本预览依赖 GDAL 打开其中的栅格瓦片表并下采样为缩略拼图（与 MBTiles 类似，用于概况浏览）。纯矢量 GPKG 或无栅格层时可能无法预览。\r\n\r\n"
        L"【限制】\r\n当前不按 zoom 在容器内逐瓦交互；若构建未启用 GDAL，此项不可用。",
        L"[How to open]\r\nClick OK, then pick a local .gpkg file.\r\n\r\n"
        L"[About]\r\nOGC GeoPackage. GDAL opens raster tile tables and builds a thumbnail mosaic like MBTiles. Vector-only "
        L"or raster-less GPKG may not preview.\r\n\r\n"
        L"[Limits]\r\nNo per-zoom interactive tiles; unavailable without GDAL in the build.");
  default:
    return L"";
  }
}

static void SetDetailTextForListIndex(HWND detailWnd, int listIndex) {
  if (!detailWnd) {
    return;
  }
  if (listIndex < 0 || listIndex >= static_cast<int>(AgisTilePreviewProtocol::kCount)) {
    SetWindowTextW(detailWnd, L"");
    return;
  }
  const auto p = static_cast<AgisTilePreviewProtocol>(listIndex);
  SetWindowTextW(detailWnd, ProtocolLongDescription(p));
}

static LRESULT CALLBACK ProtocolPickerWndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) {
  auto* ctx = reinterpret_cast<PickerUiCtx*>(GetWindowLongPtrW(hwnd, GWLP_USERDATA));
  switch (msg) {
    case WM_NCCREATE: {
      auto* c = reinterpret_cast<PickerUiCtx*>(reinterpret_cast<LPCREATESTRUCTW>(lParam)->lpCreateParams);
      SetWindowLongPtrW(hwnd, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(c));
      return TRUE;
    }
    case WM_CREATE: {
      ctx = reinterpret_cast<PickerUiCtx*>(GetWindowLongPtrW(hwnd, GWLP_USERDATA));
      const HINSTANCE hi = PickerModuleInstance(ctx->owner);

      RECT cr{};
      GetClientRect(hwnd, &cr);
      const int m = 12;
      const int innerW = (std::max)(1, static_cast<int>(cr.right - cr.left - 2 * m));

      TEXTMETRICW tm{};
      int lineH = 20;
      {
        HDC hdc = GetDC(hwnd);
        if (hdc) {
          const HFONT uiFont = static_cast<HFONT>(GetStockObject(DEFAULT_GUI_FONT));
          SelectObject(hdc, uiFont);
          if (GetTextMetricsW(hdc, &tm)) {
            lineH = static_cast<int>(tm.tmHeight) +
                    (std::max)(0, static_cast<int>(tm.tmExternalLeading));
          }
          ReleaseDC(hwnd, hdc);
        }
      }
      constexpr int kHintLines = 2;
      const int hintH = (std::max)(lineH * kHintLines + 10, 44);

      HWND hint = CreateWindowExW(
          0, L"STATIC",
          AgisPickUiLang(L"左侧选择数据源类型；右侧为打开方式与格式说明。\r\n"
                         L"仅本机路径，不支持网络地址。点「确定」后选择文件夹或文件。",
                         L"Pick a source type on the left; the right shows how to open and format notes.\r\n"
                         L"Local paths only—no network. Click OK to choose a folder or file."),
          WS_CHILD | WS_VISIBLE | SS_LEFT | SS_NOPREFIX, m, m, innerW, hintH, hwnd, nullptr, hi, nullptr);
      SendMessageW(hint, WM_SETFONT, reinterpret_cast<WPARAM>(GetStockObject(DEFAULT_GUI_FONT)), TRUE);

      constexpr int kGap = 10;
      const int listY = m + hintH + kGap;
      constexpr int kBtnH = 28;
      constexpr int kBtnW = 88;
      constexpr int kBtnGap = 10;
      const int btnRowTop = static_cast<int>(cr.bottom) - m - kBtnH;
      const int listH = (std::max)(120, btnRowTop - kGap - listY);

      constexpr int kListColW = 152;
      constexpr int kMidGap = 10;
      const int listW = (std::min)(kListColW, (std::max)(108, innerW * 28 / 100));
      const int detailW = (std::max)(160, innerW - listW - kMidGap);
      const int listX = m;
      const int detailX = m + listW + kMidGap;

      HWND lb = CreateWindowExW(
          WS_EX_CLIENTEDGE, L"LISTBOX", L"",
          WS_CHILD | WS_VISIBLE | WS_VSCROLL | WS_TABSTOP | LBS_NOTIFY | LBS_NOINTEGRALHEIGHT, listX, listY, listW, listH,
          hwnd, reinterpret_cast<HMENU>(static_cast<UINT_PTR>(kIdList)), hi, nullptr);
      ctx->list = lb;
      SendMessageW(lb, WM_SETFONT, reinterpret_cast<WPARAM>(GetStockObject(DEFAULT_GUI_FONT)), TRUE);
      FillProtocolList(lb);

      HWND det = CreateWindowExW(
          WS_EX_CLIENTEDGE, L"EDIT", L"",
          WS_CHILD | WS_VISIBLE | WS_VSCROLL | ES_MULTILINE | ES_READONLY | ES_AUTOVSCROLL, detailX, listY, detailW, listH,
          hwnd, reinterpret_cast<HMENU>(static_cast<UINT_PTR>(kIdDetail)), hi, nullptr);
      ctx->detail = det;
      SendMessageW(det, WM_SETFONT, reinterpret_cast<WPARAM>(GetStockObject(DEFAULT_GUI_FONT)), TRUE);
      SetDetailTextForListIndex(det, 0);

      const int cancelX = static_cast<int>(cr.right) - m - kBtnW;
      const int okX = cancelX - kBtnGap - kBtnW;
      CreateWindowExW(0, L"BUTTON", AgisPickUiLang(L"确定", L"OK"), WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_DEFPUSHBUTTON,
                      okX, btnRowTop, kBtnW, kBtnH, hwnd, reinterpret_cast<HMENU>(static_cast<UINT_PTR>(kIdOk)), hi,
                      nullptr);
      CreateWindowExW(0, L"BUTTON", AgisPickUiLang(L"取消", L"Cancel"), WS_CHILD | WS_VISIBLE | WS_TABSTOP, cancelX,
                      btnRowTop, kBtnW, kBtnH, hwnd,
                      reinterpret_cast<HMENU>(static_cast<UINT_PTR>(kIdCancel)), hi, nullptr);
      return 0;
    }
    case WM_COMMAND: {
      ctx = reinterpret_cast<PickerUiCtx*>(GetWindowLongPtrW(hwnd, GWLP_USERDATA));
      const int id = GET_WM_COMMAND_ID(wParam, lParam);
      const int cmd = GET_WM_COMMAND_CMD(wParam, lParam);
      if (id == kIdList && cmd == LBN_SELCHANGE && ctx->list && ctx->detail) {
        const LRESULT sel = SendMessageW(ctx->list, LB_GETCURSEL, 0, 0);
        if (sel != LB_ERR) {
          SetDetailTextForListIndex(ctx->detail, static_cast<int>(sel));
        }
        return 0;
      }
      if (id == kIdCancel) {
        ctx->accepted = false;
        ctx->done = true;
        DestroyWindow(hwnd);
        return 0;
      }
      if (id == kIdOk || (id == kIdList && cmd == LBN_DBLCLK)) {
        const LRESULT sel = SendMessageW(ctx->list, LB_GETCURSEL, 0, 0);
        if (sel == LB_ERR) {
          MessageBoxW(hwnd,
                      AgisPickUiLang(L"请先在列表中选择一种瓦片协议。", L"Select a tile protocol in the list first."),
                      AgisPickUiLang(L"瓦片预览", L"Tile preview"), MB_OK | MB_ICONINFORMATION);
          return 0;
        }
        const auto proto = static_cast<AgisTilePreviewProtocol>(static_cast<int>(sel));
        std::wstring p;
        std::wstring pe;
        EnableWindow(hwnd, FALSE);
        const bool ok = PickPathForProtocol(ctx->owner, proto, &p, &pe);
        EnableWindow(hwnd, TRUE);
        SetForegroundWindow(hwnd);
        if (!ok) {
          if (!pe.empty()) {
            MessageBoxW(hwnd, pe.c_str(), AgisPickUiLang(L"浏览失败", L"Browse failed"), MB_OK | MB_ICONWARNING);
          }
          return 0;
        }
        ctx->protocol = proto;
        ctx->path = std::move(p);
        ctx->accepted = true;
        ctx->done = true;
        DestroyWindow(hwnd);
        return 0;
      }
      return 0;
    }
    case WM_CLOSE:
      ctx = reinterpret_cast<PickerUiCtx*>(GetWindowLongPtrW(hwnd, GWLP_USERDATA));
      ctx->accepted = false;
      ctx->done = true;
      DestroyWindow(hwnd);
      return 0;
    case WM_DESTROY:
      return 0;
    default:
      return DefWindowProcW(hwnd, msg, wParam, lParam);
  }
}

static bool RegisterPickerClassOnce(HINSTANCE inst) {
  WNDCLASSW wc{};
  if (GetClassInfoW(inst, kPickerClassName, &wc) != 0) {
    return true;
  }
  wc.style = CS_DBLCLKS;
  wc.lpfnWndProc = ProtocolPickerWndProc;
  wc.hInstance = inst;
  wc.lpszClassName = kPickerClassName;
  wc.hbrBackground = reinterpret_cast<HBRUSH>(COLOR_WINDOW + 1);
  wc.hCursor = LoadCursor(nullptr, IDC_ARROW);
  return RegisterClassW(&wc) != 0;
}

static bool RunProtocolPickerModal(HWND owner, PickerUiCtx* ctx) {
  HINSTANCE inst = PickerModuleInstance(owner);
  if (!RegisterPickerClassOnce(inst)) {
    ctx->error = AgisPickUiLang(L"无法注册协议选择窗口（RegisterClass 失败）。",
                                L"Could not register protocol picker window class (RegisterClass failed).");
    return false;
  }
  RECT r{};
  if (owner) {
    GetWindowRect(owner, &r);
  } else {
    RECT wa{};
    SystemParametersInfoW(SPI_GETWORKAREA, 0, &wa, 0);
    r = wa;
  }
  // 目标客户区（控件在 WM_CREATE 中按 GetClientRect 排版）；外框用 AdjustWindowRectEx 换算，避免按钮落在非客户区外被裁切。
  constexpr int kClientW = 560;
  constexpr int kClientH = 320;
  RECT adj{0, 0, kClientW, kClientH};
  // 使用 OVERLAPPED+CAPTION（不用 WS_POPUP）以便标题栏稳定显示窗口标题。
  const DWORD kFrameStyle = WS_OVERLAPPED | WS_CAPTION | WS_SYSMENU | WS_CLIPCHILDREN;
  const DWORD kFrameExStyle = WS_EX_DLGMODALFRAME | WS_EX_WINDOWEDGE;
  AdjustWindowRectEx(&adj, kFrameStyle, FALSE, kFrameExStyle);
  const int kW = adj.right - adj.left;
  const int kH = adj.bottom - adj.top;
  const int x = r.left + ((std::max)(1, static_cast<int>(r.right - r.left)) - kW) / 2;
  const int y = r.top + ((std::max)(1, static_cast<int>(r.bottom - r.top)) - kH) / 2;

  ctx->accepted = false;
  ctx->done = false;
  ctx->path.clear();
  ctx->error.clear();
  ctx->owner = owner;

  HWND dlg = CreateWindowExW(kFrameExStyle, kPickerClassName, PickerWindowTitleW(), kFrameStyle, x, y, kW, kH, owner,
                             nullptr, inst, ctx);
  if (!dlg) {
    ctx->error = AgisPickUiLang(L"无法创建协议选择窗口（CreateWindow 失败）。",
                                L"Could not create protocol picker window (CreateWindow failed).");
    return false;
  }
  SetWindowTextW(dlg, PickerWindowTitleW());

  if (owner) {
    EnableWindow(owner, FALSE);
  }

  ShowWindow(dlg, SW_SHOW);
  UpdateWindow(dlg);
  SetForegroundWindow(dlg);

  MSG msg{};
  while (IsWindow(dlg)) {
    const BOOL br = GetMessageW(&msg, nullptr, 0, 0);
    if (br == 0) {
      PostQuitMessage(static_cast<int>(msg.wParam));
      break;
    }
    if (br == -1) {
      break;
    }
    if (msg.hwnd == dlg || IsChild(dlg, msg.hwnd)) {
      if (!IsDialogMessageW(dlg, &msg)) {
        TranslateMessage(&msg);
        DispatchMessageW(&msg);
      }
    } else {
      TranslateMessage(&msg);
      DispatchMessageW(&msg);
    }
  }

  if (owner) {
    EnableWindow(owner, TRUE);
    SetForegroundWindow(owner);
  }
  return ctx->accepted;
}

}  // namespace

bool TilePreviewShowProtocolAndPickPath(HWND owner, std::wstring* pathOut, AgisTilePreviewProtocol* protocolOut,
                                        std::wstring* pickerErrorOut) {
  if (!pathOut || !protocolOut) {
    return false;
  }
  pathOut->clear();
  if (pickerErrorOut) {
    pickerErrorOut->clear();
  }
  PickerUiCtx ctx{};
  const bool ran = RunProtocolPickerModal(owner, &ctx);
  if (!ctx.error.empty() && pickerErrorOut) {
    *pickerErrorOut = ctx.error;
  }
  if (!ran || !ctx.accepted || ctx.path.empty()) {
    return false;
  }
  *pathOut = std::move(ctx.path);
  *protocolOut = ctx.protocol;
  return true;
}

bool TilePreviewValidatePathMatchesProtocol(AgisTilePreviewProtocol protocol, const std::wstring& path,
                                            std::wstring* mismatchMessageOut) {
  if (mismatchMessageOut) {
    mismatchMessageOut->clear();
  }
  auto fail = [&](const wchar_t* msg) {
    if (mismatchMessageOut) {
      *mismatchMessageOut = msg;
    }
    return false;
  };

  std::error_code ec;
  const std::filesystem::path fp(path);
  const bool isDir = std::filesystem::is_directory(fp, ec);
  const bool isFile = std::filesystem::is_regular_file(fp, ec);
  if (!isDir && !isFile) {
    return fail(AgisPickUiLang(L"路径不存在，或无法访问（不是有效的文件/目录）。",
                               L"Path does not exist or is inaccessible (not a valid file or folder)."));
  }

  switch (protocol) {
  case AgisTilePreviewProtocol::kXyzTmsFolder:
    if (!isDir) {
      return fail(AgisPickUiLang(L"该协议需要选择「文件夹」（瓦片根目录）。当前路径不是目录。",
                                 L"This protocol needs a folder (tile root). The path is not a directory."));
    }
    return true;
  case AgisTilePreviewProtocol::kThreeDTilesJson:
    if (!isFile) {
      return fail(AgisPickUiLang(L"3D Tiles 需要选择 tileset.json「文件」，不能选择文件夹。",
                                 L"3D Tiles needs a tileset.json file, not a folder."));
    }
    if (_wcsicmp(fp.extension().c_str(), L".json") != 0) {
      return fail(AgisPickUiLang(L"请选择扩展名为 .json 的文件（通常为 tileset.json）。",
                                 L"Choose a .json file (usually tileset.json)."));
    }
    return true;
  case AgisTilePreviewProtocol::kMBTilesFile:
    if (!isFile) {
      return fail(AgisPickUiLang(L"MBTiles 需要选择单个 .mbtiles 文件。", L"MBTiles needs a single .mbtiles file."));
    }
    if (_wcsicmp(fp.extension().c_str(), L".mbtiles") != 0) {
      return fail(AgisPickUiLang(L"扩展名应为 .mbtiles。", L"Extension should be .mbtiles."));
    }
    return true;
  case AgisTilePreviewProtocol::kGeoPackageFile:
    if (!isFile) {
      return fail(AgisPickUiLang(L"GeoPackage 需要选择单个 .gpkg 文件。", L"GeoPackage needs a single .gpkg file."));
    }
    if (_wcsicmp(fp.extension().c_str(), L".gpkg") != 0) {
      return fail(AgisPickUiLang(L"扩展名应为 .gpkg。", L"Extension should be .gpkg."));
    }
    return true;
  default:
    return fail(AgisPickUiLang(L"内部错误：未知协议。", L"Internal error: unknown protocol."));
  }
}
