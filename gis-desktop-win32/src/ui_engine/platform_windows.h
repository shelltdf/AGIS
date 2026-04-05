#pragma once

#include "ui_engine/app_launch_params.h"
#include "ui_engine/platform_gui.h"

namespace agis::ui {

class PlatformWindows;

namespace detail {
/** 释放演示壳资源；在 `platform_windows.cpp` 中于 `AGIS_BUILD_UI_ENGINE_DEMO` 构建下为完整实现，否则为空操作。 */
void PlatformWindowsReleaseDemoResources(PlatformWindows* p);
}  // namespace detail

/**
 * Win32 USER32 消息循环（`GetMessage` / `DispatchMessage`）。
 * 演示壳构造 `PlatformWindows(AppLaunchParams)` 仅在定义 `AGIS_BUILD_UI_ENGINE_DEMO` 时于 `platform_windows.cpp` 实现；
 * 进程入口见 `ui_engine_demo_main.cpp`（在 TU 内使用系统入口类型，再填入 `AppLaunchParams`）。
 */
class PlatformWindows : public IGuiPlatform {
 public:
  PlatformWindows();
#if defined(_WIN32)
  explicit PlatformWindows(const AppLaunchParams& launch);
#endif
  ~PlatformWindows() override;

#if defined(_WIN32)
  bool ok() const override;
#endif

  int runEventLoop(App& app) override;
  void requestExit() override;
  const char* backendId() const override;

#if defined(_WIN32)
  /** 基础消息循环为 `Basic`；带演示 HWND 的壳为 `UiEngineDemo`。 */
  enum class Mode { Basic, UiEngineDemo };
#endif

 private:
#if defined(_WIN32)
  friend void detail::PlatformWindowsReleaseDemoResources(PlatformWindows*);
  Mode mode_{Mode::Basic};
  /** 语义同 `HINSTANCE`，存为 `void*` 以免公共头依赖 `<windows.h>`。 */
  void* native_instance_{nullptr};
  /** 语义同演示主窗口 `HWND`。 */
  void* main_window_{nullptr};
  bool class_registered_{false};
#endif
};

}  // namespace agis::ui
