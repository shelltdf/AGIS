# 物理阶段（02-physical）

按**构建目标**组织的字段级与行为级规格；单一事实来源为各目标 `spec.md`。

| target-id | 产物 | 说明 |
|-----------|------|------|
| [gis-desktop-win32](gis-desktop-win32/README.md) | `AGIS.exe`（可执行文件） | Windows Win32 SDI 主程序：菜单、工具栏、左侧图层区、2D 画布占位、状态栏 |
| [map_engine](../../map_engine/README.md)（源码根 [`map_engine/`](../../map_engine/)） | `agis_map_engine.lib`（静态库） | 2D 地图引擎：`MapEngine`、图层/文档/投影/GPU 呈现；由 `gis-desktop-win32` 的 CMake `add_subdirectory(../map_engine)` 构建 |
