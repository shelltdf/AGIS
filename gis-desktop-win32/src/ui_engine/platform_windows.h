#pragma once

#include "ui_engine/platform_gui.h"

#if defined(_WIN32)
#include <windows.h>
#endif

namespace agis::ui {

/** Win32 USER32 消息循环（`GetMessage` / `DispatchMessage`）。 */
class PlatformWindows : public IGuiPlatform {
 public:
  ~PlatformWindows() override = default;

  int runEventLoop(App& app) override;
  void requestExit() override;
  const char* backendId() const override;
};

#if defined(_WIN32)
/**
 * ui_engine 演示完整 Win32 壳：GDI+、演示窗口、Widget 树与消息循环均在本实现及 `PlatformWindowsUiEngineDemo` 内。
 * 等价于 `App app; app.setPlatform(...); return app.exec();`（见 `platform_windows.cpp`）。
 */
int RunUiEngineDemoWin32(HINSTANCE hInstance, int nShowCmd);
#endif

}  // namespace agis::ui
