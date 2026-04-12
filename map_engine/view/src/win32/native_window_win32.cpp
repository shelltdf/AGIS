#include "native_window_win32.h"

NativeWindowWin32::NativeWindowWin32(HWND hwnd) : hwnd_(hwnd) {}

void NativeWindowWin32::pumpMessages(MessageQueue& queue) {
  (void)queue;
}

int NativeWindowWin32::widthPixels() const {
  if (!hwnd_ || !IsWindow(hwnd_)) {
    return 0;
  }
  RECT rc{};
  GetClientRect(hwnd_, &rc);
  return static_cast<int>(rc.right - rc.left);
}

int NativeWindowWin32::heightPixels() const {
  if (!hwnd_ || !IsWindow(hwnd_)) {
    return 0;
  }
  RECT rc{};
  GetClientRect(hwnd_, &rc);
  return static_cast<int>(rc.bottom - rc.top);
}
