/**
 * ui_engine 演示（可移植）：完整 Widget 树构建与 App 状态摘要字符串。
 * 不包含任何操作系统 API；窗口、消息循环与 GDI+ 由平台层实现（见 ui_engine_demo_win32.cpp）。
 */

#include "app/ui_engine_demo.h"

#include "ui_engine/app.h"

#include <string>

namespace agis::ui {

namespace {

int CountWidgetsRecursive(const Widget* w) {
  int n = 1;
  for (const auto& c : w->children()) {
    n += CountWidgetsRecursive(c.get());
  }
  return n;
}

void AppendAsciiToWide(std::wstring* out, const char* ascii) {
  if (!out || !ascii) {
    return;
  }
  for (const char* p = ascii; *p; ++p) {
    *out += static_cast<wchar_t>(static_cast<unsigned char>(*p));
  }
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

  auto dockLeft = std::make_unique<DockArea>();
  dockLeft->setDockEdge(DockEdge::kLeft);
  dockLeft->setGeometry({0, 64, 220, 520});
  {
    auto strip = std::make_unique<DockButtonStrip>();
    strip->setGeometry({0, 0, 32, 520});
    dockLeft->addChild(std::move(strip));
    auto view = std::make_unique<DockView>();
    view->setGeometry({32, 0, 188, 520});
    auto layer = std::make_unique<LayerDockPanel>();
    layer->setGeometry({0, 0, 188, 260});
    view->addChild(std::move(layer));
    dockLeft->addChild(std::move(view));
  }
  root->addChild(std::move(dockLeft));

  auto center = std::make_unique<MapCanvas2D>();
  center->setGeometry({220, 64, 520, 520});
  {
    auto sh = std::make_unique<ShortcutHelpPanel>();
    sh->setGeometry({8, 8, 200, 120});
    center->addChild(std::move(sh));
    auto vis = std::make_unique<LayerVisibilityPanel>();
    vis->setGeometry({312, 8, 200, 140});
    center->addChild(std::move(vis));
    auto zoom = std::make_unique<MapZoomBar>();
    zoom->setGeometry({8, 472, 400, 40});
    center->addChild(std::move(zoom));
    auto hint = std::make_unique<MapHintOverlay>();
    hint->setGeometry({200, 480, 300, 32});
    center->addChild(std::move(hint));
    auto centerHint = std::make_unique<MapCenterHintOverlay>();
    centerHint->setGeometry({160, 200, 200, 80});
    center->addChild(std::move(centerHint));
  }
  root->addChild(std::move(center));

  auto dockRight = std::make_unique<DockArea>();
  dockRight->setDockEdge(DockEdge::kRight);
  dockRight->setGeometry({740, 64, 220, 520});
  {
    auto strip = std::make_unique<DockButtonStrip>();
    strip->setGeometry({188, 0, 32, 520});
    dockRight->addChild(std::move(strip));
    auto view = std::make_unique<DockView>();
    view->setGeometry({0, 0, 188, 520});
    auto props = std::make_unique<PropsDockPanel>();
    props->setGeometry({0, 0, 188, 520});
    view->addChild(std::move(props));
    dockRight->addChild(std::move(view));
  }
  root->addChild(std::move(dockRight));

  auto status = std::make_unique<StatusBarWidget>();
  status->setGeometry({0, 584, 960, 56});
  root->addChild(std::move(status));

  auto frame = std::make_unique<Frame>();
  frame->setGeometry({300, 200, 360, 200});
  {
    auto lab = std::make_unique<Label>();
    lab->setText(L"Label");
    lab->setGeometry({8, 8, 120, 24});
    frame->addChild(std::move(lab));
    auto btn = std::make_unique<PushButton>();
    btn->setText(L"PushButton");
    btn->setGeometry({8, 40, 100, 28});
    frame->addChild(std::move(btn));
    auto edit = std::make_unique<LineEdit>();
    edit->setText(L"LineEdit");
    edit->setGeometry({8, 76, 200, 24});
    frame->addChild(std::move(edit));
    auto split = std::make_unique<Splitter>();
    split->setOrientation(SplitterOrientation::kHorizontal);
    split->setGeometry({8, 108, 340, 8});
    frame->addChild(std::move(split));
    auto inner = std::make_unique<Label>();
    inner->setText(L"Scroll content");
    inner->setGeometry({0, 0, 400, 40});
    auto scroll = std::make_unique<ScrollArea>();
    scroll->setGeometry({8, 124, 200, 60});
    scroll->setContentWidget(std::move(inner));
    frame->addChild(std::move(scroll));
  }
  root->addChild(std::move(frame));

  auto dlg = std::make_unique<LayerDriverDialog>();
  dlg->setGeometry({100, 100, 400, 300});
  root->addChild(std::move(dlg));

  auto logWin = std::make_unique<LogWindow>();
  logWin->setGeometry({500, 100, 320, 240});
  {
    auto logPanel = std::make_unique<LogPanel>();
    logPanel->setGeometry({8, 8, 300, 200});
    logWin->addChild(std::move(logPanel));
  }
  root->addChild(std::move(logWin));

  auto ctxLayer = std::make_unique<LayerListContextMenu>();
  ctxLayer->setGeometry({0, 0, 1, 1});
  root->addChild(std::move(ctxLayer));
  auto ctxMap = std::make_unique<MapContextMenu>();
  ctxMap->setGeometry({0, 0, 1, 1});
  root->addChild(std::move(ctxMap));

  return root;
}

int CountWidgetsInTree(const Widget* root) {
  if (!root) {
    return 0;
  }
  return CountWidgetsRecursive(root);
}

std::wstring FormatUiEngineDemoStatusLine(const App& app, const Widget* root) {
  const int n = CountWidgetsInTree(root);
  std::wstring s = L"Widget tree nodes: ";
  s += std::to_wstring(n);
  s += L"  |  App + IGuiPlatform(\"";
  const char* bid = "null";
  if (app.platform()) {
    bid = app.platform()->backendId();
  }
  AppendAsciiToWide(&s, bid);
  s += L"\")  |  GDI+ paints above";
  return s;
}

}  // namespace agis::ui
