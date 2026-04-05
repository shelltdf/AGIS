# 第三方许可证摘要（AGIS）

## 运行时依赖（当前构建）

- **Windows 操作系统组件**：Win32 API、Common Controls（`comctl32`）、通用对话框（`comdlg32`）随系统提供。
- **C++ 标准库**：由编译器（如 MSVC）提供。

### 可选：GDAL（`AGIS_USE_GDAL=ON`）

- 使用 **GDAL/OGR**（MIT 风格许可，见 [GDAL License](https://gdal.org/license.html)）进行栅格与矢量读写。工程约定由**源码构建**并安装到本地前缀（见 `3rdparty/README-GDAL-BUILD.md`）；随应用分发时须将所需 DLL 与本文档一并满足合规。
- 捆绑构建链中的 **PROJ**（MIT，见 `3rdparty/proj-9.8.0/COPYING`）、**libcurl**（curl 许可证，见 `3rdparty/curl-8.19.0/COPYING`）与 **SQLite amalgamation**（公有领域，见 [SQLite 版权说明](https://www.sqlite.org/copyright.html)）来自各自上游；分发二进制时请保留相应许可/声明。
- 构建未启用 GDAL（`AGIS_USE_GDAL=OFF`）时，可执行文件不链接 GDAL。

## 构建工具

- **CMake**、**MSVC** / **MinGW** 等仅开发期使用，不视为随应用分发的第三方运行时库。
