#pragma once

#include "ui_engine/export.h"
#include "ui_engine/platform_gui.h"

#include <memory>

namespace agis::ui {

/**
 * XCB 事件循环占位实现：`xcb_connect` 后轮询 `xcb_poll_for_event`。
 * 完整产品需创建窗口并处理事件以配合 `requestExit`。
 */
class AGIS_UI_API PlatformXcb final : public IGuiPlatform {
 public:
  PlatformXcb();
  ~PlatformXcb() override;

  PlatformXcb(const PlatformXcb&) = delete;
  PlatformXcb& operator=(const PlatformXcb&) = delete;

  int runEventLoop(App& app) override;
  void requestExit() override;
  const char* backendId() const override;

 private:
  class Impl;
  std::unique_ptr<Impl> impl_;
};

}  // namespace agis::ui
