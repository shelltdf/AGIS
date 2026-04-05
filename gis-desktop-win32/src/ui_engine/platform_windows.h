#pragma once

#include "ui_engine/platform_gui.h"

namespace agis::ui {

/** Win32 USER32 消息循环（`GetMessage` / `DispatchMessage`）。 */
class PlatformWindows final : public IGuiPlatform {
 public:
  int runEventLoop(App& app) override;
  void requestExit() override;
  const char* backendId() const override;
};

}  // namespace agis::ui
