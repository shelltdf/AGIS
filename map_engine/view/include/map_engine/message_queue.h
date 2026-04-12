#pragma once

#include <cstdint>
#include <deque>

/** 与具体窗口系统解耦后的统一消息，由 ``NativeWindow::pumpMessages`` 压入队列供上层处理。 */
struct WindowMessage {
  enum class Kind : std::uint8_t {
    Paint,
    Resize,
    PointerMove,
    PointerDown,
    PointerUp,
    Key,
    Close,
    Custom,
  };
  Kind kind{Kind::Custom};
  std::int32_t param0{0};
  std::int32_t param1{0};
  std::uint64_t extra{0};
};

/** 跨平台消息队列：Win32/X11/Cocoa 等将系统事件归一化后写入此处。 */
class MessageQueue {
 public:
  void push(WindowMessage m);
  bool pop(WindowMessage* out);
  bool empty() const;
  void clear();

 private:
  std::deque<WindowMessage> messages_;
};
