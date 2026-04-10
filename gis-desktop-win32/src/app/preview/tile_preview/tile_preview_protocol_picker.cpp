#include "app/preview/tile_preview/tile_preview_protocol_picker.h"

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
constexpr int kIdList = 1001;
constexpr int kIdOk = 1002;
constexpr int kIdCancel = 1003;

struct PickerUiCtx {
  HWND owner = nullptr;
  HWND list = nullptr;
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
    pfd->SetTitle(L"选择 XYZ / TMS 瓦片根目录");
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
  bi.lpszTitle = L"选择 XYZ / TMS 瓦片根目录（备用文件夹浏览器）";
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
      *err = L"无法解析所选文件夹路径（SHGetPathFromIDList）。";
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
    return PickOpenFile(owner, L"选择 3D Tiles 的 tileset.json",
                        L"tileset.json\0tileset.json\0JSON\0*.json\0所有文件\0*.*\0\0", pathOut);
  case AgisTilePreviewProtocol::kSingleRasterImage:
    return PickOpenFile(owner, L"选择栅格图像",
                        L"栅格图像\0*.png;*.jpg;*.jpeg;*.webp;*.bmp;*.tif;*.tiff\0所有文件\0*.*\0\0", pathOut);
  case AgisTilePreviewProtocol::kMBTilesFile:
    return PickOpenFile(owner, L"选择 MBTiles 文件", L"MBTiles\0*.mbtiles\0所有文件\0*.*\0\0", pathOut);
  case AgisTilePreviewProtocol::kGeoPackageFile:
    return PickOpenFile(owner, L"选择 GeoPackage 文件", L"GeoPackage\0*.gpkg\0所有文件\0*.*\0\0", pathOut);
  default:
    if (err) {
      *err = L"内部错误：未知瓦片协议。";
    }
    return false;
  }
}

static void FillProtocolList(HWND lb) {
  SendMessageW(lb, LB_RESETCONTENT, 0, 0);
  SendMessageW(lb, LB_ADDSTRING, 0, reinterpret_cast<LPARAM>(L"XYZ / TMS — 平面金字塔（选择瓦片根目录）"));
  SendMessageW(lb, LB_ADDSTRING, 0, reinterpret_cast<LPARAM>(L"3D Tiles — tileset.json（选择文件）"));
  SendMessageW(lb, LB_ADDSTRING, 0, reinterpret_cast<LPARAM>(L"单张栅格 — PNG / JPG / WebP / BMP / TIF（选择文件）"));
  SendMessageW(lb, LB_ADDSTRING, 0, reinterpret_cast<LPARAM>(L"MBTiles — .mbtiles（选择文件，需 GDAL）"));
  SendMessageW(lb, LB_ADDSTRING, 0, reinterpret_cast<LPARAM>(L"GeoPackage — .gpkg（选择文件，需 GDAL）"));
  SendMessageW(lb, LB_SETCURSEL, 0, 0);
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
      const int cw = (std::max)(1, static_cast<int>(cr.right - cr.left - 2 * m));

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
          L"请选择要打开的瓦片协议。\r\n"
          L"点「确定」后将弹出文件夹或文件选择对话框。",
          WS_CHILD | WS_VISIBLE | SS_LEFT | SS_NOPREFIX, m, m, cw, hintH, hwnd, nullptr, hi, nullptr);
      SendMessageW(hint, WM_SETFONT, reinterpret_cast<WPARAM>(GetStockObject(DEFAULT_GUI_FONT)), TRUE);

      constexpr int kGap = 10;
      const int listY = m + hintH + kGap;
      constexpr int kBtnH = 28;
      constexpr int kBtnW = 88;
      constexpr int kBtnGap = 10;
      const int btnRowTop = static_cast<int>(cr.bottom) - m - kBtnH;
      const int listH = (std::max)(100, btnRowTop - kGap - listY);

      HWND lb = CreateWindowExW(
          WS_EX_CLIENTEDGE, L"LISTBOX", L"",
          WS_CHILD | WS_VISIBLE | WS_VSCROLL | WS_TABSTOP | LBS_NOTIFY | LBS_NOINTEGRALHEIGHT, m, listY, cw, listH, hwnd,
          reinterpret_cast<HMENU>(static_cast<UINT_PTR>(kIdList)), hi, nullptr);
      ctx->list = lb;
      SendMessageW(lb, WM_SETFONT, reinterpret_cast<WPARAM>(GetStockObject(DEFAULT_GUI_FONT)), TRUE);
      FillProtocolList(lb);

      const int cancelX = static_cast<int>(cr.right) - m - kBtnW;
      const int okX = cancelX - kBtnGap - kBtnW;
      CreateWindowExW(0, L"BUTTON", L"确定", WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_DEFPUSHBUTTON, okX, btnRowTop,
                      kBtnW, kBtnH, hwnd, reinterpret_cast<HMENU>(static_cast<UINT_PTR>(kIdOk)), hi, nullptr);
      CreateWindowExW(0, L"BUTTON", L"取消", WS_CHILD | WS_VISIBLE | WS_TABSTOP, cancelX, btnRowTop, kBtnW, kBtnH, hwnd,
                      reinterpret_cast<HMENU>(static_cast<UINT_PTR>(kIdCancel)), hi, nullptr);
      return 0;
    }
    case WM_COMMAND: {
      ctx = reinterpret_cast<PickerUiCtx*>(GetWindowLongPtrW(hwnd, GWLP_USERDATA));
      const int id = GET_WM_COMMAND_ID(wParam, lParam);
      if (id == kIdCancel) {
        ctx->accepted = false;
        ctx->done = true;
        DestroyWindow(hwnd);
        return 0;
      }
      if (id == kIdOk || (id == kIdList && GET_WM_COMMAND_CMD(wParam, lParam) == LBN_DBLCLK)) {
        const LRESULT sel = SendMessageW(ctx->list, LB_GETCURSEL, 0, 0);
        if (sel == LB_ERR) {
          MessageBoxW(hwnd, L"请先在列表中选择一种瓦片协议。", L"瓦片预览", MB_OK | MB_ICONINFORMATION);
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
            MessageBoxW(hwnd, pe.c_str(), L"浏览失败", MB_OK | MB_ICONWARNING);
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
    ctx->error = L"无法注册协议选择窗口（RegisterClass 失败）。";
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
  constexpr int kClientW = 468;
  constexpr int kClientH = 288;
  RECT adj{0, 0, kClientW, kClientH};
  const DWORD kFrameStyle = WS_POPUP | WS_CAPTION | WS_SYSMENU;
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

  HWND dlg = CreateWindowExW(kFrameExStyle, kPickerClassName, L"打开瓦片 — 选择协议", kFrameStyle, x, y, kW, kH, owner,
                             nullptr, inst, ctx);
  if (!dlg) {
    ctx->error = L"无法创建协议选择窗口（CreateWindow 失败）。";
    return false;
  }

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
    return fail(L"路径不存在，或无法访问（不是有效的文件/目录）。");
  }

  switch (protocol) {
  case AgisTilePreviewProtocol::kXyzTmsFolder:
    if (!isDir) {
      return fail(L"该协议需要选择「文件夹」（瓦片根目录）。当前路径不是目录。");
    }
    return true;
  case AgisTilePreviewProtocol::kThreeDTilesJson:
    if (!isFile) {
      return fail(L"3D Tiles 需要选择 tileset.json「文件」，不能选择文件夹。");
    }
    if (_wcsicmp(fp.extension().c_str(), L".json") != 0) {
      return fail(L"请选择扩展名为 .json 的文件（通常为 tileset.json）。");
    }
    return true;
  case AgisTilePreviewProtocol::kSingleRasterImage:
    if (!isFile) {
      return fail(L"单张栅格需要选择图像「文件」。");
    }
    {
      const std::wstring ext = fp.extension().wstring();
      const wchar_t* kOk[] = {L".png", L".jpg", L".jpeg", L".webp", L".bmp", L".tif", L".tiff"};
      bool okex = false;
      for (const wchar_t* e : kOk) {
        if (_wcsicmp(ext.c_str(), e) == 0) {
          okex = true;
          break;
        }
      }
      if (!okex) {
        return fail(L"不支持的栅格扩展名。请选择 PNG / JPG / JPEG / WebP / BMP / TIF。");
      }
    }
    return true;
  case AgisTilePreviewProtocol::kMBTilesFile:
    if (!isFile) {
      return fail(L"MBTiles 需要选择单个 .mbtiles 文件。");
    }
    if (_wcsicmp(fp.extension().c_str(), L".mbtiles") != 0) {
      return fail(L"扩展名应为 .mbtiles。");
    }
    return true;
  case AgisTilePreviewProtocol::kGeoPackageFile:
    if (!isFile) {
      return fail(L"GeoPackage 需要选择单个 .gpkg 文件。");
    }
    if (_wcsicmp(fp.extension().c_str(), L".gpkg") != 0) {
      return fail(L"扩展名应为 .gpkg。");
    }
    return true;
  default:
    return fail(L"内部错误：未知协议。");
  }
}
