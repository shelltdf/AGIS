# map 模块 UML 类图（物理阶段）

**源码根**：[`gis-desktop-win32/src/map_engine/`](../../../gis-desktop-win32/src/map_engine/)（`map_engine.*`、`map_gpu.*`、`map_projection.*`）。

**说明**：`MapLayer` 的 GDAL 具体实现类仅在 [`map_engine.cpp`](../../../gis-desktop-win32/src/map_engine/map_engine.cpp) 的 `agis_detail` 命名空间中定义，未暴露在公开头文件中；物理层类图仍列出二者以便与 `std::unique_ptr<MapLayer>` 所有权关系对齐。

---

## 核心类型与继承（map_layer.h / map_document.h + map_engine.cpp 实现类）

```mermaid
classDiagram
  direction TB

  class MapLayer {
    <<abstract>>
    +DisplayName()* wstring
    +GetExtent(out)* bool
    +Draw(hdc, client, view)* void
    +GetKind()* MapLayerKind
    +DriverKind()* MapLayerDriverKind
    +AppendDriverProperties(out) void
    +AppendSourceProperties(out) void
    +AppendDetailedProperties(out) void
    +BuildOverviews(err) bool
    +ClearOverviews(err) bool
    +IsLayerVisible() bool
    +SetLayerVisible(on) void
    #layerVisible_ bool
  }

  class RasterMapLayer {
    <<final>>
    <<namespace agis_detail>>
    <<map_engine.cpp>>
  }

  class VectorMapLayer {
    <<final>>
    <<namespace agis_detail>>
    <<map_engine.cpp>>
  }

  MapLayer <|-- RasterMapLayer : implements
  MapLayer <|-- VectorMapLayer : implements

  class MapDocument {
    +layers std::vector of std::unique_ptr MapLayer
    +view ViewExtent
    +showLatLonGrid bool
    +refViewWidthDeg double
    +refViewHeightDeg double
    +displayProjection MapDisplayProjection
    +FitViewToLayers() void
    +AddLayerFromFile(path, err) bool
    +AddLayerFromTmsUrl(url, err) bool
    +AddLayerFromWmtsUrl(url, err) bool
    +AddLayerFromArcGisRestJsonUrl(url, err) bool
    +ReplaceLayerAt(index, layer, err) bool
    +RemoveLayerAt(index, err) bool
    +MoveLayerUp(index) void
    +MoveLayerDown(index) void
    +Draw(hdcMem, client) void
    +ScreenToWorld(...) void
    +ZoomAt(...) void
    +PanPixels(...) void
    +ZoomViewAtCenter(...) void
    +ResetZoom100AnchorCenter(...) void
    +CenterContentOrigin(...) void
    +ScalePercentForUi() int
    +SetShowLatLonGrid(on) void
    +SetDisplayProjection(p) void
    +GetDisplayProjection() MapDisplayProjection
  }

  MapDocument *-- "0..*" MapLayer : owns

  class ViewExtent {
    +minX double
    +minY double
    +maxX double
    +maxY double
    +valid() bool
  }

  MapDocument --> ViewExtent : view
  MapLayer ..> ViewExtent : GetExtent

  class MapLayerKind {
    <<enumeration>>
    kRasterGdal
    kVectorGdal
    kOther
  }

  class MapLayerDriverKind {
    <<enumeration>>
    kGdalFile
    kTmsXyz
    kWmts
    kArcGisRestJson
    kSoapPlaceholder
    kWmsPlaceholder
  }

  MapLayer ..> MapLayerKind
  MapLayer ..> MapLayerDriverKind
```

---

## 地图引擎单例（map_engine.h）

应用内通过 **`MapEngine::Instance()`** 访问唯一引擎实例；**`MapDocument doc_`** 为引擎私有成员，对外经 **`Document()` / `Document() const`** 暴露。地图子窗口过程 **`MapHostProc`** 声明为 **`friend`**，可直接访问引擎内部状态（如 `doc_`、`mapHwnd_`、中键平移状态等）。GDAL 图层工厂仍见 `map_engine_internal.h` / `agis_detail`（`map_engine.cpp`）。

```mermaid
classDiagram
  direction TB
  class MapEngine {
    <<singleton>>
    +Instance() static MapEngine ref
    +Init() void
    +Shutdown() void
    +Document() MapDocument&
    +SetRenderBackend(b) void
    +GetRenderBackend() MapRenderBackend
    +RefreshLayerList(listbox) void
    +MeasureLayerListItem(mis) void
    +PaintLayerListItem(dis) void
    +OnLayerListClick(listbox, x, y) bool
    +GetLayerCount() int
    +OnAddLayerFromDialog(owner, layerList) void
    +GetLayerInfoForUi(...) void
    +IsRasterGdalLayer(index) bool
    +BuildOverviewsForLayer(...) bool
    +ClearOverviewsForLayer(...) bool
    +ReplaceLayerSourceFromUi(...) bool
    +ShowLayerDriverDialog(...) bool
    +SaveMapScreenshotToFile(...) bool
    +PromptSaveMapScreenshot(...) void
    +UpdateMapChrome() void
    -doc_ MapDocument
    -mapHwnd_ HWND
    -mapRenderBackend_ MapRenderBackend
    -mapChromeScale_ HWND
    -mapShortcutExpanded_ bool
    -mapVisExpanded_ bool
    -mlast_ POINT
    -mdrag_ bool
  }
  MapEngine *-- "1" MapDocument : doc_
  class MapHostProc {
    <<global C callback>>
    LRESULT(hwnd, msg, wp, lp)
  }
  MapHostProc ..> MapEngine : friend, uses Instance()
```

---

## 投影与空白地图（map_projection.h）

`MapDocument` 持有 `MapDisplayProjection`，无图层时与 `MapProj_*` 配合做经纬网/拾取变换；有图层时仍以数据坐标为准（见头文件注释）。

```mermaid
classDiagram
  direction LR
  class MapDisplayProjection {
    <<enumeration>>
    kGeographicWgs84
    kWebMercator3857
    kWorldMercator3395
    kPlateCarréeEqc4087
    kMollweide
    kRobinson
    kLambertAzimuthalEqualArea
    kEqualEarth8857
    kCount
  }

  class MapProj {
    <<namespace C API>>
    +MapProj_SystemInit()
    +MapProj_SystemShutdown()
    +MapProj_IsEngineAvailable() bool
    +MapProj_IsProjectionSelectable(p) bool
    +MapProj_MenuLabel(p) wchar_t*
    +MapProj_GeoLonLatToScreen(...)
    +MapProj_ScreenToGeoLonLat(...)
    +MapProj_InvalidateBoundsCache()
  }

  MapDocument ..> MapDisplayProjection
  MapProj ..> MapDisplayProjection : uses
  MapProj ..> ViewExtent : geoView
```

---

## 呈现后端（map_gpu.h）

地图客户区可在 GDI 与 GPU 呈现之间切换；**`MapEngine::SetRenderBackend` / `MapEngine::GetRenderBackend`**（经 **`Instance()`**）与 **`MapGpu_*`** 协同（实现见 `map_gpu.cpp`，含匿名命名空间内 D3D11 / OpenGL 状态，此处不展开为类）。

```mermaid
classDiagram
  direction LR
  class MapRenderBackend {
    <<enumeration>>
    kGdi
    kD3d11
    kOpenGL
  }

  class MapGpu {
    <<namespace C API>>
    +MapGpu_Init(hwnd, backend) bool
    +MapGpu_Shutdown(hwnd) void
    +MapGpu_OnResize(w, h) void
    +MapGpu_PresentFrame(hwnd, bgra, w, h) bool
    +MapGpu_GetActiveBackend() MapRenderBackend
  }

  class MapEngineGpu {
    <<MapEngine 成员>>
    +SetRenderBackend(b) void
    +GetRenderBackend() MapRenderBackend
  }

  MapGpu ..> MapRenderBackend
  MapEngineGpu ..> MapRenderBackend
```

---

## MapEngine 成员与 UI 对应（节选）

宿主与主窗口通过 **`MapEngine::Instance().…`** 调用（例如 `main.cpp`）。**`Init` / `Shutdown`** 内配合 **`MapProj_SystemInit` / `MapProj_SystemShutdown`** 与（若启用 GDAL）**`GDALAllRegister`**。

| 分组 | 成员函数（经 `Instance()`） |
|------|-----------------------------|
| 生命周期 | `Init`, `Shutdown`, `Document` |
| 宿主与叠层 | `UpdateMapChrome`；`MapHostProc`（全局 C 回调，非成员） |
| 图层列表 UI | `RefreshLayerList`, `MeasureLayerListItem`, `PaintLayerListItem`, `OnLayerListClick` |
| 图层与属性 | `GetLayerCount`, `OnAddLayerFromDialog`, `GetLayerInfoForUi`, `IsRasterGdalLayer`, `BuildOverviewsForLayer`, `ClearOverviewsForLayer`, `ReplaceLayerSourceFromUi`, `ShowLayerDriverDialog` |
| 截图 | `SaveMapScreenshotToFile`, `PromptSaveMapScreenshot` |

---

## 与 mapping 交叉引用

实现符号 → 源码路径见 [mapping.md](mapping.md) 中 `src/map_engine/` 相关行；本图侧重 **类型关系与模块边界**，字段级行为以 [spec.md](spec.md) 为准。
