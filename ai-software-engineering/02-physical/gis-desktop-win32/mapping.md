# gis-desktop-win32：模型元素 → 源码映射

| 元素 | 路径/符号 |
|------|-----------|
| 程序入口 | `gis-desktop-win32/src/app/main.cpp` → `wWinMain` |
| 主窗口过程 | `gis-desktop-win32/src/app/main.cpp` → `MainProc` |
| 图层子窗口过程 | `gis-desktop-win32/src/app/main.cpp` → `LayerPaneProc` |
| 地图宿主 / GDAL+GDI | `gis-desktop-win32/src/map_engine/map_engine.h` / `map_engine.cpp` → **`MapEngine::Instance()`**、`MapHostProc`、`MapDocument`（引擎持有）、`agis_detail::RasterMapLayer` / `VectorMapLayer` |
| 投影 / 拾取 | `gis-desktop-win32/src/map_engine/map_projection.cpp` / `map_projection.h` |
| GPU 呈现 | `gis-desktop-win32/src/map_engine/map_gpu.cpp` / `map_gpu.h` |
| GDI+ UI | `ui_engine/include/ui_engine/gdiplus_ui.h` / `ui_engine/src/gdiplus_ui.cpp` |
| ui_engine 独立演示可执行文件 `ui_engine_demo` | `ui_engine/app/ui_engine_demo.h`、`ui_engine/app/ui_engine_demo.cpp`、`ui_engine/app/ui_engine_demo_main.cpp`；由 `gis-desktop-win32/CMakeLists.txt` 的 `add_executable(ui_engine_demo …)` 引用；便捷脚本 `ui_engine/run_ui_demo.py`（调用 `gis-desktop-win32/build.py` 后构建并运行） |
| 抽象 GUI（App / Widget / 平台接口） | `ui_engine/include/ui_engine/` 下 `app.h`、`widget*.h` 等；`ui_engine/src/` 下对应 `.cpp`；**`widgets_mainframe.*`**（AGIS 主框架 Widget）、`gis-desktop-win32/src/app/ui_private.h`（主程序强绑定 Widget）、`widgets_all.h`（聚合）（类图见 [uml-class-ui.md](uml-class-ui.md)） |
| 日志缓冲 | `gis-desktop-win32/src/core/app_log.cpp` |
| 日志窗口过程 | `gis-desktop-win32/src/app/main.cpp` → `LogWndProc` |
| 菜单资源 ID | `gis-desktop-win32/src/app/resource.h` 与代码中 `#define` 一致 |
| CMake 目标 | `gis-desktop-win32/CMakeLists.txt` → `add_executable(agis_desktop ...)` |

> 随重构拆分文件时更新本表。
