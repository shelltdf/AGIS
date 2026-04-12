#pragma once

/**
 * `agis_map_engine` 静态库 / 动态库切换用的导入导出宏。
 *
 * - **静态库**：目标 `agis_map_engine` 通过 `PUBLIC` 定义 `AGIS_MAP_ENGINE_STATIC=1`，`AGIS_MAP_ENGINE_API` 为空。
 * - **动态库（Windows）**：编译库时定义 `AGIS_MAP_ENGINE_EXPORTS` → `__declspec(dllexport)`；使用者仅链接导入库，
 *   不包含 `AGIS_MAP_ENGINE_EXPORTS` → `__declspec(dllimport)`。
 * - **动态库（Unix）**：编译库时定义 `AGIS_MAP_ENGINE_EXPORTS`，符号默认可见（便于 `-fvisibility=hidden` 场景）。
 */

#if defined(AGIS_MAP_ENGINE_STATIC)
#  define AGIS_MAP_ENGINE_API
#elif defined(_WIN32)
#  if defined(AGIS_MAP_ENGINE_EXPORTS)
#    define AGIS_MAP_ENGINE_API __declspec(dllexport)
#  else
#    define AGIS_MAP_ENGINE_API __declspec(dllimport)
#  endif
#else
#  if defined(AGIS_MAP_ENGINE_EXPORTS)
#    define AGIS_MAP_ENGINE_API __attribute__((visibility("default")))
#  else
#    define AGIS_MAP_ENGINE_API __attribute__((visibility("default")))
#  endif
#endif
