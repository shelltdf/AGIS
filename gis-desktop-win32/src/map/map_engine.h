#pragma once

#include <windows.h>

#include <memory>
#include <string>
#include <vector>

#include "map/map_gpu.h"

enum class MapLayerKind { kRasterGdal, kVectorGdal, kOther };

/** UI 与列表块用短标签（UTF-16）。 */
const wchar_t* MapLayerKindLabel(MapLayerKind k);

struct ViewExtent {
  double minX = 0;
  double minY = 0;
  double maxX = 1;
  double maxY = 1;
  bool valid() const { return maxX > minX && maxY > minY; }
};

#include "map/map_projection.h"

enum class MapLayerDriverKind {
  kGdalFile,
  kTmsXyz,
  kWmts,
  /** ArcGIS REST Services Directory 风格（MapServer/ImageServer `f=json`，由 GDAL WMS 驱动解析）。 */
  kArcGisRestJson,
  /** OGC Web Services SOAP 绑定等（占位）。 */
  kSoapPlaceholder,
  /** 经典 WMS KVP GetCapabilities/GetMap（占位）。 */
  kWmsPlaceholder,
};

/** 属性面板与日志用短标签（UTF-16）。 */
const wchar_t* MapLayerDriverKindLabel(MapLayerDriverKind k);

class MapLayer {
 public:
  virtual ~MapLayer() = default;
  virtual std::wstring DisplayName() const = 0;
  virtual bool GetExtent(ViewExtent& out) const = 0;
  virtual void Draw(HDC hdc, const RECT& client, const ViewExtent& view) const = 0;
  virtual MapLayerKind GetKind() const { return MapLayerKind::kOther; }
  /** 本图层选用的数据源驱动（每种驱动对应不同属性语义与更换数据源方式）。 */
  virtual MapLayerDriverKind DriverKind() const { return MapLayerDriverKind::kGdalFile; }
  /** 属性面板：驱动侧（GDAL/OGR 能力、仿射、波段/图层几何等）。 */
  virtual void AppendDriverProperties(std::wstring* out) const { (void)out; }
  /** 属性面板：数据源侧（路径、文件列表、数据集描述与元数据域等）。 */
  virtual void AppendSourceProperties(std::wstring* out) const { (void)out; }
  /** 合并驱动 + 数据源（兼容旧调用）。 */
  void AppendDetailedProperties(std::wstring* out) const {
    if (!out) {
      return;
    }
    AppendDriverProperties(out);
    AppendSourceProperties(out);
  }
  virtual bool BuildOverviews(std::wstring& err);
  virtual bool ClearOverviews(std::wstring& err);

  bool IsLayerVisible() const { return layerVisible_; }
  void SetLayerVisible(bool on) { layerVisible_ = on; }

 protected:
  /** 列表与地图是否绘制该图层（独立于经纬网等全局可见性）。 */
  bool layerVisible_{true};
};

struct MapDocument {
  std::vector<std::unique_ptr<MapLayer>> layers;
  ViewExtent view{};
  /** 无图层时是否绘制经纬网（要素可见性）。 */
  bool showLatLonGrid{true};
  /** 「100%」缩放基准：视口经度跨度（度），默认 360（全球）。 */
  double refViewWidthDeg{360.0};
  /** 「100%」缩放基准：视口纬度跨度（度），默认 180（全球南北）。 */
  double refViewHeightDeg{180.0};
  /** 无图层时 2D 经纬网/拾取的显示投影；有图层时仍以数据坐标为准。 */
  MapDisplayProjection displayProjection{MapDisplayProjection::kGeographicWgs84};

  void FitViewToLayers();
  bool AddLayerFromFile(const std::wstring& path, std::wstring& err);
  /** TMS/XYZ 网络瓦片（依赖 GDAL XYZ 驱动，连接串形如 ZXY:https://.../{z}/{x}/{y}.png）。 */
  bool AddLayerFromTmsUrl(const std::wstring& url, std::wstring& err);
  /** WMTS（依赖 GDAL WMTS 驱动，连接串形如 WMTS:https://... 或 GetCapabilities URL）。 */
  bool AddLayerFromWmtsUrl(const std::wstring& url, std::wstring& err);
  /** ArcGIS REST Services Directory JSON（MapServer/ImageServer，由 GDAL WMS 驱动处理含 `f=json` 的 URL）。 */
  bool AddLayerFromArcGisRestJsonUrl(const std::wstring& url, std::wstring& err);
  /** 用新图层替换指定下标（用于更改数据源）。 */
  bool ReplaceLayerAt(size_t index, std::unique_ptr<MapLayer> layer, std::wstring& err);
  /** 按下标删除图层（绘制顺序与列表一致）。 */
  bool RemoveLayerAt(size_t index, std::wstring& err);
  /** 与上一层交换（列表中上移）。 */
  void MoveLayerUp(size_t index);
  /** 与下一层交换（列表中下移）。 */
  void MoveLayerDown(size_t index);
  void Draw(HDC hdcMem, const RECT& client);
  void ScreenToWorld(int sx, int sy, int cw, int ch, double* wx, double* wy) const;
  void ZoomAt(int sx, int sy, int cw, int ch, double factor);
  void PanPixels(int dx, int dy, int cw, int ch);
  void ZoomViewAtCenter(double factor, int cw, int ch);
  /** 以视口中心为锚点恢复到 refViewWidthDeg 对应的「100%」缩放。 */
  void ResetZoom100AnchorCenter(int cw, int ch);
  /** 不改变缩放，将内容包围盒中心移到视口中心（无图层时为 (0,0)）。 */
  void CenterContentOrigin(int cw, int ch);
  int ScalePercentForUi() const;
  void SetShowLatLonGrid(bool on);
  bool GetShowLatLonGrid() const { return showLatLonGrid; }
  void SetDisplayProjection(MapDisplayProjection p);
  MapDisplayProjection GetDisplayProjection() const { return displayProjection; }

 private:
  /** 无图层时：修正无效范围；纬度跨度>180° 时与经度等比例缩放回合法范围。 */
  void NormalizeEmptyMapView();
  /** 无图层时：保持视口经度:纬度 = 360:180（度空间）。 */
  void EnforceLonLatAspect360_180();
};

void MapEngine_Init();
void MapEngine_Shutdown();
MapDocument& MapEngine_Document();

/** 2D 客户区呈现后端（GDI / D3D11 / OpenGL）；失败时回退为 GDI。 */
void MapEngine_SetRenderBackend(MapRenderBackend backend);
MapRenderBackend MapEngine_GetRenderBackend();

LRESULT CALLBACK MapHostProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam);
void MapEngine_RefreshLayerList(HWND listbox);
/** 自绘列表：父窗口 WM_MEASUREITEM / WM_DRAWITEM。 */
void MapEngine_MeasureLayerListItem(LPMEASUREITEMSTRUCT mis);
void MapEngine_PaintLayerListItem(const DRAWITEMSTRUCT* dis);
/** 自绘列表内点击：点在左侧可见性区域时切换可见性并返回 true。 */
bool MapEngine_OnLayerListClick(HWND listbox, int x, int y);

/** 当前文档图层数（用于 UI 校验选中下标）。 */
int MapEngine_GetLayerCount();
void MapEngine_OnAddLayerFromDialog(HWND owner, HWND layerList);

/** 供「图层属性」Dock 显示：index 为列表选中下标，越界或无效时返回占位文案。 */
void MapEngine_GetLayerInfoForUi(int index, std::wstring* outTitle, std::wstring* outDriverProps,
                                 std::wstring* outSourceProps);

bool MapEngine_IsRasterGdalLayer(int index);
bool MapEngine_BuildOverviewsForLayer(int index, std::wstring& err);
bool MapEngine_ClearOverviewsForLayer(int index, std::wstring& err);
/** 重新选择驱动与文件/URL 并替换当前选中图层。 */
bool MapEngine_ReplaceLayerSourceFromUi(HWND owner, HWND layerListbox, int index);

/** 模态对话框：选择数据源驱动（GDAL 文件 / TMS / WMTS / ArcGIS REST JSON / SOAP·WMS 占位）。返回 false 表示取消。 */
bool MapEngine_ShowLayerDriverDialog(HWND owner, MapLayerDriverKind* outKind, std::wstring* outUrlOrEmpty);

/** 将地图子窗口客户区（与当前视图一致，含右下角操作提示叠层）保存为 PNG。 */
bool MapEngine_SaveMapScreenshotToFile(HWND mapHwnd, const wchar_t* path, std::wstring& err);

/** 弹出「另存为」并保存当前地图截图。 */
void MapEngine_PromptSaveMapScreenshot(HWND owner, HWND mapHwnd);

/** 更新地图子窗口上的比例文字等（平移/缩放后调用）。 */
void MapEngine_UpdateMapChrome();
