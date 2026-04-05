#include "ui_engine/app.h"

#include <memory>
#include <utility>

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
