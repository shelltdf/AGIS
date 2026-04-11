# gis-desktop-win32 物理规格

## 1. 构建与平台

- **语言**：C++17 及以上。  
- **生成器**：CMake 3.20+。  
- **平台**：Windows x64；`WIN32` 已定义；链接 `comctl32`（Common Controls v6）。  
- **子系统**：`WINDOWS`（非控制台）。
- **LASzip（LAZ）**：CMake 选项 **`AGIS_USE_LASZIP` 默认 ON**；在 **`../3rdparty/LASzip`** 存在有效源码树时由 `cmake/AGISBundledLASzip.cmake` 编入主程序并定义 **`AGIS_HAVE_LASZIP=1`**。源码缺失时配置仍可成功，但不会产生 **`AGIS_HAVE_LASZIP`**，LAZ 仅可走 GDAL 等回退路径。**`-DAGIS_USE_LASZIP=OFF`** 时跳过捆绑 LASzip。见 `3rdparty/README-LASZIP.md`。
- **basis_universal（KTX2 瓦片/模型贴图）**：CMake 选项 **`AGIS_USE_BASISU` 默认 ON**；在 **`../3rdparty/basis_universal`** 存在有效源码树时由 `cmake/AGISBundledBasisUniversal.cmake` 编入转换后端静态库 **`agis_convert_backend_common`**（链接 **`basisu_encoder`**）并联编主程序定义 **`AGIS_HAVE_BASISU=1`**（用于转换窗口贴图格式下拉）。源码须以 **ZIP/tarball** 获取，**勿 `git clone`**，见 **`3rdparty/README-BASIS-KTX2.md`**。缺失时 **`AGIS_HAVE_BASISU` 未开启**，`--texture-format ktx2*` 运行期报错提示；**`-DAGIS_USE_BASISU=OFF`** 跳过捆绑。
- **主程序源码（实现目录 `gis-desktop-win32/src/app/`，相对仓库根）**：`main.cpp`（`wWinMain`、`MainProc`、`BuildMenu`、`RegisterClasses`）；`main_globals.*`（顶层 `HWND` 与窗口类名字符串）；`main_chrome.cpp`（分割布局、工具栏、滚轮转发、视图/窗口菜单同步）；`main_panels.cpp`（图层面板、属性 Dock、日志窗）；**子目录 `gis_document/`**：`main_gis_xml.*`（`.gis` XML 片段解析辅助）、`main_gis_document.cpp`（读写 `.gis`、`GisNew`/`Open`/`Save`、窗口标题与文档刷新等）；`main_convert.cpp`（数据转换 UI 与后端拉起）；**预览子目录 `preview/`**：`main_model_preview.cpp`（OBJ / LAS 点云 / 瓦片栅格预览与 bgfx）、`model_preview_bgfx.*`、`model_preview_types.h`、`tiles_gltf_loader.*`（3D Tiles→网格）；`ui_font.cpp`（应用 UI 字体、日志等宽字体初始化/释放）；`ui_theme.cpp`（主题偏好、DWM/uxtheme、主客户区画刷及与侧栏主题同步）。

## 2. 可执行行为（首版）

### 2.1 启动

- `WinMain` → `MapEngine_Init()`（注册 GDAL 驱动，若构建启用 GDAL）→ 注册主窗口类 → 创建主窗口（最小客户区约 800×600）→ 消息循环。  
- **退出码**：正常 `0`。主窗口 `WM_DESTROY` 调用 `MapEngine_Shutdown()` 释放图层数据源。

### 2.2 主窗口

- **类名**：`AGISMainFrame`（或实现中固定字符串，与 `mapping.md` 一致）。  
- **标题**：`AGIS`；脏文档时标题加 ` ·` 后缀（实现工程后启用）。  
- **菜单**：至少包含 **文件**（退出）、**视图**（2D/3D、日志）、**图层**（添加数据图层…）、**语言**（中文/英文占位）、**主题**（跟随系统/浅色/深色，见 §2.4）、**帮助**（关于）。  
- **工具栏**：可选 Rebar + Toolbar；首版可为占位或单条工具栏。  
- **布局**：  
  - 左侧 **图层面板** 子窗口：类名 `AGISLayerPane`，初始宽度约 240px，可水平拖拽调整（`WM_LBUTTONDOWN` 在分割条上拖动或 `MoveWindow` 调整）。列表框列出已加载图层显示名。  
  - 右侧 **地图客户区** 子窗口：类名 `AGISMapHost`，占剩余客户区；**GDI 双缓冲**绘制；**中键拖拽**平移视域，**滚轮**以指针位置为锚缩放（`MapDocument::ZoomAt`）。  
- **状态栏**：至少两格：就绪消息、静态提示。

### 2.3 视图模式

- **命令 ID**：`ID_VIEW_MODE_2D`（默认）、`ID_VIEW_MODE_3D`。  
- **行为**：切换勾选状态；**2D** 为 GDAL+GDI 地图绘制；**3D** 首版仍为占位提示（不改变 2D 数据管线）。

### 2.3.1 图层与 GDAL（`AGIS_USE_GDAL=ON`）

- **添加数据图层**：`ID_LAYER_ADD`，`GetOpenFileName` 选择栅格/矢量（如 GeoTIFF、PNG/JPEG（地理配准依赖文件）、Shapefile、GeoJSON、GPKG 等）。  
- **栅格**：`GetRasterCount()>0` 时创建栅格图层，按 GeoTransform 计算范围；绘制时对视域做子集读取（降采样上限约 4096px 维度），`StretchDIBits` 绘制。  
- **矢量**：无栅格且 `GetLayerCount()>0` 时遍历 OGR 要素，点/线/面映射到屏幕坐标系（与视域一致）。  
- **构建选项**：`AGIS_USE_GDAL=OFF` 时不链接 GDAL，添加图层返回提示信息，地图区显示无 GDAL 说明文本。

### 2.4 语言与主题

- **语言**：菜单触发 `MessageBox` 或状态栏提示「语言切换待实现」；预留资源加载点。  
- **主题**（实现：`ui_theme.h` / `ui_theme.cpp`）：菜单 **跟随系统 / 浅色 / 深色**；`HKCU\Software\AGIS\GIS-Desktop` 下 `ThemeMenu`（DWORD 0–2）持久化；`AppsUseLightTheme`（个性化）判定系统偏暗应用。`AgisApplyTheme`：**DWM** `DWMWA_USE_IMMERSIVE_DARK_MODE`（主窗、日志、转换、数据驱动说明窗）；**uxtheme** `SetWindowTheme`（工具栏、状态栏、左右缘条 `DarkMode_Explorer` / `Explorer`）；**GDI+ 侧栏** `UiSetPanelThemeDark`；主客户区 `WM_ERASEBKGND` 使用随主题重建的画刷；**跟随系统** 时响应 `WM_SETTINGCHANGE`（`lParam == L"ImmersiveColorSet"`）重应用。属性 Dock、日志窗、转换窗、帮助窗对子控件使用 **`WM_CTLCOLOR*`** 与浅色/深色配色一致。

### 2.5 关于对话框

- **帮助 → 关于**：显示产品名、版本占位（如 `0.1.0-dev`）、版权说明。

### 2.6 Log（首版）

- **状态栏双击** 或 **菜单**：弹出简单对话框，多行 `ES_READONLY` 编辑框，按钮「复制全部」将文本送剪贴板；首版可预置若干行启动日志。

### 2.7 数据转换工具（已实现）

- **菜单路径**：`工具 → 数据转换`。  
- **窗口结构**：上半区三列（输入 / 中间参数 / 输出），下半区命令行、多段进度与日志。中间列为 **四段分组框**，与四条 CLI 轴一一对应：① **输入类型**（`--input-type`，含 `vector-mode` / `elev-horiz-ratio` / 输入侧 `coord-system` 等仅在适用时出现）；② **输入子类型**（`--input-subtype`，含 `raster-max-dim` 等）；③ **输出类型**（`--output-type`，含 `target-crs`、输出侧 `coord-system`、`output-unit`、`mesh-spacing`、`texture-format`、`obj-fp-type` 等）；④ **输出子类型**（`--output-subtype`，含 `tile-levels` 与 GIS→模型高级项）。各段内控件随当前四元选择 **动态显隐与重排**（`LayoutConvertMidColumn`）；空段的分组框隐藏。中间参数区支持 **垂直滚动条 + 鼠标滚轮**，用于容纳 GIS→模型高级参数。**命令预览**（`BuildConvertCommandLine`）：文本框**第一行**（无前置说明）即为 `AssembleConvertProcessCommandLine` 与「开始转换」完全相同的**单行可执行命令**；其下为简短说明，再为「路径 + 【1】–【4】」分项（与 `--help` 分块及中间列 ①–④ 一致）。**「复制命令」** 仅复制该首行（与进程实际参数一致；`raster-max-dim` 为 0、`tile-levels` 为 `auto` 等与后端默认值等价时省略）。未传选项由 `convert_backend_common` 的 `ParseConvertArgs` 默认填充（如 `coord-system`→`projected`、`vector-mode`→`geometry` 等）。**布局**：右侧「开始转换」为**两倍高度**；其左侧一列为「复制命令」与**正下方**「复制输出」（与命令预览区顶端对齐）。  
- **窗口行为**：主窗口创建尺寸约 **1280×800**、数据转换窗口约 **920×620**（均为含边框的窗口尺寸），在**当前显示器工作区**内居中；**不**默认最大化；`ShowWindow` 使用启动传入的 `nCmdShow`（主窗）。再次打开已存在的转换窗时仅 `SW_SHOW`，保留用户上次位置与大小。**模型 / 3D Tiles / 瓦片栅格预览**（约 **960×720**）与**日志**弹窗（约 **640×420**）在首次创建时同样按工作区居中；**数据驱动说明**弹窗相对 owner 客户区居中（已有逻辑）。  
- **支持类型**：GIS / 模型 / 瓦片。**模型** 子类型统一为 **Mesh、点云（LAS/LAZ）**。跨主类型仍为 6 条路径；**同主类型「模型→模型」** 在 UI 允许 **Mesh ↔ 点云** 组合，由第 7 个后端执行（子类型相同或其它组合将提示错误）。**TIN/DEM** 不再作为独立子类型，改由 `--gis-dem-interp` 与 `--gis-mesh-topology` 参数表达。
- **后端程序**：  
  - `agis_convert_gis_to_model.exe`
  - `agis_convert_gis_to_tile.exe`
  - `agis_convert_model_to_gis.exe`
  - `agis_convert_model_to_model.exe`（3DMesh ↔ 点云）
  - `agis_convert_model_to_tile.exe`
  - `agis_convert_tile_to_gis.exe`
  - `agis_convert_tile_to_model.exe`
- **CLI 帮助与参数语言约束**：7 个后端统一支持 `--help`/`-h`/`/?`；帮助文本按操作系统 UI 语言显示（中文系统显示中文，其它语言显示英文）。帮助信息包含**完整用法**（必填 `--input`/`--output`）、全部已支持开关、取值范围/默认值和示例。命令行**仅支持英文/ASCII 开关与非路径参数值**（例如 `--coord-system projected`、`--output-unit km`）；中文开关/中文非路径参数值将返回参数错误。**四类轴参数在 `--help` 中固定分块**，标题与桌面一致：**中文**为 **【1 输入类型】…【4 输出子类型】**（并括号注明对应中间列 ①–④ 与开关名）；**英文**为 **[1]…[4]** 并附 `desktop group ①…④` 说明；每块内先列对应 `--input-type` / `--input-subtype` / `--output-type` / `--output-subtype` 及一句话语义，再附本工具在该轴上的取值说明（由 `convert_backend_common::PrintConvertCliHelpGrouped` 统一排版；其它选项块不变）。
- **入口执行模型**：每个 `src/tools/agis_convert_*.cpp` 自带本工具 help 正文（含**简介**、用法、必填、通用/其它选项等），并通过 `PrintConvertCliHelpGrouped` 插入上述四类分组；help 末尾由 `PrintConvertCliIoSection` 追加 **【输入/输出文件（路径形态与扩展名）】**。`agis_convert_gis_to_model`：**默认 OBJ**；`--output-subtype pointcloud` 或输出为 `.las`/`.laz` 时写出 **LAS/LAZ**（内部先写临时 OBJ+albedo，再调用与 **模型→点云** 相同的 `ConvertMeshObjToLasJob` 按 UV 与贴图像素采样）。非点云模式下误用 `.las`/`.laz` 仍会改为 `.obj`。参数通过校验后**直接调用**对应转换函数；`convert_backend_common` 已移除统一 `RunConversion` 模式分发入口。  
- **help 模板一致性**：7 个工具使用统一结构（中/英自动切换）：`Usage/用法`、`Required/必填`、四类轴分组、其它可选开关与示例命令，避免不同工具输出风格不一致。
- **命令行预览**：输入/输出类型、子类型、路径与适用中间参数变化时实时更新；多行只读文本**第一行即为完整单行命令**（无前导说明）；「复制命令」仅复制该行。
- **GIS→模型高级参数（UI 已接入）**：在转换窗中当且仅当 `GIS -> 模型` 组合成立时，`④ 输出子类型` 分组下显示并参与命令拼装：`--obj-texture-mode`（color/pbr）、`--obj-visual-effect`（none/night/snow）+ `--obj-snow-scale`、`--gis-dem-interp`、`--gis-mesh-topology`、`--model-budget-mode`（memory/file/vram）、`--model-budget-mb`（默认 4096）。
- **中间参数区滚动行为（修正）**：滚轮仅在中间参数列可见区域内生效，避免输入/输出列滚轮误触导致的“整窗错乱感”；分组标题文案改为简洁的“输入选项/输出选项”风格，降低阅读噪音。
- **转换进度增强（已实现）**：转换窗进度区除主百分比外，新增细节行展示：阶段名、文件序号（当前/总数）、当前 I/O 动作、当前文件路径、已用时、估算剩余时长、预计结束时间。后端通过可解析日志行 `PROGRESS_META` 上报阶段与文件信息，前端实时解析并渲染。
- **进度时间一致性（已增强）**：GIS→模型在重循环（规则网格采样、顶点/UV 写出、三角面生成）中追加区间型平滑进度上报，减少“前快后慢/跳变大”的体感；UI 侧保留单调不回退策略。
- **窗口控件调试模式（新增）**：任意窗口按 **`Ctrl + Shift + I`** 进入/退出控件调试模式。模式下可鼠标单击选中控件、`Ctrl+单击` 多选、拖拽框选；辅助层在控件外框左上角显示控件名（优先 `IDC_*` 变量名映射）。按 **`Ctrl + C`** 复制当前选中控件名列表，按 **`Esc`** 退出模式。实现为线程级消息钩子 + 顶层透明辅助层，不改动业务控件逻辑。
- **数据转换独立程序（新增）**：新增独立可执行文件 **`AGIS-Convert.exe`**（CMake target: `agis_convert_gui`），直接启动数据转换窗口，不依赖主框架窗口。转换窗关闭后若无主窗上下文会自动 `PostQuitMessage` 退出进程。独立程序中的“内置预览”入口改为调用系统默认程序打开文件/目录，以避免引入完整主程序预览子系统依赖。
- **独立运行脚本（新增）**：仓库根新增 `run_conv.py`，行为与 `run.py` 一致（先调用 `build.py`，再查找并启动 `AGIS-Convert.exe`），用于单独运行数据转换程序。
- **输入区分组（调整）**：数据转换窗口左列“输入”相关控件（输入类型/子类型/路径/浏览/预览/信息及帮助按钮）纳入统一输入分组框 `IDC_CONV_GRP_INPUT_PANEL`，并新增输入标题控件 `IDC_CONV_INPUT_TITLE`，便于调试模式与可视层级保持一致。
- **转换窗父子层级重构（调整）**：数据转换窗口新增容器面板 `IDC_CONV_PANEL_INPUT / IDC_CONV_PANEL_MID / IDC_CONV_PANEL_OUTPUT / IDC_CONV_PANEL_TASK / IDC_CONV_PANEL_LOG`，并将对应控件重挂到各自容器内（`SetParent` + 保位重挂）。中间参数区（含四段组框与 `IDC_CONV_MID_SCROLL`）改为容器内相对坐标布局，日志区与任务区完全分离，减少折叠/滚动/缩放时跨区遮挡。
- **GIS→模型贴图后缀与编码一致（修正）**：`_albedo/_normal/_roughness/_metallic/_ao` 文件名后缀与 `--texture-format` 一致（如 `.jpg/.tif/.ktx2`），不再固定 `.png`；PBR 灰度图会按目标贴图格式统一写出（通过 RGB 等值编码兼容）。
- **GIS→模型预算分片保守化（修正）**：`--model-budget-mode file --model-budget-mb N` 新增最坏分片估算与保守系数，分片数会继续增长直到单分片估算低于预算阈值（或达到可分上限），降低“设置 100MB 仍产出 200MB”概率。
- **输出日志复制**：数据转换窗口提供「复制输出」按钮，可将输出日志框完整内容复制到剪贴板，便于问题上报与复现。
- **界面文案与排版**：转换窗口统一中文术语（如“输入数据/输出数据”“命令预览”“开始转换”），并优化中间参数区与底部命令区间距，提升信息层次与可读性。
- **参数禁用反馈（已实现）**：对当前输入/输出类型不适用的中间列参数，`EnableWindow(FALSE)` 禁用控件；其配对 Static 标签在**基准文案**后追加括号短句说明原因，并通过 `WM_CTLCOLORSTATIC` 使用较浅前景色（亮/暗主题各一套）与可用项区分；重新适用时恢复纯基准文案与正常颜色，避免括号累积。实现：`main_convert.cpp` 中 `SetConvertParamLabel`、`g_convertMutedParamLabelIds` 与 `UpdateConvertEditableControlStates`。
- **前端到后端参数映射**：转换 UI 的下拉项可显示中文说明，但生成 CLI 时会映射为后端可识别的英文 token（如 `gis/model/tile`、`3dmesh/pointcloud/auto`），避免触发“非路径参数必须英文/ASCII”的参数校验失败。
- **GIS→Tile 协议切片输出（已实现）**：`agis_convert_gis_to_tile` 支持 `--output-subtype xyz|tms|wmts|mbtiles|gpkg|3dtiles`。其中 `xyz/tms/wmts` 输出真实图片瓦片（扩展名由 `--texture-format` 决定，默认 `png`；**`ktx2` / `ktx2-etc1s`** 输出 **`.ktx2`**，在 **`AGIS_HAVE_BASISU`** 下经 **Basis Universal** 编码并含 **mipmap**；Zstd 打开时附加 **KTX2 超压缩** 标志），目录形态 `/{z}/{x}/{y}.{ext}`：`xyz` 直接使用 `y_xyz`，并生成 `tilejson.json`；`tms` 使用翻转行号 `y_tms = 2^z - 1 - y_xyz`，并生成 `tms.xml`；`wmts` 落盘采用与 `xyz` 兼容目录并额外输出 `wmts_capabilities.xml`，其中包含标准命名空间、`Layer`、`TileMatrixSetLink`、`ResourceURL` 以及按缩放级别展开的完整 `TileMatrixSet/TileMatrix`（`ScaleDenominator`、`TopLeftCorner`、`TileWidth/TileHeight`、`MatrixWidth/MatrixHeight`）。瓦片写出流程在运行时显式执行 GDAL 驱动注册；若图片落盘失败，会输出失败文件路径与返回码，便于定位 `code=7` 类问题。`mbtiles/gpkg` 改为单文件容器输出（分别 `.mbtiles/.gpkg`），通过 GDAL 驱动直接写入，容器内保留地理参考（EPSG:3857）与影像内容。**`--output` 解析**：路径为**已存在目录**时在其下写入默认 `tiles.mbtiles`/`tiles.gpkg`；为**非目录**且**无扩展名**时视为单文件主名并自动追加 `.mbtiles`/`.gpkg`（不得再因“无扩展名”误当作子目录而生成 `路径\\tiles.*`）。**MBTiles**：`GDALCreate` 仅使用驱动支持的选项（如 `TILE_FORMAT`、`BOUNDS`）；`SetGeoTransform` 必须为全球 Web Mercator 标准分辨率（与驱动按 `256·2^z` 像素推算的 zoom 一致），不得使用数据局部范围像素尺寸，否则会出现 “Could not find an appropriate zoom level…” / “georeferencing not set” 且写入失败；**勿**传入 GDAL MBTiles 驱动不支持的 `MINZOOM`/`MAXZOOM` 创建项（会告警且无效）。**`mbtiles` 要求 GDAL 构建启用 MBTiles 格式**（捆绑路径下由 `gis-desktop-win32/cmake/AGISBundledGDAL.cmake` 在链接 `agis_sqlite3` 时强制 `GDAL_ENABLE_DRIVER_MBTILES` 及 OGR GPKG/MVT），否则运行期 `GDALGetDriverByName("MBTiles")` 为空并出现 `[ERROR] GDAL driver not available: mbtiles`，需重新 CMake 配置并重编 GDAL 库。`3dtiles` 输出 `tileset.json + root.b3dm`：`root.b3dm` 内嵌 **glTF 2.0** 规则网格地形（POSITION/NORMAL/COLOR_0）， geographic 范围同瓦片 bounds；**输入栅格为 1–2 波段**时按 `TryReadRaster` 的 elev 采样作为 **WGS84 椭球高**（DEM）；**≥3 波段（RGB 等）或无数栅**时高程恒为 **0**（椭球基准面）。顶点色从 RGB 缓冲双线性采样（若存在）。模型坐标为 **瓦片中心 RTC** 下的局部 **ENU**、glTF **Y-up**，`tileset` 的 `region` 高度域按 DEM min/max 加边距写出。`tilejson.json` 的 `bounds` 与 `tms.xml` 的 `BoundingBox/Origin` 优先按输入栅格真实范围计算（含 CRS 转换），缺失时回退 `.gis` 视口，再回退全局默认范围。
- **Tile 层数控制（新增）**：`--tile-levels <auto|1..23>`。默认 `auto`：按输入原始栅格最小边自动计算最大有意义层，保证输出最高层不会超过原图最小像素分辨率（避免无效上采样）；也支持人工强制指定总层数（含 `z=0`）。
- **输入/输出信息框（仅数据摘要）**：路径编辑框下方的多行只读区**只反映路径与数据/文件系统信息**，**不重复**中间列转换设置（CRS、Mesh、贴图格式等）。**输入侧**：显示当前路径；若为 `.gis` 则解析工程摘要；否则在 **启用 GDAL** 时对已选路径做数据集摘要（栅格尺寸/波段/投影前缀、矢量图层名等）。**输出侧**：显示计划输出路径；若路径已存在则标注为目录或文件，并对已有文件给出字节大小；尚不存在时注明将由转换创建。**浏览按钮**：主类型为 **瓦片** 且子类型为 **MBTiles / GPKG Tiles** 时使用**单文件**打开/另存为（带对应扩展名过滤器）；其它瓦片子类型使用**目录**选择。**GIS 输入**对话框提供 `.gis` / 栅格 `.tif` / 矢量 `.shp;.geojson` / `.gpkg` 等分组过滤器；**模型输入**提供 `.obj` / `.las` / `.laz`。**模型输出**：子类型为**点云**时过滤器为 LAS/LAZ（含 **GIS→模型** 选点云）；**Mesh** 为 OBJ。**GIS 输出**提供 `.gis` / `.geojson` / `.tif` 等过滤器。  
- **OBJ 三角剔除**：写出 OBJ 时仅跳过边长或面积为 **非有限数**、或 **边长平方 maxE≤0** 的退化三角；**不再**用「面积平方与 maxE 的相对阈值」剔除近乎共面的规则网格三角，否则 DEM/TIN 会出现大面积「破碎三角/镂空洞」。
- **高程/水平比（1=1:1）**：中间列「转换设置」下方提供编辑框，并生成 CLI 参数 `--elev-horiz-ratio`。语义：**平面上一米与高程上一米同比例**（1 为 1:1；大于 1 表示竖向相对夸大）。后端将栅格平面坐标转为以像元中心为原点的 **本地水平米**（经纬度像元用近似米制换算；已投影米坐标则平移减中心），高程在 **`z_min` 基准上**按该系数缩放，并写入 OBJ；**3D 预览不再单独提供 Z 夸大**。
- **目标投影与统一 CRS**：中间列提供 **「目标投影」** 编辑框，对应 CLI `--target-crs`（`EPSG:xxxx` 或 GDAL 可识别的 WKT/Proj 串）。**留空为自动**：优先首栅格 SRS，否则首矢量图层 SRS，最后回退 **EPSG:4326**。启用 GDAL 时，**栅格顶点与矢量要素**均变换到该目标投影后再写入 OBJ（无 SRS 的矢量图层按 WGS84 假定）；**烘焙贴图**（`bake_texture`）在 **栅格原生 CRS** 下将矢量范围映射到像元，保证与 DEM 对齐。**等比例单位**：若目标为**地理坐标**（度），写出 OBJ 前将相对像元中心/目标中心的 **经度、纬度差** 折为 **本地近似地面米**（与「栅格无 GDAL 变换、像元为度」分支同一套近似），再与 **相对 `z_min` 的高程（米）** 一起乘以 **`output-unit`**，使 **OBJ 坐标中 X、Y、Z 的一个单位代表同一物理长度**；若目标为**投影坐标**（米、英尺等），平面平移量保持 CRS 线单位，与高程米一同缩放，同样满足等比例模型单位语义（注：极少数数据若高程与平面线单位不一致，为数据/CRS 问题）。
- **模型单位（比例尺）**：下拉 **米（m）/ 千米（km）/ 千千米（1000 km）**，对应 `--output-unit`（`m` | `km` | `1000km`）。对 **相对中心** 的平面坐标（地理目标下为近似米，投影目标下为 CRS 米等）与 **相对 `z_min` 的高程（米）** 整体乘以 **1 / 1000 / 10⁶**，使 OBJ 中 **1 个坐标单位**分别表示 **1 m / 1 km / 1000 km**（**X、Y、Z 同策略**，等比例）。
- **Mesh 间距（模型单位）**：模型单位下拉下方提供正整数编辑框，默认 **1**，对应 CLI `--mesh-spacing`（范围 **1–1 000 000**，非法或空回退 1）。语义：先按「Mesh 间距 × 当前模型单位对应的米」得到**请求步长**；**实际地面步长**取 **max(请求步长, 当前读入光栅一像元的地面较短边（米）)**；下采样读入时按 **缩聚后一像元** 所覆盖的地面尺寸计，避免网格比数据源分辨率更密。规则网格满足 **(ni−1):(nj−1)=2:1**（X 向格段数为 Y 的 2 倍），以便与 **2:1 展示宽高比** 对齐。**地理坐标目标**下包络折成米；**投影目标**下用同一米步长换为 CRS 线单位；**cecf** 或无 GDAL / 无变换时仍走像元网格（无强制 2:1 格网）。网格顶点数 **2–4096**；OBJ 注释含 `# mesh_step_meters`、`# display_xy_scale`（将包络纵横比 **非破坏** 缩放到约 **2:1**，仅 XY，Z 不变）。**模型→点云**：该间距同时作为 **贴图像素采样步长**（至少 1 像素）。
- **栅格读入最大边（像素）**：中间列提供编辑框（默认 **0**），CLI `--raster-max-dim`。**`0` 或留空（省略参数）**：按源图 **原始宽高** 读入，**不降采样**（不打印 `[RASTER] downsample read buffer`）。**`64–16384`**：限制读入内参缓冲的 **长边**，超限则整体等比缩小后再 `GDALRasterIO`（日志 `[RASTER] downsample read buffer: …`），用于内存紧张或大图试跑。非法非零值 CLI 报错；UI 非法输入按 **0** 处理。**规则网格 + 非 cecf**：`_albedo`/PBR 使用 **读入缓冲区分辨率**（经 2:1 重采样），**UV 按目标 CRS→栅格 CRS 反算像元**；全分辨率读入时贴图细节与源栅格一致。**cecf** 或无法建立该变换时，仍用 **网格 ni×nj** 的贴图与归一化网格 UV，避免错位。GIS→Tile 主读入与 GIS→模型共用该参数（曾误在 Tile 路径硬编码上限，已与 CLI 一致）。
- **模型贴图格式**：中间列提供下拉，默认 **PNG**；CLI `--texture-format` 取值为 `png` | `tif` | `tga` | `bmp`，以及在 **`AGIS_HAVE_BASISU`** 时 **`ktx2`**（UASTC + mip + 超压）与 **`ktx2-etc1s`**。用于 **点云→模型** 时写出 `map_Kd` 指定扩展名的纹理；亦可被其它管线复用（实现见 `WriteRgbTextureFile` / `ktx2_basis_encode.cpp`）。**桌面瓦片栅格预览**对 **`.ktx2`** 的解码以当前实现为准（可能需外部查看器验证）。
- **OBJ 数值精度类型**：中间列提供下拉，默认 **double**；CLI `--obj-fp-type` 取值 `double` | `float`。作用于 OBJ 数值写出精度：`double` 以更高精度保存顶点/UV，`float` 以更紧凑文本输出，便于在体积与精度间按场景选择。
- **模型↔点云（`agis_convert_model_to_model`；GIS→点云复用）**：  
  - **3DMesh→点云**（及 **GIS→模型** 在 `pointcloud`/`.las`/`.laz` 模式下）：读取 **Wavefront OBJ**（三角面 + 可选 `vt`），`mtllib`/`map_Kd` 由 GDAL 读纹理（长边上限约 **2048**）；对每个三角在 UV 包围盒内按 **Mesh 间距** 步进像素，象素中心映射到 **UV 重心**，插值 **三维位置**，颜色取贴图像素；写出 **LAS 1.2 PDRF 2（RGB）**。**GIS→模型**点云路径在临时目录生成与网格模式一致的 OBJ+albedo 后调用同一 **`ConvertMeshObjToLasJob`**。为防止贴图×碎面导致 **内存与 `reserve` 失控**，实现侧对**单三角采样像素数**设上限（约 **40 万**，超限则对该三角自动加倍有效步长并打一次告警）、对**总点数**设上限（**8×10⁷**，超限提前结束并告警）；`pts.reserve` 不再按「整幅纹理×面数」预估。索引越界时跳过像素采样。**输出 `.laz`**：若构建编入 **bundled LASzip**（`AGIS_HAVE_LASZIP`），由 **`WriteLas12Pdrf2Laz`**（`laszip_open_writer` 压缩）写入真 LAZ；否则 **降为同路径 `.las`** 并 **`[WARN]`**。  
  - **点云→模型**：读取 **LAS**（PDRF 0/1/2/3；颜色以 2/3 为优先），在 XY 平面分桶成规则网格，空桶回填全局均值高程与灰色；写出 **OBJ 3.0 多边形子集** + `mtl` + **albedo 纹理**（格式由 **模型贴图格式** 决定）。**LAZ 输入**未支持（无解压），须先用外部工具转为 LAS。
- **模型→GIS（`agis_convert_model_to_gis`）**：读取 OBJ 顶点与三角面，计算模型包围盒/中心点/高程范围，输出 GeoJSON（`Polygon` 包围盒 + `Point` 中心）。属性字段含 `vertex_count`、`triangle_count`、`z_min`、`z_max`、`z_center` 与源路径；`--target-crs` 留空时写 `EPSG:4326`，否则写用户指定 CRS 名称。
- **模型→3DTiles（`agis_convert_model_to_tile`）**：若输入为 OBJ，则先走“模型→GIS”生成临时 GeoJSON，再复用 GIS→Tile 管线产出 `xyz/tms/wmts/mbtiles/gpkg/3dtiles`；若输入本身可按 GIS 栅格读取，则直接走 GIS→Tile。
- **Tile→GIS（`agis_convert_tile_to_gis`）**：从 `/{z}/{x}/{y}.{ext}` 目录自动识别瓦片，选取最高缩放级进行拼接，输出单幅 GeoTIFF（默认后缀 `.tif`）；用于把离散瓦片合成为可继续进入 GIS 管线的完整影像层。
- **3DTiles/Tile→模型（`agis_convert_tile_to_model`）**：从 `xyz` 目录识别瓦片并在每瓦片生成一个平面四边形，批量写出 OBJ；新增 `--tile-max-memory-mb`（64..131072，默认 512），按内存预算自动拆分为多个 `tile_model_*.obj` 批次文件，避免一次性合并导致内存峰值过高。
- **纹理 PNG 宽高比**：`_albedo`、由此生成的 `_normal` / `_roughness` / `_metallic` / `_ao` 在写出前双线性重采样为 **宽:高 = 2:1** 像素（高取不小于原高的 **2**，宽 = **2×高**，宽上限 8192）。UV 仍为 0–1 铺满模型，与 OBJ 2:1 展示一致。
- **路径与预览**：输入/输出均有「浏览」「预览」；预览菜单支持「内置预览（默认）」「系统默认打开」「选择其他应用」。**内置预览**按当前侧所选 **数据大类** 分流：**GIS**→仅提示在 **主程序工程/图层** 中打开；**模型**→**`*.obj` / `*.las` / `*.laz`** 进入 **`OpenModelPreviewWindow`**（规则同 2.8）；**瓦片**→若当前转换 **子类型为 `3dtiles`**：走 **`OpenModelPreviewWindow3DTiles`**，`src/app/preview/tiles_gltf_loader.cpp` 以 **`tinygltf` + `nlohmann/json`（`../3rdparty/`，不使用 vcpkg）** 解析 **本地** **`tileset.json`** 树中的 **`b3dm`/`i3dm`(glb)/`glb`/`cmpt`/`pnts`**，合并几何后进入 **同一 bgfx 模型预览**（与 OBJ 交互一致）。**KHR_draco_mesh_compression**：依赖 **`../3rdparty/draco`**（由 Release 归档放置，见 **`3rdparty/README-DRACO.md`**；**`cmake/AGISBundledDraco.cmake`** + **`TINYGLTF_ENABLE_DRACO`**），由 tinygltf 解压后再走三角网路径。**pnts**：解析 Feature Table 中 **FLOAT `POSITION`** 或 **`POSITION_QUANTIZED`**（及可选 **RGB/RGBA**），点展开为小四边形；超大点集按面预算抽稀。仍 **不支持** 仅 **http(s)** `content.uri` 等外链。若子类型为 **其它瓦片**：**`TilePreviewWndProc`**——有 **`z/x/y` 栅格**则四叉拼图；否则若有 **`tileset.json`** 则仅 **元数据/统计面板**（不绘制 3D 网格）。**cesium-native** 见 **`3rdparty/README-CESIUM-NATIVE.md`**，主程序不链接。**WMTS / MBTiles / GPKG** 等边界写入日志。  
- **瓦片预览（内置）**：**定位**为 **仅本地路径**（本机文件夹/文件）；**不实现** **HTTP(S)、WMTS、XYZ URL** 等网络服务拉取。**「Open…」** 先弹出协议对话框（**标题栏**显示「本地瓦片预览 — 选择数据源类型」类文案）：**左侧**为数据源类型简称列表，**右侧**只读区展示该类型的打开步骤与文件/格式说明（随列表选择切换）。列表含 **本地 XYZ/TMS 根目录、本地 `tileset.json`（3D Tiles 元数据）、`*.mbtiles`、`*.gpkg`（后二者需 **GDAL**）**；**不含**「单张栅格」项（单图仍可通过拖放或命令行打开）。索引扫描上限约 **12000**。**已实现**：**XYZ 目录**四叉拼图与交互；**TMS** 目录在检测到 **`tms.xml`** 或 **`README.txt`** 含 **`protocol=tms`**（与 `agis_convert_gis_to_tile` 写出一致）时，将磁盘行号文件映射为 **XYZ 显示**（北向上）。**缩放**：滚轮 **切换磁盘上的 Z 级**（一级一屏瓦片），**光标下归一化位置对齐**；**Shift+滚轮** 以视口中心换级；**Ctrl+滚轮** 仅调 **每瓦片像素边长**（默认约 **256px**/瓦片）。**内存**：已解码瓦片 **LRU**（约 **160** 块），**换 Z 时丢弃非当前层位图缓存**。**单文件 `*.mbtiles` / `*.gpkg`**：在 **`AGIS_USE_GDAL=ON`** 时由 **GDAL 栅格**下采样缩略预览（全球墨卡托拼图，非 z/x/y 交互）。**绘制**：`WM_ERASEBKGND` 跳过默认擦除、`WM_PAINT` 内存位图 **BitBlt**。**非 3dtiles** 下 **3D Tiles** 仍为 **元数据模式**。**另**：子类型 **`3dtiles`** 的 **三维网格** 在 **模型预览窗**。**Slippy 投影语义**：**输入源**为标准 XYZ 瓦片纹理 **EPSG:3857**；**算法**（PROJ 与重采样）将「显示几何上的采样点」变换到该 CRS 读纹素（实现路径为逐像素 **屏→显示→3857→纹理**）；**显示用投影/几何**由选项中单选（`TileSlippyProjection`）决定，**不改写**磁盘 CRS；最后 **合成到屏幕像素**。**Slippy 各显示模式**下，橙色片元 HUD 均为 **宽:高 = 2:1** 的轴对齐矩形（居中且包住该片元在屏上的外接框；底图拼贴/PROJ 拓扑不变）。场景 JSON **`projection.pipeline`** 含 `conceptualFlowZh`（语义顺序与采样实现顺序）；**等经纬 / 等积圆柱** 在地图客户区外层（导出 JSON 字段 **`fullImageArea`**）内对绘制区 **`imageArea`** 做 **letterbox**，目标宽高比为视口四角 WGS84 包络的 **Δlon:Δlat**（近似 **1°经≈1°纬** 像素比；**Web 墨卡托网格**仍为整区填充）；地理片元视窗 **`world`** 仍按整区像素计算，屏上映射落在 **`imagePixels`** 内。**`schemaVersion` 11** 起与实现一致。**未实现**：MBTiles/GPKG 内按 zoom 交互拼瓦片、全规格 3D Tiles 语义（如全部量化/批次属性变体）。
- **OBJ 头注释中的范围**：历史字段 `# extent` 易误解为栅格范围，实为 `.gis` 内 `<display>` 保存的 **2D 地图视口**（经纬度视窗）。现改为：有 `.gis` 时输出 `# gis_document_viewport（…）`；有栅格时另输出 `# raster_bbox_native（…）`，由 **GeoTransform** 四角包络得到、单位为 **栅格数据源 CRS**（全球 WGS84 栅格应接近经度 ±180°、纬度 ±90°）。若视口数值异常，应在主程序中 **适配视图/重存 .gis**，勿与 TIFF 真实范围混淆。
- **OBJ 文件格式版本**：GIS→模型 管线写出 ASCII `.obj` 时，在文件首条注释声明 **`Wavefront OBJ 3.0`**（Alias/Wavefront 附录，3.0 取代 2.11）。实际使用 **多边形子集**：`v`、`vt`、`vn`、`f v/vt/vn`、`mtllib`、`usemtl`、`o`；**不**写出已废弃的 2.11 关键字（如 `bsp`/`bzp`），也**不**写出自由曲面体（`curv`/`surf` 等）。实现：`convert_backend_common.cpp` 中 `kObjFileFormatBanner30`。
- **输出路径**：GIS→模型与瓦片→模型 **仅生成 OBJ**，若 `--output` 误用 **`.las` / `.laz`**（单文件目标），实现会 **强制改为 `.obj`** 并打 `[WARN]`，避免生成「扩展名为点云、内容为 OBJ」的误导性文件（否则点云预览会报非 LASF 头）。
- **执行流程**：点击执行后按当前组合选择对应 EXE，构建命令行并创建子进程，等待退出码并写入日志。
- **执行流程（非阻塞）**：点击执行后以后台进程启动后端转换，UI 线程不阻塞；通过定时轮询进程状态更新进度与最终退出码。为避免日志洪峰卡顿，单个轮询周期对管道读取设置分块与总量上限（分块读取、单次最大排空量），下一周期继续续读，保证“及时输出”与“界面不卡死”兼顾。
- **进度显示（防倒退 + 真实上报）**：进度条采用“单调递增”策略，禁止回退；后端可输出标准标记 `"[PROGRESS N] 文本"`，前端解析后直接落到真实进度与阶段文案。未收到标记时仅做保守递增补间，避免“来回跳动/倒退”。
- **控制台编码兼容（已实现）**：转换日志管道读取新增 UTF-16LE 检测与解码，并清理内嵌 `NUL`，避免 `wcout` 输出被截断导致 `CECF-DEBUG`/进度文本只显示前缀。
- **命令行输出回显**：后端进程 `stdout/stderr` 通过管道实时回流到下方日志输出框；轮询读取改为“单次触发内持续读到管道为空”，并在工具入口启用无缓冲输出，降低日志显示延迟；状态栏同步显示百分比进度文本。

### 2.8 模型预览（已实现）

- **触发**：数据转换窗口点击输入/输出「预览」并选「内置预览（默认）」：当前侧为 **模型数据** 时，单文件 **`*.obj`**、**`*.las`**（LAS 1.2，与后端读取规则一致）或 **`*.laz`**（**优先** `ReadLazPointsLaszip`（`AGIS_HAVE_LASZIP`，静态链 `laszip` 核心库）；**失败则回退** `ReadLazPointsPreviewGdal`（`GDALOpenEx` 矢量，字段 `Color Red/Green/Blue` 等着色，无则灰））进入同一内置 3D 预览窗口；若为 **目录**，则递归搜索 **首个 `*.obj`，其次 `*.las`，再次 `*.laz`**（有扫描上限）。当前侧为 **瓦片数据** 且子类型 **`3dtiles`** 时，进入 **`OpenModelPreviewWindow3DTiles`**（`g_pendingPreviewLoadAs3DTiles`）；其它瓦片子类型进入 **瓦片采样预览**（见上节）。  
- **点云预览（LAS/LAZ，已实现）**：后台线程读取点记录；LAS **PDRF 2/3** 使用 16 位 RGB。**LAZ**：若编译期检测到 **`../3rdparty/LASzip`** 并定义 **`AGIS_HAVE_LASZIP=1`**，应用 **LASzip C API**（`laszip_open_reader` / `laszip_read_point` / `laszip_get_coordinates` 等）解压并取色（**PDRF 2/3/5/7** 的 `rgb`）；**否则**（或 LASzip 打开失败时）再走 **GDAL 矢量** 路径，此时依赖 GDAL 对 LAZ 的驱动与编译期 LASzip/LASlib 配置，失败时对话框附带 **CPL 原文**或 **LASzip 错误串**（`lazPreviewDiag`；实现：`preview/main_model_preview.cpp`）。为在现有 bgfx 着色器下 **稳定表达“约 N 像素”的点大小**（避免 `PT_POINTS` 在 D3D/驱动上多为 1px 的不确定性），对每个源点生成 **XY 平面上的正方形点斑（2 个三角，语义上等价于点精灵，非 GPU Point List 原语）**；ImGui 提供 **「点大小(像素)」** 滑条（默认 **5**），变更时以内存中的源点缓存重建网格并 `agis_bgfx_preview_reload_model`。**源点数上限**约 **2500 万**，超限拒绝加载。**无 LASzip 源码且无 GDAL** 时 **`*.laz`** 内置预览不可用（提示安装源码或开 GDAL）。实现补充：`preview/model_preview_bgfx.h` 声明 **`agis_bgfx_preview_reload_model`**；**`cmake/AGISBundledLASzip.cmake`** 负责 `add_subdirectory(LASzip)` 静态库与主程序链接。   
- **LAZ 总点数（Peek/全量读）**：`PeekLazPointCountLaszip` 与 `ReadLazPointsLaszip` 从 **`laszip_header_struct`** 取总点数：`number_of_point_records` 非零则用其，否则 **`extended_number_of_point_records`**。**不得** 使用 `laszip_get_point_count` 作为文件点数（该 API 对应读写游标 `p_count`，reader 刚打开时常为 0，会误报「无法读取有效点数」）。  
- **渲染后端（强制）**：`gis-desktop-win32/CMakeLists.txt` 要求 **`AGIS_USE_BGFX` 恒为 ON**，传入 `-DAGIS_USE_BGFX=OFF` 将导致配置失败。预览使用 **bgfx** 统一渲染；窗口内下拉可切换 **Direct3D 11** 与 **OpenGL**（均为 bgfx 后端，应用不直接调用 D3D11/OpenGL 立即模式绘制预览网格）。实现见 `src/app/preview/model_preview_bgfx.cpp`；嵌入着色器来自上游 `bgfx` 示例 `common/debugdraw`（`vs_debugdraw_fill_texture` / `fs_debugdraw_fill_texture`）：顶点 **Position + TexCoord0 + Color0**，片元 `texture2D(s_texColor, …) * v_color0`，uniform **`u_modelViewProj`** 与采样器 **`s_texColor`**。`CMakeLists.txt` 将 `examples/common/debugdraw` 加入 include 路径以引用预编译 `.bin.h`。源码中 `#if !AGIS_USE_BGFX` 包裹的旧版 WGL/DX11 预览仅作历史参照，正式构建不会编译该分支。
- **交互**：左键拖拽旋转、滚轮缩放、重置视角、适配模型（Fit）。  
- **交互增强**：网格显示开关、贴图显示开关、贴图层选择（baseColor/normal/roughness/metallic/ao）。**坐标轴（X/Y/Z）** 在 **bgfx + ImGui** 路径下使用 **ImGui 前景 DrawList** 绘制在视口左下角，保证排在 D3D11/OpenGL 呈现结果之上；不再依赖易被交换链遮挡的 GDI 叠加。  
- **性能 HUD**：**FPS / 帧时 / CPU·GPU·DrawCall / 瓶颈中文说明 / 帧时 sparkline 曲线** 均在 **ImGui「Preview Controls」面板** 内连续更新（与隐藏的原 Win32 静态控件解耦）。  
- **显示信息**：Mesh 统计（顶点/面/法线/纹理坐标）、材质与贴图统计、文件体积、内存/显存估算、FPS；加载阶段完成后写入只读信息区。  
- **预览背景与刷新**：3D 视口默认背景为浅灰色；窗口定时触发重绘并采用防重入策略（仅 `InvalidateRect`，避免高频 `UpdateWindow` 强制同步重绘），保证 FPS/性能信息持续更新且减少渲染线程卡死风险。
- **默认视角**：首次打开采用“适配模型”视角（正视 + 自动放大），优先保证模型完整可见与方向直观。  
- **材质与贴图**：支持读取 `mtllib/newmtl/usemtl/Kd/map_Kd`，按面材质将 **Kd 乘到顶点颜色**；**OBJ 解析 `vt` 与面 `f` 的纹理索引**，将 UV 写入顶点流。若 **`map_Kd` 路径非空且可被 bimg 解码**，在 **bgfx 初始化**时创建 **2D 纹理**并绑定到 `s_texColor`（失败则回退 **1×1 白色** 纹理，仍保留 Kd 着色）。V 坐标按常见用法在着色前做 **`v_tex = 1 - v_obj`** 以贴合图像行序。提供「预览贴图」按钮在系统仍可按路径打开文件。  
- **近似 PBR 光照（当前实现）**：为提升默认质感，网格构建阶段按三角面法线计算简化的 **roughness/metallic + specular/fresnel** 光照系数，并写入顶点颜色；片元仍走 `baseColorTexture * v_color0` 路径。该实现无需新增着色器二进制，可在现有 bgfx 管线下获得较明显的金属/粗糙度观感差异。`P` 快捷键会切换“基础漫反射/近似 PBR”两种着色，并在切换时重建预览网格颜色缓冲，保证开关效果即时可见；后续若接入专用 PBR shader，可平滑替换该近似方案。  
- **PBR 贴图通道准备层（已接入）**：预览上下文支持一次性接收并加载 `baseColor/normal/roughness/metallic/ao` 路径集合；当前渲染默认仍以 `baseColor` 为主并保留近似 PBR 光照，其他通道先在运行时资源层持有，为后续专用 PBR shader 的多贴图采样直连做准备。  
- **PBR 实现（当前路径）**：在预览阶段启用一套可运行的 PBR BRDF 计算：`normal/roughness/metallic/ao` 在 CPU 侧按 UV 采样后参与 **Cook-Torrance（GGX NDF + Schlick Fresnel + Smith Geometry）**，并将结果烘入顶点色；`normal` 通过面法线构造的近似 TBN 参与法线扰动。`P` 快捷键会在基础着色与 PBR BRDF 之间切换，并触发网格颜色缓冲重建以即时生效。  
- **PBR 调试视图（已实现）**：支持运行时切换 `PBRLit / Albedo / Normal / Roughness / Metallic / AO` 六种视图（快捷键 `1~6`），用于验证各通道贴图是否正确读入与参与计算；切换视图会重建预览网格颜色缓冲并即时显示。  
- **渲染模式下拉（已实现）**：将 `PBR view` 与 `layer` 逻辑收敛为单一 `Render Output` 下拉；默认 `Full PBR`（开启完整 PBR）；当选择单层贴图视图（Albedo/Normal/Roughness/Metallic/AO）时，自动关闭 PBR 光照，仅显示目标层。
- **稳定性修正（黑斑抑制）**：针对无切线数据场景下的近似 TBN 抖动与 AO 过暗问题，PBR 计算增加了「几何法线与扰动法线混合」与 AO 下限约束，并提升环境光底值；用于减少局部黑斑与闪烁暗块，同时保持通道可视化能力。  
- **预览窗口 UI 形态（渲染内）**：预览窗口不再依赖顶部 Win32 传统控件，统一采用视口内渲染 HUD + 可点击按钮条（类 ImGui 交互形态）；按钮与快捷键保持同一套动作映射（渲染器切换、网格/贴图/PBR 开关、PBR 视图 1~6、图层切换、Fit/Reset）。  
- **背面剔除开关（已实现）**：预览窗口新增背面剔除开关，支持 HUD 按钮、快捷键 `B` 与 ImGui `Backface Culling` 复选框三处联动；开启后实体渲染使用背面剔除状态，便于检查闭合网格法线方向与内部面。
- **视口与交互校正（已实现）**：3D 视口改为近乎全客户区渲染（仅保留极小边距）；`Fit/Reset` 默认缩放与相机距离同步拉近；滚轮缩放倍率和上限提升（支持继续放大）；背景色固定为浅灰。  
- **比例尺与网格长度（已实现）**：HUD 与 ImGui 控件区显示当前缩放倍数（`xN`）、比例尺（`100px ≈ x 模型单位`）以及网格总长度（当前规则网格总长 `2.0` 模型单位）。
- **大模型保护（防假死）**：**不在主线程**做整文件 OBJ 预扫；窗口立即创建后由 **后台加载线程** 完成统计与解析。面片阈值改为**软阈值告警**（不再因阈值直接拒绝加载），优先保持“预览数据与真实文件一致”。  
- **窗口先开后载（已实现）**：3D 预览窗口先创建并立即可见，OBJ 在后台线程加载；加载期间在视口中央显示加载进度条与阶段文本（统计/解析/贴图分析），加载完成后再初始化渲染资源并切换到正常交互，避免“假死观感”。
- **加载进度精度（已升级）**：OBJ 解析阶段的百分比由文件读取位置驱动（`file_size + tellg`），进度条按真实读取进度更新，不再仅依赖固定阶段比例。
- **实体显示清洁度（已实现）**：实体模式下不再叠加线框边线，仅在线框模式显示边线，以减少“脏/发灰/斑驳”观感。  
- **几何比例（bgfx）**：顶点先平移到包围盒中心，再按 **`extent = max(ΔX,ΔY,ΔZ)` 等比缩放**，与 OBJ 顶点几何一致；**高程与水平比例在数据转换对话框中设置**，预览不提供单独的竖向夸大。  
- **CECF 网格预算（完整性优先）**：`cecf` 输出对网格面数施加预算上限（转换阶段直接限面），避免导出超大 OBJ 后在预览中被抽稀成“不连续小三角/看起来只出一部分地球”。
- **CECF 坐标修正（已实现）**：`cecf` 生成前对栅格/目标 CRS 坐标先转换到 `WGS84 lon/lat` 再做 ECEF；同时网格采样改为按 `fx/fy` 对原始栅格双线性取样（不再直接用缩减后 `w/h` 索引原栅格），修复“披萨扇面”与局部扇区输出问题。
- **CECF 调试日志（已实现）**：在 `cecf` 路径输出转换前后边界框：`source CRS bbox` 与 `WGS84 bbox`，并给出 `span(lon,lat)` 与 `coverage=GLOBAL_LIKE/REGIONAL_LIKE` 判定，便于快速判断输入是全球覆盖还是区域覆盖。
- **CECF 无栅格兜底修正（已实现）**：当 `.gis` 未命中可用栅格时，不再使用 `00-concept` 视口范围直接上球；改为优先从真实图层包围盒推导经纬范围（必要时先转 WGS84），若仍无法得到有效 lon/lat bbox 则直接报错中止，避免产出“披萨扇面”伪结果。
- **CECF 默认 CRS 规则（已实现）**：当 `coord-system=cecf` 且未显式传入 `--target-crs` 时，目标 CRS 强制默认为 `EPSG:4326 (WGS84)`，并输出命中日志，避免 `auto` 选到投影 CRS 造成球面畸变。
- **下采样地理映射修正（已实现）**：当栅格读入缓冲发生下采样时，顶点地理坐标计算使用源栅格维度（`srcW/srcH`）映射，而高程/纹理采样仍使用读缓冲维度（`w/h`）；修复了将下采样索引直接代入 GeoTransform 导致的“只覆盖北极扇区/披萨片”问题。
- **极区重叠抑制（已实现）**：`cecf` 在全球覆盖近似场景采用经度半像素偏移与纬度中心采样；同时移除“极区强制同一经度”的顶点写出路径，保持极圈经度连续，避免南北极生成同位重复三角面（叠片）。
- **CECF 外法线一致性（已实现）**：写 `f` 面片时按三角形法线与“地心→三角形中心”方向做同向判定；若朝内则自动翻转绕序，保证球体法线方向一致，修复局部“法线反了/开剔除后缺片”问题。
- **CECF 极区 UV 稳定（已实现）**：南北极行 `vt` 的 `u` 固定为 `0.5`（中经线），并将 `v` 从纹理边界内缩半像素（避免直接采样最边界行），消除经线收敛与边界像素导致的极区黑圈/多色条带。
- **CECF 极帽单顶点重建（已实现）**：`cecf` 网格在建面时不再直接使用最北/最南整圈参与四边形剖分；改为追加北极/南极两个单顶点，并以扇形连接到次极圈（`row=1` / `row=h-2`）。该策略可显著降低极区退化三角、重叠面与黑圈伪影。
- **Projected 网格法线方向统一（已实现）**：非 `cecf` 网格在写三角面时按 `+Z` 方向统一绕序（必要时交换顶点顺序），修复 `projected` 输出在预览中出现的“法线反向、明暗颠倒”。
- **预览法线方向自校正（已实现）**：3D 预览构网阶段增加方向自检：球形网格按“中心外向”统一法线；非球形网格若检测到主导法线朝下则自动翻转绕序，降低来源 OBJ 绕序不一致导致的“看起来法线反了”问题。
- **极区 PBR 抑噪（已实现）**：球形模型极区（`v` 接近 0/1）在预览中对 AO 与法线贴图采样做保护（抬高 AO 下限、法线回退中性），抑制南北极黑环/色带。
- **深度与叠加（bgfx）**：清屏含深度；实体三角列 **不启用背面剔除**（避免 OBJ 绕序差异导致整网不可见）；线框为折线列表叠加。  
- **性能与适配**：预览窗口使用定时器约 **33ms**（~30FPS）触发重绘，兼顾观测连续性与资源占用；模型面片默认优先完整显示，仅在超大规模面片时才启用抽稀，避免“残缺小三角”观感。OpenGL 后端默认关闭 VSync 以降低驱动侧卡顿风险。「适配模型」只调视角/缩放，不在 CPU 侧重复除 `extent`。运行时统计复用窗口创建时缓存的 OBJ 统计，避免周期性重扫模型文件造成额外 I/O 开销。

## 3. 消息与命令 ID（约定）

| ID | 值（建议） | 说明 |
|----|------------|------|
| ID_FILE_EXIT | 40001 | 退出 |
| ID_VIEW_MODE_2D | 40101 | 2D |
| ID_VIEW_MODE_3D | 40102 | 3D |
| ID_HELP_ABOUT | 40201 | 关于 |
| ID_LAYER_ADD | 40501 | 添加数据图层 |

实现可在 `src/app/resource.h` 中定义，与 `mapping.md` 同步。

## 4. 错误与日志

- **首版**：`OutputDebugString` + Log 对话框追加；不向 stderr 写（无控制台）。

## 5. 后续扩展（非首版阻塞）

- GDAL/OGR 动态加载、工程文件格式、真实 2D 绘制与 3D 设备上下文。
