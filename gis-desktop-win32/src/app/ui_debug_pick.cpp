#include "app/ui_debug_pick.h"

#include <algorithm>
#include <string>
#include <vector>
#include <windowsx.h>

#include "app/resource.h"

namespace {
constexpr int kDebugPickHotkeyId = 0x4911;
constexpr wchar_t kDebugOverlayClass[] = L"AGIS_DEBUG_PICK_OVERLAY";
constexpr COLORREF kOverlayColorKey = RGB(1, 2, 3);

HINSTANCE g_inst = nullptr;
HHOOK g_getMsgHook = nullptr;
HWND g_overlay = nullptr;
bool g_enabled = false;
bool g_dragging = false;
POINT g_dragStart{};
POINT g_dragNow{};
RECT g_dragRect{};
std::vector<HWND> g_selected;

int g_vsLeft = 0;
int g_vsTop = 0;
int g_vsW = 0;
int g_vsH = 0;

const wchar_t* ControlIdName(int id) {
  switch (id) {
    case IDC_CONV_GRP_INPUT_PANEL: return L"IDC_CONV_GRP_INPUT_PANEL";
    case IDC_CONV_INPUT_TITLE: return L"IDC_CONV_INPUT_TITLE";
    case IDC_CONV_INPUT_TYPE: return L"IDC_CONV_INPUT_TYPE";
    case IDC_CONV_INPUT_TYPE_HELP: return L"IDC_CONV_INPUT_TYPE_HELP";
    case IDC_CONV_INPUT_SUBTYPE: return L"IDC_CONV_INPUT_SUBTYPE";
    case IDC_CONV_INPUT_SUBTYPE_HELP: return L"IDC_CONV_INPUT_SUBTYPE_HELP";
    case IDC_CONV_INPUT_PATH: return L"IDC_CONV_INPUT_PATH";
    case IDC_CONV_INPUT_BROWSE: return L"IDC_CONV_INPUT_BROWSE";
    case IDC_CONV_INPUT_PREVIEW: return L"IDC_CONV_INPUT_PREVIEW";
    case IDC_CONV_INPUT_INFO: return L"IDC_CONV_INPUT_INFO";
    case IDC_CONV_GRP_OUTPUT_PANEL: return L"IDC_CONV_GRP_OUTPUT_PANEL";
    case IDC_CONV_OUTPUT_TITLE: return L"IDC_CONV_OUTPUT_TITLE";
    case IDC_CONV_GRP_IN_TYPE: return L"IDC_CONV_GRP_IN_TYPE";
    case IDC_CONV_GRP_IN_SUB: return L"IDC_CONV_GRP_IN_SUB";
    case IDC_CONV_GRP_OUT_TYPE: return L"IDC_CONV_GRP_OUT_TYPE";
    case IDC_CONV_GRP_OUT_SUB: return L"IDC_CONV_GRP_OUT_SUB";
    case IDC_CONV_ELEV_HORIZ_LBL: return L"IDC_CONV_ELEV_HORIZ_LBL";
    case IDC_CONV_OUTPUT_TYPE: return L"IDC_CONV_OUTPUT_TYPE";
    case IDC_CONV_OUTPUT_TYPE_HELP: return L"IDC_CONV_OUTPUT_TYPE_HELP";
    case IDC_CONV_OUTPUT_SUBTYPE: return L"IDC_CONV_OUTPUT_SUBTYPE";
    case IDC_CONV_OUTPUT_SUBTYPE_HELP: return L"IDC_CONV_OUTPUT_SUBTYPE_HELP";
    case IDC_CONV_OUTPUT_PATH: return L"IDC_CONV_OUTPUT_PATH";
    case IDC_CONV_OUTPUT_BROWSE: return L"IDC_CONV_OUTPUT_BROWSE";
    case IDC_CONV_OUTPUT_PREVIEW: return L"IDC_CONV_OUTPUT_PREVIEW";
    case IDC_CONV_OUTPUT_INFO: return L"IDC_CONV_OUTPUT_INFO";
    case IDC_CONV_GRP_TASK_PANEL: return L"IDC_CONV_GRP_TASK_PANEL";
    case IDC_CONV_PANEL_MID: return L"IDC_CONV_PANEL_MID";
    case IDC_CONV_CMDLINE: return L"IDC_CONV_CMDLINE";
    case IDC_CONV_PROGRESS: return L"IDC_CONV_PROGRESS";
    case IDC_CONV_MSG: return L"IDC_CONV_MSG";
    case IDC_CONV_MSG_DETAIL: return L"IDC_CONV_MSG_DETAIL";
    case IDC_CONV_LOG: return L"IDC_CONV_LOG";
    case IDC_CONV_RUN: return L"IDC_CONV_RUN";
    case IDC_CONV_COPY_CMD: return L"IDC_CONV_COPY_CMD";
    case IDC_CONV_TOGGLE_LOG: return L"IDC_CONV_TOGGLE_LOG";
    case IDC_CONV_COPY_LOG: return L"IDC_CONV_COPY_LOG";
    case IDC_CONV_MID_SCROLL: return L"IDC_CONV_MID_SCROLL";
    case IDC_CONV_MODEL_COORD: return L"IDC_CONV_MODEL_COORD";
    case IDC_CONV_VECTOR_MODE: return L"IDC_CONV_VECTOR_MODE";
    case IDC_CONV_ELEV_HORIZ_RATIO: return L"IDC_CONV_ELEV_HORIZ_RATIO";
    case IDC_CONV_TARGET_CRS_LBL: return L"IDC_CONV_TARGET_CRS_LBL";
    case IDC_CONV_TARGET_CRS: return L"IDC_CONV_TARGET_CRS";
    case IDC_CONV_OUTPUT_UNIT_LBL: return L"IDC_CONV_OUTPUT_UNIT_LBL";
    case IDC_CONV_OUTPUT_UNIT: return L"IDC_CONV_OUTPUT_UNIT";
    case IDC_CONV_MESH_SPACING_LBL: return L"IDC_CONV_MESH_SPACING_LBL";
    case IDC_CONV_MESH_SPACING: return L"IDC_CONV_MESH_SPACING";
    case IDC_CONV_RASTER_MAX_LBL: return L"IDC_CONV_RASTER_MAX_LBL";
    case IDC_CONV_TEXTURE_FMT_LBL: return L"IDC_CONV_TEXTURE_FMT_LBL";
    case IDC_CONV_TEXTURE_FORMAT: return L"IDC_CONV_TEXTURE_FORMAT";
    case IDC_CONV_RASTER_MAX: return L"IDC_CONV_RASTER_MAX";
    case IDC_CONV_OBJ_FP_TYPE_LBL: return L"IDC_CONV_OBJ_FP_TYPE_LBL";
    case IDC_CONV_OBJ_FP_TYPE: return L"IDC_CONV_OBJ_FP_TYPE";
    case IDC_CONV_MODEL_COORD_LBL: return L"IDC_CONV_MODEL_COORD_LBL";
    case IDC_CONV_OBJ_TEX_MODE: return L"IDC_CONV_OBJ_TEX_MODE";
    case IDC_CONV_VECTOR_MODE_LBL: return L"IDC_CONV_VECTOR_MODE_LBL";
    case IDC_CONV_OBJ_TEX_MODE_LBL: return L"IDC_CONV_OBJ_TEX_MODE_LBL";
    case IDC_CONV_OBJ_VIS_EFFECT: return L"IDC_CONV_OBJ_VIS_EFFECT";
    case IDC_CONV_OBJ_VIS_EFFECT_LBL: return L"IDC_CONV_OBJ_VIS_EFFECT_LBL";
    case IDC_CONV_OBJ_SNOW_SCALE_LBL: return L"IDC_CONV_OBJ_SNOW_SCALE_LBL";
    case IDC_CONV_OBJ_SNOW_SCALE: return L"IDC_CONV_OBJ_SNOW_SCALE";
    case IDC_CONV_GIS_DEM_INTERP_LBL: return L"IDC_CONV_GIS_DEM_INTERP_LBL";
    case IDC_CONV_GIS_DEM_INTERP: return L"IDC_CONV_GIS_DEM_INTERP";
    case IDC_CONV_GIS_MESH_TOPO_LBL: return L"IDC_CONV_GIS_MESH_TOPO_LBL";
    case IDC_CONV_GIS_MESH_TOPO: return L"IDC_CONV_GIS_MESH_TOPO";
    case IDC_CONV_MODEL_BUDGET_MODE_LBL: return L"IDC_CONV_MODEL_BUDGET_MODE_LBL";
    case IDC_CONV_MODEL_BUDGET_MODE: return L"IDC_CONV_MODEL_BUDGET_MODE";
    case IDC_CONV_MODEL_BUDGET_MB_LBL: return L"IDC_CONV_MODEL_BUDGET_MB_LBL";
    case IDC_CONV_MODEL_BUDGET_MB: return L"IDC_CONV_MODEL_BUDGET_MB";
    default: return nullptr;
  }
}

std::wstring HwndName(HWND h) {
  if (!h || !IsWindow(h)) return L"<invalid>";
  const int id = GetDlgCtrlID(h);
  if (const wchar_t* n = ControlIdName(id)) return n;
  wchar_t cls[128]{};
  GetClassNameW(h, cls, 128);
  if (id > 0) {
    return std::wstring(cls) + L"#" + std::to_wstring(id);
  }
  return std::wstring(cls);
}

void CopyText(const std::wstring& s) {
  if (!OpenClipboard(nullptr)) return;
  EmptyClipboard();
  const size_t bytes = (s.size() + 1) * sizeof(wchar_t);
  HGLOBAL h = GlobalAlloc(GMEM_MOVEABLE, bytes);
  if (h) {
    void* p = GlobalLock(h);
    if (p) {
      memcpy(p, s.c_str(), bytes);
      GlobalUnlock(h);
      SetClipboardData(CF_UNICODETEXT, h);
      h = nullptr;
    }
    if (h) GlobalFree(h);
  }
  CloseClipboard();
}

void RefreshVirtualRect() {
  g_vsLeft = GetSystemMetrics(SM_XVIRTUALSCREEN);
  g_vsTop = GetSystemMetrics(SM_YVIRTUALSCREEN);
  g_vsW = GetSystemMetrics(SM_CXVIRTUALSCREEN);
  g_vsH = GetSystemMetrics(SM_CYVIRTUALSCREEN);
}

RECT NormalizeRect(POINT a, POINT b) {
  RECT r{};
  r.left = (std::min)(a.x, b.x);
  r.top = (std::min)(a.y, b.y);
  r.right = (std::max)(a.x, b.x);
  r.bottom = (std::max)(a.y, b.y);
  return r;
}

void InvalidateOverlay() {
  if (g_overlay && IsWindow(g_overlay)) {
    InvalidateRect(g_overlay, nullptr, TRUE);
  }
}

void SetOverlayVisible(bool on) {
  if (!g_overlay || !IsWindow(g_overlay)) return;
  if (on) {
    RefreshVirtualRect();
    SetWindowPos(g_overlay, HWND_TOPMOST, g_vsLeft, g_vsTop, g_vsW, g_vsH, SWP_NOACTIVATE | SWP_SHOWWINDOW);
  } else {
    ShowWindow(g_overlay, SW_HIDE);
  }
}

BOOL CALLBACK EnumChildCollect(HWND hwnd, LPARAM lParam) {
  auto* out = reinterpret_cast<std::vector<HWND>*>(lParam);
  if (IsWindowVisible(hwnd) && hwnd != g_overlay) out->push_back(hwnd);
  return TRUE;
}

BOOL CALLBACK EnumTopCollect(HWND hwnd, LPARAM lParam) {
  auto* out = reinterpret_cast<std::vector<HWND>*>(lParam);
  if (!IsWindowVisible(hwnd) || hwnd == g_overlay) return TRUE;
  out->push_back(hwnd);
  EnumChildWindows(hwnd, EnumChildCollect, lParam);
  return TRUE;
}

std::vector<HWND> CollectThreadWindows() {
  std::vector<HWND> all;
  EnumThreadWindows(GetCurrentThreadId(), EnumTopCollect, reinterpret_cast<LPARAM>(&all));
  return all;
}

void SelectSingle(HWND h) {
  g_selected.clear();
  if (h && IsWindow(h) && h != g_overlay) g_selected.push_back(h);
}

void ToggleSelect(HWND h) {
  if (!h || !IsWindow(h) || h == g_overlay) return;
  auto it = std::find(g_selected.begin(), g_selected.end(), h);
  if (it == g_selected.end()) g_selected.push_back(h);
  else g_selected.erase(it);
}

void SelectByRect(const RECT& sr, bool additive) {
  if (!additive) g_selected.clear();
  const auto all = CollectThreadWindows();
  for (HWND h : all) {
    RECT wr{};
    if (!GetWindowRect(h, &wr)) continue;
    RECT inter{};
    if (IntersectRect(&inter, &wr, &sr)) {
      if (std::find(g_selected.begin(), g_selected.end(), h) == g_selected.end()) g_selected.push_back(h);
    }
  }
}

LRESULT CALLBACK OverlayProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) {
  switch (msg) {
    case WM_NCHITTEST: return HTTRANSPARENT;
    case WM_ERASEBKGND: return 1;
    case WM_PAINT: {
      PAINTSTRUCT ps{};
      HDC hdc = BeginPaint(hwnd, &ps);
      RECT rc{};
      GetClientRect(hwnd, &rc);
      HBRUSH clear = CreateSolidBrush(kOverlayColorKey);
      FillRect(hdc, &rc, clear);
      DeleteObject(clear);
      SetBkMode(hdc, TRANSPARENT);
      SetTextColor(hdc, RGB(230, 240, 255));
      const std::wstring hint =
          L"[UI调试模式] 单击选中 | Ctrl+单击多选 | 拖拽框选 | Ctrl+C复制控件名 | Esc退出";
      RECT hintRc{12, 8, rc.right - 12, 28};
      HBRUSH hintBg = CreateSolidBrush(RGB(20, 40, 70));
      FillRect(hdc, &hintRc, hintBg);
      DeleteObject(hintBg);
      TextOutW(hdc, 16, 11, hint.c_str(), static_cast<int>(hint.size()));
      HPEN pen = CreatePen(PS_SOLID, 2, RGB(20, 180, 255));
      HGDIOBJ oldPen = SelectObject(hdc, pen);
      HGDIOBJ oldBrush = SelectObject(hdc, GetStockObject(NULL_BRUSH));
      for (HWND h : g_selected) {
        RECT wr{};
        if (!IsWindow(h) || !GetWindowRect(h, &wr)) continue;
        OffsetRect(&wr, -g_vsLeft, -g_vsTop);
        Rectangle(hdc, wr.left, wr.top, wr.right, wr.bottom);
        const std::wstring name = HwndName(h);
        SIZE ts{};
        GetTextExtentPoint32W(hdc, name.c_str(), static_cast<int>(name.size()), &ts);
        RECT tr{wr.left, (std::max)(0L, wr.top - ts.cy - 6), wr.left + ts.cx + 8, (std::max)(0L, wr.top - 2)};
        HBRUSH bg = CreateSolidBrush(RGB(20, 180, 255));
        FillRect(hdc, &tr, bg);
        DeleteObject(bg);
        SetTextColor(hdc, RGB(15, 20, 30));
        TextOutW(hdc, tr.left + 4, tr.top + 2, name.c_str(), static_cast<int>(name.size()));
      }
      if (g_dragging) {
        RECT dr = g_dragRect;
        OffsetRect(&dr, -g_vsLeft, -g_vsTop);
        HPEN boxPen = CreatePen(PS_DOT, 1, RGB(255, 210, 60));
        SelectObject(hdc, boxPen);
        Rectangle(hdc, dr.left, dr.top, dr.right, dr.bottom);
        DeleteObject(boxPen);
      }
      SelectObject(hdc, oldBrush);
      SelectObject(hdc, oldPen);
      DeleteObject(pen);
      EndPaint(hwnd, &ps);
      return 0;
    }
    default: break;
  }
  return DefWindowProcW(hwnd, msg, wParam, lParam);
}

void EnsureOverlayWindow() {
  if (g_overlay && IsWindow(g_overlay)) return;
  WNDCLASSW wc{};
  wc.lpfnWndProc = OverlayProc;
  wc.hInstance = g_inst;
  wc.lpszClassName = kDebugOverlayClass;
  wc.hCursor = LoadCursor(nullptr, IDC_CROSS);
  wc.hbrBackground = nullptr;
  RegisterClassW(&wc);
  RefreshVirtualRect();
  g_overlay = CreateWindowExW(WS_EX_LAYERED | WS_EX_TRANSPARENT | WS_EX_TOPMOST | WS_EX_TOOLWINDOW | WS_EX_NOACTIVATE,
                              kDebugOverlayClass, L"", WS_POPUP, g_vsLeft, g_vsTop, g_vsW, g_vsH, nullptr, nullptr, g_inst,
                              nullptr);
  if (g_overlay) {
    SetLayeredWindowAttributes(g_overlay, kOverlayColorKey, 255, LWA_COLORKEY);
  }
}

void ToggleDebugMode() {
  g_enabled = !g_enabled;
  if (g_enabled) {
    EnsureOverlayWindow();
    g_selected.clear();
    g_dragging = false;
    SetOverlayVisible(true);
    MessageBeep(MB_ICONINFORMATION);
  } else {
    g_dragging = false;
    SetOverlayVisible(false);
    MessageBeep(MB_ICONASTERISK);
  }
}

POINT MsgClientToScreen(HWND hwnd, LPARAM lp) {
  POINT p{GET_X_LPARAM(lp), GET_Y_LPARAM(lp)};
  if (hwnd && IsWindow(hwnd)) ClientToScreen(hwnd, &p);
  return p;
}

bool HandleMsg(MSG* msg) {
  if (!msg) return false;
  if (msg->message == WM_HOTKEY && static_cast<int>(msg->wParam) == kDebugPickHotkeyId) {
    ToggleDebugMode();
    msg->message = WM_NULL;
    return true;
  }
  if (!g_enabled) return false;
  auto consume = [&]() {
    msg->message = WM_NULL;
    return true;
  };
  if (msg->message == WM_KEYDOWN && msg->wParam == VK_ESCAPE) {
    g_enabled = false;
    SetOverlayVisible(false);
    return consume();
  }
  if (msg->message == WM_KEYDOWN && (msg->wParam == 'C' || msg->wParam == 'c') && (GetKeyState(VK_CONTROL) & 0x8000)) {
    std::wstring all;
    for (size_t i = 0; i < g_selected.size(); ++i) {
      if (i) all += L"\r\n";
      all += HwndName(g_selected[i]);
    }
    if (!all.empty()) CopyText(all);
    return consume();
  }
  if (msg->message == WM_LBUTTONDOWN) {
    g_dragging = true;
    g_dragStart = MsgClientToScreen(msg->hwnd, msg->lParam);
    g_dragNow = g_dragStart;
    g_dragRect = NormalizeRect(g_dragStart, g_dragNow);
    InvalidateOverlay();
    return consume();
  }
  if (msg->message == WM_MOUSEMOVE && g_dragging) {
    g_dragNow = MsgClientToScreen(msg->hwnd, msg->lParam);
    g_dragRect = NormalizeRect(g_dragStart, g_dragNow);
    InvalidateOverlay();
    return consume();
  }
  if (msg->message == WM_LBUTTONUP && g_dragging) {
    g_dragging = false;
    const POINT up = MsgClientToScreen(msg->hwnd, msg->lParam);
    const RECT sr = NormalizeRect(g_dragStart, up);
    const bool ctrl = (GetKeyState(VK_CONTROL) & 0x8000) != 0;
    const int dx = sr.right - sr.left;
    const int dy = sr.bottom - sr.top;
    if (dx <= 3 && dy <= 3) {
      HWND h = WindowFromPoint(up);
      if (ctrl) ToggleSelect(h);
      else SelectSingle(h);
    } else {
      SelectByRect(sr, ctrl);
    }
    InvalidateOverlay();
    return consume();
  }

  // 调试模式下冻结业务窗口交互：吞掉绝大多数输入。
  switch (msg->message) {
    case WM_MOUSEMOVE:
    case WM_MOUSEWHEEL:
    case WM_MOUSEHWHEEL:
    case WM_LBUTTONDOWN:
    case WM_LBUTTONUP:
    case WM_LBUTTONDBLCLK:
    case WM_RBUTTONDOWN:
    case WM_RBUTTONUP:
    case WM_RBUTTONDBLCLK:
    case WM_MBUTTONDOWN:
    case WM_MBUTTONUP:
    case WM_MBUTTONDBLCLK:
    case WM_XBUTTONDOWN:
    case WM_XBUTTONUP:
    case WM_XBUTTONDBLCLK:
    case WM_NCMOUSEMOVE:
    case WM_NCLBUTTONDOWN:
    case WM_NCLBUTTONUP:
    case WM_NCRBUTTONDOWN:
    case WM_NCRBUTTONUP:
    case WM_KEYDOWN:
    case WM_KEYUP:
    case WM_SYSKEYDOWN:
    case WM_SYSKEYUP:
    case WM_CHAR:
    case WM_SYSCHAR:
      return consume();
    default: break;
  }
  return false;
}

LRESULT CALLBACK DebugGetMsgHook(int nCode, WPARAM wParam, LPARAM lParam) {
  if (nCode >= 0 && lParam) {
    MSG* msg = reinterpret_cast<MSG*>(lParam);
    HandleMsg(msg);
  }
  return CallNextHookEx(g_getMsgHook, nCode, wParam, lParam);
}
}  // namespace

void AgisUiDebugPickInit(HINSTANCE inst) {
  g_inst = inst;
  RegisterHotKey(nullptr, kDebugPickHotkeyId, MOD_CONTROL | MOD_SHIFT, 'I');
  g_getMsgHook = SetWindowsHookExW(WH_GETMESSAGE, DebugGetMsgHook, nullptr, GetCurrentThreadId());
}

void AgisUiDebugPickShutdown() {
  UnregisterHotKey(nullptr, kDebugPickHotkeyId);
  if (g_getMsgHook) {
    UnhookWindowsHookEx(g_getMsgHook);
    g_getMsgHook = nullptr;
  }
  if (g_overlay && IsWindow(g_overlay)) {
    DestroyWindow(g_overlay);
    g_overlay = nullptr;
  }
}

bool AgisUiDebugPickHandleMessage(MSG* msg) {
  return HandleMsg(msg);
}

