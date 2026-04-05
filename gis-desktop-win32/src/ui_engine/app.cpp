#include "ui_engine/app.h"

#include <memory>
#include <utility>

#if defined(_WIN32)
#include "ui_engine/platform_windows.h"
#elif defined(__APPLE__)
#include "ui_engine/platform_cocoa.h"
#elif defined(__linux__) || defined(__unix__)
#if defined(AGIS_UI_USE_XCB)
#include "ui_engine/platform_xcb.h"
#else
#include "ui_engine/platform_xlib.h"
#endif
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

App::App() {
#if defined(_WIN32)
  platform_ = std::make_unique<PlatformWindows>();
#elif defined(__APPLE__)
  platform_ = std::make_unique<PlatformCocoa>();
#elif defined(__linux__) || defined(__unix__)
#if defined(AGIS_UI_USE_XCB)
  platform_ = std::make_unique<PlatformXcb>();
#else
  platform_ = std::make_unique<PlatformXlib>();
#endif
#endif
}

void App::setPlatform(std::unique_ptr<IGuiPlatform> platform) {
  platform_ = std::move(platform);
}

void App::clearRootWidgets() { rootWidgets_.clear(); }

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

}  // namespace agis::ui
