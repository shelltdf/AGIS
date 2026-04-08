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
| ui_engine 独立演示可执行文件 `ui_engine_demo` | `ui_engine/app/ui_engine_demo.h`、`ui_engine/app/ui_engine_demo.cpp`、`ui_engine/app/ui_engine_demo_main.cpp`；由 `gis-desktop-win32/CMakeLists.txt` 的 `add_executable(ui_engine_demo …)` 引用；便捷脚本 `ui_engine/run_ui_demo.py`（仅配置若需 + `--target ui_engine_demo`，不整库 `build.py`） |
| 抽象 GUI（App / Widget / 平台接口） | `ui_engine/include/ui_engine/` 下 `app.h`、`widget*.h` 等；`ui_engine/src/` 下对应 `.cpp`；**`widgets_mainframe.*`**（AGIS 主框架 Widget）、`gis-desktop-win32/src/app/ui_private.h`（主程序强绑定 Widget）、`widgets_all.h`（聚合）（类图见 [uml-class-ui.md](uml-class-ui.md)） |
| 日志缓冲 | `gis-desktop-win32/src/core/app_log.cpp` |
| 日志窗口过程 | `gis-desktop-win32/src/app/main.cpp` → `LogWndProc` |
| 数据转换窗口过程 | `gis-desktop-win32/src/app/main.cpp` → `ConvertWndProc` |
| 转换后端调度 | `gis-desktop-win32/src/app/main.cpp` → `RunConvertBackend`、`BuildConvertCommandLine` |
| 模型预览窗口过程 | `gis-desktop-win32/src/app/main.cpp` → `ModelPreviewWndProc` |
| 模型预览类型 / 抽稀步长 | `gis-desktop-win32/src/app/model_preview_types.h` → `ObjPreviewModel`、`ModelPreviewFaceStride` |
| 模型预览 bgfx 路径（CMake 固定 `AGIS_USE_BGFX=ON`） | `gis-desktop-win32/src/app/model_preview_bgfx.cpp/.h` → `agis_bgfx_preview_init`、`agis_bgfx_preview_draw`、`agis_bgfx_preview_shutdown` |
| 模型预览旧版 OpenGL（`main.cpp` 内 `#if !AGIS_USE_BGFX`，正式构建不启用） | `gis-desktop-win32/src/app/main.cpp` → `InitPreviewGl`、`DrawModelPreviewOpenGL` |
| 模型预览旧版 DX11（同上） | `gis-desktop-win32/src/app/main.cpp` → `InitPreviewDx`、`DrawModelPreviewDx11` |
| OBJ/MTL 解析 | `gis-desktop-win32/src/app/main.cpp` → `ParseObjModel`、`ParseMtlMaterials` |
| 转换后端公共库 | `gis-desktop-win32/src/tools/convert_backend_common.cpp/.h` |
| 六个转换命令行工具入口 | `gis-desktop-win32/src/tools/agis_convert_*.cpp` |
| 菜单资源 ID | `gis-desktop-win32/src/app/resource.h` 与代码中 `#define` 一致 |
| CMake 目标 | `gis-desktop-win32/CMakeLists.txt` → `add_executable(agis_desktop ...)` |

> 随重构拆分文件时更新本表。
