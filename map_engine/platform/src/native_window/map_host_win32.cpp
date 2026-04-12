#include "map_host_win32.h"

#include "map_engine/map_engine.h"
#include "map_engine/map.h"
#include "map_engine/render_device_context.h"
#include "native_window_win32.h"

#include "core/resource.h"
#include "core/app_log.h"
#include "utils/agis_ui_l10n.h"
#include "utils/ui_font.h"
#include "ui_engine/gdiplus_ui.h"

#include <algorithm>
#include <cstring>
#include <vector>

#include <windowsx.h>
#include <commctrl.h>

#pragma comment(lib, "comctl32.lib")

#include <gdiplus.h>

namespace {

constexpr UINT_PTR kMapHostChildInputSubclassId = static_cast<UINT_PTR>(0x41474953u);

LRESULT CALLBACK MapHostChildInputSubclass(HWND h, UINT m, WPARAM w, LPARAM l, UINT_PTR subclassId,
                                           DWORD_PTR mapHwndAsPtr) {
  (void)subclassId;
  const HWND mapHwnd = reinterpret_cast<HWND>(mapHwndAsPtr);
  switch (m) {
    case WM_MOUSEWHEEL:
    case WM_MOUSEHWHEEL:
      if (mapHwnd && IsWindow(mapHwnd)) {
        return SendMessageW(mapHwnd, m, w, l);
      }
      break;
    case WM_MBUTTONDOWN:
    case WM_MBUTTONUP:
    case WM_MBUTTONDBLCLK:
      if (mapHwnd && IsWindow(mapHwnd)) {
        POINT pt{GET_X_LPARAM(l), GET_Y_LPARAM(l)};
        MapWindowPoints(h, mapHwnd, &pt, 1);
        return SendMessageW(mapHwnd, m, w, MAKELPARAM(pt.x, pt.y));
      }
      break;
    default:
      break;
  }
  return DefSubclassProc(h, m, w, l);
}

void MapHostAttachChildInputForwarding(HWND mapHwnd) {
  for (HWND c = GetWindow(mapHwnd, GW_CHILD); c; c = GetWindow(c, GW_HWNDNEXT)) {
    SetWindowSubclass(c, MapHostChildInputSubclass, kMapHostChildInputSubclassId,
                      reinterpret_cast<DWORD_PTR>(mapHwnd));
  }
}

void MapHostDetachChildInputForwarding(HWND mapHwnd) {
  for (HWND c = GetWindow(mapHwnd, GW_CHILD); c; c = GetWindow(c, GW_HWNDNEXT)) {
    RemoveWindowSubclass(c, MapHostChildInputSubclass, kMapHostChildInputSubclassId);
  }
}

void MapHostApplyUiFontToChildren(HWND parent) {
  const HFONT f = UiGetAppFont();
  for (HWND c = GetWindow(parent, GW_CHILD); c; c = GetWindow(c, GW_HWNDNEXT)) {
    SendMessageW(c, WM_SETFONT, reinterpret_cast<WPARAM>(f), TRUE);
  }
}

void LayoutMapOverlayControls(HWND hwnd) {
  RECT r{};
  GetClientRect(hwnd, &r);
  const int cw = (std::max)(0, static_cast<int>(r.right));
  const int ch = (std::max)(0, static_cast<int>(r.bottom));
  constexpr int m = 8;
  constexpr int btnH = 22;
  constexpr int btnW = 52;
  constexpr int rowGap = 4;
  HWND hShortcut = GetDlgItem(hwnd, IDC_MAP_SHORTCUT_TOGGLE);
  HWND hEdit = GetDlgItem(hwnd, IDC_MAP_SHORTCUT_EDIT);
  HWND hVis = GetDlgItem(hwnd, IDC_MAP_VIS_TOGGLE);
  HWND hGrid = GetDlgItem(hwnd, IDC_MAP_VIS_GRID);
  HWND hFit = GetDlgItem(hwnd, IDC_MAP_FIT);
  HWND hOrig = GetDlgItem(hwnd, IDC_MAP_ORIGIN);
  HWND hReset = GetDlgItem(hwnd, IDC_MAP_RESET);
  HWND hZm = GetDlgItem(hwnd, IDC_MAP_ZOOM_OUT);
  HWND hZp = GetDlgItem(hwnd, IDC_MAP_ZOOM_IN);
  HWND hSc = GetDlgItem(hwnd, IDC_MAP_SCALE_TEXT);
  if (hShortcut) {
    MoveWindow(hShortcut, m, m, 80, btnH, TRUE);
  }
  if (hEdit) {
    const int eh = 100;
    const int ew = (std::max)(120, (std::min)(300, cw - 2 * m));
    MoveWindow(hEdit, m, m + btnH + rowGap, ew, eh, TRUE);
  }
  if (hVis) {
    MoveWindow(hVis, (std::max)(m, cw - 108), m, 100, btnH, TRUE);
  }
  if (hGrid) {
    const int gx = (std::max)(m, cw - 224);
    const int gy = m + btnH + rowGap;
    MoveWindow(hGrid, gx, gy, 216, btnH, TRUE);
  }
  const int bottomY = (std::max)(m + btnH + rowGap + 4, ch - 2 * btnH - 2 * m);
  const int bottomY2 = ch - m - btnH;
  if (hFit) {
    MoveWindow(hFit, m, bottomY, btnW, btnH, TRUE);
  }
  if (hOrig) {
    MoveWindow(hOrig, m + btnW + rowGap, bottomY, btnW, btnH, TRUE);
  }
  if (hReset) {
    MoveWindow(hReset, m + 2 * (btnW + rowGap), bottomY, btnW, btnH, TRUE);
  }
  if (hZm && hSc && hZp) {
    const int scaleW = 56;
    MoveWindow(hZm, m, bottomY2, 28, btnH, TRUE);
    MoveWindow(hSc, m + 30, bottomY2, scaleW, btnH, TRUE);
    MoveWindow(hZp, m + 30 + scaleW + rowGap, bottomY2, 28, btnH, TRUE);
  }

  MapEngine& eg = MapEngine::Instance();
  const MapRenderBackendType mapRb = eg.GetRenderBackend();
  const bool hideNativeChrome = MapGpu_Is3DBackend(mapRb);
  if (hideNativeChrome) {
    for (HWND h : {hShortcut, hEdit, hVis, hGrid, hFit, hOrig, hReset, hZm, hSc, hZp}) {
      if (h) {
        ShowWindow(h, SW_HIDE);
      }
    }
    return;
  }
  if (hShortcut) {
    ShowWindow(hShortcut, eg.IsMapUiShowShortcutChrome() ? SW_SHOW : SW_HIDE);
  }
  if (hEdit) {
    ShowWindow(hEdit, (eg.IsMapUiShowShortcutChrome() && eg.IsMapShortcutHelpExpanded()) ? SW_SHOW : SW_HIDE);
  }
  if (hVis) {
    ShowWindow(hVis, eg.IsMapUiShowVisChrome() ? SW_SHOW : SW_HIDE);
  }
  if (hGrid) {
    ShowWindow(hGrid, (eg.IsMapUiShowVisChrome() && eg.IsMapVisibilityPanelExpanded()) ? SW_SHOW : SW_HIDE);
  }
  for (HWND h : {hFit, hOrig, hReset, hZm, hSc, hZp}) {
    if (h) {
      ShowWindow(h, eg.IsMapUiShowBottomChrome() ? SW_SHOW : SW_HIDE);
    }
  }
}

}  // namespace

bool MapHostRenderClientToTopDownBgra(HWND hwnd, const RECT& client, std::vector<std::uint8_t>* outPixels) {
  if (!outPixels) {
    return false;
  }
  const int cw = client.right - client.left;
  const int ch = client.bottom - client.top;
  if (cw <= 0 || ch <= 0) {
    return false;
  }
  BITMAPINFO bi{};
  bi.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
  bi.bmiHeader.biWidth = cw;
  bi.bmiHeader.biHeight = -ch;
  bi.bmiHeader.biPlanes = 1;
  bi.bmiHeader.biBitCount = 32;
  bi.bmiHeader.biCompression = BI_RGB;
  void* bits = nullptr;
  HDC hdc = GetDC(hwnd);
  if (!hdc) {
    return false;
  }
  HDC mem = CreateCompatibleDC(hdc);
  if (!mem) {
    ReleaseDC(hwnd, hdc);
    return false;
  }
  HBITMAP dib = CreateDIBSection(hdc, &bi, DIB_RGB_COLORS, &bits, nullptr, 0);
  if (!dib || !bits) {
    DeleteDC(mem);
    ReleaseDC(hwnd, hdc);
    return false;
  }
  const HGDIOBJ old = SelectObject(mem, dib);
  const RECT inner{0, 0, cw, ch};
  MapEngine::Instance().Document().Draw(mem, inner);
  if (MapEngine::Instance().IsMapUiShowHintOverlay()) {
    UiPaintMapHintOverlay(mem, inner, AgisTr(AgisUiStr::MapHintPanZoom));
  }
  const size_t nbytes = static_cast<size_t>(cw) * static_cast<size_t>(ch) * 4u;
  outPixels->resize(nbytes);
  std::memcpy(outPixels->data(), bits, nbytes);
  SelectObject(mem, old);
  DeleteObject(dib);
  DeleteDC(mem);
  ReleaseDC(hwnd, hdc);
  return true;
}

AGIS_MAP_ENGINE_API LRESULT CALLBACK MapHostProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) {
  MapEngine& eng = MapEngine::Instance();
  switch (msg) {
    case WM_CREATE:
      eng.mapHwnd_ = hwnd;
      eng.doc_.view = DefaultGeographicView();
      eng.doc_.refViewWidthDeg = 360.0;
      eng.doc_.refViewHeightDeg = 180.0;
      if (!eng.ensureRenderDeviceContext()->init(hwnd, eng.mapRenderBackend_)) {
        AppLogLine(AgisTr(AgisUiStr::MapLogGpuInitFail));
        eng.mapRenderBackend_ = MapRenderBackendType::kGdi;
        eng.renderDeviceContext_->init(hwnd, MapRenderBackendType::kGdi);
      }
      {
        RECT cr{};
        GetClientRect(hwnd, &cr);
        eng.renderDeviceContext_->onResize(cr.right - cr.left, cr.bottom - cr.top);
      }
      {
        HINSTANCE inst = GetModuleHandleW(nullptr);
        CreateWindowW(L"BUTTON", AgisTr(AgisUiStr::MapBtnShortcutCollapsed), WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON, 8, 8,
                      80, 22, hwnd, reinterpret_cast<HMENU>(static_cast<INT_PTR>(IDC_MAP_SHORTCUT_TOGGLE)), inst,
                      nullptr);
        CreateWindowExW(WS_EX_CLIENTEDGE, L"EDIT", AgisTr(AgisUiStr::MapShortcutHelpBody),
                        WS_CHILD | ES_MULTILINE | ES_READONLY | ES_AUTOVSCROLL | WS_VSCROLL | WS_TABSTOP, 8, 36, 280,
                        100, hwnd, reinterpret_cast<HMENU>(static_cast<INT_PTR>(IDC_MAP_SHORTCUT_EDIT)), inst, nullptr);
        ShowWindow(GetDlgItem(hwnd, IDC_MAP_SHORTCUT_EDIT), SW_HIDE);
        CreateWindowW(L"BUTTON", AgisTr(AgisUiStr::MapBtnVisExpanded), WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON, 200, 8, 100,
                      22, hwnd, reinterpret_cast<HMENU>(static_cast<INT_PTR>(IDC_MAP_VIS_TOGGLE)), inst, nullptr);
        CreateWindowW(L"BUTTON", AgisTr(AgisUiStr::MapChkLatLonGrid), WS_CHILD | WS_VISIBLE | BS_AUTOCHECKBOX | WS_TABSTOP,
                      200, 36, 120, 22, hwnd, reinterpret_cast<HMENU>(static_cast<INT_PTR>(IDC_MAP_VIS_GRID)), inst,
                      nullptr);
        SendMessageW(GetDlgItem(hwnd, IDC_MAP_VIS_GRID), BM_SETCHECK, BST_CHECKED, 0);
        CreateWindowW(L"BUTTON", AgisTr(AgisUiStr::MapBtnFit), WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON | WS_TABSTOP, 8, 200,
                      52, 22, hwnd, reinterpret_cast<HMENU>(static_cast<INT_PTR>(IDC_MAP_FIT)), inst, nullptr);
        CreateWindowW(L"BUTTON", AgisTr(AgisUiStr::MapBtnOrigin), WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON | WS_TABSTOP, 64,
                      200, 52, 22, hwnd, reinterpret_cast<HMENU>(static_cast<INT_PTR>(IDC_MAP_ORIGIN)), inst, nullptr);
        CreateWindowW(L"BUTTON", AgisTr(AgisUiStr::MapBtnReset), WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON | WS_TABSTOP, 120,
                      200, 52, 22, hwnd, reinterpret_cast<HMENU>(static_cast<INT_PTR>(IDC_MAP_RESET)), inst, nullptr);
        CreateWindowW(L"BUTTON", L"−", WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON | WS_TABSTOP, 8, 228, 28, 22, hwnd,
                      reinterpret_cast<HMENU>(static_cast<INT_PTR>(IDC_MAP_ZOOM_OUT)), inst, nullptr);
        eng.mapChromeScale_ =
            CreateWindowW(L"STATIC", L"100%", WS_CHILD | WS_VISIBLE | SS_CENTER | SS_CENTERIMAGE, 40, 228, 56, 22,
                          hwnd, reinterpret_cast<HMENU>(static_cast<INT_PTR>(IDC_MAP_SCALE_TEXT)), inst, nullptr);
        CreateWindowW(L"BUTTON", L"+", WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON | WS_TABSTOP, 100, 228, 28, 22, hwnd,
                      reinterpret_cast<HMENU>(static_cast<INT_PTR>(IDC_MAP_ZOOM_IN)), inst, nullptr);
        LayoutMapOverlayControls(hwnd);
        MapHostApplyUiFontToChildren(hwnd);
        MapHostAttachChildInputForwarding(hwnd);
        eng.UpdateMapChrome();
      }
      return 0;
    case WM_DESTROY:
      MapHostDetachChildInputForwarding(hwnd);
      if (eng.renderDeviceContext_) {
        eng.renderDeviceContext_->shutdown(hwnd);
      }
      if (auto* nw = dynamic_cast<NativeWindowWin32*>(eng.DefaultMapView().nativeWindow())) {
        nw->attachRenderDeviceContext(nullptr);
      }
      eng.renderDeviceContext_.reset();
      eng.mapHwnd_ = nullptr;
      eng.mapChromeScale_ = nullptr;
      return 0;
    case WM_SIZE: {
      int nw = LOWORD(lParam);
      int nh = HIWORD(lParam);
      if (wParam == SIZE_MINIMIZED) {
        nw = 0;
        nh = 0;
      }
      if (eng.renderDeviceContext_) {
        eng.renderDeviceContext_->onResize(nw, nh);
      }
      LayoutMapOverlayControls(hwnd);
      InvalidateRect(hwnd, nullptr, FALSE);
      return 0;
    }
    case WM_COMMAND: {
      const int id = LOWORD(wParam);
      HWND ctl = reinterpret_cast<HWND>(lParam);
      if (ctl == nullptr) {
        if (id >= ID_VIEW_RENDER_FIRST && id <= ID_VIEW_RENDER_LAST) {
          MapRenderBackendType b = MapRenderBackendType::kGdi;
          if (id == ID_VIEW_RENDER_GDIPLUS) {
            b = MapRenderBackendType::kGdiPlus;
          } else if (id == ID_VIEW_RENDER_D2D) {
            b = MapRenderBackendType::kD2d;
          } else if (id == ID_VIEW_RENDER_BGFX_D3D11) {
            b = MapRenderBackendType::kBgfxD3d11;
          } else if (id == ID_VIEW_RENDER_BGFX_OPENGL) {
            b = MapRenderBackendType::kBgfxOpenGL;
          } else if (id == ID_VIEW_RENDER_BGFX_AUTO) {
            b = MapRenderBackendType::kBgfxAuto;
          }
          eng.SetRenderBackend(b);
          LayoutMapOverlayControls(hwnd);
          return 0;
        }
        if (id == IDC_MAP_UI_SHOW_SHORTCUT) {
          eng.mapUiShowShortcut_ = !eng.mapUiShowShortcut_;
          LayoutMapOverlayControls(hwnd);
          InvalidateRect(hwnd, nullptr, FALSE);
          return 0;
        }
        if (id == IDC_MAP_UI_SHOW_VIS) {
          eng.mapUiShowVis_ = !eng.mapUiShowVis_;
          LayoutMapOverlayControls(hwnd);
          InvalidateRect(hwnd, nullptr, FALSE);
          return 0;
        }
        if (id == IDC_MAP_UI_SHOW_BOTTOM) {
          eng.mapUiShowBottom_ = !eng.mapUiShowBottom_;
          LayoutMapOverlayControls(hwnd);
          InvalidateRect(hwnd, nullptr, FALSE);
          return 0;
        }
        if (id == IDC_MAP_UI_SHOW_HINT) {
          eng.mapUiShowHint_ = !eng.mapUiShowHint_;
          InvalidateRect(hwnd, nullptr, FALSE);
          return 0;
        }
        if (id == IDC_MAP_UI_GRID) {
          const bool on = !eng.doc_.GetShowLatLonGrid();
          eng.doc_.SetShowLatLonGrid(on);
          if (HWND g = GetDlgItem(hwnd, IDC_MAP_VIS_GRID)) {
            SendMessageW(g, BM_SETCHECK, on ? BST_CHECKED : BST_UNCHECKED, 0);
          }
          InvalidateRect(hwnd, nullptr, FALSE);
          return 0;
        }
        break;
      }
      const int code = HIWORD(wParam);
      if (code == BN_CLICKED) {
        RECT r{};
        GetClientRect(hwnd, &r);
        const int cw = r.right - r.left;
        const int ch = r.bottom - r.top;
        if (id == IDC_MAP_SHORTCUT_TOGGLE) {
          eng.mapShortcutExpanded_ = !eng.mapShortcutExpanded_;
          if (HWND hEd = GetDlgItem(hwnd, IDC_MAP_SHORTCUT_EDIT)) {
            ShowWindow(hEd, eng.mapShortcutExpanded_ ? SW_SHOW : SW_HIDE);
          }
          SetWindowTextW(GetDlgItem(hwnd, IDC_MAP_SHORTCUT_TOGGLE),
                         eng.mapShortcutExpanded_ ? AgisTr(AgisUiStr::MapBtnShortcutExpanded)
                                                : AgisTr(AgisUiStr::MapBtnShortcutCollapsed));
          LayoutMapOverlayControls(hwnd);
          InvalidateRect(hwnd, nullptr, FALSE);
          return 0;
        }
        if (id == IDC_MAP_VIS_TOGGLE) {
          eng.mapVisExpanded_ = !eng.mapVisExpanded_;
          if (HWND hGr = GetDlgItem(hwnd, IDC_MAP_VIS_GRID)) {
            ShowWindow(hGr, eng.mapVisExpanded_ ? SW_SHOW : SW_HIDE);
          }
          SetWindowTextW(GetDlgItem(hwnd, IDC_MAP_VIS_TOGGLE),
                         eng.mapVisExpanded_ ? AgisTr(AgisUiStr::MapBtnVisExpanded)
                                             : AgisTr(AgisUiStr::MapBtnVisCollapsed));
          LayoutMapOverlayControls(hwnd);
          InvalidateRect(hwnd, nullptr, FALSE);
          return 0;
        }
        if (id == IDC_MAP_VIS_GRID) {
          const UINT st = static_cast<UINT>(
              SendMessageW(GetDlgItem(hwnd, IDC_MAP_VIS_GRID), BM_GETCHECK, 0, 0));
          eng.doc_.SetShowLatLonGrid(st == BST_CHECKED);
          InvalidateRect(hwnd, nullptr, FALSE);
          return 0;
        }
        if (id == IDC_MAP_FIT) {
          eng.doc_.FitViewToLayers();
          InvalidateRect(hwnd, nullptr, FALSE);
          return 0;
        }
        if (id == IDC_MAP_ORIGIN) {
          eng.doc_.CenterContentOrigin(cw, ch);
          InvalidateRect(hwnd, nullptr, FALSE);
          return 0;
        }
        if (id == IDC_MAP_RESET) {
          eng.doc_.ResetZoom100AnchorCenter(cw, ch);
          InvalidateRect(hwnd, nullptr, FALSE);
          return 0;
        }
        if (id == IDC_MAP_ZOOM_OUT) {
          eng.doc_.ZoomViewAtCenter(1.0 / 1.1, cw, ch);
          InvalidateRect(hwnd, nullptr, FALSE);
          return 0;
        }
        if (id == IDC_MAP_ZOOM_IN) {
          eng.doc_.ZoomViewAtCenter(1.1, cw, ch);
          InvalidateRect(hwnd, nullptr, FALSE);
          return 0;
        }
      }
      break;
    }
    case WM_ERASEBKGND:
      return 1;
    case WM_CONTEXTMENU:
      return 0;
    case WM_LBUTTONDOWN:
      SetFocus(hwnd);
      return 0;
    case WM_MBUTTONDOWN:
      if (eng.renderDeviceContext_ &&
          eng.renderDeviceContext_->imguiMapToolbarHitClient(GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam))) {
        return 0;
      }
      SetFocus(hwnd);
      eng.mdrag_ = true;
      eng.mlast_.x = GET_X_LPARAM(lParam);
      eng.mlast_.y = GET_Y_LPARAM(lParam);
      SetCapture(hwnd);
      return 0;
    case WM_MBUTTONUP:
      if (eng.mdrag_) {
        eng.mdrag_ = false;
        ReleaseCapture();
      }
      return 0;
    case WM_MOUSEWHEEL: {
      int delta = GET_WHEEL_DELTA_WPARAM(wParam);
      RECT r{};
      GetClientRect(hwnd, &r);
      const int cw = r.right - r.left;
      const int ch = r.bottom - r.top;
      POINT pt{GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam)};
      ScreenToClient(hwnd, &pt);
      const int mx = pt.x;
      const int my = pt.y;
      if (eng.renderDeviceContext_ && eng.renderDeviceContext_->imguiMapToolbarHitClient(mx, my)) {
        return 0;
      }
      const double factor = delta > 0 ? 1.1 : 1.0 / 1.1;
      eng.doc_.ZoomAt(mx, my, cw, ch, factor);
      RedrawWindow(hwnd, nullptr, nullptr,
                   RDW_INVALIDATE | RDW_ERASE | RDW_UPDATENOW | RDW_ALLCHILDREN);
      return 0;
    }
    case WM_MOUSEMOVE:
      if (eng.mdrag_ && (wParam & MK_MBUTTON) != 0) {
        const int x = GET_X_LPARAM(lParam);
        const int y = GET_Y_LPARAM(lParam);
        const int dx = x - eng.mlast_.x;
        const int dy = y - eng.mlast_.y;
        eng.mlast_.x = x;
        eng.mlast_.y = y;
        RECT r2{};
        GetClientRect(hwnd, &r2);
        eng.doc_.PanPixels(dx, dy, r2.right - r2.left, r2.bottom - r2.top);
        InvalidateRect(hwnd, nullptr, FALSE);
      }
      return 0;
    case WM_CAPTURECHANGED:
      if (reinterpret_cast<HWND>(lParam) != hwnd) {
        eng.mdrag_ = false;
      }
      return 0;
    case WM_PAINT: {
      PAINTSTRUCT ps{};
      HDC hdc = BeginPaint(hwnd, &ps);
      RECT client{};
      GetClientRect(hwnd, &client);
      const int cw = client.right - client.left;
      const int ch = client.bottom - client.top;
      const MapRenderBackendType paintRb =
          eng.renderDeviceContext_ ? eng.renderDeviceContext_->activeBackend() : MapRenderBackendType::kGdi;
      if (paintRb == MapRenderBackendType::kGdi) {
        HDC mem = CreateCompatibleDC(hdc);
        HBITMAP bmp = CreateCompatibleBitmap(hdc, cw, ch);
        const HGDIOBJ oldBmp = SelectObject(mem, bmp);
        eng.doc_.Draw(mem, client);
        if (eng.IsMapUiShowHintOverlay()) {
          UiPaintMapHintOverlay(mem, client, AgisTr(AgisUiStr::MapHintPanZoom));
        }
        BitBlt(hdc, 0, 0, cw, ch, mem, 0, 0, SRCCOPY);
        SelectObject(mem, oldBmp);
        DeleteObject(bmp);
        DeleteDC(mem);
      } else if (paintRb == MapRenderBackendType::kGdiPlus) {
        Gdiplus::Bitmap off(cw, ch, PixelFormat32bppPARGB);
        Gdiplus::Graphics gx(&off);
        gx.SetSmoothingMode(Gdiplus::SmoothingModeAntiAlias);
        gx.SetTextRenderingHint(Gdiplus::TextRenderingHintClearTypeGridFit);
        HDC memdc = gx.GetHDC();
        eng.doc_.Draw(memdc, client);
        if (eng.IsMapUiShowHintOverlay()) {
          UiPaintMapHintOverlay(memdc, client, AgisTr(AgisUiStr::MapHintPanZoom));
        }
        gx.ReleaseHDC(memdc);
        Gdiplus::Graphics screen(hdc);
        screen.DrawImage(&off, 0, 0, cw, ch);
      } else {
        std::vector<uint8_t> pix;
        if (MapHostRenderClientToTopDownBgra(hwnd, client, &pix) && cw > 0 && ch > 0) {
          if (eng.renderDeviceContext_) {
            eng.renderDeviceContext_->presentFrame(hwnd, pix.data(), cw, ch);
          }
        }
      }
      EndPaint(hwnd, &ps);
      return 0;
    }
    default:
      break;
  }
  return DefWindowProcW(hwnd, msg, wParam, lParam);
}
