#pragma once

#include "ui_engine/app_launch_params.h"
#include "ui_engine/export.h"

#include <memory>

namespace agis::ui {

class App;

/**
 * 跨平台 GUI 后端抽象：事件循环、原生表面创建与消息分发由各实现提供。
 * 典型实现：Win32（USER32/GDI）、Linux（XCB / Xlib + 可选渲染）、macOS（Cocoa/AppKit）。
 */
class AGIS_UI_API IGuiPlatform {
 public:
  virtual ~IGuiPlatform() = default;

  /** 进入并运行主循环，直到 quit 或窗口全部关闭（语义接近 Qt QApplication::exec）。 */
  virtual int runEventLoop(App& app) = 0;

  /** 请求结束主循环（可从任意线程由后端映射到平台 quit）。 */
  virtual void requestExit() = 0;

  /** 稳定标识，如 "win32" / "xcb" / "xlib" / "cocoa" / "null"。 */
  virtual const char* backendId() const = 0;

  /** 平台是否已就绪（窗口/表面创建成功等）；默认 true。 */
  virtual bool ok() const { return true; }
};

/**
 * 按当前编译目标**固定**选择一种 `IGuiPlatform` 实现（Windows / macOS / Linux XCB|Xlib），无需注册表或插件。
 * - Windows：`launch.native_app_instance != nullptr` 时使用带演示壳的构造（与 `wWinMain` 传入的 `HINSTANCE` 等一致）；否则为基础消息循环实现。
 * - 其它平台：忽略 `launch` 中与该平台无关的字段。
 */
AGIS_UI_API std::unique_ptr<IGuiPlatform> CreateGuiPlatform(const AppLaunchParams& launch = {});

}  // namespace agis::ui
