/**
 * ui_engine 演示 — 非 Windows 入口（`main`）。
 * Windows：`wWinMain` 与演示壳已并入 `ui_engine/platform_windows.cpp`（定义 `AGIS_BUILD_UI_ENGINE_DEMO` 时编译）。
 */

#if defined(__APPLE__)

#include "ui_engine/app.h"

int main(int argc, char** argv) {
  (void)argc;
  (void)argv;
  agis::ui::App app;
  return app.exec();
}

#elif defined(__linux__) || defined(__unix__)

#include "ui_engine/app.h"

int main(int argc, char** argv) {
  (void)argc;
  (void)argv;
  agis::ui::App app;
  return app.exec();
}

#elif defined(_WIN32)
// 见 platform_windows.cpp
#else
#error "ui_engine_demo: unsupported platform"
#endif
