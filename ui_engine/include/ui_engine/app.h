#pragma once

#include "ui_engine/platform_gui.h"
#include "ui_engine/widget.h"

#include <functional>
#include <memory>
#include <string>
#include <type_traits>
#include <vector>

namespace agis::ui {

/**
 * 应用程序对象（类似 QApplication）：**进程内单例**，持有 GUI 后端，统一 `exec()` / `quit`。
 * 使用 `App::instance()` 取得引用；构造私有化，禁止复制。
 *
 * **根 Widget**：可注册多个顶层/根 `Widget`（如 `MainFrame`）；平台实现应为**每个**根创建**一个**顶级原生窗口，**全部关闭**后主循环结束。`exec()` 时若**尚未注册任何根 Widget**，返回 `kExitNoRootWidgets`（错误退出）。
 *
 * **事件循环**：`exec()` 调用 `IGuiPlatform::runEventLoop`；构造时通过 `CreateGuiPlatform()` 按平台安装默认 `IGuiPlatform`；未知平台则保持未设置，`exec()` 再回退到空后端。
 *
 * 可在首次 `exec()` 前调用 `setPlatform` 覆盖默认实现；Windows 演示壳请通过 `CreateGuiPlatform(launch)` 传入 `AppLaunchParams`（含 `native_app_instance`）。
 */
class AGIS_UI_API App {
 public:
  static App& instance();

  /** `exec()` 在无任何根 Widget 时返回（约定为错误退出）。 */
  static constexpr int kExitNoRootWidgets = 1;
  /** 平台未就绪（如 `IGuiPlatform::ok()==false`）时进程入口可选用。 */
  static constexpr int kExitPlatformNotReady = 2;

  App(const App&) = delete;
  App& operator=(const App&) = delete;

  /** 注册一个根/顶层 Widget（如 `MainFrame`）；可多次调用以挂载多棵顶层树。 */
  template <typename T, typename = std::enable_if_t<std::is_base_of_v<Widget, T>>>
  void addRootWidget(std::unique_ptr<T> root) {
    if (!root) {
      return;
    }
    rootWidgets_.push_back(std::move(root));
  }

  /** 清空已注册的根 Widget（例如顶层 HWND 销毁时由平台层调用）。 */
  void clearRootWidgets();

  /** 第一棵根树指针；无则 `nullptr`。 */
  Widget* primaryRootWidget() { return rootWidgets_.empty() ? nullptr : rootWidgets_.front().get(); }

  const Widget* primaryRootWidget() const {
    return rootWidgets_.empty() ? nullptr : rootWidgets_.front().get();
  }

  const std::vector<std::unique_ptr<Widget>>& rootWidgets() const { return rootWidgets_; }

  /** 覆盖默认平台后端（取得所有权）。 */
  void setPlatform(std::unique_ptr<IGuiPlatform> platform);

  IGuiPlatform* platform() const { return platform_.get(); }

  /** 进入主循环：无根 Widget 时返回 `kExitNoRootWidgets`；否则委托 `IGuiPlatform::runEventLoop`；无后端时安装 null 后端。 */
  int exec();

  void requestQuit();

  bool quitRequested() const { return quitRequested_; }

  /** 演示壳：客户区内上一帧指针位置（Win32 客户区坐标）。 */
  void setPointerClient(int x, int y) {
    pointer_x_ = x;
    pointer_y_ = y;
  }
  int pointerClientX() const { return pointer_x_; }
  int pointerClientY() const { return pointer_y_; }

  /** 当前悬停的最内层 Widget（命中测试叶子）；平台在 `WM_MOUSEMOVE` 等中更新。 */
  void setHoverWidget(Widget* w) { hover_widget_ = w; }
  Widget* hoverWidget() const { return hover_widget_; }

  /** 状态栏等可读的简短提示（如鼠标按下反馈）。 */
  void setStatusHint(std::wstring s) { status_hint_ = std::move(s); }
  const std::wstring& statusHint() const { return status_hint_; }

  /** 顶层窗口客户区尺寸变化时由平台调用；演示程序可注册以重排 `MainFrame` 子控件。 */
  using ClientResizeHandler = std::function<void(Widget* root, int client_w, int client_h)>;
  void setClientResizeHandler(ClientResizeHandler h) { client_resize_handler_ = std::move(h); }
  void notifyClientResize(Widget* root, int client_w, int client_h) {
    if (client_resize_handler_) {
      client_resize_handler_(root, client_w, client_h);
    }
  }

 private:
  App();

  std::unique_ptr<IGuiPlatform> platform_;
  std::vector<std::unique_ptr<Widget>> rootWidgets_;
  bool quitRequested_{false};

  int pointer_x_{-1};
  int pointer_y_{-1};
  Widget* hover_widget_{nullptr};
  std::wstring status_hint_;
  ClientResizeHandler client_resize_handler_;
};

}  // namespace agis::ui
