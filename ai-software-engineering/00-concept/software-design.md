# AGIS 软件设计

## 1. 架构总览

```
┌─────────────────────────────────────────────────────────────┐
│  Presentation (Win32 SDI Shell, Menus, Toolbars, Dock)      │
├─────────────────────────────────────────────────────────────┤
│  Application / Session (Project, Undo, Commands)            │
├─────────────────────────────────────────────────────────────┤
│  Map & View (2D Canvas, 3D Viewport, Camera, Simulation)   │
├─────────────────────────────────────────────────────────────┤
│  Layer Model (Layer tree, drivers, style, visibility)       │
├─────────────────────────────────────────────────────────────┤
│  Data Access (GDAL/OGR, TMS, streams, codecs — phased)      │
└─────────────────────────────────────────────────────────────┘
```

## 2. 与构建目标对应关系

| 构建目标 | 说明 |
|----------|------|
| `gis-desktop-win32` | 主程序：Win32 SDI 壳、菜单/工具栏/状态栏、左侧图层 Dock、中央 2D/3D 视口占位 |

后续可增设静态库目标（如 `agis-core`）承载与 UI 无关的领域模型；物理目录在确立时写入 `02-physical/README.md`。

## 3. 模块职责（逻辑）

- **Shell**：窗口类、布局、消息泵、Dock 与分割条、主题与语言切换入口。
- **Project**：当前工程路径、脏标记、保存格式（初期可为占位 JSON/二进制，后续固化）。
- **LayerRegistry**：图层类型注册、驱动工厂（GDAL、TMS、PointCloud、Model、Stream 等按阶段注册）。
- **MapView2D / MapView3D**：视口变换、输入、与图层渲染协调。
- **Georeferencing**：无 CRS/无坐标数据时的配准流程（GCP、仿射/多项式等，分阶段）。
- **Simulation**：载具/相机状态机，与 3D 场景更新循环对接。

## 4. SDI 行为约定

- **顶层窗口**：每个运行实例至少一个主框架窗口。
- **多工程**：推荐 **一工程一顶层窗口**；若采用单窗口多标签，须在 `01-logic/detailed-design-ui-shell.md` 与物理规格中写明。

## 5. 依赖策略

- **首期**：Win32 API + CMake；不强制链接 GDAL，以便环境极简可编译。
- **二期起**：可选 `GIS_DESKTOP_WITH_GDAL` CMake 选项引入 GDAL/OGR；文档与 `THIRD_PARTY_LICENSES.md` 同步更新。
