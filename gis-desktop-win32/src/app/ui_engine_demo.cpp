/**
 * ui_engine 功能演示：Win32 窗口 + GDI+ 绘制 API、完整 Widget 树（通用 + AGIS 壳层类型）、
 * App::exec + 自定义 IGuiPlatform 消息循环。
 */
#include <windows.h>

#include <memory>
#include <string>

#include "ui_engine/app.h"
#include "ui_engine/gdiplus_ui.h"
#include "ui_engine/platform_gui.h"
#include "ui_engine/widgets_all.h"

namespace {

constexpr wchar_t kClassName[] = L"AGIS_UIEngineDemo";
constexpr wchar_t kTitle[] = L"ui_engine demo";

std::unique_ptr<agis::ui::MainFrame> g_root;

int CountWidgets(const agis::ui::Widget* w) {
  int n = 1;
  for (const auto& c : w->children()) {
    n += CountWidgets(c.get());
  }
  return n;
}

void BuildDemoWidgetTree() {
  auto root = std::make_unique<agis::ui::MainFrame>();
  root->setTitle(L"demo");
  root->setGeometry({0, 0, 960, 640});

  auto menu = std::make_unique<agis::ui::MenuBarWidget>();
  menu->setGeometry({0, 0, 960, 28});
  root->addChild(std::move(menu));

  auto tb = std::make_unique<agis::ui::ToolBarWidget>();
  tb->setGeometry({0, 28, 960, 36});
  root->addChild(std::move(tb));

  auto dockLeft = std::make_unique<agis::ui::DockArea>();
  dockLeft->setDockEdge(agis::ui::DockEdge::kLeft);
  dockLeft->setGeometry({0, 64, 220, 520});
  {
    auto strip = std::make_unique<agis::ui::DockButtonStrip>();
    strip->setGeometry({0, 0, 32, 520});
    dockLeft->addChild(std::move(strip));
    auto view = std::make_unique<agis::ui::DockView>();
    view->setGeometry({32, 0, 188, 520});
    auto layer = std::make_unique<agis::ui::LayerDockPanel>();
    layer->setGeometry({0, 0, 188, 260});
    view->addChild(std::move(layer));
    dockLeft->addChild(std::move(view));
  }
  root->addChild(std::move(dockLeft));

  auto center = std::make_unique<agis::ui::MapCanvas2D>();
  center->setGeometry({220, 64, 520, 520});
  {
    auto sh = std::make_unique<agis::ui::ShortcutHelpPanel>();
    sh->setGeometry({8, 8, 200, 120});
    center->addChild(std::move(sh));
    auto vis = std::make_unique<agis::ui::LayerVisibilityPanel>();
    vis->setGeometry({312, 8, 200, 140});
    center->addChild(std::move(vis));
    auto zoom = std::make_unique<agis::ui::MapZoomBar>();
    zoom->setGeometry({8, 472, 400, 40});
    center->addChild(std::move(zoom));
    auto hint = std::make_unique<agis::ui::MapHintOverlay>();
    hint->setGeometry({200, 480, 300, 32});
    center->addChild(std::move(hint));
    auto centerHint = std::make_unique<agis::ui::MapCenterHintOverlay>();
    centerHint->setGeometry({160, 200, 200, 80});
    center->addChild(std::move(centerHint));
  }
  root->addChild(std::move(center));

  auto dockRight = std::make_unique<agis::ui::DockArea>();
  dockRight->setDockEdge(agis::ui::DockEdge::kRight);
  dockRight->setGeometry({740, 64, 220, 520});
  {
    auto strip = std::make_unique<agis::ui::DockButtonStrip>();
    strip->setGeometry({188, 0, 32, 520});
    dockRight->addChild(std::move(strip));
    auto view = std::make_unique<agis::ui::DockView>();
    view->setGeometry({0, 0, 188, 520});
    auto props = std::make_unique<agis::ui::PropsDockPanel>();
    props->setGeometry({0, 0, 188, 520});
    view->addChild(std::move(props));
    dockRight->addChild(std::move(view));
  }
  root->addChild(std::move(dockRight));

  auto status = std::make_unique<agis::ui::StatusBarWidget>();
  status->setGeometry({0, 584, 960, 56});
  root->addChild(std::move(status));

  // 通用 widgets.h：Frame / Label / PushButton / LineEdit / ScrollArea / Splitter / Dialog / Popup
  auto frame = std::make_unique<agis::ui::Frame>();
  frame->setGeometry({300, 200, 360, 200});
  {
    auto lab = std::make_unique<agis::ui::Label>();
    lab->setText(L"Label");
    lab->setGeometry({8, 8, 120, 24});
    frame->addChild(std::move(lab));
    auto btn = std::make_unique<agis::ui::PushButton>();
    btn->setText(L"PushButton");
    btn->setGeometry({8, 40, 100, 28});
    frame->addChild(std::move(btn));
    auto edit = std::make_unique<agis::ui::LineEdit>();
    edit->setText(L"LineEdit");
    edit->setGeometry({8, 76, 200, 24});
    frame->addChild(std::move(edit));
    auto split = std::make_unique<agis::ui::Splitter>();
    split->setOrientation(agis::ui::SplitterOrientation::kHorizontal);
    split->setGeometry({8, 108, 340, 8});
    frame->addChild(std::move(split));
    auto inner = std::make_unique<agis::ui::Label>();
    inner->setText(L"Scroll content");
    inner->setGeometry({0, 0, 400, 40});
    auto scroll = std::make_unique<agis::ui::ScrollArea>();
    scroll->setGeometry({8, 124, 200, 60});
    scroll->setContentWidget(std::move(inner));
    frame->addChild(std::move(scroll));
  }
  root->addChild(std::move(frame));

  auto dlg = std::make_unique<agis::ui::LayerDriverDialog>();
  dlg->setGeometry({100, 100, 400, 300});
  root->addChild(std::move(dlg));

  auto logWin = std::make_unique<agis::ui::LogWindow>();
  logWin->setGeometry({500, 100, 320, 240});
  {
    auto logPanel = std::make_unique<agis::ui::LogPanel>();
    logPanel->setGeometry({8, 8, 300, 200});
    logWin->addChild(std::move(logPanel));
  }
  root->addChild(std::move(logWin));

  auto ctxLayer = std::make_unique<agis::ui::LayerListContextMenu>();
  ctxLayer->setGeometry({0, 0, 1, 1});
  root->addChild(std::move(ctxLayer));
  auto ctxMap = std::make_unique<agis::ui::MapContextMenu>();
  ctxMap->setGeometry({0, 0, 1, 1});
  root->addChild(std::move(ctxMap));

  g_root = std::move(root);
}

class Win32DemoPlatform final : public agis::ui::IGuiPlatform {
 public:
  int runEventLoop(agis::ui::App& app) override {
    (void)app;
    MSG msg{};
    while (GetMessageW(&msg, nullptr, 0, 0) > 0) {
      TranslateMessage(&msg);
      DispatchMessageW(&msg);
    }
    return static_cast<int>(msg.wParam);
  }

  void requestExit() override { PostQuitMessage(0); }

  const char* backendId() const override { return "win32-demo"; }
};

void DemoPaintClient(HDC hdc, const RECT& client) {
  const int w = client.right - client.left;
  const int h = client.bottom - client.top;
  HBRUSH bg = CreateSolidBrush(RGB(240, 242, 245));
  FillRect(hdc, &client, bg);
  DeleteObject(bg);

  RECT rcLayer{client.left + 8, client.top + 8, client.left + 8 + 200, client.top + 8 + 180};
  UiPaintLayerPanel(hdc, rcLayer);

  RECT rcProps{client.right - 280, client.top + 8, client.right - 8, client.top + 200};
  RECT driverCard{rcProps.left + 12, rcProps.top + 40, rcProps.right - 12, rcProps.top + 100};
  RECT sourceCard{rcProps.left + 12, rcProps.top + 108, rcProps.right - 12, rcProps.top + 180};
  UiPaintLayerPropsDockFrame(hdc, rcProps, &driverCard, &sourceCard, L"ui_engine_demo");

  RECT rcCenter{client.left + w / 4, client.top + h / 4, client.right - w / 4, client.bottom - h / 4};
  UiPaintMapCenterHint(hdc, rcCenter, L"MapCenterHint / GDI+");

  RECT rcSmall{client.left + 230, client.top + 200, client.left + 430, client.top + 320};
  UiPaintLayerPropsPanel(hdc, rcSmall, L"PropsPanel", L"body line");

  RECT rcHint{client.right - 320, client.bottom - 48, client.right - 12, client.bottom - 12};
  UiPaintMapHintOverlay(hdc, rcHint, L"UiPaintMapHintOverlay — ESC 退出");

  SetBkMode(hdc, TRANSPARENT);
  SetTextColor(hdc, RGB(20, 24, 32));
  wchar_t line[256]{};
  const int n = g_root ? CountWidgets(g_root.get()) : 0;
  wsprintfW(line, L"Widget tree nodes: %d  |  App + IGuiPlatform(\"%hs\")  |  GDI+ paints above",
            n, agis::ui::App::instance().platform()
                   ? agis::ui::App::instance().platform()->backendId()
                   : "null");
  RECT rcText{client.left + 12, client.bottom - 72, client.right - 12, client.bottom - 36};
  DrawTextW(hdc, line, -1, &rcText, DT_LEFT | DT_WORDBREAK);
}

LRESULT CALLBACK WndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) {
  switch (msg) {
    case WM_DESTROY:
      g_root.reset();
      PostQuitMessage(0);
      return 0;
    case WM_KEYDOWN:
      if (wParam == VK_ESCAPE) {
        agis::ui::App::instance().requestQuit();
      }
      return 0;
    case WM_PAINT: {
      PAINTSTRUCT ps{};
      HDC hdc = BeginPaint(hwnd, &ps);
      RECT client{};
      GetClientRect(hwnd, &client);
      DemoPaintClient(hdc, client);
      EndPaint(hwnd, &ps);
      return 0;
    }
    default:
      return DefWindowProcW(hwnd, msg, wParam, lParam);
  }
}

}  // namespace

int APIENTRY wWinMain(HINSTANCE hInst, HINSTANCE, LPWSTR, int show) {
  UiGdiplusInit();
  BuildDemoWidgetTree();

  WNDCLASSEXW wc{};
  wc.cbSize = sizeof(wc);
  wc.style = CS_HREDRAW | CS_VREDRAW;
  wc.lpfnWndProc = WndProc;
  wc.hInstance = hInst;
  wc.hCursor = LoadCursor(nullptr, IDC_ARROW);
  wc.hbrBackground = reinterpret_cast<HBRUSH>(COLOR_WINDOW + 1);
  wc.lpszClassName = kClassName;
  RegisterClassExW(&wc);

  HWND hwnd = CreateWindowExW(0, kClassName, kTitle, WS_OVERLAPPEDWINDOW, CW_USEDEFAULT, CW_USEDEFAULT,
                              1000, 720, nullptr, nullptr, hInst, nullptr);
  if (!hwnd) {
    UiGdiplusShutdown();
    return 1;
  }
  agis::ui::App::instance().setPlatform(std::make_unique<Win32DemoPlatform>());
  ShowWindow(hwnd, show);
  UpdateWindow(hwnd);

  const int code = agis::ui::App::instance().exec();
  UiGdiplusShutdown();
  return code;
}
