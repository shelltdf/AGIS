#pragma once

namespace agis::ui {

class App;

/**
 * 跨平台 GUI 后端抽象：事件循环、原生表面创建与消息分发由各实现提供。
 * 典型实现：Win32（USER32/GDI）、Linux（XCB / Xlib + 可选渲染）、macOS（Cocoa/AppKit）。
 */
class IGuiPlatform {
 public:
  virtual ~IGuiPlatform() = default;

  /** 进入并运行主循环，直到 quit 或窗口全部关闭（语义接近 Qt QApplication::exec）。 */
  virtual int runEventLoop(App& app) = 0;

  /** 请求结束主循环（可从任意线程由后端映射到平台 quit）。 */
  virtual void requestExit() = 0;

  /** 稳定标识，如 "win32" / "xcb" / "xlib" / "cocoa" / "null"。 */
  virtual const char* backendId() const = 0;
};

}  // namespace agis::ui
