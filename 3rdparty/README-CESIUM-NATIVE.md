# Cesium cesium-native（C++ 3D Tiles 运行时）

本仓库将 **[CesiumGS/cesium-native](https://github.com/CesiumGS/cesium-native)** 的 **源码包** 解压到：

`3rdparty/cesium-native-0.59.0/`

（对应 tag **v0.59.0**；更新版本可替换同名目录或并列新版本并改文档版本号。）

## 与 AGIS 主程序的关系

- **当前 AGIS 工程**未默认链接 cesium-native：主工程已捆绑 **GDAL/PROJ/curl** 等；cesium-native 官方构建强依赖 **vcpkg**，若你希望 **完全不用 vcpkg**，请勿在 AGIS 主 CMake 内衔接 cesium-native。
- **内置「3D Tiles 三维预览」**（数据转换侧子类型为 **3dtiles** 时）走 **`tinygltf` + `nlohmann/json`**（源码在 `3rdparty/tinygltf/`、`3rdparty/nlohmann/`），于 **`gis-desktop-win32/src/app/preview/tiles_gltf_loader.cpp`** 中解析 **本地** **`b3dm` / `i3dm`(glb) / `glb` / `cmpt` / `pnts`**，再进入既有 **bgfx** 模型预览。**KHR_draco_mesh_compression** 需按 **`3rdparty/README-DRACO.md`** 放置 **`3rdparty/draco`**（Release 包，勿 git clone）。仍 **不**支持仅 **`http(s)`** 外链 `content.uri` 等（见错误提示）。
- **「瓦片预览」窗口**（`TilePreviewWndProc`）仍为 **XYZ 栅格拼图 + 3D Tiles 文案/统计**；与上述 **模型预览窗**（`OpenModelPreviewWindow3DTiles`）分流。
- 若要将 **cesium-native** 编进 AGIS：建议 **先在独立 build 目录** 按官方文档完成 **install**，再以 **`CMAKE_PREFIX_PATH`** 指向安装前缀做 `find_package`，而不是贸然 `add_subdirectory` 进 `agis_desktop`（除非你已全局改用 vcpkg 工具链并接受重配依赖）。

## 获取源码（与 git clone 等价内容）

- 发布归档：**`https://github.com/CesiumGS/cesium-native/archive/refs/tags/v0.59.0.tar.gz`**

依赖通过 **vcpkg**（含仓库内 `extern/vcpkg` 覆盖端口时由 CMake 自动参与）解析；首次配置会下载大量三方库，**磁盘与时间**请预留余量。

## Windows 推荐阅读

- 官方：`cesium-native/doc/topics/developer-setup.md`
- 与 AGIS 一致使用 **`/MD`** 时，triplet 常选 **`x64-windows-static-md`**（与 cesium-native `CMakeLists.txt` 中 Windows 默认调整一致）。

## 临时下载包

配置过程中若曾下载 **`cesium-native-tmp.tgz`** 仅供解压，可手工删除；勿提交到版本库。
