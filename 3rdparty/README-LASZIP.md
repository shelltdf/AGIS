# LASzip 源码（AGIS 模型预览 LAZ 解压）

本仓库**不**通过 `git clone` 子模块引入 LASzip；请使用官方 **Release 源码包**。

## 获取

- 发行页：`https://github.com/LASzip/LASzip/releases`
- 示例（3.4.4）：`laszip-src-3.4.4.tar.gz`，直接下载链接形如：  
  `https://github.com/LASzip/LASzip/releases/download/3.4.4/laszip-src-3.4.4.tar.gz`

## 解压与目录名

1. 将 `.tar.gz` 解压到本目录 `3rdparty/` 下。
2. GitHub 提供的该 tarball **顶层文件夹名可能为** `laszip-src-3.4.4.tar.gz`（与压缩包文件名相同）。
3. **请将该文件夹重命名为** `LASzip`，使存在文件：  
   `3rdparty/LASzip/CMakeLists.txt`

`gis-desktop-win32/CMakeLists.txt` 中 **`AGIS_USE_LASZIP` 默认为 ON**（可用 `-DAGIS_USE_LASZIP=OFF` 关闭）。开启时由 `cmake/AGISBundledLASzip.cmake` 检测该路径：成功则主程序与 **`agis_convert_backend_common`**（各 `agis_convert_*` 工具）均定义 `AGIS_HAVE_LASZIP=1` 并静态链接 `laszip3`（Windows）/`laszip`（其它平台）：**模型预览**优先用 LASzip 读 LAZ；**`agis_convert_model_to_model`** 在 **3DMesh→点云** 且输出为 **`.laz`** 时用 **`laszip_open_writer`** 写入压缩 LAZ（与读路径相同的 LAS 1.2 PDRF 2 语义）。若未编入 LASzip，输出 **`.laz`** 时会降为 **`.las`**。

## 许可

LASzip 许可证见其源码树内 `COPYING` / 头文件声明（Apache License 2.0）。
