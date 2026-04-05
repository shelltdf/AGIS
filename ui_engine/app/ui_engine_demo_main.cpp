/**
 * ui_engine 演示 — 进程入口：Windows 为 `wWinMain`，其它平台为 `main`。
 *
 * 约定：`App` 在 `exec()` 前须至少注册一个根 Widget；此处注册**空的** `MainFrame`（无子控件）。
 * Windows 下将入口信息填入 `AppLaunchParams` 后 `CreateGuiPlatform(launch)`；**可移植类型**见 `ui_engine/app_launch_params.h`。
 */

#if defined(_WIN32)

#include <windows.h>

#include <cstdlib>
#include <memory>

#include "ui_engine/app.h"
#include "ui_engine/app_launch_params.h"
#include "ui_engine/platform_gui.h"
#include "ui_engine/widgets_all.h"
#include "ui_engine_demo.h"

int APIENTRY wWinMain(HINSTANCE hInst, HINSTANCE, LPWSTR, int show) {
  agis::ui::App& app = agis::ui::App::instance();
  app.addRootWidget(BuildUiEngineDemoWidgetTree());

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

#include "ui_engine/app.h"
#include "ui_engine/app_launch_params.h"
#include "ui_engine/widgets_all.h"
#include "ui_engine_demo.h"

#include <memory>

int main(int argc, char** argv) {
  [[maybe_unused]] const agis::ui::AppLaunchParams launch = agis::ui::make_launch_params(argc, argv);
  agis::ui::App& app = agis::ui::App::instance();
  app.addRootWidget(agis::ui::BuildUiEngineDemoWidgetTree());
  return app.exec();
}

#elif defined(__linux__) || defined(__unix__)

#include "ui_engine/app.h"
#include "ui_engine/app_launch_params.h"
#include "ui_engine/widgets_all.h"
#include "ui_engine_demo.h"

#include <memory>

int main(int argc, char** argv) {
  [[maybe_unused]] const agis::ui::AppLaunchParams launch = agis::ui::make_launch_params(argc, argv);
  agis::ui::App& app = agis::ui::App::instance();
  app.addRootWidget(agis::ui::BuildUiEngineDemoWidgetTree());
  return app.exec();
}

#else
#error "ui_engine_demo: unsupported platform"
#endif
