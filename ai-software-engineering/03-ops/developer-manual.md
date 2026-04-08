# AGIS 开发维护说明

## 1. 仓库结构

| 路径 | 说明 |
|------|------|
| `ai-software-engineering/` | 四阶段工程文档（概念/逻辑/物理/运维） |
| `gis-desktop-win32/` | Windows Win32 主程序源码与 CMake |
| `.cursor/rules/` | Cursor 规则 |

**实现产物**不得写入 `ai-software-engineering/`（见 `.cursor/rules/ai-docs-code-boundary.mdc`）。

## 2. 构建

- 前置：**CMake 3.20+**、**C++17 编译器**（Visual Studio 2022 或 Clang/LLVM on Windows、或 MinGW）。  
- **GDAL（`CMakeLists.txt` 与 `build.py` 均默认 `AGIS_USE_GDAL=ON`；未设置环境变量时脚本始终传入开启）**  
  - CMake 优先 **`find_package(PROJ/GDAL)`**（如 **`3rdparty/proj-install`**、**`3rdparty/gdal-install`**）；否则对 **`3rdparty/proj-9.8.0`**、**`3rdparty/gdal-3.12.3`** 做 **`add_subdirectory`** 捆绑编译，见 **`3rdparty/README-GDAL-BUILD.md`**（含 SQLite3 / `sqlite3.exe` 前提）。  
  - 显式 **`AGIS_USE_GDAL=off`** 可编无 GIS 壳程序；脚本会合并已有 `proj-install` / `gdal-install` 到 **`CMAKE_PREFIX_PATH`**。  
- 无 GDAL 或暂不想链接时：

```text
cmake -B build -DAGIS_USE_GDAL=OFF
cmake --build build --config Release
```

或使用环境变量关闭后仍用脚本：`AGIS_USE_GDAL=off python build.py`（Windows：`set AGIS_USE_GDAL=off`）。

- 在 `gis-desktop-win32/` 目录执行：

```text
python build.py
```

或直接：

```text
cmake -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build --config Release
```

- Visual Studio 多配置生成器下，可执行文件通常在 `build/Release/AGIS.exe`（以 `CMakeLists.txt` 中输出名为准）。

## 3. 运行与测试

```text
python run.py
python test.py
```

`test.py` / `run.py` / `publish.py` 均通过 `agis_build_util.ensure_project_built()` **每次调用** `build.py`。`build.py` 每次执行 **`cmake -B`** 与 **`cmake --build`**；是否只做增量编译/链接由 **CMake 与 MSBuild（或 Ninja）** 自行决定，脚本侧不再根据文件新旧跳过配置或构建。

## 4. 发布

```text
python publish.py
```

将可执行文件复制到 `dist/`（或脚本约定目录），便于打包。

## 5. 后续依赖（GDAL）

- 引入 GDAL 时：在 CMake 中增加选项 `GIS_DESKTOP_WITH_GDAL`，更新根目录 `THIRD_PARTY_LICENSES.md`，并注明运行时 DLL 部署方式。

## 6. 文档同步

- 修改行为或界面时，同步更新 `ai-software-engineering/02-physical/gis-desktop-win32/spec.md` 与用户手册相关章节。

## 7. 数据转换后端维护

- 转换公共逻辑：`gis-desktop-win32/src/tools/convert_backend_common.cpp/.h`。  
- 六个后端入口：`gis-desktop-win32/src/tools/agis_convert_*.cpp`，分别对应 6 条互转路径。  
- 主程序调度：`gis-desktop-win32/src/app/main.cpp` 中根据输入/输出大类映射到具体 EXE，并构建命令行参数。  
- `CMakeLists.txt` 中：
  - `agis_convert_backend_common` 为静态库；
  - 6 个后端可执行通过 `add_executable` 构建；
  - `agis_desktop` 依赖这些后端，且在构建后复制到主程序输出目录。

## 8. 模型预览维护

- 当前实现位于 `gis-desktop-win32/src/app/main.cpp`，包含：
  - OBJ/MTL 解析（`ParseObjModel` / `ParseMtlMaterials`）；
  - OpenGL 预览后端；
  - DirectX11 预览后端；
  - 预览窗口消息处理与 UI 交互逻辑。
- **性能**：不对空闲视口做固定帧率刷新；当面片数极大时对绘制列表按步长抽稀（上限约 12 万面），减轻立即模式 GL 压力。
- **适配**：`FitPreviewCamera` 使用的 `zoom` 与渲染中的 `1/extent` 归一化一致，避免重复按包围盒缩放。
- 若后续做结构化重构，建议拆分为：
  - `src/app/model_preview.h/.cpp`（窗口与渲染）
  - `src/app/model_preview_parser.h/.cpp`（OBJ/MTL 解析）
  并在 `CMakeLists.txt` 给 `agis_desktop` 追加源文件。
