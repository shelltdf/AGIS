# basis_universal（KTX2 / Basis Universal / 可选 Zstd 超压）

用于 `gis-desktop-win32` 转换工具在 **`--texture-format`** 下输出 **`.ktx2`**（含 **mipmap** 与 **Basis Universal** 有效载荷；启用 Zstd 时附加 **KTX2 级超压缩**）。与 LASzip、Draco 相同：**请用 ZIP/tarball 获取源码，不要使用 `git clone`**。

## 放置目录

将源码树解压到：

`AGIS/3rdparty/basis_universal/`

（该目录下应有顶层 `CMakeLists.txt`。若解压后目录名为 `basis_universal-master` 等，请重命名为 `basis_universal`。）

## 下载方式（示例）

- 固定分支 ZIP：  
  `https://github.com/BinomialLLC/basis_universal/archive/refs/heads/master.zip`
- 或 Releases / Tags 页下载 **Source code (zip)**。

## CMake

- **`AGIS_USE_BASISU`**（`gis-desktop-win32/CMakeLists.txt`）默认 **ON**；源码缺失时配置仍成功，**不会**定义 `AGIS_HAVE_BASISU`，`--texture-format ktx2*` 将报错提示。
- 可选缓存：**`AGIS_BASISU_ROOT`** 指向其它解压路径（与 `AGISBundledBasisUniversal.cmake` 一致）。

## 格式说明（CLI）

- **`ktx2`**（默认 UASTC LDR 4×4）：体积较大、质量较好。
- **`ktx2-etc1s`**：ETC1S / Basis 传统模式，通常更小。

二者均生成 **`.ktx2`**，编码路径启用 **mipmap（clamp）**、**sRGB** 输入语义；在开启 **`BASISU_ZSTD`** 的构建中附加 **KTX2 UASTC 超压缩** 标志（与上游 `cFlagKTX2UASTCSuperCompression` 一致）。

## 运行时与预览

本仓库 **桌面瓦片预览** 若仍按栅格扩展名加载，**可能尚未解码 `.ktx2`**；请在支持 KTX2/Basis 的引擎或外部查看器中验证输出。
