/**
 * ui_engine 演示 — 进程入口：Windows 为 `wWinMain`，其它平台为 `main`。
 */

#if defined(_WIN32)

#include <windows.h>

#include <memory>

#include "ui_engine/app.h"
#include "ui_engine/platform_windows.h"

int APIENTRY wWinMain(HINSTANCE hInst, HINSTANCE, LPWSTR, int show) {
  agis::ui::App& app = agis::ui::App::instance();

  auto platform = std::make_unique<agis::ui::PlatformWindows>(hInst, show);
  if (!platform->ok()) {
    return 1;
  }

  app.setPlatform(std::move(platform));
  return app.exec();
}

#elif defined(__APPLE__)

#include "ui_engine/app.h"

int main(int argc, char** argv) {
  (void)argc;
  (void)argv;
  return agis::ui::App::instance().exec();
}

#elif defined(__linux__) || defined(__unix__)

#include "ui_engine/app.h"

int main(int argc, char** argv) {
  (void)argc;
  (void)argv;
  return agis::ui::App::instance().exec();
}

#else
#error "ui_engine_demo: unsupported platform"
#endif
