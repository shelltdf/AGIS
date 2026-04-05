# GDAL 依赖库说明（AGIS 捆绑构建对照）

本文档对应 **GDAL 3.12.x** 的 CMake 依赖探测逻辑（源码中 `cmake/helpers/CheckDependentLibraries*.cmake`），并标注 **当前 AGIS 在 `gis-desktop-win32` 中通过 `add_subdirectory(3rdparty/gdal-3.12.3)` 捆绑构建**时的实际支持情况。

- **已支持**：本仓库已纳入源码捆绑、或已接到 CMake 目标/系统库，当前配置下可参与链接。
- **未支持 / 未接入**：CMake 未找到或未开启对应 `GDAL_USE_*`，相关驱动或功能被关闭；若需启用，请自行下载源码或安装到前缀后，通过 **`CMAKE_PREFIX_PATH`** / **`gdal-install`** 等让 `find_package` 发现（参见 `README-GDAL-BUILD.md`）。

**官方总览**（驱动与可选组件以你本地 `cmake` 配置结束时的 FeatureSummary 为准）：

- GDAL 文档：<https://gdal.org/>
- 构建说明：<https://gdal.org/en/latest/development/building_from_source.html>

---

## 一、对 AGIS 当前捆绑链而言的「硬依赖」

| 库 | 作用 | AGIS 状态 | 获取 |
|----|------|-----------|------|
| **PROJ** ≥ 6.3（建议 ≥ 8） | 坐标变换、GDAL 与 PROJ 链接 | **已支持**：`3rdparty/proj-9.8.0` 内联 | <https://proj.org/> · <https://github.com/OSGeo/PROJ> |
| **libcurl**（≥ 7.68 为 GDAL 推荐版本检查） | HTTP/HTTPS、网络驱动与 `/vsicurl/` 等 | **已支持**：`3rdparty/curl-8.19.0` 内联（Windows **Schannel**，静态 `CURL_STATICLIB`） | <https://curl.se/> · <https://github.com/curl/curl> |
| **SQLite3** | GPKG、SQLite OGR、Rasterlite、MBTiles 等 | **已支持**：与 PROJ 共用 **`agis_sqlite3`** 目标（`3rdparty/sqlite-amalgamation-3510300`） | <https://www.sqlite.org/download.html> |
| **C/C++ 编译器、CMake** | 构建 | **系统** | MSVC / CMake 等 |

---

## 二、由 GDAL 源码树「内置」编译的库（不依赖你单独放 `3rdparty`）

以下在 CMake 中通常表现为 **`GDAL_USE_*_INTERNAL=ON`**（未找到合适外部包时使用内置副本）。**AGIS 当前配置下多为内置**。

| 库 | 用途摘要 | AGIS 状态 |
|----|----------|-----------|
| **zlib** | 压缩基础 | 内置（`frmts/zlib`） |
| **libtiff** / **GeoTIFF** | GeoTIFF 等 | 内置（`frmts/gtiff/...`） |
| **libpng** | PNG 驱动 | 内置 |
| **libjpeg** / **jpeg12** | JPEG 驱动 | 内置 |
| **giflib** | GIF 驱动 | 内置 |
| **json-c** | JSON/GeoJSON 等 | 内置（`ogr/.../libjson`） |
| **LERC** | 栅格压缩（GTiff/MRF 等） | 内置（`third_party/LercLib`） |
| **Qhull** | 部分算法 | 内置 |
| **libopencad** | CAD 驱动 | 内置 |
| **shapelib** | Shapefile（默认推荐内置） | 内置 |
| **libcsf** | PCRaster 等 | 内置（`frmts/pcraster/libcsf`） |

若希望**改用系统或自行编译的外部副本**，需关闭对应 `GDAL_USE_*_INTERNAL` 并提供 `find_package` 能找到的安装前缀（见 GDAL 官方构建文档）。

---

## 三、系统或 Windows SDK 提供（无需 `3rdparty` 源码）

| 库 | 用途 | AGIS 状态 | 说明 |
|----|------|-----------|------|
| **ODBC** | 多种 ODBC 相关 OGR 驱动 | **已支持**（典型为 `odbc32.lib` 等） | Windows SDK / 系统已提供导入库 |

---

## 四、可选 / 推荐但当前未单独接入 `3rdparty` 的库

下列为 GDAL CMake 中常见 **可选**依赖；**未在表中「已支持」即表示当前 AGIS 默认捆绑**未链接外部副本，对应功能可能缺失或以简化方式编译。

| 库 | GDAL 中角色 | AGIS 默认 | 下载 / 主页 |
|----|----------------|-----------|-------------|
| **GEOS** ≥ 3.8 | OGR 几何谓词、拓扑等（**强烈推荐**） | **未单独引入** | <https://libgeos.org/> · <https://github.com/libgeos/geos> |
| **OpenSSL**（SSL Crypto） | 加密、部分网络栈可选 | **未使用**（curl 使用 Schannel） | <https://www.openssl.org/> |
| **libiconv** | 字符集转换 | **未使用** | GNU libiconv 各发行版 / <https://www.gnu.org/software/libiconv/> |
| **Libxml2** | XML 读写 | **未使用** | <https://gitlab.gnome.org/GNOME/libxml2> |
| **Expat** | XML（轻量） | **未使用** | <https://github.com/libexpat/libexpat> |
| **Xerces-C++** | GMLAS、ILI 等 | **未使用** | <https://xerces.apache.org/xerces-c/> |
| **libdeflate** | 与 zlib 配合的压缩 | **未使用** | <https://github.com/ebiggers/libdeflate> |
| **Crypto++** | CPL 可选加密 | **未使用** | <https://www.cryptopp.com/> |
| **Zstd** | 压缩 | **未使用** | <https://github.com/facebook/zstd> |
| **SFCGAL** | 3D/ISO 19107 等 | **未使用** | <https://sfcgal.gitlab.io/> |
| **HDF5** | HDF5 驱动 | **未使用** | <https://www.hdfgroup.org/solutions/hdf5/> |
| **NetCDF** | 网络 Common Data Form | **未使用** | <https://www.unidata.ucar.edu/software/netcdf/> |
| **OpenJPEG** ≥ 2.3.1 | JPEG2000（OpenJPEG） | **未使用** | <https://www.openjpeg.org/> |
| **libwebp** | WebP | **未使用** | <https://developers.google.com/speed/webp> |
| **FreeXL** | XLS | **未使用** | <https://github.com/SoftDB/FreeXL> |
| **libkml** | KML（LIBKML） | **未使用** | <https://github.com/google/libkml> |
| **PostgreSQL** | PostGIS 等 | **未使用** | <https://www.postgresql.org/> |
| **MySQL** | MySQL OGR | **未使用** | <https://dev.mysql.com/> |
| **MRSID / ECW / Kakadu** 等 | 商业/专有 SDK 驱动 | **未使用** | 各厂商 SDK |
| **Poppler / PDFium** | PDF 栅格驱动 | **未使用** | Poppler：<https://poppler.freedesktop.org/> |
| **PCRE2 / PCRE** | SQLite 可选功能 | **未使用** | PCRE2：<https://www.pcre.org/> |
| **SpatiaLite** / **RasterLite2** | 扩展 SQLite | **未使用** | OSGeo SpatiaLite 文档 |
| **liblzma / lz4 / blosc** 等 | 多种压缩场景 | **未使用** | 各项目 GitHub |
| **Armadillo + LAPACK** | 样条等数值 | **未使用** | <https://arma.sourceforge.net/> |
| **OpenEXR** | EXR 栅格 | **未使用** | <https://www.openexr.com/> |
| **libheif / libavif** | HEIF/AVIF | **未使用** | 各项目主页 |

需要某一库时：将其安装到固定前缀（例如 `3rdparty/foo-install`），在配置 AGIS 前设置 **`CMAKE_PREFIX_PATH`** 指向该前缀，并打开对应 **`GDAL_USE_*`**（名称以 `gdal-3.12.3/CMakeLists.txt` 与 CMake 缓存为准）。

---

## 五、与 PROJ 共用但角色不同的组件

| 组件 | PROJ 用途 | GDAL 用途 |
|------|-----------|-----------|
| **SQLite** | 生成/嵌入 `proj.db` 等 | GPKG、SQLite 格式等（链接同一 **`agis_sqlite3`**） |
| **libcurl** | 网络网格与 `projsync` 等 | 网络驱动、`/vsicurl/` 等 |

---

## 六、维护说明

- 升级 **GDAL 小版本**时，依赖项名称以新版本 `cmake/helpers/CheckDependentLibraries.cmake` 为准，并对比配置结束时 **FeatureSummary** 输出更新本文档。
- 与 **`README-GDAL-BUILD.md`**、**`THIRD_PARTY_LICENSES.md`** 保持许可来源一致；新增随二进制分发的第三方库时须更新许可证汇总。
