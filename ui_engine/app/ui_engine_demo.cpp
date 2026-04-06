/**
 * ui_engine 演示（可移植）：`BuildUiEngineDemoWidgetTree`。
 * 不包含任何操作系统 API；窗口与消息循环由平台层实现。
 */

#include "ui_engine_demo.h"

#include "ui_engine/app.h"

#include <algorithm>
#include <memory>
#include <utility>

namespace agis::ui {

/** 菜单栏内每个 `Menu` 固定宽度、左起排列（与 `kMenuH` 行高一致）。 */
constexpr int kMenuCellW = 120;

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
  constexpr int margin = 6;
  constexpr int gap = 4;
  int x = margin;
  const int btn_h = std::max(22, bar_h - 8);
  const int y = (bar_h - btn_h) / 2;
  for (auto& ch : bar->children()) {
    if (auto* tb = dynamic_cast<ToolButton*>(ch.get())) {
      const int bw = tb->intrinsicWidth();
      tb->setGeometry({x, y, bw, btn_h});
      x += bw + gap;
    } else if (auto* lb = dynamic_cast<Label*>(ch.get())) {
      const int bw = std::max(12, static_cast<int>(lb->text().size()) * 8 + 8);
      lb->setGeometry({x, y, bw, btn_h});
      x += bw + gap;
    }
  }
}

namespace {

constexpr int kStrip = 32;

/** 左侧缘条 + 内容区（条带在左）。 */
void AddDockLeftStrip(DockArea* dock, int w, int h) {
  auto strip = std::make_unique<DockButtonStrip>();
  strip->setGeometry({0, 0, kStrip, h});
  dock->addChild(std::move(strip));
  const int vw = w - kStrip;
  auto view = std::make_unique<DockView>();
  view->setGeometry({kStrip, 0, vw, h});
  auto panel = std::make_unique<LayerDockPanel>();
  panel->setGeometry({0, 0, vw, h});
  view->addChild(std::move(panel));
  dock->addChild(std::move(view));
}

/** 右侧缘条 + 内容区（条带在右，与 AGIS 右侧 Props 缘条一致）。 */
void AddDockRightStrip(DockArea* dock, int w, int h, bool use_props_panel) {
  const int vw = w - kStrip;
  auto strip = std::make_unique<DockButtonStrip>();
  strip->setGeometry({vw, 0, kStrip, h});
  dock->addChild(std::move(strip));
  auto view = std::make_unique<DockView>();
  view->setGeometry({0, 0, vw, h});
  std::unique_ptr<DockPanel> panel =
      use_props_panel ? std::unique_ptr<DockPanel>(std::make_unique<PropsDockPanel>())
                      : std::unique_ptr<DockPanel>(std::make_unique<DockPanel>());
  panel->setGeometry({0, 0, vw, h});
  view->addChild(std::move(panel));
  dock->addChild(std::move(view));
}

/** 顶侧缘条 + 内容区（条带在上）。 */
void AddDockTopStrip(DockArea* dock, int w, int h) {
  auto strip = std::make_unique<DockButtonStrip>();
  strip->setGeometry({0, 0, w, kStrip});
  dock->addChild(std::move(strip));
  const int vh = h - kStrip;
  auto view = std::make_unique<DockView>();
  view->setGeometry({0, kStrip, w, vh});
  auto panel = std::make_unique<DockPanel>();
  panel->setGeometry({0, 0, w, vh});
  view->addChild(std::move(panel));
  dock->addChild(std::move(view));
}

/** 底侧缘条 + 内容区（条带在下）。 */
void AddDockBottomStrip(DockArea* dock, int w, int h) {
  const int vh = h - kStrip;
  auto view = std::make_unique<DockView>();
  view->setGeometry({0, 0, w, vh});
  auto panel = std::make_unique<DockPanel>();
  panel->setGeometry({0, 0, w, vh});
  view->addChild(std::move(panel));
  dock->addChild(std::move(view));
  auto strip = std::make_unique<DockButtonStrip>();
  strip->setGeometry({0, vh, w, kStrip});
  dock->addChild(std::move(strip));
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

void RelayoutDockRightStrip(DockArea* dock, int w, int h, bool expanded) {
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
  if (!view->children().empty()) {
    view->children()[0]->setGeometry({0, 0, vw, h});
    view->children()[0]->setVisible(expanded);
  }
}

void RelayoutDockTopStrip(DockArea* dock, int w, int h, bool expanded) {
  auto& c = dock->children();
  if (c.size() < 2) {
    return;
  }
  auto* strip = dynamic_cast<DockButtonStrip*>(c[0].get());
  auto* view = dynamic_cast<DockView*>(c[1].get());
  if (!strip || !view) {
    return;
  }
  strip->setGeometry({0, 0, w, kStrip});
  const int vh = expanded ? std::max(0, h - kStrip) : 0;
  view->setGeometry({0, kStrip, w, vh});
  view->setVisible(expanded);
  if (!view->children().empty()) {
    view->children()[0]->setGeometry({0, 0, w, vh});
    view->children()[0]->setVisible(expanded);
  }
}

void RelayoutDockBottomStrip(DockArea* dock, int w, int h, bool expanded) {
  auto& c = dock->children();
  if (c.size() < 2) {
    return;
  }
  auto* view = dynamic_cast<DockView*>(c[0].get());
  auto* strip = dynamic_cast<DockButtonStrip*>(c[1].get());
  if (!view || !strip) {
    return;
  }
  const int vh = expanded ? std::max(0, h - kStrip) : 0;
  view->setGeometry({0, 0, w, vh});
  view->setVisible(expanded);
  if (!view->children().empty()) {
    view->children()[0]->setGeometry({0, 0, w, vh});
    view->children()[0]->setVisible(expanded);
  }
  strip->setGeometry({0, vh, w, kStrip});
}

constexpr int kMenuH = 28;
constexpr int kTbH = 36;
constexpr int kStatusH = 24;
constexpr int kLeftW = 200;
constexpr int kRightW = 200;
constexpr int kTopDockH = 48;
constexpr int kBotDockH = 48;

void RelayoutMapCanvasOverlays(MapCanvas2D* map, int cw, int ch) {
  if (!map || cw <= 0 || ch <= 0) {
    return;
  }
  ShortcutHelpPanel* sh = nullptr;
  LayerVisibilityPanel* vis = nullptr;
  MapZoomBar* zb = nullptr;
  for (const auto& ch : map->children()) {
    if (auto* p = dynamic_cast<ShortcutHelpPanel*>(ch.get())) {
      sh = p;
    }
    if (auto* p = dynamic_cast<LayerVisibilityPanel*>(ch.get())) {
      vis = p;
    }
    if (auto* p = dynamic_cast<MapZoomBar*>(ch.get())) {
      zb = p;
    }
  }
  const int sh_h = (sh && sh->expanded()) ? 132 : 26;
  if (sh) {
    sh->setGeometry({8, 8, std::min(300, std::max(120, cw - 16)), sh_h});
  }
  const int vis_w = std::min(220, std::max(140, cw / 3));
  if (vis) {
    vis->setGeometry({cw - vis_w - 8, 8, vis_w, 72});
  }
  constexpr int bar_h = 36;
  if (zb) {
    zb->setGeometry({8, ch - bar_h - 8, std::max(120, cw - 16), bar_h});
  }
}

void RelayoutMainFrameForClientSizeImpl(MainFrame* root, int client_w, int client_h) {
  if (!root || client_w <= 0 || client_h <= 0) {
    return;
  }
  auto& kids = root->children();
  if (kids.size() < 10) {
    return;
  }

  root->setGeometry({0, 0, client_w, client_h});

  /** `kids[1]` 为菜单栏、`kids[0]` 为工具栏：子控件逆序命中，菜单栏须后添加（见 `BuildUiEngineDemoWidgetTree`）。 */
  kids[1]->setGeometry({0, 0, client_w, kMenuH});
  if (auto* mb = dynamic_cast<MenuBarWidget*>(kids[1].get())) {
    RelayoutMenuBarChildren(mb, client_w, kMenuH);
  }
  /** 下拉为叠层，不占用主壳层纵向排版；工具栏紧贴固定高度菜单栏下缘。 */
  kids[0]->setGeometry({0, kMenuH, client_w, kTbH});
  if (auto* tbw = dynamic_cast<ToolBarWidget*>(kids[0].get())) {
    RelayoutToolBarChildren(tbw, client_w, kTbH);
  }

  const auto* dock_left = dynamic_cast<DockArea*>(kids[2].get());
  const auto* dr1 = dynamic_cast<DockArea*>(kids[3].get());
  const auto* dr2 = dynamic_cast<DockArea*>(kids[4].get());
  const auto* dr3 = dynamic_cast<DockArea*>(kids[5].get());
  const auto* dock_top = dynamic_cast<DockArea*>(kids[6].get());
  const auto* dock_bottom = dynamic_cast<DockArea*>(kids[8].get());

  const bool le = dock_left && dock_left->contentExpanded();
  const int lw = dock_left ? (le ? kLeftW : kStrip) : 0;
  const int r1w = dr1 ? (dr1->contentExpanded() ? kRightW : kStrip) : kStrip;
  const int r2w = dr2 ? (dr2->contentExpanded() ? kRightW : kStrip) : kStrip;
  const int r3w = dr3 ? (dr3->contentExpanded() ? kRightW : kStrip) : kStrip;
  const int rw = std::max({r1w, r2w, r3w});

  const bool te = dock_top && dock_top->contentExpanded();
  const bool be = dock_bottom && dock_bottom->contentExpanded();
  const int top_h = dock_top ? (te ? kTopDockH : kStrip) : 0;
  const int bot_h = dock_bottom ? (be ? kBotDockH : kStrip) : 0;

  const int y0 = kMenuH + kTbH;
  const int content_h = std::max(0, client_h - y0 - kStatusH);
  const int mid_x = lw;
  const int center_w = std::max(0, client_w - lw - rw);
  const int rx = mid_x + center_w;

  const int rh1 = content_h > 0 ? content_h / 3 : 0;
  const int rh2 = content_h > 0 ? content_h / 3 : 0;
  const int rh3 = std::max(0, content_h - rh1 - rh2);

  const int canvas_h = std::max(0, content_h - top_h - bot_h);

  if (auto* dl = dynamic_cast<DockArea*>(kids[2].get())) {
    dl->setGeometry({0, y0, lw, content_h});
    RelayoutDockLeftStrip(dl, lw, content_h, le);
  }

  if (auto* d1 = dynamic_cast<DockArea*>(kids[3].get())) {
    d1->setGeometry({rx, y0, rw, rh1});
    RelayoutDockRightStrip(d1, rw, rh1, d1->contentExpanded());
  }
  if (auto* d2 = dynamic_cast<DockArea*>(kids[4].get())) {
    d2->setGeometry({rx, y0 + rh1, rw, rh2});
    RelayoutDockRightStrip(d2, rw, rh2, d2->contentExpanded());
  }
  if (auto* d3 = dynamic_cast<DockArea*>(kids[5].get())) {
    d3->setGeometry({rx, y0 + rh1 + rh2, rw, rh3});
    RelayoutDockRightStrip(d3, rw, rh3, d3->contentExpanded());
  }

  if (auto* dt = dynamic_cast<DockArea*>(kids[6].get())) {
    dt->setGeometry({mid_x, y0, center_w, top_h});
    RelayoutDockTopStrip(dt, center_w, top_h, te);
  }

  kids[7]->setGeometry({mid_x, y0 + top_h, center_w, canvas_h});
  if (auto* map = dynamic_cast<MapCanvas2D*>(kids[7].get())) {
    RelayoutMapCanvasOverlays(map, center_w, canvas_h);
  }

  if (auto* db = dynamic_cast<DockArea*>(kids[8].get())) {
    db->setGeometry({mid_x, y0 + content_h - bot_h, center_w, bot_h});
    RelayoutDockBottomStrip(db, center_w, bot_h, be);
  }

  kids[9]->setGeometry({0, client_h - kStatusH, client_w, kStatusH});
}

}  // namespace

void RelayoutMainFrameForClientSize(MainFrame* root, int client_w, int client_h) {
  RelayoutMainFrameForClientSizeImpl(root, client_w, client_h);
}

std::unique_ptr<MainFrame> BuildUiEngineDemoWidgetTree() {
  auto root = std::make_unique<MainFrame>();
  root->setTitle(L"ui_engine 演示 — 菜单 / 工具栏 / 多 Dock / 2D 画布叠层 / 状态栏");
  root->setGeometry({0, 0, 960, 640});

  auto menu_bar = std::make_unique<MenuBarWidget>();
  menu_bar->setGeometry({0, 0, 960, 28});

  auto file = std::make_unique<Menu>();
  file->setTitle(L"File");
  {
    auto n1 = std::make_unique<MenuItem>();
    n1->setText(L"New");
    auto n2 = std::make_unique<MenuItem>();
    n2->setText(L"Open");
    auto n3 = std::make_unique<MenuItem>();
    n3->setText(L"Save");
    file->addChild(std::move(n1));
    file->addChild(std::move(n2));
    file->addChild(std::move(n3));
  }
  menu_bar->addChild(std::move(file));

  auto language = std::make_unique<Menu>();
  language->setTitle(L"Language");
  {
    auto n1 = std::make_unique<MenuItem>();
    n1->setText(L"中文");
    auto n2 = std::make_unique<MenuItem>();
    n2->setText(L"English");
    language->addChild(std::move(n1));
    language->addChild(std::move(n2));
  }
  menu_bar->addChild(std::move(language));

  auto theme = std::make_unique<Menu>();
  theme->setTitle(L"Theme");
  {
    auto n1 = std::make_unique<MenuItem>();
    n1->setText(L"Follow system");
    auto n2 = std::make_unique<MenuItem>();
    n2->setText(L"Light");
    auto n3 = std::make_unique<MenuItem>();
    n3->setText(L"Dark");
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
    help->addChild(std::move(n1));
    help->addChild(std::move(n2));
  }
  menu_bar->addChild(std::move(help));

  RelayoutMenuBarChildren(menu_bar.get(), 960, 28);

  auto tb = std::make_unique<ToolBarWidget>();
  tb->setGeometry({0, 28, 960, 36});
  for (const wchar_t* cap : {L"New", L"Open", L"Save"}) {
    auto btn = std::make_unique<ToolButton>();
    btn->setText(cap);
    tb->addChild(std::move(btn));
  }
  {
    auto sep = std::make_unique<Label>();
    sep->setText(L"|");
    tb->addChild(std::move(sep));
  }
  {
    auto btn = std::make_unique<ToolButton>();
    btn->setText(L"Tools");
    tb->addChild(std::move(btn));
  }
  RelayoutToolBarChildren(tb.get(), 960, 36);
  root->addChild(std::move(tb));
  root->addChild(std::move(menu_bar));

  constexpr int y0 = 64;
  constexpr int cw = 560;
  constexpr int ch = 520;
  constexpr int left_w = 200;
  constexpr int right_w = 200;
  constexpr int mid_x = left_w;

  auto dock_left = std::make_unique<DockArea>();
  dock_left->setDockEdge(DockEdge::kLeft);
  dock_left->setGeometry({0, y0, left_w, ch});
  AddDockLeftStrip(dock_left.get(), left_w, ch);
  root->addChild(std::move(dock_left));

  const int rx = mid_x + cw;
  const int rh1 = 173;
  const int rh2 = 173;
  const int rh3 = 174;

  auto dr1 = std::make_unique<DockArea>();
  dr1->setDockEdge(DockEdge::kRight);
  dr1->setGeometry({rx, y0, right_w, rh1});
  AddDockRightStrip(dr1.get(), right_w, rh1, true);
  root->addChild(std::move(dr1));

  auto dr2 = std::make_unique<DockArea>();
  dr2->setDockEdge(DockEdge::kRight);
  dr2->setGeometry({rx, y0 + rh1, right_w, rh2});
  AddDockRightStrip(dr2.get(), right_w, rh2, false);
  root->addChild(std::move(dr2));

  auto dr3 = std::make_unique<DockArea>();
  dr3->setDockEdge(DockEdge::kRight);
  dr3->setGeometry({rx, y0 + rh1 + rh2, right_w, rh3});
  AddDockRightStrip(dr3.get(), right_w, rh3, false);
  root->addChild(std::move(dr3));

  constexpr int top_h = 48;
  auto dock_top = std::make_unique<DockArea>();
  dock_top->setDockEdge(DockEdge::kTop);
  dock_top->setGeometry({mid_x, y0, cw, top_h});
  AddDockTopStrip(dock_top.get(), cw, top_h);
  root->addChild(std::move(dock_top));

  constexpr int bot_h = 48;
  const int canvas_y = y0 + top_h;
  const int canvas_h = ch - top_h - bot_h;

  auto center = std::make_unique<MapCanvas2D>();
  center->setGeometry({mid_x, canvas_y, cw, canvas_h});
  center->addChild(std::make_unique<ShortcutHelpPanel>());
  center->addChild(std::make_unique<LayerVisibilityPanel>());
  center->addChild(std::make_unique<MapZoomBar>());
  MapCanvas2D* map_for_app = center.get();
  root->addChild(std::move(center));
  App::instance().setDemoMapCanvas(map_for_app);

  auto dock_bottom = std::make_unique<DockArea>();
  dock_bottom->setDockEdge(DockEdge::kBottom);
  dock_bottom->setGeometry({mid_x, y0 + ch - bot_h, cw, bot_h});
  AddDockBottomStrip(dock_bottom.get(), cw, bot_h);
  root->addChild(std::move(dock_bottom));

  auto status = std::make_unique<StatusBarWidget>();
  status->setGeometry({0, 616, 960, kStatusH});
  root->addChild(std::move(status));

  return root;
}

}  // namespace agis::ui
