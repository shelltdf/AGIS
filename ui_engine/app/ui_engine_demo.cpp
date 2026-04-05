/**
 * ui_engine 演示（可移植）：`BuildUiEngineDemoWidgetTree`。
 * 不包含任何操作系统 API；窗口与消息循环由平台层实现。
 */

#include "ui_engine_demo.h"

#include <memory>
#include <utility>

namespace agis::ui {

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
  auto panel = std::make_unique<DockPanel>();
  panel->setGeometry({0, 0, vw, h});
  view->addChild(std::move(panel));
  dock->addChild(std::move(view));
}

/** 右侧缘条 + 内容区（条带在右，与 AGIS 右侧 Props 缘条一致）。 */
void AddDockRightStrip(DockArea* dock, int w, int h) {
  const int vw = w - kStrip;
  auto strip = std::make_unique<DockButtonStrip>();
  strip->setGeometry({vw, 0, kStrip, h});
  dock->addChild(std::move(strip));
  auto view = std::make_unique<DockView>();
  view->setGeometry({0, 0, vw, h});
  auto panel = std::make_unique<DockPanel>();
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

}  // namespace

std::unique_ptr<MainFrame> BuildUiEngineDemoWidgetTree() {
  auto root = std::make_unique<MainFrame>();
  root->setTitle(L"demo");
  root->setGeometry({0, 0, 960, 640});

  auto menu = std::make_unique<MenuBarWidget>();
  menu->setGeometry({0, 0, 960, 28});
  root->addChild(std::move(menu));

  auto tb = std::make_unique<ToolBarWidget>();
  tb->setGeometry({0, 28, 960, 36});
  root->addChild(std::move(tb));

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
  AddDockRightStrip(dr1.get(), right_w, rh1);
  root->addChild(std::move(dr1));

  auto dr2 = std::make_unique<DockArea>();
  dr2->setDockEdge(DockEdge::kRight);
  dr2->setGeometry({rx, y0 + rh1, right_w, rh2});
  AddDockRightStrip(dr2.get(), right_w, rh2);
  root->addChild(std::move(dr2));

  auto dr3 = std::make_unique<DockArea>();
  dr3->setDockEdge(DockEdge::kRight);
  dr3->setGeometry({rx, y0 + rh1 + rh2, right_w, rh3});
  AddDockRightStrip(dr3.get(), right_w, rh3);
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
  root->addChild(std::move(center));

  auto dock_bottom = std::make_unique<DockArea>();
  dock_bottom->setDockEdge(DockEdge::kBottom);
  dock_bottom->setGeometry({mid_x, y0 + ch - bot_h, cw, bot_h});
  AddDockBottomStrip(dock_bottom.get(), cw, bot_h);
  root->addChild(std::move(dock_bottom));

  auto status = std::make_unique<StatusBarWidget>();
  status->setGeometry({0, 584, 960, 56});
  root->addChild(std::move(status));

  return root;
}

}  // namespace agis::ui
