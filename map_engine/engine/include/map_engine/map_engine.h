#pragma once

#include <windows.h>

#include <memory>
#include <string>

#include "map_engine/export.h"
#include "map_engine/map.h"
#include "map_engine/render_device_context.h"
#include "map_host_win32.h"
#include "map_engine/map_scheduler.h"
#include "map_engine/map_view.h"
#include "map_engine/scene_graph.h"

/** 地图引擎：文档、图层列表 UI、地图宿主窗口状态与 GDAL 图层工厂（单例）。 */
class AGIS_MAP_ENGINE_API MapEngine {
 public:
  static MapEngine& Instance();

  MapEngine(const MapEngine&) = delete;
  MapEngine& operator=(const MapEngine&) = delete;

  void Init();
  void Shutdown();

  Map& Document() { return doc_; }
  const Map& Document() const { return doc_; }

  /** 默认视口（``MapView``），在 ``InitDefaultMapViewStack`` 中绑定窗口并挂接渲染器与场景根。 */
  MapView& DefaultMapView() { return defaultMapView_; }
  const MapView& DefaultMapView() const { return defaultMapView_; }

  SceneGraph& GetSceneGraph() { return sceneGraph_; }
  const SceneGraph& GetSceneGraph() const { return sceneGraph_; }

  MapScheduler& GetMapScheduler() { return mapScheduler_; }
  const MapScheduler& GetMapScheduler() const { return mapScheduler_; }

  /**
   * 按当前 ``Document()`` 重建 ``GetSceneGraph()`` 并更新默认 ``MapView`` 的场景根（图层增删、GIS 重载后应调用）。
   */
  void SyncSceneGraphFromMap();

  /**
   * 在已创建的 Win32 宿主 ``HWND`` 就绪后调用：建立默认场景根、``Renderer2D``、``CreateNativeWindow(WinIDFromHwnd(...))``。
   */
  void InitDefaultMapViewStack(HWND mapHwnd);

  void SetRenderBackend(MapRenderBackendType backend);
  MapRenderBackendType GetRenderBackend() const;

  void RefreshLayerList(HWND listbox);
  void MeasureLayerListItem(LPMEASUREITEMSTRUCT mis);
  void PaintLayerListItem(const DRAWITEMSTRUCT* dis);
  bool OnLayerListClick(HWND listbox, int x, int y);

  int GetLayerCount() const;
  void OnAddLayerFromDialog(HWND owner, HWND layerList);

  void GetLayerInfoForUi(int index, std::wstring* outTitle, std::wstring* outDriverProps,
                         std::wstring* outSourceProps);

  bool IsRasterGdalLayer(int index) const;
  bool BuildOverviewsForLayer(int index, std::wstring& err);
  bool ClearOverviewsForLayer(int index, std::wstring& err);
  bool ReplaceLayerSourceFromUi(HWND owner, HWND layerListbox, int index);

  bool ShowLayerDataSourceDialog(HWND owner, MapDataSourceKind* outKind, std::wstring* outUrlOrEmpty);

  bool SaveMapScreenshotToFile(HWND mapHwnd, const wchar_t* path, std::wstring& err);
  void PromptSaveMapScreenshot(HWND owner, HWND mapHwnd);

  void UpdateMapChrome();

  /** 刷新地图宿主内叠加按钮/说明文案（语言菜单切换后调用）。 */
  void ApplyMapHostUiLanguage(HWND mapHost);

  bool IsMapShortcutHelpExpanded() const { return mapShortcutExpanded_; }
  bool IsMapVisibilityPanelExpanded() const { return mapVisExpanded_; }
  bool IsMapUiShowHintOverlay() const { return mapUiShowHint_; }
  bool IsMapUiShowShortcutChrome() const { return mapUiShowShortcut_; }
  bool IsMapUiShowVisChrome() const { return mapUiShowVis_; }
  bool IsMapUiShowBottomChrome() const { return mapUiShowBottom_; }

 private:
  MapEngine() = default;
  friend AGIS_MAP_ENGINE_API LRESULT CALLBACK MapHostProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam);

  /** 懒创建；地图宿主与 ``InitDefaultMapViewStack`` 共用同一实例。 */
  RenderDeviceContext* ensureRenderDeviceContext();

  MapScheduler mapScheduler_{};
  SceneGraph sceneGraph_{};
  MapView defaultMapView_{};
  SceneNode* defaultSceneRoot_{nullptr};

  Map doc_{};
  HWND mapHwnd_{nullptr};
  std::unique_ptr<RenderDeviceContext> renderDeviceContext_;
  MapRenderBackendType mapRenderBackend_{MapRenderBackendType::kGdi};
  HWND mapChromeScale_{nullptr};
  bool mapShortcutExpanded_{false};
  bool mapVisExpanded_{true};
  POINT mlast_{};
  bool mdrag_{false};

  /** 地图宿主内叠加 UI 是否显示（顶栏「界面」菜单可切换）。 */
  bool mapUiShowShortcut_{true};
  bool mapUiShowVis_{true};
  bool mapUiShowBottom_{true};
  bool mapUiShowHint_{true};
};
