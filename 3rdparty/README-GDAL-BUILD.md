# 从源码构建 PROJ 与 GDAL（AGIS 约定）

**GDAL 必选/可选依赖与 AGIS 支持对照**见同目录 **[README-GDAL-DEPS.md](README-GDAL-DEPS.md)**。

本仓库**不使用**包管理器预编译包。启用 **`AGIS_USE_GDAL=ON`** 时，CMake 会按顺序：

1. 优先 **`find_package(PROJ)`** / **`find_package(GDAL)`**（例如已安装到 `3rdparty/proj-install`、`3rdparty/gdal-install`）。
2. 若仍未找到 **PROJ**，则对 **`3rdparty/proj-9.8.0`** 执行 **`add_subdirectory`** 在工程内编译。
3. 若仍未找到 **GDAL**，则对 **`3rdparty/gdal-3.12.3`** 执行 **`add_subdirectory`** 在工程内编译。

捆绑 **PROJ** 时，**SQLite** 使用官方 **amalgamation** 目录 **`3rdparty/sqlite-amalgamation-3510300`**（内含 **`sqlite3.c`**、**`sqlite3.h`**、**`shell.c`** 等），**无需**先运行 nmake 生成 amalgamation。**libcurl** 使用 **`3rdparty/curl-8.19.0`** 源码内联编译（Windows 默认 **Schannel** TLS，不依赖 OpenSSL）。

## SQLite（amalgamation）

请使用官方发布的 **sqlite-amalgamation-*.zip** 内容，解压到：

`3rdparty/sqlite-amalgamation-3510300/`

至少包含：**`sqlite3.c`**、**`sqlite3.h`**、**`shell.c`**（及通常随附的 **`sqlite3ext.h`**）。AGIS CMake 会编译静态库 **`agis_sqlite3`** 与命令行 **`sqlite3.exe`**，供 PROJ 生成 `proj.db`。

若你改用完整 **`sqlite-src-*`** 源码树，需自行用 MSVC `nmake` 生成 amalgamation，或继续使用本仓库的 **`3rdparty/gen-sqlite-amalgamation.bat`**（针对 `sqlite-src-*` 目录）。

## 目录约定

| 路径 | 含义 |
|------|------|
| `3rdparty/sqlite-amalgamation-3510300/` | **SQLite amalgamation**（官方 zip，直接可编） |
| `3rdparty/curl-8.19.0/` | 捆绑 **libcurl**（供 PROJ `ENABLE_CURL`；Schannel） |
| `3rdparty/proj-9.8.0/` | 捆绑 **PROJ 9.8.0**（内联编译） |
| `3rdparty/gdal-3.12.3/` | 捆绑 **GDAL**（内联编译） |
| `3rdparty/proj-install/` | （可选）你已单独安装的 PROJ 前缀 |
| `3rdparty/gdal-install/` | （可选）你已单独安装的 GDAL 前缀 |

为减少依赖，PROJ 捆绑构建默认 **`ENABLE_TIFF=OFF`**；**`ENABLE_CURL=ON`** 且使用上述 **libcurl** 源码树（不再关闭网络能力）。

## 构建 AGIS（含 GIS）

```bat
set AGIS_USE_GDAL=on
cd gis-desktop-win32
python build.py
```

仓库中存在 **`3rdparty/gdal-3.12.3`** 时，`build.py` **默认会打开 `AGIS_USE_GDAL`**；若 CMake 缺依赖失败，可 `set AGIS_USE_GDAL=off` 先编壳程序。

`build.py` 会将存在的 `proj-install` / `gdal-install` 合并进 **`CMAKE_PREFIX_PATH`**。

## 仅主程序（无 GDAL）

```bat
set AGIS_USE_GDAL=off
python build.py
```
