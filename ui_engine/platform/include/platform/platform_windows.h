#pragma once

#include "ui_engine/export.h"
#include "ui_engine/app_launch_params.h"
#include "ui_engine/platform_gui.h"

#include <vector>

namespace agis::ui {

class PlatformWindows;

/**
 * Win32 USER32 消息循环（`GetMessage` / `DispatchMessage`）。
 * `UiEngineDemo`：`runEventLoop` 内按 `App::rootWidgets()` 为每个根创建一个顶级 `HWND`，全部关闭后 `PostQuitMessage`。
 * 带 `AppLaunchParams` 的构造见 `platform_windows.cpp`（`AGIS_BUILD_UI_ENGINE_DEMO`）；入口见 `ui_engine/demo/src/ui_engine_demo_main.cpp`。
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

  /** 由 `DemoWndProc` 在顶级窗口 `WM_DESTROY` 时调用：最后一扇关闭后结束消息循环。 */
  void NotifyRootWindowDestroyed(void* hwnd);

  /** 基础消息循环为 `Basic`；带演示 HWND 的壳为 `UiEngineDemo`。 */
  enum class Mode { Basic, UiEngineDemo };

 private:
  Mode mode_{Mode::Basic};
  /** 语义同 `HINSTANCE`，存为 `void*` 以免公共头依赖 `<windows.h>`。 */
  void* native_instance_{nullptr};
  /** `UiEngineDemo`：每个根 Widget 对应一个顶级 `HWND`（与 `root_windows_` 一一对应）。 */
  std::vector<void*> root_windows_{};
  bool class_registered_{false};
  int show_window_command_{1};
  /** 析构中销毁 `HWND` 时不再 `PostQuitMessage`，避免重入。 */
  bool teardown_without_quit_{false};
};

}  // namespace agis::ui
