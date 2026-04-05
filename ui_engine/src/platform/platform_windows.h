#pragma once

#include "ui_engine/export.h"
#include "ui_engine/app_launch_params.h"
#include "ui_engine/platform_gui.h"

namespace agis::ui {

class PlatformWindows;

/**
 * Win32 USER32 消息循环（`GetMessage` / `DispatchMessage`）。
 * 演示壳构造 `PlatformWindows(AppLaunchParams)` 仅在定义 `AGIS_BUILD_UI_ENGINE_DEMO` 时于 `platform_windows.cpp` 实现；
 * 进程入口见 `ui_engine/app/ui_engine_demo_main.cpp`（在 TU 内使用系统入口类型，再填入 `AppLaunchParams`）。
 */
class AGIS_UI_API PlatformWindows : public IGuiPlatform {
 public:
  PlatformWindows();
  explicit PlatformWindows(const AppLaunchParams& launch);
  ~PlatformWindows() override;

  bool ok() const override;

  int runEventLoop(App& app) override;
  void requestExit() override;
  const char* backendId() const override;

  /** 基础消息循环为 `Basic`；带演示 HWND 的壳为 `UiEngineDemo`。 */
  enum class Mode { Basic, UiEngineDemo };

 private:
  Mode mode_{Mode::Basic};
  /** 语义同 `HINSTANCE`，存为 `void*` 以免公共头依赖 `<windows.h>`。 */
  void* native_instance_{nullptr};
  /** 语义同演示主窗口 `HWND`。 */
  void* main_window_{nullptr};
  bool class_registered_{false};
};

}  // namespace agis::ui
