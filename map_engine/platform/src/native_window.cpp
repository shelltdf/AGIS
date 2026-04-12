#include "map_engine/native_window.h"

#if defined(_WIN32)
#  include "native_window_win32.h"
#endif

std::unique_ptr<NativeWindow> CreateNativeWindow(WinID id) {
#if defined(_WIN32)
  if (!id.native) {
    return nullptr;
  }
  return std::make_unique<NativeWindowWin32>(reinterpret_cast<HWND>(id.native));
#else
  (void)id;
  return nullptr;
#endif
}
