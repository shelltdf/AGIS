#pragma once

#include <windows.h>

#include <string>

#include "map_engine/map_document.h"
#include "map_engine/map_gpu.h"

/** 地图引擎：文档、图层列表 UI、地图宿主窗口状态与 GDAL 图层工厂（单例）。 */
class MapEngine {
 public:
  static MapEngine& Instance();

  void Init();
  void Shutdown();

  MapDocument& Document() { return doc_; }
  const MapDocument& Document() const { return doc_; }

  void SetRenderBackend(MapRenderBackend backend);
  MapRenderBackend GetRenderBackend() const;

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

  bool ShowLayerDriverDialog(HWND owner, MapLayerDriverKind* outKind, std::wstring* outUrlOrEmpty);

  bool SaveMapScreenshotToFile(HWND mapHwnd, const wchar_t* path, std::wstring& err);
  void PromptSaveMapScreenshot(HWND owner, HWND mapHwnd);

  void UpdateMapChrome();

 private:
  MapEngine() = default;
  friend LRESULT CALLBACK MapHostProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam);

  MapDocument doc_{};
  HWND mapHwnd_{nullptr};
  MapRenderBackend mapRenderBackend_{MapRenderBackend::kGdi};
  HWND mapChromeScale_{nullptr};
  bool mapShortcutExpanded_{false};
  bool mapVisExpanded_{true};
  POINT mlast_{};
  bool mdrag_{false};
};

LRESULT CALLBACK MapHostProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam);
