# gis-desktop-win32：模型元素 → 源码映射

| 元素 | 路径/符号 |
|------|-----------|
| 程序入口 | `gis-desktop-win32/src/main.cpp` → `wWinMain` |
| 主窗口过程 | `gis-desktop-win32/src/main.cpp` → `MainProc` |
| 图层子窗口过程 | `gis-desktop-win32/src/main.cpp` → `LayerPaneProc` |
| 地图宿主 / GDAL+GDI | `gis-desktop-win32/src/map_engine.cpp` → `MapHostProc`、`MapDocument`、`agis_detail::RasterMapLayer` / `VectorMapLayer` |
| 日志窗口过程 | `gis-desktop-win32/src/main.cpp` → `LogWndProc` |
| 菜单资源 ID | `gis-desktop-win32/src/resource.h`（若存在）或与代码中 `#define` 一致 |
| CMake 目标 | `gis-desktop-win32/CMakeLists.txt` → `add_executable(agis_desktop ...)` |

> 随重构拆分文件时更新本表。
