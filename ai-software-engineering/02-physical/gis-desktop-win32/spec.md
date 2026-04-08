# gis-desktop-win32 物理规格

## 1. 构建与平台

- **语言**：C++17 及以上。  
- **生成器**：CMake 3.20+。  
- **平台**：Windows x64；`WIN32` 已定义；链接 `comctl32`（Common Controls v6）。  
- **子系统**：`WINDOWS`（非控制台）。
- **主程序源码（实现目录 `gis-desktop-win32/src/app/`，相对仓库根）**：`main.cpp`（`wWinMain`、`MainProc`、`BuildMenu`、`RegisterClasses`）；`main_globals.*`（顶层 `HWND` 与窗口类名字符串）；`main_chrome.cpp`（分割布局、工具栏、滚轮转发、视图/窗口菜单同步）；`main_panels.cpp`（图层面板、属性 Dock、日志窗）；`main_gis_xml.*`（`.gis` 片段解析辅助）；`main_gis_document.cpp`（读写 `.gis`、`GisNew`/`Open`/`Save`）；`main_convert.cpp`（数据转换 UI 与后端拉起）；`main_model_preview.cpp`（OBJ 预览与 bgfx）；`ui_font.cpp`（应用 UI 字体、日志等宽字体初始化/释放）；`ui_theme.cpp`（主题偏好、DWM/uxtheme、主客户区画刷及与侧栏主题同步）。

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
- **窗口结构**：上半区三列（输入 / 设置 / 输出），下半区命令行、多段进度与日志。  
- **支持类型**：GIS / 模型 / 瓦片。**模型** 子类型含 **TIN、DEM、3DMesh、点云（LAS/LAZ）**。跨主类型仍为 6 条路径；**同主类型「模型→模型」** 在 UI 允许 **3DMesh ↔ 点云** 组合，由第 7 个后端执行（子类型相同或其它组合将提示错误）。
- **后端程序**：  
  - `agis_convert_gis_to_model.exe`
  - `agis_convert_gis_to_tile.exe`
  - `agis_convert_model_to_gis.exe`
  - `agis_convert_model_to_model.exe`（3DMesh ↔ 点云）
  - `agis_convert_model_to_tile.exe`
  - `agis_convert_tile_to_gis.exe`
  - `agis_convert_tile_to_model.exe`
- **命令行预览**：输入/输出类型、子类型、路径变化时实时更新；控件为多行只读文本，支持复制。
- **输入/输出信息框（GIS 摘要）**：三列顶部「输入/输出」下的多行说明除类型模板外，随路径与中间区参数刷新。**输入侧**：若扩展名为 `.gis`，解析 `display` 的 `projection` / `showGrid` / 视口四角与 `<layer>` 的 `name`/`driver`/`source`（摘要列表，截断长路径）；其它已选文件在 **启用 GDAL** 时尝试 `GDALOpenEx` 显示栅格尺寸、波段数、数据集 `ProjectionRef` 前缀及矢量图层名列表（失败则提示）。**输出侧**：同步显示目标 CRS、模型坐标语义、矢量策略、输出单位、Mesh 间距、高程/水平比及输出路径。
- **高程/水平比（1=1:1）**：中间列「转换设置」下方提供编辑框，并生成 CLI 参数 `--elev-horiz-ratio`。语义：**平面上一米与高程上一米同比例**（1 为 1:1；大于 1 表示竖向相对夸大）。后端将栅格平面坐标转为以像元中心为原点的 **本地水平米**（经纬度像元用近似米制换算；已投影米坐标则平移减中心），高程在 **`z_min` 基准上**按该系数缩放，并写入 OBJ；**3D 预览不再单独提供 Z 夸大**。
- **目标投影与统一 CRS**：中间列提供 **「目标投影」** 编辑框，对应 CLI `--target-crs`（`EPSG:xxxx` 或 GDAL 可识别的 WKT/Proj 串）。**留空为自动**：优先首栅格 SRS，否则首矢量图层 SRS，最后回退 **EPSG:4326**。启用 GDAL 时，**栅格顶点与矢量要素**均变换到该目标投影后再写入 OBJ（无 SRS 的矢量图层按 WGS84 假定）；**烘焙贴图**（`bake_texture`）在 **栅格原生 CRS** 下将矢量范围映射到像元，保证与 DEM 对齐。**等比例单位**：若目标为**地理坐标**（度），写出 OBJ 前将相对像元中心/目标中心的 **经度、纬度差** 折为 **本地近似地面米**（与「栅格无 GDAL 变换、像元为度」分支同一套近似），再与 **相对 `z_min` 的高程（米）** 一起乘以 **`output-unit`**，使 **OBJ 坐标中 X、Y、Z 的一个单位代表同一物理长度**；若目标为**投影坐标**（米、英尺等），平面平移量保持 CRS 线单位，与高程米一同缩放，同样满足等比例模型单位语义（注：极少数数据若高程与平面线单位不一致，为数据/CRS 问题）。
- **模型单位（比例尺）**：下拉 **米（m）/ 千米（km）/ 千千米（1000 km）**，对应 `--output-unit`（`m` | `km` | `1000km`）。对 **相对中心** 的平面坐标（地理目标下为近似米，投影目标下为 CRS 米等）与 **相对 `z_min` 的高程（米）** 整体乘以 **1 / 1000 / 10⁶**，使 OBJ 中 **1 个坐标单位**分别表示 **1 m / 1 km / 1000 km**（**X、Y、Z 同策略**，等比例）。
- **Mesh 间距（模型单位）**：模型单位下拉下方提供正整数编辑框，默认 **1**，对应 CLI `--mesh-spacing`（范围 **1–1 000 000**，非法或空回退 1）。语义：先按「Mesh 间距 × 当前模型单位对应的米」得到**请求步长**；**实际地面步长**取 **max(请求步长, 当前读入光栅一像元的地面较短边（米）)**；下采样读入时按 **缩聚后一像元** 所覆盖的地面尺寸计，避免网格比数据源分辨率更密。规则网格满足 **(ni−1):(nj−1)=2:1**（X 向格段数为 Y 的 2 倍），以便与 **2:1 展示宽高比** 对齐。**地理坐标目标**下包络折成米；**投影目标**下用同一米步长换为 CRS 线单位；**cecf** 或无 GDAL / 无变换时仍走像元网格（无强制 2:1 格网）。网格顶点数 **2–4096**；OBJ 注释含 `# mesh_step_meters`、`# display_xy_scale`（将包络纵横比 **非破坏** 缩放到约 **2:1**，仅 XY，Z 不变）。**模型→点云**：该间距同时作为 **贴图像素采样步长**（至少 1 像素）。
- **栅格读入最大边（像素）**：中间列提供编辑框（默认 **4096**），CLI `--raster-max-dim`（**64–16384**）。GIS→模型 读 GeoTIFF 时，若长边超过该值则 **整体等比缩小** 后再装入内存（日志 `[RASTER] downsample read buffer: …`）。**过小时贴图糊，过大占内存**。历史实现曾硬编码 **768**，易误为“贴图质量问题”；现已改为可配置。**规则网格 + 非 cecf**：`_albedo`/PBR 使用 **读入缓冲区分辨率**（经 2:1 重采样），**UV 按目标 CRS→栅格 CRS 反算像元**，与高分辨率贴图对齐。**cecf** 或无法建立该变换时，仍用 **网格 ni×nj** 的贴图与归一化网格 UV，避免错位。
- **模型贴图格式**：中间列提供下拉，默认 **PNG**；CLI `--texture-format` 取值为 `png` | `tif` | `tga` | `bmp`。用于 **点云→模型** 时写出 `map_Kd` 指定扩展名的纹理；亦可被其它管线复用（实现见 `WriteRgbTextureFile`）。
- **模型↔点云（`agis_convert_model_to_model`）**：  
  - **3DMesh→点云**：读取 **Wavefront OBJ**（三角面 + 可选 `vt`），`mtllib`/`map_Kd` 由 GDAL 读纹理（长边上限约 **2048**）；对每个三角在 UV 包围盒内按 **Mesh 间距** 步进像素，象素中心映射到 **UV 重心**，插值 **三维位置**，颜色取贴图像素；写出 **LAS 1.2 PDRF 2（RGB）**。**输出 `.laz`** 时实现侧 **不写 LAZ 压缩**（需 LASzip），改为同路径 **`.las`** 并打日志提示。  
  - **点云→模型**：读取 **LAS**（PDRF 0/1/2/3；颜色以 2/3 为优先），在 XY 平面分桶成规则网格，空桶回填全局均值高程与灰色；写出 **OBJ 3.0 多边形子集** + `mtl` + **albedo 纹理**（格式由 **模型贴图格式** 决定）。**LAZ 输入**未支持（无解压），须先用外部工具转为 LAS。
- **纹理 PNG 宽高比**：`_albedo`、由此生成的 `_normal` / `_roughness` / `_metallic` / `_ao` 在写出前双线性重采样为 **宽:高 = 2:1** 像素（高取不小于原高的 **2**，宽 = **2×高**，宽上限 8192）。UV 仍为 0–1 铺满模型，与 OBJ 2:1 展示一致。
- **路径与预览**：输入/输出均有「浏览」「预览」；预览菜单支持「内置预览（默认）」「系统默认打开」「选择其他应用」。
- **OBJ 头注释中的范围**：历史字段 `# extent` 易误解为栅格范围，实为 `.gis` 内 `<display>` 保存的 **2D 地图视口**（经纬度视窗）。现改为：有 `.gis` 时输出 `# gis_document_viewport（…）`；有栅格时另输出 `# raster_bbox_native（…）`，由 **GeoTransform** 四角包络得到、单位为 **栅格数据源 CRS**（全球 WGS84 栅格应接近经度 ±180°、纬度 ±90°）。若视口数值异常，应在主程序中 **适配视图/重存 .gis**，勿与 TIFF 真实范围混淆。
- **OBJ 文件格式版本**：GIS→模型 管线写出 ASCII `.obj` 时，在文件首条注释声明 **`Wavefront OBJ 3.0`**（Alias/Wavefront 附录，3.0 取代 2.11）。实际使用 **多边形子集**：`v`、`vt`、`vn`、`f v/vt/vn`、`mtllib`、`usemtl`、`o`；**不**写出已废弃的 2.11 关键字（如 `bsp`/`bzp`），也**不**写出自由曲面体（`curv`/`surf` 等）。实现：`convert_backend_common.cpp` 中 `kObjFileFormatBanner30`。
- **执行流程**：点击执行后按当前组合选择对应 EXE，构建命令行并创建子进程，等待退出码并写入日志。

### 2.8 模型预览（已实现）

- **触发**：数据转换窗口点击输入/输出「预览」，若当前侧类型为模型数据，默认进入内置 3D 预览窗口。  
- **渲染后端（强制）**：`gis-desktop-win32/CMakeLists.txt` 要求 **`AGIS_USE_BGFX` 恒为 ON**，传入 `-DAGIS_USE_BGFX=OFF` 将导致配置失败。预览使用 **bgfx** 统一渲染；窗口内下拉可切换 **Direct3D 11** 与 **OpenGL**（均为 bgfx 后端，应用不直接调用 D3D11/OpenGL 立即模式绘制预览网格）。实现见 `src/app/model_preview_bgfx.cpp`；嵌入着色器来自上游 `bgfx` 示例 `common/debugdraw`（`vs_debugdraw_fill_texture` / `fs_debugdraw_fill_texture`）：顶点 **Position + TexCoord0 + Color0**，片元 `texture2D(s_texColor, …) * v_color0`，uniform **`u_modelViewProj`** 与采样器 **`s_texColor`**。`CMakeLists.txt` 将 `examples/common/debugdraw` 加入 include 路径以引用预编译 `.bin.h`。源码中 `#if !AGIS_USE_BGFX` 包裹的旧版 WGL/DX11 预览仅作历史参照，正式构建不会编译该分支。
- **交互**：左键拖拽旋转、滚轮缩放、重置视角、适配模型（Fit）。  
- **显示信息**：Mesh 统计（顶点/面/法线/纹理坐标）、材质与贴图统计、文件体积、内存/显存估算、FPS。  
- **材质与贴图**：支持读取 `mtllib/newmtl/usemtl/Kd/map_Kd`，按面材质将 **Kd 乘到顶点颜色**；**OBJ 解析 `vt` 与面 `f` 的纹理索引**，将 UV 写入顶点流。若 **`map_Kd` 路径非空且可被 bimg 解码**，在 **bgfx 初始化**时创建 **2D 纹理**并绑定到 `s_texColor`（失败则回退 **1×1 白色** 纹理，仍保留 Kd 着色）。V 坐标按常见用法在着色前做 **`v_tex = 1 - v_obj`** 以贴合图像行序。提供「预览贴图」按钮在系统仍可按路径打开文件。  
- **几何比例（bgfx）**：顶点先平移到包围盒中心，再按 **`extent = max(ΔX,ΔY,ΔZ)` 等比缩放**，与 OBJ 顶点几何一致；**高程与水平比例在数据转换对话框中设置**，预览不提供单独的竖向夸大。  
- **深度与叠加（bgfx）**：清屏含深度；实体三角列 **不启用背面剔除**（避免 OBJ 绕序差异导致整网不可见）；线框为折线列表叠加。  
- **性能与适配**：预览不在空闲时以固定帧率强制重绘（仅在交互/窗口变化等触发 `WM_PAINT` 时绘制）。当面片数极大时，对绘制面列表按步长抽稀至约 **12 万**面量级（逻辑见 `model_preview_types.h` 的 `ModelPreviewFaceStride`）。「适配模型」只调视角/缩放，不在 CPU 侧重复除 `extent`。

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
