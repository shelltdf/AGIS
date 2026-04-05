#pragma once

#include "ui_engine/platform_gui.h"

#if defined(_WIN32)
#include <windows.h>
#endif

namespace agis::ui {

class PlatformWindows;

namespace detail {
/** 释放演示壳资源；在 `platform_windows.cpp` 中于 `AGIS_BUILD_UI_ENGINE_DEMO` 构建下为完整实现，否则为空操作。 */
void PlatformWindowsReleaseDemoResources(PlatformWindows* p);
}  // namespace detail

/**
 * Win32 USER32 消息循环（`GetMessage` / `DispatchMessage`）。
 * `PlatformWindows(HINSTANCE, …)` 仅在定义 `AGIS_BUILD_UI_ENGINE_DEMO` 时于 `platform_windows.cpp` 编译；进程入口 `wWinMain` 在 `ui_engine_demo_main.cpp`。
 */
class PlatformWindows : public IGuiPlatform {
 public:
  PlatformWindows();
#if defined(_WIN32)
  PlatformWindows(HINSTANCE hinst, int nShowCmd);
#endif
  ~PlatformWindows() override;

#if defined(_WIN32)
  bool ok() const;
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
  HINSTANCE hinst_{nullptr};
  HWND hwnd_{nullptr};
  bool class_registered_{false};
#endif
};

}  // namespace agis::ui
