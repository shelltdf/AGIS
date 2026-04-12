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
| 文档模型 | `document/` | `map.h` | `map.cpp` |
| 投影 | `projection/` | `map_projection.h` | `map_projection.cpp` |
| 场景图 | `scene_graph/` | `scene_graph.h` | `scene_graph.cpp` |
| 视口 | `view/` | `map_viewport.h` | `map_viewport.cpp` |
| 呈现后端 | `render/` | `map_gpu.h` | `map_gpu*.cpp` |
| 引擎宿主 | `engine/` | `map_engine.h`、`export.h` | `map_engine.cpp` |

示例：

```text
map_engine/
  layer/include/map_engine/map_layer.h
  layer/src/map_layer.cpp
  document/include/map_engine/map.h
  document/src/map.cpp
  engine/include/map_engine/map_engine.h
  engine/src/map_engine.cpp
  …
```

依赖关系与新增代码约定见各组职责说明（`view/` 依赖 `layer`（复用 ``ViewExtent``）；`scene_graph` 当前独立占位；`engine/` 依赖 document / render / layer / projection；document 依赖 layer、projection；`render/` 相对独立等）。
