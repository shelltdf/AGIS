# agis_map_engine

仓库根目录下的 **2D 地图引擎**静态库。

- **对外包含**：`#include "map_engine/map_*.h"`（不变）
- **构建**：[`gis-desktop-win32/CMakeLists.txt`](../gis-desktop-win32/CMakeLists.txt) 中 `add_subdirectory(../map_engine …)`；目标 **`agis_map_engine`**

CMake 将各组下的 `*/include` 全部加入 **PUBLIC**，因此任意 `map_engine/xxx.h` 仍从单一逻辑前缀解析。

仍依赖桌面工程私有头（`resource.h`、`app_log`、`common/*` 等），见 `CMakeLists.txt` 中 `../gis-desktop-win32/src`。

---

## 目录：功能组 = include + src

每组 **先按功能分组**，组内再分 **`include/map_engine/`**（公开头）与 **`src/`**（实现）：

| 组 | 路径 | 头文件 | 源文件 |
|----|------|--------|--------|
| 图层与驱动 | `layer/` | `map_layer*.h`、`map_engine_internal.h` | `map_layer*.cpp` |
| 文档模型 | `document/` | `map_document.h` | `map_document.cpp` |
| 投影 | `projection/` | `map_projection.h` | `map_projection.cpp` |
| 呈现后端 | `gpu/` | `map_gpu.h` | `map_gpu*.cpp` |
| 壳层 / 宿主 | `shell/` | `map_engine.h`、`map_utf8.h` | `map_engine.cpp` |

示例：

```text
map_engine/
  layer/include/map_engine/map_layer.h
  layer/src/map_layer.cpp
  document/include/map_engine/map_document.h
  document/src/map_document.cpp
  …
```

依赖关系与新增代码约定见各组职责说明（shell 依赖 document / gpu / layer / projection；document 依赖 layer、projection；gpu 相对独立等）。
