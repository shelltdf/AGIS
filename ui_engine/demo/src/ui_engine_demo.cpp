/**
 * ui_engine 演示（可移植）：`BuildUiEngineDemoWidgetTree` / `WireUiEngineDemoShell`。
 * 文件对话框等仅在 `_WIN32` 构建中启用。
 */

#include "ui_engine_demo.h"

#include "ui_engine/app.h"
#include "ui_engine/widget_core.h"

#include <algorithm>
#include <memory>
#include <string>
#include <utility>
#include <vector>

#if defined(_WIN32)
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <commctrl.h>
#include <commdlg.h>
#include <windows.h>
#endif

namespace agis::ui {

namespace {

/** 菜单栏内每个 `Menu` 固定宽度。 */
constexpr int kMenuCellW = 132;

constexpr int kStrip = 32;
constexpr int kMenuH = 32;
constexpr int kTbH = 44;
constexpr int kStatusH = 28;
constexpr int kLeftW = 268;
constexpr int kRightW = 248;

std::vector<DemoTestRowSpec> g_demo_specs;

void ClearMapDemoWidgets(MapCanvas2D* map) {
  if (!map) {
    return;
  }
  std::vector<Widget*> remove;
  for (auto& ch : map->children()) {
    Widget* w = ch.get();
    if (dynamic_cast<ShortcutHelpPanel*>(w) || dynamic_cast<LayerVisibilityPanel*>(w) ||
        dynamic_cast<MapZoomBar*>(w)) {
      continue;
    }
    remove.push_back(w);
  }
  for (Widget* w : remove) {
    map->removeChild(w);
  }
}

std::unique_ptr<Widget> MakeDemoWidget(int test_id) {
  switch (test_id) {
    case 0: {
      auto l = std::make_unique<Label>();
      l->setText(L"Label");
      l->setGeometry({20, 20, 240, 28});
      return l;
    }
    case 1: {
      auto b = std::make_unique<PushButton>();
      b->setText(L"PushButton");
      b->setGeometry({20, 56, 180, 34});
      return b;
    }
    case 2: {
      auto e = std::make_unique<LineEdit>();
      e->setText(L"LineEdit");
      e->setGeometry({20, 100, 300, 30});
      return e;
    }
    case 3: {
      auto f = std::make_unique<Frame>();
      f->setGeometry({16, 16, 400, 160});
      auto inner = std::make_unique<Label>();
      inner->setText(L"Frame 内标签");
      inner->setGeometry({14, 14, 220, 26});
      f->addChild(std::move(inner));
      return f;
    }
    case 4: {
      auto t = std::make_unique<ToolButton>();
      t->setText(L"ToolButton");
      t->setGeometry({20, 200, 120, 32});
      return t;
    }
    case 5: {
      auto sp = std::make_unique<Splitter>();
      sp->setOrientation(SplitterOrientation::kHorizontal);
      sp->setGeometry({16, 240, 420, 12});
      return sp;
    }
    case 6: {
      auto d = std::make_unique<DockPanel>();
      d->setGeometry({10, 10, std::max(100, 400), std::max(80, 260)});
      return d;
    }
    case 7: {
      auto s = std::make_unique<ScrollArea>();
      s->setGeometry({12, 12, 360, 220});
      auto inner = std::make_unique<Label>();
      inner->setText(L"ScrollArea · inner label");
      inner->setGeometry({10, 10, 300, 28});
      s->setContentWidget(std::move(inner));
      return s;
    }
    case 8: {
      auto c = std::make_unique<Canvas2D>();
      c->setGeometry({12, 12, 400, 240});
      return c;
    }
    case 9: {
      auto s = std::make_unique<StatusBarWidget>();
      s->setGeometry({0, 0, 520, kStatusH});
      return s;
    }
    case 10: {
      auto t = std::make_unique<ToolBarWidget>();
      t->setGeometry({0, 0, 480, 40});
      auto b = std::make_unique<ToolButton>();
      b->setText(L"Demo");
      b->setGeometry({10, 6, 72, 28});
      t->addChild(std::move(b));
      return t;
    }
    case 11: {
      auto d = std::make_unique<DockArea>();
      d->setDockEdge(DockEdge::kLeft);
      d->setGeometry({8, 8, 280, 200});
      return d;
    }
    default:
      return nullptr;
  }
}

void RunDemoTestActionImpl(int action, int row) {
  if (row < 0 || row >= static_cast<int>(g_demo_specs.size())) {
    return;
  }
  const DemoTestRowSpec& sp = g_demo_specs[static_cast<size_t>(row)];
  if (sp.is_header) {
    return;
  }
#if defined(_WIN32)
  HWND owner = static_cast<HWND>(App::instance().demoHostWindow());
  if (action == 1) {
    std::wstring msg = L"测试项：";
    msg += sp.label;
    msg += L"\n\n标识：";
    msg += std::to_wstring(sp.test_id);
    MessageBoxW(owner, msg.c_str(), L"详情", MB_OK | MB_ICONINFORMATION);
    return;
  }
  if (sp.no_root_test) {
    MessageBoxW(owner,
                L"该项无法作为根控件单独测试。\n请通过主框架组合场景或其它集成测试查看。",
                L"ui_engine 演示", MB_OK | MB_ICONINFORMATION);
    return;
  }
#else
  (void)action;
  if (sp.no_root_test) {
    return;
  }
#endif
  MapCanvas2D* map = App::instance().demoMapCanvas();
  if (!map) {
    return;
  }
  ClearMapDemoWidgets(map);
  auto w = MakeDemoWidget(sp.test_id);
  if (!w) {
    return;
  }
  const Rect g = map->geometry();
  w->setGeometry({0, 0, std::max(1, g.w), std::max(1, g.h)});
  map->addChild(std::move(w));
  App::instance().setStatusHint(L"已在画布上挂载演示控件");
  App::instance().invalidateAll();
}

void FillDemoSpecs(std::vector<DemoTestRowSpec>& o) {
  o.clear();
  o.push_back({true, L"widget_core.h", -1, false});
  o.push_back({false, L"Label", 0, false});
  o.push_back({false, L"PushButton", 1, false});
  o.push_back({false, L"LineEdit", 2, false});
  o.push_back({false, L"Frame", 3, false});
  o.push_back({false, L"ScrollArea", 7, false});
  o.push_back({false, L"Splitter", 5, false});
  o.push_back({false, L"Canvas2D", 8, false});
  o.push_back({false, L"Window", -1, true});
  o.push_back({false, L"DialogWindow", -1, true});
  o.push_back({false, L"PopupMenu", -1, true});

  o.push_back({true, L"widgets_mainframe.h", -1, false});
  o.push_back({false, L"ToolButton", 4, false});
  o.push_back({false, L"ToolBarWidget", 10, false});
  o.push_back({false, L"StatusBarWidget", 9, false});
  o.push_back({false, L"DockPanel", 6, false});
  o.push_back({false, L"DockArea（简）", 11, false});
  o.push_back({false, L"MenuBarWidget", -1, true});
  o.push_back({false, L"MainFrame 壳层", -1, true});
  o.push_back({false, L"MapCanvas2D（本画布）", -1, true});
  o.push_back({false, L"ShortcutHelp / LayerVisibility / MapZoomBar（叠层）", -1, true});
}

#if defined(_WIN32)
void DemoFileNew() {
  MapCanvas2D* m = App::instance().demoMapCanvas();
  if (!m) {
    return;
  }
  ClearMapDemoWidgets(m);
  m->clearImage();
  m->setDocumentPath(L"");
  App::instance().setStatusHint(L"新建：已清空画布与演示控件");
  App::instance().invalidateAll();
}

void DemoFileSaveAs();

void DemoFileOpen() {
  wchar_t buf[MAX_PATH]{};
  OPENFILENAMEW ofn{};
  ofn.lStructSize = sizeof(ofn);
  ofn.lpstrFile = buf;
  ofn.nMaxFile = MAX_PATH;
  ofn.lpstrFilter = L"Images\0*.png;*.bmp;*.jpg;*.jpeg;*.gif\0All\0*.*\0";
  ofn.nFilterIndex = 1;
  ofn.Flags = OFN_FILEMUSTEXIST | OFN_PATHMUSTEXIST;
  if (GetOpenFileNameW(&ofn) == 0) {
    return;
  }
  MapCanvas2D* m = App::instance().demoMapCanvas();
  if (!m || !m->loadImageFile(buf)) {
    MessageBoxW(static_cast<HWND>(App::instance().demoHostWindow()), L"无法加载该图像文件。", L"Open",
                MB_OK | MB_ICONWARNING);
    return;
  }
  ClearMapDemoWidgets(m);
  App::instance().setStatusHint(L"已打开图像");
  App::instance().invalidateAll();
}

void DemoFileSave() {
  MapCanvas2D* m = App::instance().demoMapCanvas();
  if (!m || !m->hasImage()) {
    App::instance().setStatusHint(L"无图像可保存");
    return;
  }
  if (m->documentPath().empty()) {
    DemoFileSaveAs();
    return;
  }
  if (!m->saveImageToFile(m->documentPath().c_str())) {
    MessageBoxW(static_cast<HWND>(App::instance().demoHostWindow()), L"保存失败。", L"Save", MB_OK | MB_ICONWARNING);
    return;
  }
  App::instance().setStatusHint(L"已保存");
}

void DemoFileSaveAs() {
  MapCanvas2D* m = App::instance().demoMapCanvas();
  if (!m || !m->hasImage()) {
    App::instance().setStatusHint(L"无图像可保存");
    return;
  }
  wchar_t buf[MAX_PATH]{};
  OPENFILENAMEW ofn{};
  ofn.lStructSize = sizeof(ofn);
  ofn.lpstrFile = buf;
  ofn.nMaxFile = MAX_PATH;
  ofn.lpstrFilter = L"PNG\0*.png\0BMP\0*.bmp\0All\0*.*\0";
  ofn.nFilterIndex = 1;
  ofn.Flags = OFN_OVERWRITEPROMPT;
  if (GetSaveFileNameW(&ofn) == 0) {
    return;
  }
  if (!m->saveImageToFile(buf)) {
    MessageBoxW(static_cast<HWND>(App::instance().demoHostWindow()), L"保存失败。", L"Save As",
                MB_OK | MB_ICONWARNING);
    return;
  }
  m->setDocumentPath(buf);
  App::instance().setStatusHint(L"已另存为");
  App::instance().invalidateAll();
}

void DemoHelpTopics() {
  HWND owner = static_cast<HWND>(App::instance().demoHostWindow());
  const bool zh = App::instance().uiLanguage() == UiLanguage::kZhCN;
  const wchar_t* body =
      zh ? L"- 左侧：按头文件分组的控件测试；双击运行，右键「运行 / 详情」。\n"
           L"- 中间：2D 画布支持中键平移、滚轮缩放（命中叠层时仍作用在画布上）；文件菜单可加载位图。\n"
           L"- 右侧：单个 Dock 区域内三栏。\n"
           L"- 语言 / 主题：菜单即时切换。"
         : L"- Left: tests grouped by header file; double-click or context menu Run / Details.\n"
           L"- Center: 2D canvas — middle-drag pan, wheel zoom (routed even over overlays); File loads bitmaps.\n"
           L"- Right: one Dock area with three panels.\n"
           L"- Language / Theme: from the menu.";
  int btn = 0;
  TaskDialog(owner, nullptr, L"AGIS", zh ? L"帮助主题" : L"Help topics", body, TDCBF_OK_BUTTON,
             TD_INFORMATION_ICON, &btn);
}

void DemoAbout() {
  HWND owner = static_cast<HWND>(App::instance().demoHostWindow());
  const bool zh = App::instance().uiLanguage() == UiLanguage::kZhCN;
  const wchar_t* body =
      zh ? L"用于验证自绘主框架、Dock、2D 画布与控件树。\n\n版本：演示构建\nAGIS / ui_engine"
         : L"Self-drawn shell, Dock, 2D canvas, widget tree.\n\nDemo build\nAGIS / ui_engine";
  int btn = 0;
  TaskDialog(owner, nullptr, L"AGIS", zh ? L"关于 ui_engine 演示" : L"About ui_engine demo", body, TDCBF_OK_BUTTON,
             TD_INFORMATION_ICON, &btn);
}
#endif

void UpdateRightDockTitles(MainFrame* mf) {
  if (!mf) {
    return;
  }
  auto& kids = mf->children();
  if (kids.size() < 6) {
    return;
  }
  auto* dr = dynamic_cast<DockArea*>(kids[3].get());
  if (!dr || dr->children().size() < 2) {
    return;
  }
  auto* view = dynamic_cast<DockView*>(dr->children()[1].get());
  if (!view) {
    return;
  }
  const bool zh = App::instance().uiLanguage() == UiLanguage::kZhCN;
  const wchar_t* titles[3] = {zh ? L"属性 / 备注" : L"Properties / notes",
                               zh ? L"剪贴板 / 缓冲" : L"Clipboard / buffer",
                               zh ? L"预览 / 占位" : L"Preview / placeholder"};
  const int n = static_cast<int>(view->children().size());
  for (int i = 0; i < n && i < 3; ++i) {
    if (auto* p = dynamic_cast<DemoRightSlotPanel*>(view->children()[static_cast<size_t>(i)].get())) {
      p->setSlot(i, titles[i]);
    }
  }
}

void ApplyDemoShellLanguage(MainFrame* mf) {
  if (!mf) {
    return;
  }
  auto& kids = mf->children();
  if (kids.size() < 6) {
    return;
  }
  auto* mb = dynamic_cast<MenuBarWidget*>(kids[1].get());
  if (!mb || mb->children().size() < 4) {
    return;
  }
  const bool zh = App::instance().uiLanguage() == UiLanguage::kZhCN;

  auto* m_file = dynamic_cast<Menu*>(mb->children()[0].get());
  auto* m_lang = dynamic_cast<Menu*>(mb->children()[1].get());
  auto* m_theme = dynamic_cast<Menu*>(mb->children()[2].get());
  auto* m_help = dynamic_cast<Menu*>(mb->children()[3].get());
  if (m_file) {
    m_file->setTitle(zh ? L"文件" : L"File");
    const wchar_t* tx[] = {zh ? L"新建" : L"New", zh ? L"打开…" : L"Open…", zh ? L"保存" : L"Save",
                          zh ? L"另存为…" : L"Save As…"};
    int i = 0;
    for (auto& ch : m_file->children()) {
      if (auto* mi = dynamic_cast<MenuItem*>(ch.get())) {
        if (i < 4) {
          mi->setText(tx[i]);
        }
        ++i;
      }
    }
  }
  if (m_lang) {
    m_lang->setTitle(zh ? L"语言" : L"Language");
    int i = 0;
    const wchar_t* tx[] = {zh ? L"中文" : L"Chinese (zh-CN)", zh ? L"English" : L"English"};
    for (auto& ch : m_lang->children()) {
      if (auto* mi = dynamic_cast<MenuItem*>(ch.get())) {
        if (i < 2) {
          mi->setText(tx[i]);
        }
        ++i;
      }
    }
  }
  if (m_theme) {
    m_theme->setTitle(zh ? L"主题" : L"Theme");
    const wchar_t* tx[] = {zh ? L"跟随系统" : L"Follow system", zh ? L"浅色" : L"Light", zh ? L"深色" : L"Dark"};
    int i = 0;
    for (auto& ch : m_theme->children()) {
      if (auto* mi = dynamic_cast<MenuItem*>(ch.get())) {
        if (i < 3) {
          mi->setText(tx[i]);
        }
        ++i;
      }
    }
  }
  if (m_help) {
    m_help->setTitle(zh ? L"帮助" : L"Help");
    const wchar_t* tx[] = {zh ? L"帮助主题" : L"Help topics", zh ? L"关于" : L"About"};
    int i = 0;
    for (auto& ch : m_help->children()) {
      if (auto* mi = dynamic_cast<MenuItem*>(ch.get())) {
        if (i < 2) {
          mi->setText(tx[i]);
        }
        ++i;
      }
    }
  }

  mf->setTitle(zh ? L"ui_engine 演示 — 自绘主框架 / 2D 画布" : L"ui_engine demo — main frame / 2D canvas");
  UpdateRightDockTitles(mf);
  App::instance().relayoutFromLastClientSize();
}

/** 左侧缘条 + 测试列表。 */
void AddDockLeftTestList(DockArea* dock, int w, int h, DemoTestListPanel* list) {
  auto strip = std::make_unique<DockButtonStrip>();
  strip->setGeometry({0, 0, kStrip, h});
  dock->addChild(std::move(strip));
  const int vw = w - kStrip;
  auto view = std::make_unique<DockView>();
  view->setGeometry({kStrip, 0, vw, h});
  list->setGeometry({0, 0, vw, h});
  view->addChild(std::unique_ptr<DemoTestListPanel>(list));
  dock->addChild(std::move(view));
}

/** 右侧单 Dock 区：缘条 + 三栏。 */
void AddDockRightThreeSlots(DockArea* dock, int w, int h, const std::wstring (&titles)[3]) {
  const int vw = w - kStrip;
  auto strip = std::make_unique<DockButtonStrip>();
  strip->setGeometry({vw, 0, kStrip, h});
  dock->addChild(std::move(strip));
  auto view = std::make_unique<DockView>();
  view->setGeometry({0, 0, vw, h});
  const int h3 = h > 0 ? h / 3 : 0;
  int y = 0;
  for (int i = 0; i < 3; ++i) {
    auto p = std::make_unique<DemoRightSlotPanel>();
    p->setSlot(i, titles[static_cast<size_t>(i)]);
    const int hh = (i == 2) ? std::max(0, h - y) : h3;
    p->setGeometry({0, y, vw, hh});
    y += hh;
    view->addChild(std::move(p));
  }
  dock->addChild(std::move(view));
}

void RelayoutDockLeftStrip(DockArea* dock, int w, int h, bool expanded) {
  auto& c = dock->children();
  if (c.size() < 2) {
    return;
  }
  auto* strip = dynamic_cast<DockButtonStrip*>(c[0].get());
  auto* view = dynamic_cast<DockView*>(c[1].get());
  if (!strip || !view) {
    return;
  }
  strip->setGeometry({0, 0, kStrip, h});
  const int vw = expanded ? std::max(0, w - kStrip) : 0;
  view->setGeometry({kStrip, 0, vw, h});
  view->setVisible(expanded);
  if (!view->children().empty()) {
    view->children()[0]->setGeometry({0, 0, vw, h});
    view->children()[0]->setVisible(expanded);
  }
}

void RelayoutDockRightThreeSlots(DockArea* dock, int w, int h, bool expanded) {
  auto& c = dock->children();
  if (c.size() < 2) {
    return;
  }
  auto* strip = dynamic_cast<DockButtonStrip*>(c[0].get());
  auto* view = dynamic_cast<DockView*>(c[1].get());
  if (!strip || !view) {
    return;
  }
  const int vw = expanded ? std::max(0, w - kStrip) : 0;
  strip->setGeometry({vw, 0, kStrip, h});
  view->setGeometry({0, 0, vw, h});
  view->setVisible(expanded);
  const int n = static_cast<int>(view->children().size());
  const int h3 = (expanded && n == 3 && h > 0) ? h / 3 : 0;
  int y = 0;
  for (int i = 0; i < n; ++i) {
    const int hh = (expanded && n == 3) ? ((i == n - 1) ? std::max(0, h - y) : h3) : 0;
    view->children()[static_cast<size_t>(i)]->setGeometry({0, y, vw, hh});
    view->children()[static_cast<size_t>(i)]->setVisible(expanded);
    y += hh;
  }
}

}  // namespace

static void RelayoutMenuBarChildren(MenuBarWidget* bar, int w, int h) {
  if (!bar || w <= 0 || h <= 0) {
    return;
  }
  auto& c = bar->children();
  const int n = static_cast<int>(c.size());
  if (n == 0) {
    return;
  }
  int x = 0;
  for (int i = 0; i < n; ++i) {
    const int cell_w = std::min(kMenuCellW, std::max(0, w - x));
    if (cell_w <= 0) {
      break;
    }
    if (auto* menu = dynamic_cast<Menu*>(c[i].get())) {
      menu->syncGeometryWithBarCell(x, 0, cell_w, h);
    } else {
      c[i]->setGeometry({x, 0, cell_w, h});
    }
    x += cell_w;
  }
}

void RelayoutToolBarChildren(ToolBarWidget* bar, int bar_w, int bar_h) {
  if (!bar || bar_w <= 0 || bar_h <= 0) {
    return;
  }
  constexpr int margin = 8;
  constexpr int gap = 6;
  int x = margin;
  const int btn_h = std::max(24, bar_h - 10);
  const int y = (bar_h - btn_h) / 2;
  for (auto& ch : bar->children()) {
    if (auto* tb = dynamic_cast<ToolButton*>(ch.get())) {
      const int bw = tb->intrinsicWidth();
      tb->setGeometry({x, y, bw, btn_h});
      x += bw + gap;
    } else if (auto* lb = dynamic_cast<Label*>(ch.get())) {
      const int bw = std::max(12, static_cast<int>(lb->text().size()) * 9 + 10);
      lb->setGeometry({x, y, bw, btn_h});
      x += bw + gap;
    }
  }
}

void RelayoutMapCanvasOverlays(MapCanvas2D* map, int cw, int ch) {
  if (!map || cw <= 0 || ch <= 0) {
    return;
  }
  for (auto& child : map->children()) {
    Widget* w = child.get();
    if (dynamic_cast<ShortcutHelpPanel*>(w) || dynamic_cast<LayerVisibilityPanel*>(w) ||
        dynamic_cast<MapZoomBar*>(w)) {
      continue;
    }
    w->setGeometry({0, 0, cw, ch});
  }
  ShortcutHelpPanel* sh = nullptr;
  LayerVisibilityPanel* vis = nullptr;
  MapZoomBar* zb = nullptr;
  for (const auto& child : map->children()) {
    if (auto* p = dynamic_cast<ShortcutHelpPanel*>(child.get())) {
      sh = p;
    }
    if (auto* p = dynamic_cast<LayerVisibilityPanel*>(child.get())) {
      vis = p;
    }
    if (auto* p = dynamic_cast<MapZoomBar*>(child.get())) {
      zb = p;
    }
  }
  const int sh_h = (sh && sh->expanded()) ? 140 : 28;
  if (sh) {
    sh->setGeometry({10, 10, std::min(320, std::max(140, cw - 20)), sh_h});
  }
  const int vis_w = std::min(240, std::max(150, cw / 3));
  if (vis) {
    vis->setGeometry({cw - vis_w - 10, 10, vis_w, 76});
  }
  constexpr int bar_h = 38;
  if (zb) {
    zb->setGeometry({10, ch - bar_h - 10, std::max(140, cw - 20), bar_h});
  }
}

void RelayoutMainFrameForClientSizeImpl(MainFrame* root, int client_w, int client_h) {
  if (!root || client_w <= 0 || client_h <= 0) {
    return;
  }
  auto& kids = root->children();
  if (kids.size() < 6) {
    return;
  }

  root->setGeometry({0, 0, client_w, client_h});

  kids[1]->setGeometry({0, 0, client_w, kMenuH});
  if (auto* mb = dynamic_cast<MenuBarWidget*>(kids[1].get())) {
    RelayoutMenuBarChildren(mb, client_w, kMenuH);
  }
  kids[0]->setGeometry({0, kMenuH, client_w, kTbH});
  if (auto* tbw = dynamic_cast<ToolBarWidget*>(kids[0].get())) {
    RelayoutToolBarChildren(tbw, client_w, kTbH);
  }

  const auto* dock_left = dynamic_cast<DockArea*>(kids[2].get());
  const auto* dock_right = dynamic_cast<DockArea*>(kids[3].get());

  const bool le = dock_left && dock_left->contentExpanded();
  const bool re = dock_right && dock_right->contentExpanded();
  const int lw = dock_left ? (le ? kLeftW : kStrip) : 0;
  const int rw = dock_right ? (re ? kRightW : kStrip) : 0;

  const int y0 = kMenuH + kTbH;
  const int content_h = std::max(0, client_h - y0 - kStatusH);
  const int mid_x = lw;
  const int center_w = std::max(0, client_w - lw - rw);

  if (auto* dl = dynamic_cast<DockArea*>(kids[2].get())) {
    dl->setGeometry({0, y0, lw, content_h});
    RelayoutDockLeftStrip(dl, lw, content_h, le);
  }
  if (auto* dr = dynamic_cast<DockArea*>(kids[3].get())) {
    dr->setGeometry({mid_x + center_w, y0, rw, content_h});
    RelayoutDockRightThreeSlots(dr, rw, content_h, re);
  }

  kids[4]->setGeometry({mid_x, y0, center_w, content_h});
  if (auto* map = dynamic_cast<MapCanvas2D*>(kids[4].get())) {
    RelayoutMapCanvasOverlays(map, center_w, content_h);
  }

  kids[5]->setGeometry({0, client_h - kStatusH, client_w, kStatusH});
}

void RelayoutMainFrameForClientSize(MainFrame* root, int client_w, int client_h) {
  RelayoutMainFrameForClientSizeImpl(root, client_w, client_h);
}

void WireUiEngineDemoShell(MainFrame* root) {
  App& app = App::instance();
  app.setDemoTestRunner([](int action, int row) { RunDemoTestActionImpl(action, row); });
  app.setDemoShellInvalidatedHandler([&app]() {
    if (auto* mf = dynamic_cast<MainFrame*>(app.primaryRootWidget())) {
      ApplyDemoShellLanguage(mf);
    }
  });
  ApplyDemoShellLanguage(root);
}

std::unique_ptr<MainFrame> BuildUiEngineDemoWidgetTree() {
  FillDemoSpecs(g_demo_specs);

  constexpr int kClientW = 1100;
  constexpr int kTotalH = 720;

  auto root = std::make_unique<MainFrame>();
  root->setGeometry({0, 0, kClientW, kTotalH});

  auto menu_bar = std::make_unique<MenuBarWidget>();
  menu_bar->setGeometry({0, 0, kClientW, kMenuH});

  auto file = std::make_unique<Menu>();
  file->setTitle(L"File");
  {
    auto n1 = std::make_unique<MenuItem>();
    n1->setText(L"New");
    auto n2 = std::make_unique<MenuItem>();
    n2->setText(L"Open");
    auto n3 = std::make_unique<MenuItem>();
    n3->setText(L"Save");
    auto n4 = std::make_unique<MenuItem>();
    n4->setText(L"Save As");
#if defined(_WIN32)
    n1->setOnActivate(DemoFileNew);
    n2->setOnActivate(DemoFileOpen);
    n3->setOnActivate(DemoFileSave);
    n4->setOnActivate(DemoFileSaveAs);
#endif
    file->addChild(std::move(n1));
    file->addChild(std::move(n2));
    file->addChild(std::move(n3));
    file->addChild(std::move(n4));
  }
  menu_bar->addChild(std::move(file));

  auto language = std::make_unique<Menu>();
  language->setTitle(L"Language");
  {
    auto n1 = std::make_unique<MenuItem>();
    n1->setText(L"中文");
    n1->setOnActivate([]() { App::instance().setUiLanguage(UiLanguage::kZhCN); });
    auto n2 = std::make_unique<MenuItem>();
    n2->setText(L"English");
    n2->setOnActivate([]() { App::instance().setUiLanguage(UiLanguage::kEnglish); });
    language->addChild(std::move(n1));
    language->addChild(std::move(n2));
  }
  menu_bar->addChild(std::move(language));

  auto theme = std::make_unique<Menu>();
  theme->setTitle(L"Theme");
  {
    auto n1 = std::make_unique<MenuItem>();
    n1->setText(L"Follow system");
    n1->setOnActivate([]() { App::instance().setUiTheme(UiTheme::kFollowSystem); });
    auto n2 = std::make_unique<MenuItem>();
    n2->setText(L"Light");
    n2->setOnActivate([]() { App::instance().setUiTheme(UiTheme::kLight); });
    auto n3 = std::make_unique<MenuItem>();
    n3->setText(L"Dark");
    n3->setOnActivate([]() { App::instance().setUiTheme(UiTheme::kDark); });
    theme->addChild(std::move(n1));
    theme->addChild(std::move(n2));
    theme->addChild(std::move(n3));
  }
  menu_bar->addChild(std::move(theme));

  auto help = std::make_unique<Menu>();
  help->setTitle(L"Help");
  {
    auto n1 = std::make_unique<MenuItem>();
    n1->setText(L"Help topics");
    auto n2 = std::make_unique<MenuItem>();
    n2->setText(L"About");
#if defined(_WIN32)
    n1->setOnActivate(DemoHelpTopics);
    n2->setOnActivate(DemoAbout);
#endif
    help->addChild(std::move(n1));
    help->addChild(std::move(n2));
  }
  menu_bar->addChild(std::move(help));

  RelayoutMenuBarChildren(menu_bar.get(), kClientW, kMenuH);

  auto tb = std::make_unique<ToolBarWidget>();
  tb->setGeometry({0, kMenuH, kClientW, kTbH});
  for (const wchar_t* cap : {L"New", L"Open", L"Save", L"Save As"}) {
    auto btn = std::make_unique<ToolButton>();
    btn->setText(cap);
#if defined(_WIN32)
    if (wcscmp(cap, L"New") == 0) {
      btn->setOnActivate(DemoFileNew);
    } else if (wcscmp(cap, L"Open") == 0) {
      btn->setOnActivate(DemoFileOpen);
    } else if (wcscmp(cap, L"Save") == 0) {
      btn->setOnActivate(DemoFileSave);
    } else if (wcscmp(cap, L"Save As") == 0) {
      btn->setOnActivate(DemoFileSaveAs);
    }
#endif
    tb->addChild(std::move(btn));
  }
  {
    auto sep = std::make_unique<Label>();
    sep->setText(L"│");
    tb->addChild(std::move(sep));
  }
  {
    auto btn = std::make_unique<ToolButton>();
    btn->setText(L"Help");
#if defined(_WIN32)
    btn->setOnActivate(DemoHelpTopics);
#endif
    tb->addChild(std::move(btn));
  }
  RelayoutToolBarChildren(tb.get(), kClientW, kTbH);

  root->addChild(std::move(tb));
  root->addChild(std::move(menu_bar));

  const int y0 = kMenuH + kTbH;
  const int content_h = kTotalH - y0 - kStatusH;
  constexpr int left_w = kLeftW;
  constexpr int right_w = kRightW;
  const int cw = std::max(100, kClientW - left_w - right_w);
  const int mid_x = left_w;

  auto dock_left = std::make_unique<DockArea>();
  dock_left->setDockEdge(DockEdge::kLeft);
  dock_left->setGeometry({0, y0, left_w, content_h});
  auto* list_ptr = new DemoTestListPanel();
  list_ptr->setSpecs(g_demo_specs);
  AddDockLeftTestList(dock_left.get(), left_w, content_h, list_ptr);
  root->addChild(std::move(dock_left));

  const int rx = mid_x + cw;
  std::wstring rtitles[3] = {L"属性 / 备注", L"剪贴板 / 缓冲", L"预览 / 占位"};
  auto dock_right = std::make_unique<DockArea>();
  dock_right->setDockEdge(DockEdge::kRight);
  dock_right->setGeometry({rx, y0, right_w, content_h});
  AddDockRightThreeSlots(dock_right.get(), right_w, content_h, rtitles);
  root->addChild(std::move(dock_right));

  auto center = std::make_unique<MapCanvas2D>();
  center->setGeometry({mid_x, y0, cw, content_h});
  center->addChild(std::make_unique<ShortcutHelpPanel>());
  center->addChild(std::make_unique<LayerVisibilityPanel>());
  center->addChild(std::make_unique<MapZoomBar>());
  MapCanvas2D* map_for_app = center.get();
  root->addChild(std::move(center));
  App::instance().setDemoMapCanvas(map_for_app);

  auto status = std::make_unique<StatusBarWidget>();
  status->setGeometry({0, kTotalH - kStatusH, kClientW, kStatusH});
  root->addChild(std::move(status));

  WireUiEngineDemoShell(root.get());
  return root;
}

}  // namespace agis::ui
