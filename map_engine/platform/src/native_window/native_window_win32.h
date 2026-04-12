#pragma once

#include "map_host_win32.h"
#include "map_engine/native_window.h"

#include <windows.h>

class RenderDeviceContext;

/** Win32：用 ``HWND`` 实现 ``NativeWindow``；客户区尺寸随 ``GetClientRect``。 */
class NativeWindowWin32 : public NativeWindow {
 public:
  explicit NativeWindowWin32(HWND hwnd);

  /**
   * 演示宿主若仍使用外层 ``GetMessage`` 主循环，此处可不抽消息（避免与主循环重复分发）。
   * 队列驱动架构下可改为 ``PeekMessage`` 并写入 ``queue``。
   */
  void pumpMessages(MessageQueue& queue) override;

  int widthPixels() const override;
  int heightPixels() const override;

  RenderDeviceContext* renderDeviceContext() override { return renderDeviceContext_; }
  const RenderDeviceContext* renderDeviceContext() const override { return renderDeviceContext_; }

  /** 将引擎持有的 ``RenderDeviceContext`` 关联到本窗口（不取得所有权）。 */
  void attachRenderDeviceContext(RenderDeviceContext* ctx) { renderDeviceContext_ = ctx; }

  HWND hwnd() const { return hwnd_; }

 private:
  HWND hwnd_{};
  RenderDeviceContext* renderDeviceContext_{nullptr};
};

/** 将 Win32 窗口句柄转为引擎通用的 ``WinID``（供 ``CreateNativeWindow`` 使用）。 */
inline WinID WinIDFromHwnd(HWND hwnd) noexcept {
  return WinID{reinterpret_cast<void*>(hwnd)};
}
