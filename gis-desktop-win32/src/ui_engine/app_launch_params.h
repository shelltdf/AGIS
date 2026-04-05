#pragma once

namespace agis::ui {

/**
 * 各操作系统进程入口向 `App` / 平台后端传递的**可移植**启动参数。
 * 原生句柄仅以 `void*` 承载语义（如 Win32 的 `HINSTANCE`），公共头不依赖 `<windows.h>` 等系统头。
 */
struct AppLaunchParams {
  int argc{0};
  char** argv{nullptr};

  /** 平台「应用实例」：Win32 下为模块实例句柄，与 `void*` 互转。 */
  void* native_app_instance{nullptr};

  /**
   * 首窗口显示方式提示：Win32 下对应 `nShowCmd`（如 `SW_SHOWNORMAL`）；
   * 其它平台可忽略。
   */
  int show_window_command{0};
};

/** 便捷构造（各入口填充后交给 `PlatformWindows(launch)` 等）。 */
inline AppLaunchParams make_launch_params(int argc, char** argv, void* native_app_instance = nullptr,
                                          int show_window_command = 0) {
  AppLaunchParams p;
  p.argc = argc;
  p.argv = argv;
  p.native_app_instance = native_app_instance;
  p.show_window_command = show_window_command;
  return p;
}

}  // namespace agis::ui
