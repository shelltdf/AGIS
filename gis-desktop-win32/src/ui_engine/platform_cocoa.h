#pragma once

#include "ui_engine/platform_gui.h"

namespace agis::ui {

/**
 * macOS AppKit：`NSApplication` 主循环（在 `platform_cocoa.mm` 中实现）。
 */
class PlatformCocoa final : public IGuiPlatform {
 public:
  PlatformCocoa();
  ~PlatformCocoa() override;

  PlatformCocoa(const PlatformCocoa&) = delete;
  PlatformCocoa& operator=(const PlatformCocoa&) = delete;

  int runEventLoop(App& app) override;
  void requestExit() override;
  const char* backendId() const override;
};

}  // namespace agis::ui
