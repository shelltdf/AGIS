#include "ui_engine/platform_windows.h"

#include "ui_engine/app.h"

#include <windows.h>

namespace agis::ui {

int PlatformWindows::runEventLoop(App& app) {
  (void)app;
  MSG msg{};
  while (GetMessageW(&msg, nullptr, 0, 0) > 0) {
    TranslateMessage(&msg);
    DispatchMessageW(&msg);
  }
  return static_cast<int>(msg.wParam);
}

void PlatformWindows::requestExit() { PostQuitMessage(0); }

const char* PlatformWindows::backendId() const { return "win32"; }

}  // namespace agis::ui
