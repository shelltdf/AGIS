# gis-desktop-win32：模型元素 → 源码映射

| 元素 | 路径/符号 |
|------|-----------|
| 程序入口 | `gis-desktop-win32/src/app/main.cpp` → `wWinMain` |
| 主窗口过程 | `gis-desktop-win32/src/app/main.cpp` → `MainProc` |
| 图层子窗口过程 | `gis-desktop-win32/src/app/main.cpp` → `LayerPaneProc` |
| 地图宿主 / GDAL+GDI | `map_engine/shell/include/map_engine/map_engine.h`、`map_engine/shell/src/map_engine.cpp` → **`MapEngine::Instance()`**、`MapHostProc`、`MapDocument`（引擎持有）、`agis_detail::RasterMapLayer` / `VectorMapLayer`；CMake 目标 **`agis_map_engine`**（`map_engine/CMakeLists.txt`） |
| 投影 / 拾取 | `map_engine/projection/src/map_projection.cpp` / `map_engine/projection/include/map_engine/map_projection.h` |
| GPU 呈现 | `map_engine/gpu/src/map_gpu*.cpp` / `map_engine/gpu/include/map_engine/map_gpu.h` |
| GDI+ UI | `ui_engine/include/ui_engine/gdiplus_ui.h` / `ui_engine/src/gdiplus_ui.cpp` |
| ui_engine 独立演示可执行文件 `ui_engine_demo` | `ui_engine/app/ui_engine_demo.h`、`ui_engine/app/ui_engine_demo.cpp`、`ui_engine/app/ui_engine_demo_main.cpp`；由 `gis-desktop-win32/CMakeLists.txt` 的 `add_executable(ui_engine_demo …)` 引用；便捷脚本 `ui_engine/run_ui_demo.py`（仅配置若需 + `--target ui_engine_demo`，不整库 `build.py`） |
| 抽象 GUI（App / Widget / 平台接口） | `ui_engine/include/ui_engine/` 下 `app.h`、`widget*.h` 等；`ui_engine/src/` 下对应 `.cpp`；**`widgets_mainframe.*`**（AGIS 主框架 Widget）、`gis-desktop-win32/src/app/ui_private.h`（主程序强绑定 Widget）、`widgets_all.h`（聚合）（类图见 [uml-class-ui.md](uml-class-ui.md)） |
| 日志缓冲 | `gis-desktop-win32/src/core/app_log.cpp` |
| 日志窗口过程 | `gis-desktop-win32/src/app/main.cpp` → `LogWndProc` |
| `.gis` 工程 XML 辅助 | `gis-desktop-win32/src/app/gis_document/main_gis_xml.cpp/.h` → `XmlEscape`、`GetXmlAttr`、`Parse*Attr` |
| `.gis` 读写 / 菜单动作 | `gis-desktop-win32/src/app/gis_document/main_gis_document.cpp` → `LoadGisXmlFrom`、`SaveGisXmlTo`、`GisNew`、`GisOpen`、`GisSave`、`GisSaveAs`、`CurrentWindowTitle`、`RefreshUiAfterDocumentReload` 等（声明见 `main_app.h`） |
| 数据转换窗口过程 | `gis-desktop-win32/src/app/main_convert.cpp` → `ConvertWndProc` |
| 转换后端调度 | `gis-desktop-win32/src/app/main_convert.cpp` → `RunConvertBackendAsync`、`BuildConvertCommandLine` |
| 模型 / 3D Tiles 预览窗口 | `gis-desktop-win32/src/app/preview/main_model_preview.cpp` → `ModelPreviewWndProc`、`OpenModelPreviewWindow` |
| 瓦片栅格本地预览（仅本机路径；无网络拉流） | `gis-desktop-win32/src/app/preview/tile_preview/tile_raster_preview.cpp` → `TilePreviewWndProc`、`OpenTileRasterPreviewWindow`；`tile_preview/tile_preview_protocol_picker.*` |
| 模型预览类型 / 抽稀步长 | `gis-desktop-win32/src/app/preview/model_preview_types.h` → `ObjPreviewModel`、`ModelPreviewFaceStride` |
| 模型预览 bgfx 路径（CMake 固定 `AGIS_USE_BGFX=ON`） | `gis-desktop-win32/src/app/preview/model_preview_bgfx.cpp/.h` → `agis_bgfx_preview_init`、`agis_bgfx_preview_draw`、`agis_bgfx_preview_shutdown` |
| 3D Tiles → `ObjPreviewModel`（tinygltf） | `gis-desktop-win32/src/app/preview/tiles_gltf_loader.cpp` → `AgisLoad3DTilesForPreview` |
| 模型预览旧版 OpenGL（`#if !AGIS_USE_BGFX`，正式构建不启用） | `gis-desktop-win32/src/app/preview/main_model_preview.cpp` → `InitPreviewGl`、`DrawModelPreviewOpenGL` |
| 模型预览旧版 DX11（同上） | `gis-desktop-win32/src/app/preview/main_model_preview.cpp` → `InitPreviewDx`、`DrawModelPreviewDx11` |
| OBJ/MTL 解析 | `gis-desktop-win32/src/app/preview/main_model_preview.cpp` → `ParseObjModel`、`ParseMtlMaterials` |
| 转换后端公共库 | `gis-desktop-win32/src/tools/convert_backend_common.cpp/.h` → `PrintConvertCliHelpGrouped`、`PrintConvertCliIoSection`、`ConvertGisToModel` 等 |
| KTX2 / Basis 编码（可选 `AGIS_HAVE_BASISU`） | `gis-desktop-win32/src/tools/ktx2_basis_encode.cpp/.h`、`cmake/AGISBundledBasisUniversal.cmake`、`3rdparty/basis_universal`（ZIP 安置，见 `3rdparty/README-BASIS-KTX2.md`） |
| 七个转换命令行工具入口 | `gis-desktop-win32/src/tools/agis_convert_*.cpp` |
| 菜单资源 ID | `gis-desktop-win32/src/app/resource.h` 与代码中 `#define` 一致 |
| CMake 目标 | `gis-desktop-win32/CMakeLists.txt` → `add_executable(agis_desktop …)`、`add_subdirectory(../map_engine)` → **`agis_map_engine`** |

> 随重构拆分文件时更新本表。
