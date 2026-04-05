#pragma once

/**
 * `agis_ui_engine` 静态库 / 动态库切换用的导入导出宏。
 *
 * - **静态库**：目标 `agis_ui_engine` 通过 `INTERFACE`/`PUBLIC` 定义 `AGIS_UI_STATIC`，`AGIS_UI_API` 为空。
 * - **动态库（Windows）**：编译库时定义 `AGIS_UI_EXPORTS` → `__declspec(dllexport)`；使用者仅链接导入库，
 *   不包含 `AGIS_UI_EXPORTS` → `__declspec(dllimport)`。
 * - **动态库（Unix）**：编译库时定义 `AGIS_UI_EXPORTS`，符号默认可见（便于 `-fvisibility=hidden` 场景）。
 */

#if defined(AGIS_UI_STATIC)
#  define AGIS_UI_API
#elif defined(_WIN32)
#  if defined(AGIS_UI_EXPORTS)
#    define AGIS_UI_API __declspec(dllexport)
#  else
#    define AGIS_UI_API __declspec(dllimport)
#  endif
#else
#  if defined(AGIS_UI_EXPORTS)
#    define AGIS_UI_API __attribute__((visibility("default")))
#  else
#    define AGIS_UI_API __attribute__((visibility("default")))
#  endif
#endif
