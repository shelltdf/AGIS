#include "ui_engine/app.h"
#include "ui_engine/widgets_mainframe.h"

#include <memory>
#include <utility>

#if defined(_WIN32)
#include <windows.h>
#endif

namespace agis::ui {

namespace {

class NullGuiPlatform final : public IGuiPlatform {
 public:
  int runEventLoop(App& app) override {
    (void)app;
    return 0;
  }

  void requestExit() override {}

  const char* backendId() const override { return "null"; }
};

}  // namespace

App& App::instance() {
  static App inst;
  return inst;
}

App::App() { platform_ = CreateGuiPlatform(); }

void App::setPlatform(std::unique_ptr<IGuiPlatform> platform) {
  platform_ = std::move(platform);
}

void App::clearRootWidgets() {
  rootWidgets_.clear();
  open_drop_down_menu_ = nullptr;
  middle_drag_canvas_ = nullptr;
  demo_map_canvas_ = nullptr;
  demo_host_hwnd_ = nullptr;
  invalidate_all_ = {};
}

int App::exec() {
  if (rootWidgets_.empty()) {
    return kExitNoRootWidgets;
  }
  if (!platform_) {
    platform_ = std::make_unique<NullGuiPlatform>();
  }
  quitRequested_ = false;
  return platform_->runEventLoop(*this);
}

void App::requestQuit() {
  quitRequested_ = true;
  if (platform_) {
    platform_->requestExit();
  }
}

void App::setOpenDropDownMenu(Menu* m) {
  if (open_drop_down_menu_ == m) {
    return;
  }
  Menu* prev = open_drop_down_menu_;
  open_drop_down_menu_ = nullptr;
  if (prev) {
    prev->closeDropDownVisual();
  }
  open_drop_down_menu_ = m;
  if (m) {
    m->openDropDownVisual();
  }
}

#if defined(_WIN32)
namespace {

bool WindowsAppsUseLightTheme() {
  DWORD v = 1;
  DWORD cb = sizeof(v);
  if (RegGetValueW(HKEY_CURRENT_USER,
                   L"Software\\Microsoft\\Windows\\CurrentVersion\\Themes\\Personalize", L"AppsUseLightTheme",
                   RRF_RT_REG_DWORD, nullptr, &v, &cb) != ERROR_SUCCESS) {
    return true;
  }
  return v != 0;
}

UiTheme EffectiveTheme(UiTheme t) {
  if (t == UiTheme::kFollowSystem) {
    return WindowsAppsUseLightTheme() ? UiTheme::kLight : UiTheme::kDark;
  }
  return t;
}

}  // namespace

void App::setUiTheme(UiTheme t) {
  ui_theme_ = t;
  notifyDemoShellInvalidated();
  invalidateAll();
}

void App::setUiLanguage(UiLanguage l) {
  ui_language_ = l;
  notifyDemoShellInvalidated();
  invalidateAll();
}

COLORREF App::themeColorSurface() const {
  const UiTheme e = EffectiveTheme(ui_theme_);
  return e == UiTheme::kDark ? RGB(38, 40, 46) : RGB(250, 251, 253);
}

COLORREF App::themeColorSurfaceAlt() const {
  const UiTheme e = EffectiveTheme(ui_theme_);
  return e == UiTheme::kDark ? RGB(48, 50, 58) : RGB(255, 255, 255);
}

COLORREF App::themeColorText() const {
  const UiTheme e = EffectiveTheme(ui_theme_);
  return e == UiTheme::kDark ? RGB(230, 232, 238) : RGB(28, 32, 40);
}

COLORREF App::themeColorMuted() const {
  const UiTheme e = EffectiveTheme(ui_theme_);
  return e == UiTheme::kDark ? RGB(150, 155, 170) : RGB(95, 102, 118);
}

COLORREF App::themeColorAccent() const {
  return RGB(88, 120, 220);
}

#else

void App::setUiTheme(UiTheme) {}
void App::setUiLanguage(UiLanguage) {}

#endif

}  // namespace agis::ui
