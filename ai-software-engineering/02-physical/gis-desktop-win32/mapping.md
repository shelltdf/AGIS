# gis-desktop-win32：模型元素 → 源码映射

| 元素 | 路径/符号 |
|------|-----------|
| 程序入口 | `gis-desktop-win32/src/app/main.cpp` → `wWinMain` |
| 主窗口过程 | `gis-desktop-win32/src/app/main.cpp` → `MainProc` |
| 图层子窗口过程 | `gis-desktop-win32/src/app/main.cpp` → `LayerPaneProc` |
| 地图宿主 / GDAL+GDI | `gis-desktop-win32/src/map_engine/map_engine.h` / `map_engine.cpp` → **`MapEngine::Instance()`**、`MapHostProc`、`MapDocument`（引擎持有）、`agis_detail::RasterMapLayer` / `VectorMapLayer` |
| 投影 / 拾取 | `gis-desktop-win32/src/map_engine/map_projection.cpp` / `map_projection.h` |
| GPU 呈现 | `gis-desktop-win32/src/map_engine/map_gpu.cpp` / `map_gpu.h` |
| GDI+ UI | `gis-desktop-win32/src/ui_engine/gdiplus_ui.h` / `gdiplus_ui.cpp` |
| 抽象 GUI（App / Widget / 平台接口） | `gis-desktop-win32/src/ui_engine/app.*`、`widget.*`、`widgets.*`（通用控件）、**`widgets_shell.*`**（AGIS 专用 Widget）、`widgets_all.h`（含 shell）、`ui_types.h`、`platform_gui.h`（类图见 [uml-class-ui.md](uml-class-ui.md)） |
| 日志缓冲 | `gis-desktop-win32/src/core/app_log.cpp` |
| 日志窗口过程 | `gis-desktop-win32/src/app/main.cpp` → `LogWndProc` |
| 菜单资源 ID | `gis-desktop-win32/src/app/resource.h` 与代码中 `#define` 一致 |
| CMake 目标 | `gis-desktop-win32/CMakeLists.txt` → `add_executable(agis_desktop ...)` |

> 随重构拆分文件时更新本表。
