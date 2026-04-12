# Draco 源码（AGIS 3D Tiles / glTF Draco 解压）

本仓库**不**通过 `git clone` 引入 Draco；请使用 **GitHub Release 或「Source code」归档包**（zip / tar.gz），与 **LASzip**（`README-LASZIP.md`）方式一致。

## 获取

- 发行页：`https://github.com/google/draco/releases`
- 当前与工程验证过的版本：**1.5.7**
- **源码 zip（推荐）**：  
  `https://github.com/google/draco/archive/refs/tags/1.5.7.zip`
- **tar.gz**：  
  `https://github.com/google/draco/archive/refs/tags/1.5.7.tar.gz`

## 解压与目录名

1. 将归档解压到本目录 `3rdparty/` 下。  
   GitHub 解压后顶层文件夹名为 **`draco-1.5.7`**（与 tag 一致）。
2. **请将该文件夹重命名为** `draco`，使存在文件：  
   `3rdparty/draco/CMakeLists.txt`
3. 目录内**不应依赖** `.git`（纯源码树即可）。

CMake 通过 `gis-desktop-win32/cmake/AGISBundledDraco.cmake` 检测该路径；成功时主程序定义 **`TINYGLTF_ENABLE_DRACO=1`** 并静态链接 **`draco`**，供 **`tinygltf`** 处理 **`KHR_draco_mesh_compression`**。

## 头文件生成说明

配置工程后，Draco 会在 **`<仓库根>/build/draco/draco_features.h`**（默认 out-of-source 构建目录）生成特性头文件；`gis-desktop-win32/CMakeLists.txt` 已将 **`${CMAKE_BINARY_DIR}`** 加入 `agis_desktop` 的 include（与 Draco 上游 CMake 行为一致）。

## 下载失败（Windows）

若 `curl` 报 `CRYPT_E_NO_REVOCATION_CHECK` 等证书吊销检查错误，可改用：

`curl.exe --ssl-no-revoke -L -o draco-1.5.7.zip "https://github.com/google/draco/archive/refs/tags/1.5.7.zip"`

或在浏览器中下载同名 zip 后解压。

## 许可

Draco 许可证见其源码树内 `LICENSE`（Apache License 2.0）。
