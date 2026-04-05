#pragma once

#include "ui_engine/platform_gui.h"

#include <memory>

namespace agis::ui {

/**
 * 应用程序对象（类似 QApplication）：可栈上构造 `App app;`，持有 GUI 后端，统一 `exec()` / `quit`。
 * **事件循环**：`exec()` 调用 `IGuiPlatform::runEventLoop(*this)`，由各平台实现封装操作系统消息循环
 *（如 Win32 `GetMessage`、macOS `NSApplication run` 等）；未设置平台时使用空后端并立即返回。
 *
 * 具体窗口与控件树由 Widget 子类构成；主程序也可仍用原生 WinMain 自管消息泵，与本类并存。
 */
class App {
 public:
  App() = default;
  App(const App&) = delete;
  App& operator=(const App&) = delete;

  /** 安装平台后端；须在 exec 前调用（或由集成层在启动时注入）。 */
  void setPlatform(std::unique_ptr<IGuiPlatform> platform);

  IGuiPlatform* platform() const { return platform_.get(); }

  /** 进入主循环：委托 `IGuiPlatform::runEventLoop`（内部即各 OS 的循环）；无后端时安装 null 后端。 */
  int exec();

  void requestQuit();

  bool quitRequested() const { return quitRequested_; }

 private:
  std::unique_ptr<IGuiPlatform> platform_;
  bool quitRequested_{false};
};

}  // namespace agis::ui
