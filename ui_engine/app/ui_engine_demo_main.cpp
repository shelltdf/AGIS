/**
 * ui_engine 演示 — 进程入口：Windows 为 `wWinMain`，其它平台为 `main`。
 *
 * 约定：`App` 在 `exec()` 前须至少注册一个根 Widget（如 `BuildUiEngineDemoWidgetTree()` 的 `MainFrame`），
 * 否则 `exec()` 返回 `App::kExitNoRootWidgets`。各平台先把入口信息填入 `AppLaunchParams`，再交给
 * `PlatformWindows(launch)`（Windows 演示壳）；**可移植类型**见 `ui_engine/app_launch_params.h`。
 */

#if defined(_WIN32)

#include <windows.h>

#include <cstdlib>
#include <memory>

#include "ui_engine_demo.h"
#include "ui_engine/app.h"
#include "ui_engine/app_launch_params.h"
#include "ui_engine/platform_gui.h"

int APIENTRY wWinMain(HINSTANCE hInst, HINSTANCE, LPWSTR, int show) {
  agis::ui::App& app = agis::ui::App::instance();
  app.addRootWidget(agis::ui::BuildUiEngineDemoWidgetTree());

  const agis::ui::AppLaunchParams launch =
      agis::ui::make_launch_params(__argc, __argv, reinterpret_cast<void*>(hInst), show);
  app.setPlatform(agis::ui::CreateGuiPlatform(launch));

  agis::ui::IGuiPlatform* plat = app.platform();
  if (plat && !plat->ok()) {
    return agis::ui::App::kExitPlatformNotReady;
  }
  return app.exec();
}

#elif defined(__APPLE__)

#include "ui_engine_demo.h"
#include "ui_engine/app.h"
#include "ui_engine/app_launch_params.h"

int main(int argc, char** argv) {
  [[maybe_unused]] const agis::ui::AppLaunchParams launch = agis::ui::make_launch_params(argc, argv);
  agis::ui::App& app = agis::ui::App::instance();
  app.addRootWidget(agis::ui::BuildUiEngineDemoWidgetTree());
  return app.exec();
}

#elif defined(__linux__) || defined(__unix__)

#include "ui_engine_demo.h"
#include "ui_engine/app.h"
#include "ui_engine/app_launch_params.h"

int main(int argc, char** argv) {
  [[maybe_unused]] const agis::ui::AppLaunchParams launch = agis::ui::make_launch_params(argc, argv);
  agis::ui::App& app = agis::ui::App::instance();
  app.addRootWidget(agis::ui::BuildUiEngineDemoWidgetTree());
  return app.exec();
}

#else
#error "ui_engine_demo: unsupported platform"
#endif
