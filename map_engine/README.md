# agis_map_engine

仓库根目录下的 **2D 地图引擎**静态库：`MapEngine`、`MapDocument`、`MapHostProc`、`map_gpu_*`、`map_projection` 等。

- **公开头文件**：`include/map_engine/*.h`（包含：`#include "map_engine/map_engine.h"`）
- **构建**：由 [`gis-desktop-win32/CMakeLists.txt`](../gis-desktop-win32/CMakeLists.txt) 中 `add_subdirectory("${CMAKE_CURRENT_SOURCE_DIR}/../map_engine" …)` 引入；CMake 目标名 **`agis_map_engine`**。

仍依赖桌面工程中的私有头（`resource.h`、`app_log`、`common/*` 等），通过 `map_engine/CMakeLists.txt` 中的 `../gis-desktop-win32/src` 注入；与父工程共用 **GDAL / PROJ / bgfx / ui_engine** 配置。
