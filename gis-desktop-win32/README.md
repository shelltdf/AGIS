# AGIS 桌面端（Win32）

Windows **本地 Win32 API** 实现的 **SDI** 主程序；默认在含捆绑源码时启用 **GDAL**（可用 `AGIS_USE_GDAL=off` 关闭）。

## 默认构建

仓库含 **`../3rdparty/gdal-3.12.3`** 时，`python build.py` **默认 `AGIS_USE_GDAL=ON`**（链接 GDAL，需满足 `3rdparty/README-GDAL-BUILD.md` 中的依赖）。

若仅需无 GIS 的壳程序（无需 PROJ/SQLite）：

```bat
set AGIS_USE_GDAL=off
python build.py
```

产物：`build/Release/AGIS.exe`。

## GDAL / PROJ（捆绑源码）

1. 默认会尝试链接 GDAL；捆绑目录为 **`../3rdparty/proj-9.8.0`**、**`../3rdparty/gdal-3.12.3`**。PROJ 需要 **SQLite3** 与 **`sqlite3.exe`（PATH）**，见 **`../3rdparty/README-GDAL-BUILD.md`**。
2. 若已将 GDAL/PROJ 单独安装到 **`../3rdparty/gdal-install`**、**`../3rdparty/proj-install`**（或设置 **`AGIS_GDAL_PREFIX`** / **`AGIS_PROJ_PREFIX`**），CMake **优先**使用已安装包。

`build.py` 会把存在的 `proj-install` / `gdal-install` 合并进 **`CMAKE_PREFIX_PATH`**。

## 运行与测试

```text
python run.py
python test.py
```

## 发布目录

```text
python publish.py
```

输出：`dist/AGIS.exe`。

## 功能摘要

- **图层 → 添加数据图层**：启用 GDAL 时打开栅格/矢量文件。
- **地图**：GDI 双缓冲；**中键**平移、**滚轮**以指针为锚缩放。
