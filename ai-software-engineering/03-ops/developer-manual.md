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
- **GDAL（`CMakeLists.txt` 默认 `AGIS_USE_GDAL=ON`；`build.py` 在存在 `3rdparty/gdal-3.12.3` 时默认开启）**  
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

`test.py` / `run.py` / `publish.py` 通过 `agis_build_util.ensure_project_built()` 决定是否需要构建：**当 `build/.../AGIS.exe` 已存在且不比 `src/**` 源码与 `CMakeLists.txt`、`cmake/`、`app.manifest` 旧时，会跳过整个 `build.py` 调用**（节省重复配置与链接时间）。需要强制构建时设 **`AGIS_ALWAYS_BUILD=1`**。直接运行 **`python build.py`** 时：若 CMake 缓存已存在且上述 CMake 相关文件未比 `CMakeCache.txt` 更新，则**跳过 `cmake -B` 配置步骤**，仅执行 `cmake --build`（仍由 MSBuild 做增量编译）；需要重配时设 **`AGIS_FORCE_CONFIGURE=1`** 或删除 `build/`。流水线已编译时可设 **`AGIS_SKIP_BUILD=1`** 跳过前置构建。

## 4. 发布

```text
python publish.py
```

将可执行文件复制到 `dist/`（或脚本约定目录），便于打包。

## 5. 后续依赖（GDAL）

- 引入 GDAL 时：在 CMake 中增加选项 `GIS_DESKTOP_WITH_GDAL`，更新根目录 `THIRD_PARTY_LICENSES.md`，并注明运行时 DLL 部署方式。

## 6. 文档同步

- 修改行为或界面时，同步更新 `ai-software-engineering/02-physical/gis-desktop-win32/spec.md` 与用户手册相关章节。
