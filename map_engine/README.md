# agis_map_engine

仓库根目录下的 **2D 地图引擎**静态库。

- **对外包含**：`#include "map_engine/map_*.h"`（不变）
- **构建**：[`gis-desktop-win32/CMakeLists.txt`](../gis-desktop-win32/CMakeLists.txt) 中 `add_subdirectory(../map_engine …)`；目标 **`agis_map_engine`**

CMake 将各组下的 `*/include` 全部加入 **PUBLIC**，因此任意 `map_engine/xxx.h` 仍从单一逻辑前缀解析。

共享头与实现来自仓库根目录 `common/`（CMake 目标 `agis_common`，动态库为 `agis_common.dll`），由 `gis-desktop-win32` 的 CMake 引入；`map_engine` 不再向 `gis-desktop-win32/src` 要 include 路径。

---

## 目录：功能组 = include + src

每组 **先按功能分组**，组内再分 **`include/map_engine/`**（公开头）与 **`src/`**（实现）：

| 组 | 路径 | 头文件 | 源文件 |
|----|------|--------|--------|
| 地图与图层 | `map/` | `map.h`、`map_layer*.h`、`map_engine_internal.h`、`map_data_source.h` | `map.cpp`、`map_layer*.cpp`、`map_data_source*.cpp` |
| 投影 | `projection/` | `map_projection.h` | `map_projection.cpp` |
| 场景图 | `scene_graph/` | `scene_graph.h`、`scene_node.h`、`shape.h`、`material.h`、`geometry.h`、`mesh.h` | `scene_graph.cpp`、`scene_node.cpp`（与 ``map`` 内图层解耦） |
| 视口 | `view/` | `map_view.h`（`include/map_engine/`） | `map_view.cpp` |
| 平台抽象 | `platform/` | `message_queue.h`、`native_window.h`、`render_device_context.h`、`platform.h`（`include/map_engine/`）；Win32 宿主与包装在 `src/native_window/`（`map_host_win32.*`、`native_window_win32.*` 等，短文件名 include） | `message_queue.cpp`、`native_window.cpp`、`render_device_context.cpp`、`src/render_device_context/render_device_context_d2d.cpp` / `render_device_context_bgfx.cpp`、`src/native_window/...`；演示 `src/platform/platform_win32.cpp` 仅 ``map_engine_demo`` |
| 呈现（场景渲染抽象） | `render/` | `renderer.h` | `renderer.cpp` |
| IO | `io/` | `map_io.h`、`io_types.h`、`io_read_channel.h`、`local_file_io.h`、`remote_file_io.h`、`archive_file_io.h` | `io.cpp` |
| GIS 工程 XML | `io_gis/` | `gis_xml.h`、`gis_project_xml.h` | `gis_xml.cpp`、`gis_project_xml.cpp` |
| 引擎宿主 | `engine/` | `map_engine.h`、`export.h` | `map_engine.cpp` |

示例：

```text
map_engine/
  map/include/map_engine/map.h
  map/include/map_engine/map_layer.h
  map/include/map_engine/map_data_source.h
  map/src/map.cpp
  map/src/map_layer.cpp
  map/src/map_data_source.cpp
  map/src/map_data_source/map_data_source_gdal.h
  map/src/map_data_source/map_data_source_gdal.cpp
  io_gis/include/map_engine/gis_xml.h
  io_gis/src/gis_xml.cpp
  engine/include/map_engine/map_engine.h
  engine/src/map_engine.cpp
  platform/include/map_engine/message_queue.h
  platform/include/map_engine/native_window.h
  platform/include/map_engine/platform.h
  platform/src/message_queue.cpp
  platform/src/native_window.cpp
  platform/src/native_window/map_host_win32.h
  platform/src/native_window/map_host_win32.cpp
  platform/src/native_window/native_window_win32.cpp
  platform/src/platform/platform_win32.cpp
  …
```

依赖关系与新增代码约定见各组职责说明（`io/` 相对独立（C++20 文件系统 + 标准库异步占位）；`view/` 依赖 `platform`（``NativeWindow`` / ``MessageQueue``）、`render`（``Renderer``）与 `scene_graph`（``SceneNode`` 指针）；`render/renderer.cpp` 依赖 `scene_graph`；`scene_graph` 与 `map` 内图层类型解耦；`engine/` 依赖 `map` / render / projection / platform；`map` 内图层依赖 projection 等）。
