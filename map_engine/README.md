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
| 图层与驱动 | `layer/` | `map_layer*.h`、`map_engine_internal.h` | `map_layer*.cpp` |
| 地图文档模型 | `map/` | `map.h` | `map.cpp` |
| 投影 | `projection/` | `map_projection.h` | `map_projection.cpp` |
| 场景图 | `scene_graph/` | `scene_graph.h`、`scene_node.h`、`shape.h`、`material.h`、`geometry.h`、`mesh.h` | `scene_graph.cpp`、`scene_node.cpp` |
| 视口 | `view/` | `map_view.h`、`native_window.h`、`message_queue.h`（`include/map_engine/`）；Win32 宿主/包装头与实现同在 `src/win32/`（`map_host_win32.*`、`native_window_win32.*`） | `map_view.cpp`、`message_queue.cpp`、`native_window.cpp`、`src/win32/*.cpp` |
| 呈现后端 | `render/` | `map_gpu.h`、`renderer.h` | `map_gpu*.cpp`、`renderer.cpp` |
| IO | `io/` | `map_io.h`、`io_types.h`、`io_read_channel.h`、`local_file_io.h`、`remote_file_io.h`、`archive_file_io.h` | `io.cpp` |
| GIS 工程 XML | `io_gis/` | `gis_xml.h`、`gis_project_xml.h` | `gis_xml.cpp`、`gis_project_xml.cpp` |
| 引擎宿主 | `engine/` | `map_engine.h`、`export.h` | `map_engine.cpp` |
| 演示平台（与 `demo/` 并列，不入 ``agis_map_engine``） | `platform/` | `map_engine_demo/platform.h`（`include/map_engine_demo/`） | `win32.cpp` 仅由 ``map_engine_demo`` 编译；include 由该可执行目标 ``PRIVATE`` 引用 |

示例：

```text
map_engine/
  layer/include/map_engine/map_layer.h
  layer/src/map_layer.cpp
  map/include/map_engine/map.h
  map/src/map.cpp
  io_gis/include/map_engine/gis_xml.h
  io_gis/src/gis_xml.cpp
  engine/include/map_engine/map_engine.h
  engine/src/map_engine.cpp
  platform/include/map_engine_demo/platform.h
  platform/src/win32.cpp
  …
```

依赖关系与新增代码约定见各组职责说明（`io/` 相对独立（C++20 文件系统 + 标准库异步占位）；`view/` 依赖 `render`（``Renderer``）与 `scene_graph`（``SceneNode`` 指针）；`render/renderer.cpp` 依赖 `scene_graph`；`scene_graph` 与 `layer` 解耦；`engine/` 依赖 `map` / render / layer / projection；`map` 依赖 layer、projection 等）。
