#pragma once

#include "ui_engine/export.h"
#include "ui_engine/platform_gui.h"

#include <memory>

namespace agis::ui {

/**
 * X11 Xlib 事件循环占位实现：持有 `Display*`，在 `runEventLoop` 中轮询 `XPending` / `XNextEvent`。
 * 完整产品需创建窗口并处理 `ClientMessage` 等以配合 `requestExit`。
 */
class AGIS_UI_API PlatformXlib final : public IGuiPlatform {
 public:
  PlatformXlib();
  ~PlatformXlib() override;

  PlatformXlib(const PlatformXlib&) = delete;
  PlatformXlib& operator=(const PlatformXlib&) = delete;

  int runEventLoop(App& app) override;
  void requestExit() override;
  const char* backendId() const override;

 private:
  class Impl;
  std::unique_ptr<Impl> impl_;
};

}  // namespace agis::ui
