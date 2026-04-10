#include "app/ui_debug_pick.h"

#include <algorithm>
#include <sstream>
#include <string>
#include <vector>
#include <commctrl.h>
#include <windowsx.h>

#include "app/resource.h"

namespace {
constexpr int kDebugPickHotkeyId = 0x4911;
constexpr int kDebugCopyNameHotkeyId = 0x4912;
constexpr int kDebugCopyPathHotkeyId = 0x4913;
constexpr int kDebugExitHotkeyId = 0x4914;
constexpr wchar_t kDebugOverlayClass[] = L"AGIS_DEBUG_PICK_OVERLAY";
constexpr wchar_t kDebugInspectorClass[] = L"AGIS_DEBUG_PICK_OUTLINE";
constexpr wchar_t kDebugPropsClass[] = L"AGIS_DEBUG_PICK_PROPS";
constexpr COLORREF kOverlayColorKey = RGB(1, 2, 3);
constexpr int kInspectorTreeId = 1001;
constexpr int kInspectorPropsId = 1002;
constexpr int kInspectorModeToggleId = 1003;
constexpr int kInspectorCopyPropsId = 1004;

HINSTANCE g_inst = nullptr;
HHOOK g_getMsgHook = nullptr;
HWND g_overlay = nullptr;
HWND g_inspector = nullptr;
HWND g_propsWindow = nullptr;
HWND g_tree = nullptr;
HWND g_props = nullptr;
HWND g_propsModeToggle = nullptr;
HWND g_propsCopyBtn = nullptr;
HFONT g_jsonFont = nullptr;
HFONT g_uiFont = nullptr;
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
bool g_updatingTree = false;
bool g_propsShowSubtree = false;

HFONT EnsureJsonFont() {
  if (g_jsonFont) return g_jsonFont;
  g_jsonFont = CreateFontW(-16, 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE, DEFAULT_CHARSET, OUT_DEFAULT_PRECIS,
                           CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY, FIXED_PITCH | FF_MODERN, L"Consolas");
  if (!g_jsonFont) {
    g_jsonFont = static_cast<HFONT>(GetStockObject(ANSI_FIXED_FONT));
  }
  return g_jsonFont;
}

HFONT EnsureUiFont() {
  if (g_uiFont) return g_uiFont;
  g_uiFont = CreateFontW(-15, 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE, DEFAULT_CHARSET, OUT_DEFAULT_PRECIS,
                         CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY, VARIABLE_PITCH | FF_SWISS, L"Segoe UI");
  if (!g_uiFont) {
    g_uiFont = static_cast<HFONT>(GetStockObject(DEFAULT_GUI_FONT));
  }
  return g_uiFont;
}

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

std::wstring HwndFullPathName(HWND h) {
  if (!h || !IsWindow(h)) return L"<invalid>";
  std::vector<std::wstring> parts;
  HWND cur = h;
  while (cur && IsWindow(cur)) {
    parts.push_back(HwndName(cur));
    HWND p = GetParent(cur);
    if (!p || p == cur) break;
    cur = p;
  }
  std::wstring path;
  for (auto it = parts.rbegin(); it != parts.rend(); ++it) {
    if (!path.empty()) path += L" / ";
    path += *it;
  }
  return path;
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

void CopySelectedNames() {
  std::wstring all;
  for (size_t i = 0; i < g_selected.size(); ++i) {
    if (i) all += L"\r\n";
    all += HwndName(g_selected[i]);
  }
  if (!all.empty()) CopyText(all);
}

void CopySelectedFullPaths() {
  std::wstring all;
  for (size_t i = 0; i < g_selected.size(); ++i) {
    if (i) all += L"\r\n";
    all += HwndFullPathName(g_selected[i]);
  }
  if (!all.empty()) CopyText(all);
}

void SetDebugRuntimeHotkeys(bool enable) {
  if (enable) {
    RegisterHotKey(nullptr, kDebugCopyNameHotkeyId, MOD_CONTROL, 'C');
    RegisterHotKey(nullptr, kDebugCopyPathHotkeyId, MOD_CONTROL | MOD_SHIFT, 'C');
    RegisterHotKey(nullptr, kDebugExitHotkeyId, MOD_NOREPEAT, VK_ESCAPE);
  } else {
    UnregisterHotKey(nullptr, kDebugCopyNameHotkeyId);
    UnregisterHotKey(nullptr, kDebugCopyPathHotkeyId);
    UnregisterHotKey(nullptr, kDebugExitHotkeyId);
  }
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

bool IsInspectorWindowOrChild(HWND h) {
  if (!h) return false;
  if (g_inspector && IsWindow(g_inspector) && (h == g_inspector || IsChild(g_inspector, h))) return true;
  if (g_propsWindow && IsWindow(g_propsWindow) && (h == g_propsWindow || IsChild(g_propsWindow, h))) return true;
  return false;
}

bool IsHwndSelected(HWND h) {
  return std::find(g_selected.begin(), g_selected.end(), h) != g_selected.end();
}

void RefreshInspectorTree();

HTREEITEM FindTreeItemByHwnd(HWND tree, HTREEITEM root, HWND target) {
  if (!tree || !root || !target) return nullptr;
  TVITEMW it{};
  it.mask = TVIF_PARAM;
  it.hItem = root;
  if (TreeView_GetItem(tree, &it) && reinterpret_cast<HWND>(it.lParam) == target) {
    return root;
  }
  HTREEITEM child = TreeView_GetChild(tree, root);
  while (child) {
    if (HTREEITEM hit = FindTreeItemByHwnd(tree, child, target)) {
      return hit;
    }
    child = TreeView_GetNextSibling(tree, child);
  }
  return nullptr;
}

void SyncTreeSelectionFromCurrent() {
  if (!g_tree || !IsWindow(g_tree) || g_selected.empty()) return;
  auto findInTree = [&](HWND target) -> HTREEITEM {
    HTREEITEM root = TreeView_GetRoot(g_tree);
    while (root) {
      if (HTREEITEM hit = FindTreeItemByHwnd(g_tree, root, target)) {
        return hit;
      }
      root = TreeView_GetNextSibling(g_tree, root);
    }
    return nullptr;
  };
  HWND target = g_selected.front();
  HTREEITEM hit = findInTree(target);
  // 树中未命中时再刷新，避免每次刷新导致节点重建后错位感。
  if (!hit) {
    RefreshInspectorTree();
    hit = findInTree(target);
  }
  // 精确匹配失败时，向父链回退到树中最近可见节点。
  while (!hit && target && IsWindow(target)) {
    target = GetParent(target);
    if (target) {
      hit = findInTree(target);
      if (!hit) {
        RefreshInspectorTree();
        hit = findInTree(target);
      }
    }
  }
  if (!hit) return;
  g_updatingTree = true;
  TreeView_SelectItem(g_tree, hit);
  TreeView_EnsureVisible(g_tree, hit);
  g_updatingTree = false;
}

std::wstring JsonEscape(const std::wstring& s) {
  std::wstring out;
  out.reserve(s.size() + 16);
  for (wchar_t ch : s) {
    switch (ch) {
      case L'\\': out += L"\\\\"; break;
      case L'"': out += L"\\\""; break;
      case L'\r': out += L"\\r"; break;
      case L'\n': out += L"\\n"; break;
      case L'\t': out += L"\\t"; break;
      default: out.push_back(ch); break;
    }
  }
  return out;
}

void AppendIndent(std::wstringstream& ss, int n) {
  for (int i = 0; i < n; ++i) ss << L' ';
}

void AppendWindowJson(std::wstringstream& ss, HWND h, int indent, bool includeChildren) {
  RECT wr{};
  GetWindowRect(h, &wr);
  wchar_t cls[128]{};
  GetClassNameW(h, cls, 128);
  wchar_t txt[256]{};
  GetWindowTextW(h, txt, 256);
  const int id = GetDlgCtrlID(h);

  AppendIndent(ss, indent);
  ss << L"{\n";
  AppendIndent(ss, indent + 2);
  ss << L"\"name\": \"" << JsonEscape(HwndName(h)) << L"\",\n";
  AppendIndent(ss, indent + 2);
  ss << L"\"fullPath\": \"" << JsonEscape(HwndFullPathName(h)) << L"\",\n";
  AppendIndent(ss, indent + 2);
  ss << L"\"hwnd\": \"0x" << std::hex << reinterpret_cast<std::uintptr_t>(h) << std::dec << L"\",\n";
  AppendIndent(ss, indent + 2);
  ss << L"\"class\": \"" << JsonEscape(cls) << L"\",\n";
  AppendIndent(ss, indent + 2);
  ss << L"\"id\": " << id << L",\n";
  AppendIndent(ss, indent + 2);
  ss << L"\"text\": \"" << JsonEscape(txt) << L"\",\n";
  AppendIndent(ss, indent + 2);
  ss << L"\"rect\": {\n";
  AppendIndent(ss, indent + 4);
  ss << L"\"left\": " << wr.left << L",\n";
  AppendIndent(ss, indent + 4);
  ss << L"\"top\": " << wr.top << L",\n";
  AppendIndent(ss, indent + 4);
  ss << L"\"right\": " << wr.right << L",\n";
  AppendIndent(ss, indent + 4);
  ss << L"\"bottom\": " << wr.bottom << L"\n";
  AppendIndent(ss, indent + 2);
  ss << L"},\n";
  AppendIndent(ss, indent + 2);
  ss << L"\"size\": {\n";
  AppendIndent(ss, indent + 4);
  ss << L"\"width\": " << (wr.right - wr.left) << L",\n";
  AppendIndent(ss, indent + 4);
  ss << L"\"height\": " << (wr.bottom - wr.top) << L"\n";
  AppendIndent(ss, indent + 2);
  ss << L"},\n";
  AppendIndent(ss, indent + 2);
  ss << L"\"visible\": " << (IsWindowVisible(h) ? L"true" : L"false") << L",\n";
  AppendIndent(ss, indent + 2);
  ss << L"\"enabled\": " << (IsWindowEnabled(h) ? L"true" : L"false");
  if (includeChildren) {
    std::vector<HWND> children;
    for (HWND c = GetWindow(h, GW_CHILD); c; c = GetWindow(c, GW_HWNDNEXT)) {
      if (!IsWindow(c) || c == g_overlay || c == g_inspector || c == g_propsWindow) continue;
      children.push_back(c);
    }
    if (children.empty()) {
      ss << L",\n";
      AppendIndent(ss, indent + 2);
      ss << L"\"children\": []\n";
    } else {
      ss << L",\n";
      AppendIndent(ss, indent + 2);
      ss << L"\"children\": [\n";
      for (size_t i = 0; i < children.size(); ++i) {
        if (i) ss << L",\n";
        AppendWindowJson(ss, children[i], indent + 4, true);
      }
      ss << L"\n";
      AppendIndent(ss, indent + 2);
      ss << L"]\n";
    }
    AppendIndent(ss, indent);
    ss << L"}";
    return;
  }
  ss << L"\n";
  AppendIndent(ss, indent);
  ss << L"}";
}

std::wstring NormalizeNewlinesForWinEdit(const std::wstring& s) {
  std::wstring out;
  out.reserve(s.size() + 32);
  for (size_t i = 0; i < s.size(); ++i) {
    const wchar_t ch = s[i];
    if (ch == L'\r') {
      if (i + 1 < s.size() && s[i + 1] == L'\n') {
        out += L"\r\n";
        ++i;
      } else {
        out += L"\r\n";
      }
    } else if (ch == L'\n') {
      out += L"\r\n";
    } else {
      out.push_back(ch);
    }
  }
  return out;
}

void UpdateInspectorProps() {
  if (!g_props || !IsWindow(g_props)) return;
  if (g_selected.empty()) {
    SetWindowTextW(g_props, L"{\r\n  \"selected\": null\r\n}");
    return;
  }
  HWND h = g_selected.front();
  std::wstringstream ss;
  ss << L"{\r\n";
  ss << L"  \"mode\": \"" << (g_propsShowSubtree ? L"with_children" : L"single") << L"\",\r\n";
  ss << L"  \"selectedCount\": " << static_cast<int>(g_selected.size()) << L",\r\n";
  ss << L"  \"selected\":\r\n";
  AppendWindowJson(ss, h, 2, g_propsShowSubtree);
  ss << L"\r\n}";
  const std::wstring pretty = NormalizeNewlinesForWinEdit(ss.str());
  SetWindowTextW(g_props, pretty.c_str());
}

HTREEITEM AddTreeNode(HWND tree, HTREEITEM parent, HWND h) {
  std::wstring name = HwndName(h);
  TVINSERTSTRUCTW ins{};
  ins.hParent = parent;
  ins.hInsertAfter = TVI_LAST;
  ins.item.mask = TVIF_TEXT | TVIF_PARAM;
  ins.item.pszText = const_cast<wchar_t*>(name.c_str());
  ins.item.lParam = reinterpret_cast<LPARAM>(h);
  return TreeView_InsertItem(tree, &ins);
}

void BuildTreeChildren(HWND tree, HTREEITEM parentItem, HWND parentHwnd) {
  for (HWND c = GetWindow(parentHwnd, GW_CHILD); c; c = GetWindow(c, GW_HWNDNEXT)) {
    if (!IsWindow(c) || c == g_overlay || c == g_inspector || c == g_propsWindow) continue;
    HTREEITEM item = AddTreeNode(tree, parentItem, c);
    BuildTreeChildren(tree, item, c);
  }
}

void RefreshInspectorTree() {
  if (!g_tree || !IsWindow(g_tree)) return;
  HWND selected = g_selected.empty() ? nullptr : g_selected.front();
  g_updatingTree = true;
  TreeView_DeleteAllItems(g_tree);
  EnumThreadWindows(GetCurrentThreadId(),
                    [](HWND h, LPARAM lp) -> BOOL {
                      HWND tree = reinterpret_cast<HWND>(lp);
                      if (h == g_overlay || h == g_inspector || h == g_propsWindow) return TRUE;
                      HTREEITEM root = AddTreeNode(tree, TVI_ROOT, h);
                      BuildTreeChildren(tree, root, h);
                      return TRUE;
                    },
                    reinterpret_cast<LPARAM>(g_tree));
  if (selected && IsWindow(selected)) {
    auto findInTree = [&](HWND target) -> HTREEITEM {
      HTREEITEM root = TreeView_GetRoot(g_tree);
      while (root) {
        if (HTREEITEM hit = FindTreeItemByHwnd(g_tree, root, target)) {
          return hit;
        }
        root = TreeView_GetNextSibling(g_tree, root);
      }
      return nullptr;
    };
    HTREEITEM hit = findInTree(selected);
    while (!hit && selected && IsWindow(selected)) {
      selected = GetParent(selected);
      if (selected) hit = findInTree(selected);
    }
    if (hit) {
      TreeView_SelectItem(g_tree, hit);
      TreeView_EnsureVisible(g_tree, hit);
    }
  }
  g_updatingTree = false;
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

HWND PickWindowAtPoint(POINT screenPt) {
  const auto all = CollectThreadWindows();
  HWND best = nullptr;
  LONG bestArea = LONG_MAX;
  for (HWND h : all) {
    if (!IsWindow(h) || h == g_overlay || IsInspectorWindowOrChild(h)) continue;
    RECT wr{};
    if (!GetWindowRect(h, &wr)) continue;
    if (!PtInRect(&wr, screenPt)) continue;
    const LONG w = (std::max)(0L, wr.right - wr.left);
    const LONG hgt = (std::max)(0L, wr.bottom - wr.top);
    const LONG area = w * hgt;
    if (!best || area < bestArea) {
      best = h;
      bestArea = area;
    }
  }
  return best ? best : WindowFromPoint(screenPt);
}

void SelectSingle(HWND h) {
  g_selected.clear();
  if (h && IsWindow(h) && h != g_overlay) g_selected.push_back(h);
  UpdateInspectorProps();
  SyncTreeSelectionFromCurrent();
  if (g_tree && IsWindow(g_tree)) InvalidateRect(g_tree, nullptr, TRUE);
}

void ToggleSelect(HWND h) {
  if (!h || !IsWindow(h) || h == g_overlay) return;
  auto it = std::find(g_selected.begin(), g_selected.end(), h);
  if (it == g_selected.end()) g_selected.push_back(h);
  else g_selected.erase(it);
  UpdateInspectorProps();
  SyncTreeSelectionFromCurrent();
  if (g_tree && IsWindow(g_tree)) InvalidateRect(g_tree, nullptr, TRUE);
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
  UpdateInspectorProps();
  SyncTreeSelectionFromCurrent();
  if (g_tree && IsWindow(g_tree)) InvalidateRect(g_tree, nullptr, TRUE);
}

LRESULT CALLBACK OutlineProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) {
  switch (msg) {
    case WM_CREATE: {
      g_tree = CreateWindowExW(WS_EX_CLIENTEDGE, WC_TREEVIEWW, L"", WS_CHILD | WS_VISIBLE | TVS_HASLINES | TVS_LINESATROOT |
                                                                 TVS_HASBUTTONS | TVS_SHOWSELALWAYS,
                               0, 0, 100, 100, hwnd, reinterpret_cast<HMENU>(static_cast<INT_PTR>(kInspectorTreeId)), g_inst,
                               nullptr);
      SendMessageW(g_tree, WM_SETFONT, reinterpret_cast<WPARAM>(EnsureUiFont()), TRUE);
      RefreshInspectorTree();
      UpdateInspectorProps();
      return 0;
    }
    case WM_SIZE: {
      RECT rc{};
      GetClientRect(hwnd, &rc);
      const int w = rc.right - rc.left;
      const int h = rc.bottom - rc.top;
      if (g_tree) MoveWindow(g_tree, 0, 0, (std::max)(120, w), (std::max)(120, h), TRUE);
      return 0;
    }
    case WM_NOTIFY: {
      const NMHDR* hdr = reinterpret_cast<const NMHDR*>(lParam);
      if (hdr && hdr->idFrom == kInspectorTreeId) {
        if (hdr->code == TVN_SELCHANGEDW && !g_updatingTree) {
          const auto* n = reinterpret_cast<const NMTREEVIEWW*>(lParam);
          HWND h = reinterpret_cast<HWND>(n->itemNew.lParam);
          if (h && IsWindow(h)) {
            if ((GetKeyState(VK_CONTROL) & 0x8000) != 0) {
              ToggleSelect(h);
            } else {
              SelectSingle(h);
            }
            InvalidateOverlay();
          }
        } else if (hdr->code == NM_CUSTOMDRAW) {
          auto* cd = reinterpret_cast<NMTVCUSTOMDRAW*>(lParam);
          if (cd->nmcd.dwDrawStage == CDDS_PREPAINT) {
            return CDRF_NOTIFYITEMDRAW;
          }
          if (cd->nmcd.dwDrawStage == CDDS_ITEMPREPAINT) {
            const HWND itemHwnd = reinterpret_cast<HWND>(cd->nmcd.lItemlParam);
            if (IsHwndSelected(itemHwnd)) {
              cd->clrText = RGB(255, 255, 255);
              cd->clrTextBk = RGB(0, 0, 0);
              return CDRF_NEWFONT;
            }
          }
        }
      }
      return 0;
    }
    default: break;
  }
  return DefWindowProcW(hwnd, msg, wParam, lParam);
}

LRESULT CALLBACK PropsProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) {
  switch (msg) {
    case WM_CREATE: {
      g_propsModeToggle = CreateWindowW(L"BUTTON", L"显示全部子节点", WS_CHILD | WS_VISIBLE | BS_AUTOCHECKBOX, 8, 8, 180, 24, hwnd,
                                        reinterpret_cast<HMENU>(static_cast<INT_PTR>(kInspectorModeToggleId)), g_inst, nullptr);
      g_propsCopyBtn = CreateWindowW(L"BUTTON", L"复制文本", WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON, 196, 8, 90, 24, hwnd,
                                     reinterpret_cast<HMENU>(static_cast<INT_PTR>(kInspectorCopyPropsId)), g_inst, nullptr);
      SendMessageW(g_propsModeToggle, BM_SETCHECK, g_propsShowSubtree ? BST_CHECKED : BST_UNCHECKED, 0);
      g_props = CreateWindowExW(WS_EX_CLIENTEDGE, L"EDIT", L"", WS_CHILD | WS_VISIBLE | ES_MULTILINE | ES_AUTOVSCROLL |
                                                               ES_AUTOHSCROLL | ES_READONLY | WS_VSCROLL | WS_HSCROLL,
                                8, 36, 100, 100, hwnd, reinterpret_cast<HMENU>(static_cast<INT_PTR>(kInspectorPropsId)), g_inst,
                                nullptr);
      SendMessageW(g_propsModeToggle, WM_SETFONT, reinterpret_cast<WPARAM>(EnsureUiFont()), TRUE);
      SendMessageW(g_propsCopyBtn, WM_SETFONT, reinterpret_cast<WPARAM>(EnsureUiFont()), TRUE);
      SendMessageW(g_props, WM_SETFONT, reinterpret_cast<WPARAM>(EnsureJsonFont()), TRUE);
      SendMessageW(g_props, EM_SETMARGINS, EC_LEFTMARGIN | EC_RIGHTMARGIN, MAKELPARAM(8, 8));
      UpdateInspectorProps();
      return 0;
    }
    case WM_SIZE: {
      RECT rc{};
      GetClientRect(hwnd, &rc);
      const int w = rc.right - rc.left;
      const int h = rc.bottom - rc.top;
      if (g_propsModeToggle) MoveWindow(g_propsModeToggle, 8, 8, (std::max)(120, w - 116), 24, TRUE);
      if (g_propsCopyBtn) MoveWindow(g_propsCopyBtn, (std::max)(8, w - 98), 8, 90, 24, TRUE);
      if (g_props) MoveWindow(g_props, 8, 36, (std::max)(120, w - 16), (std::max)(120, h - 44), TRUE);
      return 0;
    }
    case WM_COMMAND: {
      if (LOWORD(wParam) == kInspectorModeToggleId) {
        g_propsShowSubtree = (SendMessageW(g_propsModeToggle, BM_GETCHECK, 0, 0) == BST_CHECKED);
        UpdateInspectorProps();
        return 0;
      }
      if (LOWORD(wParam) == kInspectorCopyPropsId) {
        if (g_props && IsWindow(g_props)) {
          const int n = GetWindowTextLengthW(g_props);
          std::wstring text(static_cast<size_t>((std::max)(0, n)) + 1, L'\0');
          if (n > 0) {
            GetWindowTextW(g_props, &text[0], n + 1);
            text.resize(static_cast<size_t>(n));
          } else {
            text.clear();
          }
          CopyText(text);
        }
        return 0;
      }
      break;
    }
    default: break;
  }
  return DefWindowProcW(hwnd, msg, wParam, lParam);
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
          L"[UI调试模式] 单击选中 | Ctrl+单击多选 | 拖拽框选 | Ctrl+C复制控件名 | Ctrl+Shift+C复制全路径 | Esc退出";
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
        HPEN boxPen = CreatePen(PS_SOLID, 3, RGB(180, 80, 255));
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

void EnsureInspectorWindow() {
  WNDCLASSW wc1{};
  wc1.lpfnWndProc = OutlineProc;
  wc1.hInstance = g_inst;
  wc1.lpszClassName = kDebugInspectorClass;
  wc1.hCursor = LoadCursor(nullptr, IDC_ARROW);
  wc1.hbrBackground = reinterpret_cast<HBRUSH>(COLOR_WINDOW + 1);
  RegisterClassW(&wc1);

  WNDCLASSW wc2{};
  wc2.lpfnWndProc = PropsProc;
  wc2.hInstance = g_inst;
  wc2.lpszClassName = kDebugPropsClass;
  wc2.hCursor = LoadCursor(nullptr, IDC_ARROW);
  wc2.hbrBackground = reinterpret_cast<HBRUSH>(COLOR_WINDOW + 1);
  RegisterClassW(&wc2);

  RefreshVirtualRect();
  if (!g_inspector || !IsWindow(g_inspector)) {
    g_inspector = CreateWindowExW(WS_EX_TOOLWINDOW | WS_EX_TOPMOST, kDebugInspectorClass, L"UI 调试大纲（左）",
                                  WS_OVERLAPPEDWINDOW | WS_VISIBLE, g_vsLeft + 20, g_vsTop + 80, 420, 620, nullptr, nullptr, g_inst,
                                  nullptr);
  }
  if (!g_propsWindow || !IsWindow(g_propsWindow)) {
    g_propsWindow = CreateWindowExW(WS_EX_TOOLWINDOW | WS_EX_TOPMOST, kDebugPropsClass, L"UI 调试属性（右，JSON）",
                                    WS_OVERLAPPEDWINDOW | WS_VISIBLE, g_vsLeft + g_vsW - 520, g_vsTop + 80, 500, 620, nullptr, nullptr,
                                    g_inst, nullptr);
  }
}

void ToggleDebugMode() {
  g_enabled = !g_enabled;
  if (g_enabled) {
    EnsureOverlayWindow();
    EnsureInspectorWindow();
    g_selected.clear();
    g_dragging = false;
    SetOverlayVisible(true);
    ShowWindow(g_inspector, SW_SHOW);
    ShowWindow(g_propsWindow, SW_SHOW);
    SetWindowPos(g_inspector, HWND_TOPMOST, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE);
    SetWindowPos(g_propsWindow, HWND_TOPMOST, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE);
    RefreshInspectorTree();
    UpdateInspectorProps();
    SetDebugRuntimeHotkeys(true);
    MessageBeep(MB_ICONINFORMATION);
  } else {
    g_dragging = false;
    SetOverlayVisible(false);
    if (g_inspector && IsWindow(g_inspector)) {
      DestroyWindow(g_inspector);
      g_inspector = nullptr;
      g_tree = nullptr;
    }
    if (g_propsWindow && IsWindow(g_propsWindow)) {
      DestroyWindow(g_propsWindow);
      g_propsWindow = nullptr;
    }
    g_propsModeToggle = nullptr;
    g_props = nullptr;
    SetDebugRuntimeHotkeys(false);
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
  if (msg->message == WM_HOTKEY && g_enabled) {
    const int hk = static_cast<int>(msg->wParam);
    if (hk == kDebugExitHotkeyId) {
      ToggleDebugMode();
      msg->message = WM_NULL;
      return true;
    }
    if (hk == kDebugCopyPathHotkeyId) {
      CopySelectedFullPaths();
      msg->message = WM_NULL;
      return true;
    }
    if (hk == kDebugCopyNameHotkeyId) {
      CopySelectedNames();
      msg->message = WM_NULL;
      return true;
    }
  }
  if (!g_enabled) return false;
  auto consume = [&]() {
    msg->message = WM_NULL;
    return true;
  };
  if (msg->message == WM_KEYDOWN && msg->wParam == VK_ESCAPE) {
    if (g_enabled) {
      ToggleDebugMode();
      return consume();
    }
  }
  // 允许调试检查器窗口正常交互，不冻结它。
  if (IsInspectorWindowOrChild(msg->hwnd)) {
    return false;
  }
  if (msg->message == WM_KEYDOWN && (msg->wParam == 'C' || msg->wParam == 'c') &&
      (GetKeyState(VK_CONTROL) & 0x8000) && (GetKeyState(VK_SHIFT) & 0x8000)) {
    CopySelectedFullPaths();
    return consume();
  }
  if (msg->message == WM_KEYDOWN && (msg->wParam == 'C' || msg->wParam == 'c') && (GetKeyState(VK_CONTROL) & 0x8000)) {
    CopySelectedNames();
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
      HWND h = PickWindowAtPoint(up);
      if (ctrl) ToggleSelect(h);
      else SelectSingle(h);
    } else {
      SelectByRect(sr, ctrl);
    }
    InvalidateOverlay();
    RefreshInspectorTree();
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
  SetDebugRuntimeHotkeys(false);
  if (g_getMsgHook) {
    UnhookWindowsHookEx(g_getMsgHook);
    g_getMsgHook = nullptr;
  }
  if (g_overlay && IsWindow(g_overlay)) {
    DestroyWindow(g_overlay);
    g_overlay = nullptr;
  }
  if (g_inspector && IsWindow(g_inspector)) {
    DestroyWindow(g_inspector);
    g_inspector = nullptr;
  }
  if (g_propsWindow && IsWindow(g_propsWindow)) {
    DestroyWindow(g_propsWindow);
    g_propsWindow = nullptr;
  }
  if (g_jsonFont && g_jsonFont != static_cast<HFONT>(GetStockObject(ANSI_FIXED_FONT))) {
    DeleteObject(g_jsonFont);
  }
  if (g_uiFont && g_uiFont != static_cast<HFONT>(GetStockObject(DEFAULT_GUI_FONT))) {
    DeleteObject(g_uiFont);
  }
  g_jsonFont = nullptr;
  g_uiFont = nullptr;
  g_propsModeToggle = nullptr;
  g_propsCopyBtn = nullptr;
  g_props = nullptr;
}

bool AgisUiDebugPickHandleMessage(MSG* msg) {
  return HandleMsg(msg);
}

