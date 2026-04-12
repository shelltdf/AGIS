#pragma once

#include "map_engine/export.h"
#include "map_engine/message_queue.h"

#include <memory>

/**
 * 跨平台窗口引用：封装当前操作系统下的原生窗口句柄或指针，避免在公共头中直接依赖平台类型。
 *
 * - **Win32**：值为 ``reinterpret_cast<void*>(HWND)``，见 ``native_window_win32.h``（与 ``map_host_win32.*`` 同在 ``view/src/win32``）中的 ``WinIDFromHwnd``。
 * - **其它平台**：预留为同类不透明指针；未绑定时为 ``nullptr``。
 */
struct WinID {
  void* native = nullptr;
};

/**
 * 本地窗口抽象：从各平台句柄（Win32 ``HWND``、X11 ``Window``、macOS ``NSView``/``Window`` 等）
 * 封装像素尺寸、呈现表面与系统消息泵。消息经 ``pumpMessages`` 写入 ``MessageQueue``，供引擎统一调度。
 */
class NativeWindow {
 public:
  virtual ~NativeWindow() = default;

  /** 从系统队列取事件并转换为 ``WindowMessage`` 写入 ``queue``。 */
  virtual void pumpMessages(MessageQueue& queue) = 0;

  virtual int widthPixels() const = 0;
  virtual int heightPixels() const = 0;
};

/**
 * 由 ``WinID`` 构造具体平台的 ``NativeWindow`` 实现（例如 Win32 的 ``NativeWindowWin32``）。
 * 不支持的平台或 ``native == nullptr`` 时返回 ``nullptr``。
 */
AGIS_MAP_ENGINE_API std::unique_ptr<NativeWindow> CreateNativeWindow(WinID id);
