# AGIS

类 QGIS 的 **GIS 数据编辑与处理** 软件（Windows 本地桌面端），采用 **SDI** 主窗口；工程文档与实现分离。

| 路径 | 说明 |
|------|------|
| `ai-software-engineering/` | 四阶段文档（概念 / 逻辑 / 物理 / 运维） |
| `gis-desktop-win32/` | **Win32 API** 主程序源码与 CMake |

快速构建：在 `gis-desktop-win32/` 下执行 `python build.py`，运行 `build/Release/AGIS.exe`。

详细说明见 `ai-software-engineering/03-ops/developer-manual.md`。
