#pragma once

#include "ui_engine/platform_gui.h"

#include <memory>

namespace agis::ui {

/**
 * 应用程序对象（类似 QApplication）：**进程内单例**，持有 GUI 后端，统一 `exec()` / `quit`。
 * 使用 `App::instance()` 取得引用；构造私有化，禁止复制。
 *
 * **事件循环**：`exec()` 调用 `IGuiPlatform::runEventLoop`；构造时按目标操作系统宏安装默认 `IGuiPlatform`；未知平台则保持未设置，`exec()` 再回退到空后端。
 *
 * 可在首次 `exec()` 前调用 `setPlatform` 覆盖默认实现（例如 Windows 演示壳需传入 `HINSTANCE`）。
 */
class App {
 public:
  static App& instance();

  App(const App&) = delete;
  App& operator=(const App&) = delete;

  /** 覆盖默认平台后端（取得所有权）。 */
  void setPlatform(std::unique_ptr<IGuiPlatform> platform);

  IGuiPlatform* platform() const { return platform_.get(); }

  /** 进入主循环：委托 `IGuiPlatform::runEventLoop`（内部即各 OS 的循环）；无后端时安装 null 后端。 */
  int exec();

  void requestQuit();

  bool quitRequested() const { return quitRequested_; }

 private:
  App();

  std::unique_ptr<IGuiPlatform> platform_;
  bool quitRequested_{false};
};

}  // namespace agis::ui
