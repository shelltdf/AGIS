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
  - CMake 优先 **`find_package(PROJ/GDAL)`**（如 **`3rdparty/proj-install`**、**`3rdparty/gdal-install`**）；否则对 **`3rdparty/proj-9.8.0`**、**`3rdparty/gdal-3.12.3`** 做 **`add_subdirectory`** 捆绑编译，见 **`3rdparty/README-GDAL-BUILD.md`**（含 SQLite3 / `sqlite3.exe` 前提）。捆绑路径下 **`cmake/AGISBundledGDAL.cmake`** 在检测到 **`agis_sqlite3`** 时会强制 **`GDAL_ENABLE_DRIVER_MBTILES`** 及 OGR **GPKG/MVT**（MBTiles 驱动的 CMake 依赖链）；若曾无 SQLite 配置过 GDAL，需重新 **CMake 配置** 并 **整库重编 GDAL** 后，`agis_convert_*` 中 **`GDALGetDriverByName("MBTiles")`** 才可用。  
  - **`GDAL_DATA`**：捆绑构建会将 **`gcore/data`**（含 **`tms_NZTM2000.json`** 等）阶段到 **`build/gdal_data`**，并在 **`POST_BUILD`** 复制到 **`AGIS.exe` / 各 `agis_convert_*.exe` 同级的 **`gdal_data/`**；`AgisEnsureGdalDataPath`（`agis_gdal_runtime_env.cpp`）在 **`GDALAllRegister` 之前** 自 exe 目录向上探测并 **`CPLSetConfigOption("GDAL_DATA", …)`**，避免出现 “**GDAL_DATA is not defined**” 类告警。若单独拷贝转换工具到其它机器，请连同 **`gdal_data`** 一并发布，或在外部设置 **`GDAL_DATA`** 环境变量。
  - **`PROJ_DATA` / `proj.db`**：捆绑 PROJ 时先阶段到 **`build/proj_data`**，并在 **`agis_desktop` 链接后 `POST_BUILD`** 复制到与 **`AGIS.exe` 同级的 **`proj_data/`**（与同目录 **`gdal_data`** 相同，避免多 exe 并行各拷一份；同 **`Release/`** 下的预览/转换工具共用该目录）。运行时仍可由 `AgisEnsureGdalDataPath` 自 exe 向上探测 **`proj_data`**（含 **`proj.db`**）。
  - **LAZ 与 LASzip**：**推荐**将 LASzip **Release 源码包**解压到 **`3rdparty/LASzip`**（**勿用 git clone**；步骤见 **`3rdparty/README-LASZIP.md`**）。CMake 检测到后将 **静态链接** LASzip，`*.laz` 预览 **优先走 LASzip C API**。**回退**：若未放置源码或打开失败，且 **`AGIS_USE_GDAL=ON`**，则仍尝试 **`GDALOpenEx`** 矢量读 LAZ（此时依赖 GDAL 侧 LASzip/LAS 配置，失败见 CPL 提示）。两者皆不可用时预览会提示最小依赖说明。  
  - **3D Tiles 三维预览（无 vcpkg）**：主程序使用 **`3rdparty/tinygltf`** + **`3rdparty/nlohmann/json.hpp`**，在 **`gis-desktop-win32/src/app/preview/tiles_gltf_loader.cpp`** 解析 **本地** **`b3dm`/`i3dm`/`glb`/`cmpt`/`pnts`** 与 glTF。**Draco**：**勿 `git clone`**；按 **`3rdparty/README-DRACO.md`** 下载 **Release 归档**（推荐 **1.5.7** zip）解压并重命名为 **`3rdparty/draco`**；**`cmake/AGISBundledDraco.cmake`** 静态链入并定义 **`TINYGLTF_ENABLE_DRACO`** 以支持 **`KHR_draco_mesh_compression`**。**CMake** 须将 **`../3rdparty` 与 `../3rdparty/tinygltf` 置于 bgfx `examples/common` 之后**，否则会错误包含 **`3rdparty/imgui`** 导致 `imguiCreate` 未定义。可选 **Cesium cesium-native**（强依赖 vcpkg）仅供对照，见 **`3rdparty/README-CESIUM-NATIVE.md`**。
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

或直接（**构建目录在仓库根**，与 `CMakePresets.json` / `build.py` 一致）：

```text
# 在仓库根目录
cmake -S gis-desktop-win32 -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build --config Release
```

或在 `gis-desktop-win32/` 下：

```text
cmake -B ../build -DCMAKE_BUILD_TYPE=Release
cmake --build ../build --config Release
```

- Visual Studio 多配置生成器下，可执行文件通常在 **仓库根** `build/Release/AGIS.exe`（以 `CMakeLists.txt` 中输出名为准）。

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
- 七个后端入口：`gis-desktop-win32/src/tools/agis_convert_*.cpp`，覆盖 GIS/模型/瓦片的 7 条转换路径（含 模型↔模型）。  
- 主程序调度：`gis-desktop-win32/src/app/main.cpp` 中根据输入/输出大类映射到具体 EXE，并构建命令行参数。  
- `CMakeLists.txt` 中：
  - `agis_convert_backend_common` 为静态库；
  - 7 个后端可执行通过 `add_executable` 构建；
  - `agis_desktop` 依赖这些后端，且在构建后复制到主程序输出目录。
- `agis_convert_tile_to_model` 支持 `--tile-max-memory-mb`（64..131072，默认 512）控制瓦片合并内存上限，超限自动拆分多个 OBJ。

## 8. 模型预览维护

- **源码目录**：`gis-desktop-win32/src/app/preview/`（`main_model_preview.cpp`、`model_preview_bgfx.*`、`model_preview_types.h`、`tiles_gltf_loader.*`）。**窗口类注册**仍在 `src/app/main.cpp`（`ModelPreviewWndProc` / `TilePreviewWndProc`）。
- **渲染**：正式构建为 **bgfx**（`AGIS_USE_BGFX=ON`）；`main_model_preview.cpp` 内 `#if !AGIS_USE_BGFX` 分支为历史参照。
- **性能**：不对空闲视口做固定帧率滥用刷新；面片/点云有预算与抽稀（参见 `02-physical/gis-desktop-win32/spec.md` 2.8）。
- **适配**：`FitPreviewCamera` 等与包围盒/zoom 一致，避免重复缩放。
