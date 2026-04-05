#pragma once

#include "ui/platform_gui.h"

#include <memory>

namespace agis::ui {

/**
 * 应用程序对象（类似 QApplication）：单例入口，持有当前 GUI 后端，负责 exec / quit。
 * 具体窗口与控件树由 Widget 子类构成；本类不替代现有 WinMain 消息泵时可仅作类型占位与设计边界。
 */
class App {
 public:
  static App& instance();

  App(const App&) = delete;
  App& operator=(const App&) = delete;

  /** 安装平台后端；须在 exec 前调用（或由集成层在启动时注入）。 */
  void setPlatform(std::unique_ptr<IGuiPlatform> platform);

  IGuiPlatform* platform() const { return platform_.get(); }

  /** 委托给 IGuiPlatform::runEventLoop；无后端时返回 0。 */
  int exec();

  void requestQuit();

  bool quitRequested() const { return quitRequested_; }

 private:
  App() = default;

  std::unique_ptr<IGuiPlatform> platform_;
  bool quitRequested_{false};
};

}  // namespace agis::ui
