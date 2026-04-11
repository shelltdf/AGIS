# gis-desktop-win32

| 项 | 内容 |
|----|------|
| 产物名 | `AGIS.exe`（CMake 目标名 `agis_desktop`） |
| 类型 | Windows x64 可执行文件（Win32 子系统） |
| 源码根 | 仓库根目录 [`gis-desktop-win32/`](../../../gis-desktop-win32/)（相对本文件：`../../../gis-desktop-win32/`） |
| CMake | [`gis-desktop-win32/CMakeLists.txt`](../../../gis-desktop-win32/CMakeLists.txt) |

- **规格**：[spec.md](spec.md)  
- **映射**：[mapping.md](mapping.md)  
- **类图**：[uml-class.md](uml-class.md)（应用壳过程入口）  
- **map 模块类图**：[uml-class-map.md](uml-class-map.md)（根目录 [`map_engine/`](../../../map_engine/)：图层、文档、投影、GPU 后端；库目标 `agis_map_engine`）  
- **ui 模块类图**：[uml-class-ui.md](uml-class-ui.md)（仓库根 [`ui_engine/`](../../../ui_engine/)：按模块 `core` / `widgets` / `gdiplus` / `platform` / `demo` 分 `include`+`src`）
