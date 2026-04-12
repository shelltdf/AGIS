#include "utils/agis_ui_l10n.h"

#include <windows.h>

#include <string>

namespace {

constexpr wchar_t kRegAgis[] = L"Software\\AGIS\\GIS-Desktop";
constexpr wchar_t kRegLangVal[] = L"UiLanguage";

AgisUiLanguage g_uiLang = AgisUiLanguage::kZh;

}  // namespace

AGIS_COMMON_API AgisUiLanguage AgisGetUiLanguage() {
  return g_uiLang;
}

AGIS_COMMON_API void AgisSetUiLanguage(AgisUiLanguage lang) {
  g_uiLang = lang;
}

AGIS_COMMON_API void AgisLoadUiLanguagePreference() {
  HKEY hkey = nullptr;
  if (RegOpenKeyExW(HKEY_CURRENT_USER, kRegAgis, 0, KEY_READ, &hkey) != ERROR_SUCCESS) {
    return;
  }
  DWORD v = 0;
  DWORD cb = sizeof(v);
  if (RegQueryValueExW(hkey, kRegLangVal, nullptr, nullptr, reinterpret_cast<BYTE*>(&v), &cb) == ERROR_SUCCESS) {
    if (v <= 1) {
      g_uiLang = static_cast<AgisUiLanguage>(v);
    }
  }
  RegCloseKey(hkey);
}

AGIS_COMMON_API void AgisSaveUiLanguagePreference() {
  HKEY hkey = nullptr;
  DWORD disp = 0;
  if (RegCreateKeyExW(HKEY_CURRENT_USER, kRegAgis, 0, nullptr, 0, KEY_WRITE, nullptr, &hkey, &disp) != ERROR_SUCCESS) {
    return;
  }
  const DWORD v = static_cast<DWORD>(g_uiLang);
  RegSetValueExW(hkey, kRegLangVal, 0, REG_DWORD, reinterpret_cast<const BYTE*>(&v), sizeof(v));
  RegCloseKey(hkey);
}

AGIS_COMMON_API const wchar_t* AgisTr(AgisUiStr id) {
  const bool en = g_uiLang == AgisUiLanguage::kEn;
  switch (id) {
    case AgisUiStr::MenuFile:
      return en ? L"&File" : L"文件(&F)";
    case AgisUiStr::MenuFileNew:
      return en ? L"&New" : L"新建(&N)";
    case AgisUiStr::MenuFileOpen:
      return en ? L"&Open..." : L"打开(&O)...";
    case AgisUiStr::MenuFileSave:
      return en ? L"&Save" : L"保存(&S)";
    case AgisUiStr::MenuFileSaveAs:
      return en ? L"Save &As..." : L"另存(&A)...";
    case AgisUiStr::MenuFileScreenshot:
      return en ? L"Save Map &Screenshot..." : L"保存地图截图(&S)...";
    case AgisUiStr::MenuFileExit:
      return en ? L"E&xit" : L"退出(&X)";
    case AgisUiStr::MenuView:
      return en ? L"&View" : L"视图(&V)";
    case AgisUiStr::MenuView2d:
      return en ? L"&2D Mode" : L"2D 模式(&2)";
    case AgisUiStr::MenuView3d:
      return en ? L"&3D Mode" : L"3D 模式(&3)";
    case AgisUiStr::MenuViewRenderRoot:
      return en ? L"2D &Render" : L"2D 渲染(&R)";
    case AgisUiStr::MenuRenderGdi:
      return L"GDI(&G)";
    case AgisUiStr::MenuRenderGdiPlus:
      return L"GDI+(&I)";
    case AgisUiStr::MenuRenderD2d:
      return L"Direct2D(&2)";
    case AgisUiStr::MenuRenderBgfxD3d11:
      return L"Bgfx + D3D11(&D)";
    case AgisUiStr::MenuRenderBgfxGl:
      return L"Bgfx + OpenGL(&O)";
    case AgisUiStr::MenuRenderBgfxAuto:
      return en ? L"Bgfx &Auto" : L"Bgfx 自动(&B)";
    case AgisUiStr::MenuViewProjRoot:
      return en ? L"&Projection" : L"投影(&J)";
    case AgisUiStr::MenuMapChromeRoot:
      return en ? L"&Map Chrome" : L"地图界面(&M)";
    case AgisUiStr::MenuMapShortcut:
      return en ? L"Shortcut bar" : L"快捷键区";
    case AgisUiStr::MenuMapVis:
      return en ? L"Visibility && graticule" : L"可见性与经纬网";
    case AgisUiStr::MenuMapBottom:
      return en ? L"Bottom zoom bar" : L"底部缩放与适应栏";
    case AgisUiStr::MenuMapHint:
      return en ? L"Hint overlay" : L"操作提示文字";
    case AgisUiStr::MenuMapGrid:
      return en ? L"Draw lat/lon grid" : L"绘制经纬网";
    case AgisUiStr::MenuViewLog:
      return en ? L"&Log..." : L"日志(&L)...";
    case AgisUiStr::MenuWindow:
      return en ? L"&Window" : L"窗口(&W)";
    case AgisUiStr::MenuWinLayerDock:
      return en ? L"&Layers Dock" : L"图层 Dock(&L)";
    case AgisUiStr::MenuWinPropsDock:
      return en ? L"&Properties Dock" : L"图层属性 Dock(&P)";
    case AgisUiStr::MenuLayer:
      return en ? L"&Layers" : L"图层(&Y)";
    case AgisUiStr::MenuLayerAdd:
      return en ? L"&Add Data Layer..." : L"添加数据图层(&A)...";
    case AgisUiStr::MenuTools:
      return en ? L"&Tools" : L"工具(&T)";
    case AgisUiStr::MenuToolsConvert:
      return en ? L"&Data Convert..." : L"数据转换(&C)...";
    case AgisUiStr::MenuLang:
      return en ? L"&Language" : L"语言(&L)";
    case AgisUiStr::MenuLangZh:
      return L"中文(&Z)";
    case AgisUiStr::MenuLangEn:
      return L"English(&E)";
    case AgisUiStr::MenuTheme:
      return en ? L"&Theme" : L"主题(&T)";
    case AgisUiStr::MenuThemeSystem:
      return en ? L"&Follow system" : L"跟随系统(&S)";
    case AgisUiStr::MenuThemeLight:
      return en ? L"&Light" : L"浅色(&I)";
    case AgisUiStr::MenuThemeDark:
      return en ? L"&Dark" : L"深色(&D)";
    case AgisUiStr::MenuHelp:
      return en ? L"&Help" : L"帮助(&H)";
    case AgisUiStr::MenuHelpDrivers:
      return en ? L"&Data drivers..." : L"数据驱动说明(&D)...";
    case AgisUiStr::MenuHelpAbout:
      return en ? L"&About..." : L"关于(&A)...";
    case AgisUiStr::MenuDebug:
      return en ? L"D&ebug" : L"调试(&G)";
    case AgisUiStr::MenuDebugClip:
      return en ? L"Screenshot to &clipboard" : L"截图到剪贴板(&S)";
    case AgisUiStr::MenuDebugJson:
      return en ? L"Copy UI &JSON" : L"复制界面信息 JSON(&J)";
    case AgisUiStr::TipFileNew:
      return en ? L"New GIS project — no shortcut" : L"新建 GIS 工程 — 无全局快捷键";
    case AgisUiStr::TipFileOpen:
      return en ? L"Open .gis / XML — no shortcut" : L"打开 .gis / XML — 无全局快捷键";
    case AgisUiStr::TipFileSave:
      return en ? L"Save — no shortcut" : L"保存 — 无全局快捷键";
    case AgisUiStr::TipFileSaveAs:
      return en ? L"Save As — no shortcut" : L"另存为 — 无全局快捷键";
    case AgisUiStr::TipLayerAdd:
      return en ? L"Add layer (GDAL / TMS / WMS) — no shortcut"
                : L"添加图层（先选择 GDAL / TMS / WMS） — 无全局快捷键";
    case AgisUiStr::TipLayerAddNoGdal:
      return en ? L"Add layer (GDAL disabled in this build)" : L"添加图层（本构建未启用 GDAL，已禁用） — 无全局快捷键";
    case AgisUiStr::TipScreenshot:
      return en ? L"Save map view as PNG — no shortcut" : L"将当前地图视图保存为 PNG — 无全局快捷键";
    case AgisUiStr::TipLog:
      return en ? L"Open log — no shortcut" : L"打开日志窗口 — 无全局快捷键";
    case AgisUiStr::Tip2d:
      return en ? L"2D map — no shortcut" : L"2D 地图 — 无全局快捷键";
    case AgisUiStr::Tip3d:
      return en ? L"3D mode (placeholder) — no shortcut" : L"3D 模式（占位） — 无全局快捷键";
    case AgisUiStr::TipHelpDrivers:
      return en ? L"Data drivers help — F1" : L"数据驱动说明（图层类型、GDAL 格式、输入方式） — F1";
    case AgisUiStr::TipAbout:
      return en ? L"About AGIS — no shortcut" : L"关于 AGIS — 无全局快捷键";
    case AgisUiStr::StatusReady:
      return en ? L"Ready — double-click to open log" : L"就绪 — 双击此处打开日志";
    case AgisUiStr::WinTitleNoDoc:
      return en ? L"AGIS — Map View (SDI)" : L"AGIS — 地图视图（单文档 SDI）";
    case AgisUiStr::MsgGdalOffTitle:
      return L"AGIS";
    case AgisUiStr::MsgGdalOffBody:
      return en ? L"This build has GDAL disabled (GIS_DESKTOP_HAVE_GDAL=0).\n\nThe default build enables GDAL "
                  L"(3rdparty sources in repo). If you used AGIS_USE_GDAL=off, remove it and re-run build.py; "
                  L"otherwise check CMake finds GDAL/PROJ (see 3rdparty/README-GDAL-BUILD.md)."
                : L"本程序未启用 GDAL（GIS_DESKTOP_HAVE_GDAL=0）。\n\n默认构建应已启用 GDAL（仓库含 3rdparty "
                  L"源码）。若曾设置 AGIS_USE_GDAL=off，请去掉后重新运行 python build.py；否则请检查 CMake 是否找到 "
                  L"GDAL/PROJ（见 3rdparty/README-GDAL-BUILD.md）。";
    case AgisUiStr::LogStartup:
      return en ? L"AGIS started." : L"AGIS 启动完成。";
    case AgisUiStr::LogGdalOn:
      return en ? L"[GIS] GDAL enabled: you can add data layers." : L"[GIS] GDAL 已启用：可添加数据图层。";
    case AgisUiStr::LogGdalOff:
      return en ? L"[GIS] GDAL disabled (GIS_DESKTOP_HAVE_GDAL=0). Default builds enable GDAL (3rdparty in repo). "
                  L"If you used AGIS_USE_GDAL=off, remove it and re-run build.py; ensure CMake finds GDAL/PROJ "
                  L"(see 3rdparty/README-GDAL-BUILD.md)."
                : L"[GIS] 本构建未启用 GDAL（GIS_DESKTOP_HAVE_GDAL=0）；「添加数据图层」入口已禁用。默认构建应已启用 "
                  L"GDAL（仓库含 3rdparty 源码）；若曾使用 AGIS_USE_GDAL=off，请去掉该设置后重新运行 python "
                  L"build.py，并确保 CMake 能完成 GDAL/PROJ（见 3rdparty/README-GDAL-BUILD.md）。";
    case AgisUiStr::LogViewProjPrefix:
      return en ? L"[View] Projection: " : L"[视图] 投影：";
    case AgisUiStr::LogLangZh:
      return L"[Language] UI: Chinese (中文).";
    case AgisUiStr::LogLangEn:
      return L"[Language] UI: English.";
    case AgisUiStr::LogThemeFollowSystem:
      return en ? L"[Theme] Updated from system color settings (follow system)."
                : L"[主题] 已随系统颜色设置更新（跟随系统）。";
    case AgisUiStr::LogDockLayerExpanded:
      return en ? L"[Window] Layers Dock content expanded." : L"[窗口] 已展开图层 Dock 内容区。";
    case AgisUiStr::LogDockLayerCollapsed:
      return en ? L"[Window] Layers Dock content collapsed." : L"[窗口] 已折叠图层 Dock 内容区。";
    case AgisUiStr::LogDockPropsExpanded:
      return en ? L"[Window] Properties Dock content expanded." : L"[窗口] 已展开图层属性 Dock 内容区。";
    case AgisUiStr::LogDockPropsCollapsed:
      return en ? L"[Window] Properties Dock content collapsed." : L"[窗口] 已折叠图层属性 Dock 内容区。";
    case AgisUiStr::LogWinLayerShown:
      return en ? L"[Window] Layers Dock shown." : L"[窗口] 已显示图层 Dock。";
    case AgisUiStr::LogWinLayerHidden:
      return en ? L"[Window] Layers Dock hidden." : L"[窗口] 已隐藏图层 Dock。";
    case AgisUiStr::LogWinPropsShown:
      return en ? L"[Window] Properties Dock shown." : L"[窗口] 已显示图层属性 Dock。";
    case AgisUiStr::LogWinPropsHidden:
      return en ? L"[Window] Properties Dock hidden." : L"[窗口] 已隐藏图层属性 Dock。";
    case AgisUiStr::LogView2dOn:
      return en ? L"[View] Switched to 2D mode." : L"[视图] 已切换到 2D 模式。";
    case AgisUiStr::LogView3dOn:
      return en ? L"[View] Switched to 3D mode (placeholder)." : L"[视图] 已切换到 3D 模式（占位）。";
    case AgisUiStr::LogProjLayersHint:
      return en ? L"[Hint] Layers are already loaded; the view still draws in data coordinates. Projection mainly "
                  L"affects the lat/lon grid when there are no layers."
                : L"[提示] 当前已有图层，视图仍以数据坐标系绘制；投影切换主要作用于无图层时的经纬网显示。";
    case AgisUiStr::DebugClipOk:
      return en ? L"[Debug] Main window screenshot copied to clipboard (bitmap)."
                : L"[调试] 主窗口截图已复制到剪贴板（位图）。";
    case AgisUiStr::DebugClipFail:
      return en ? L"[Debug] Failed to copy main window screenshot to clipboard."
                : L"[调试] 主窗口截图复制到剪贴板失败。";
    case AgisUiStr::ThemeAppliedDark:
      return en ? L"[Theme] Dark UI applied." : L"[主题] 已应用深色界面。";
    case AgisUiStr::ThemeAppliedLight:
      return en ? L"[Theme] Light UI applied." : L"[主题] 已应用浅色界面。";
    case AgisUiStr::GisErrOpenFile:
      return en ? L"Cannot open file." : L"无法打开文件。";
    case AgisUiStr::GisErrInvalidXml:
      return en ? L"Not a valid .gis (XML) file." : L"不是有效的 .gis(XML) 文件。";
    case AgisUiStr::GisLogLayerRestoreFailPrefix:
      return en ? L"[GIS] Layer restore failed: " : L"[GIS] 图层恢复失败：";
    case AgisUiStr::GisLogLayerRestoreReasonSep:
      return en ? L" — reason: " : L"，原因：";
    case AgisUiStr::GisLogFileReadOk:
      return en ? L"[GIS] Read .gis: layers and view state restored." : L"[GIS] 已读取 .gis 文件：图层与显示状态已恢复。";
    case AgisUiStr::GisLogRestoreStatsPrefix:
      return en ? L"[GIS] Layers restored OK / failed: " : L"[GIS] 恢复图层成功/失败：";
    case AgisUiStr::GisLogNewDoc:
      return en ? L"[GIS] New empty .gis document." : L"[GIS] 新建 .gis 文档。";
    case AgisUiStr::GisMsgNewDocOk:
      return en ? L"A new empty .gis document (XML) was created." : L"已新建空白 .gis 文档（XML）。";
    case AgisUiStr::GisCapOpenFail:
      return en ? L"Open .gis failed" : L"打开 .gis 失败";
    case AgisUiStr::GisCapSave:
      return en ? L"Save .gis" : L"保存 .gis";
    case AgisUiStr::GisMsgSaveFail:
      return en ? L"Save failed." : L"保存失败。";
    case AgisUiStr::GisLogOpenedPrefix:
      return en ? L"[GIS] Opened file: " : L"[GIS] 打开文件：";
    case AgisUiStr::GisLogSavedPrefix:
      return en ? L"[GIS] Saved: " : L"[GIS] 已保存：";
    case AgisUiStr::CtxAddLayer:
      return en ? L"Add layer…" : L"添加图层…";
    case AgisUiStr::CtxDelete:
      return en ? L"Delete" : L"删除";
    case AgisUiStr::CtxUp:
      return en ? L"Move up" : L"上移";
    case AgisUiStr::CtxDown:
      return en ? L"Move down" : L"下移";
    case AgisUiStr::LogLayerCtxAdd:
      return en ? L"[Layers] Add layer requested from context menu." : L"[图层] 已通过右键菜单发起添加。";
    case AgisUiStr::LogLayerDeleted:
      return en ? L"[Layers] Selected layer removed." : L"[图层] 已删除所选图层。";
    case AgisUiStr::LogLayerMovedUp:
      return en ? L"[Layers] Moved up." : L"[图层] 已上移。";
    case AgisUiStr::LogLayerMovedDown:
      return en ? L"[Layers] Moved down." : L"[图层] 已下移。";
    case AgisUiStr::BtnBuildPyramid:
      return en ? L"Build overviews" : L"生成金字塔";
    case AgisUiStr::BtnClearPyramid:
      return en ? L"Clear overviews" : L"删除金字塔";
    case AgisUiStr::BtnChangeSource:
      return en ? L"Change data source…" : L"更换数据源…";
    case AgisUiStr::LogOvrBuilt:
      return en ? L"[Layers] Overviews built." : L"[图层] 已生成金字塔。";
    case AgisUiStr::LogOvrCleared:
      return en ? L"[Layers] Overviews cleared." : L"[图层] 已删除金字塔。";
    case AgisUiStr::LogErrOvrBuildPrefix:
      return en ? L"[Error] Build overviews: " : L"[错误] 生成金字塔：";
    case AgisUiStr::LogErrOvrClearPrefix:
      return en ? L"[Error] Clear overviews: " : L"[错误] 删除金字塔：";
    case AgisUiStr::BtnCopyAll:
      return en ? L"Copy all" : L"复制全部";
    case AgisUiStr::WinLogTitle:
      return en ? L"Log" : L"日志";
    case AgisUiStr::LogClipboardOk:
      return en ? L"[Log] Copied to clipboard." : L"[日志] 已复制到剪贴板。";
    case AgisUiStr::AboutLine1:
      return en ? L"[About] AGIS — GIS data editing and processing (dev build)"
                : L"[关于] AGIS — GIS 数据编辑与处理（开发版）";
    case AgisUiStr::AboutLine2:
      return en ? L"[About] Version 0.1.0-dev · Windows SDI · GDAL/GDI+ integration in progress"
                : L"[关于] 版本 0.1.0-dev · Windows 本地 SDI · GDAL/GDI+ 集成中";
    case AgisUiStr::AboutLine3:
      return en ? L"[Hint] See log above; use Help → About to append again."
                : L"[提示] 关于信息见上文；可通过菜单「帮助 → 关于」再次写入日志。";
    case AgisUiStr::DebugJsonEncodeFail:
      return en ? L"[Debug] Failed to encode UI JSON." : L"[调试] 界面信息 JSON 生成失败（编码）。";
    case AgisUiStr::DebugJsonCopied:
      return en ? L"[Debug] Copied workbench UI state JSON to clipboard (UTF-16 text)."
                : L"[调试] 已复制界面状态 JSON 到剪贴板（UTF-16 文本）。";
    case AgisUiStr::DockLayerTitle:
      return en ? L"Layers" : L"图层";
    case AgisUiStr::DockLayerSubtitle:
      return en ? L"Dock · data list" : L"Dock · 数据列表";
    case AgisUiStr::DockPropsTitle:
      return en ? L"Layer properties" : L"图层属性";
    case AgisUiStr::DockPropsSubtitleDefault:
      return en ? L"Dock · selection" : L"Dock · 选中项";
    case AgisUiStr::DockChipRight:
      return en ? L"R" : L"右";
    case AgisUiStr::CardDriverTitle:
      return en ? L"Driver" : L"驱动属性";
    case AgisUiStr::CardDriverSubtitle:
      return en ? L"Driver type, overviews, dataset access" : L"驱动类型、金字塔与数据集访问";
    case AgisUiStr::CardSourceTitle:
      return en ? L"Data source" : L"数据源属性";
    case AgisUiStr::CardSourceSubtitle:
      return en ? L"Path, connection, GDAL metadata" : L"路径、连接串与 GDAL 元数据";
    case AgisUiStr::HelpDlgTitle:
      return en ? L"Data drivers" : L"数据驱动说明";
    case AgisUiStr::HelpBtnClose:
      return en ? L"Close" : L"关闭";
    case AgisUiStr::BtnOK:
      return en ? L"OK" : L"确定";
    case AgisUiStr::BtnCancel:
      return en ? L"Cancel" : L"取消";
    case AgisUiStr::LayerKindRasterGdal:
      return en ? L"Raster (GDAL)" : L"栅格（GDAL）";
    case AgisUiStr::LayerKindVectorGdal:
      return en ? L"Vector (GDAL)" : L"矢量（GDAL）";
    case AgisUiStr::LayerKindOther:
      return en ? L"Other" : L"其他";
    case AgisUiStr::LayerDriverGdalFile:
      return en ? L"GDAL file / VSI path" : L"GDAL 本地/虚拟文件";
    case AgisUiStr::LayerDriverTmsXyz:
      return L"TMS / XYZ";
    case AgisUiStr::LayerDriverWmts:
      return en ? L"WMTS (OGC Web Map Tile Service)" : L"WMTS（OGC Web Map Tile Service）";
    case AgisUiStr::LayerDriverArcGisJson:
      return en ? L"ArcGIS REST JSON (Services Directory, GDAL WMS)"
                : L"ArcGIS REST JSON（Services Directory，GDAL WMS）";
    case AgisUiStr::LayerDriverSoapPh:
      return en ? L"OGC SOAP (placeholder)" : L"OGC SOAP（占位）";
    case AgisUiStr::LayerDriverWmsPh:
      return en ? L"WMS KVP (placeholder)" : L"WMS KVP（占位）";
    case AgisUiStr::LayerUnknown:
      return en ? L"Unknown" : L"未知";
    case AgisUiStr::LayerErrOvrUnsupported:
      return en ? L"This layer type does not support raster overviews." : L"此图层类型不支持金字塔。";
    case AgisUiStr::MapDriverShortGdal:
      return en ? L"GDAL file" : L"GDAL 文件";
    case AgisUiStr::MapDriverShortTms:
      return L"TMS/XYZ";
    case AgisUiStr::MapDriverShortWmts:
      return L"WMTS";
    case AgisUiStr::MapDriverShortArcGis:
      return L"ArcGIS";
    case AgisUiStr::MapDriverShortSoap:
      return en ? L"SOAP (ph.)" : L"SOAP（占位）";
    case AgisUiStr::MapDriverShortWms:
      return en ? L"WMS (ph.)" : L"WMS（占位）";
    case AgisUiStr::MapBackendBgfxAuto:
      return en ? L"Bgfx (auto backend)" : L"Bgfx（自动后端）";
    case AgisUiStr::MapBackendUnknown:
      return en ? L"Unknown" : L"未知";
    case AgisUiStr::MapLogGpuInitFail:
      return en ? L"[Map] GPU renderer init failed; fell back to GDI." : L"[地图] GPU 呈现初始化失败，已回退为 GDI。";
    case AgisUiStr::MapLogBackendSetPendingHead:
      return en ? L"[Map] Render mode set to " : L"[地图] 呈现方式已设为 ";
    case AgisUiStr::MapLogBackendSetPendingTail:
      return en ? L" (map host not ready yet; will apply when the window is ready)."
                : L"（地图宿主尚未创建，将在窗口就绪后应用）。";
    case AgisUiStr::MapLogBackendFailHead:
      return en ? L"[Map] Render switch failed for " : L"[地图] 呈现切换失败：";
    case AgisUiStr::MapLogBackendFailTail:
      return en ? L" — init failed; falling back to GDI." : L" 初始化未成功，正在回退为 GDI。";
    case AgisUiStr::MapLogGdiFallbackFail:
      return en ? L"[Map] GDI fallback init also failed; the map may not draw."
                : L"[地图] 呈现切换失败：GDI 回退初始化仍失败，地图可能无法绘制。";
    case AgisUiStr::MapLogGdiFallbackOk:
      return en ? L"[Map] Fell back to GDI and initialized." : L"[地图] 已回退为 GDI 并完成初始化。";
    case AgisUiStr::MapLogBackendOkTail:
      return en ? L"[Map] Render switch OK: " : L"[地图] 呈现切换成功：";
    case AgisUiStr::MapMsgNoLayers:
      return en ? L"(No layers — use the Layers menu to add)" : L"（无图层，使用「图层」菜单添加）";
    case AgisUiStr::MapLabelShow:
      return en ? L"Show" : L"显示";
    case AgisUiStr::MapLabelHide:
      return en ? L"Hide" : L"隐藏";
    case AgisUiStr::MapFmtLayerRow2:
      return en ? L"Visible: %s    Driver: %s" : L"可见：%s    驱动：%s";
    case AgisUiStr::MapFmtDataLine:
      return en ? L"Data: %s" : L"数据：%s";
    case AgisUiStr::PropsNoSelTitle:
      return en ? L"No layer selected" : L"未选择图层";
    case AgisUiStr::PropsNoSelDriver:
      return en ? L"Click a row in the Layers list on the left to see driver and format details here."
                : L"在左侧「图层」列表中单击一行，可在此查看驱动与格式相关属性。";
    case AgisUiStr::PropsNoSelSource:
      return en ? L"After selecting a layer, this area shows data path, file list, and dataset description."
                : L"选中图层后，此处显示数据源路径、文件列表与数据集描述等。";
    case AgisUiStr::PropsNoDriverExtra:
      return en ? L"(No extra driver-side properties)" : L"（无驱动侧附加属性）";
    case AgisUiStr::PropsNoSourceExtra:
      return en ? L"(No extra data-source properties)" : L"（无数据源侧附加属性）";
    case AgisUiStr::PropsTitle:
      return en ? L"Layer properties" : L"图层属性";
    case AgisUiStr::PropsGdalOffDriver:
      return en ? L"GDAL is disabled in this build." : L"当前构建未启用 GDAL。";
    case AgisUiStr::PropsGdalOffSource:
      return en ? L"No vector/raster layer information." : L"无矢量/栅格图层信息。";
    case AgisUiStr::ErrInvalidLayer:
      return en ? L"Invalid layer." : L"无效图层。";
    case AgisUiStr::ErrGdalDisabled:
      return en ? L"GDAL is not enabled." : L"未启用 GDAL。";
    case AgisUiStr::MsgSelectValidLayer:
      return en ? L"Please select a valid layer first." : L"请先选择有效图层。";
    case AgisUiStr::MsgWmsNotAvail:
      return en
          ? L"WMS (KVP GetCapabilities/GetMap) is not wired up yet.\nUse WMTS, ArcGIS REST JSON, TMS/XYZ, or a local "
            L"GDAL file."
          : L"WMS（KVP GetCapabilities/GetMap）尚未接入。\n请使用 WMTS、ArcGIS REST JSON、TMS/XYZ 或本地 GDAL 文件。";
    case AgisUiStr::MsgSoapNotAvail:
      return en ? L"OGC Web Services SOAP binding is not wired up yet.\nUse WMTS, ArcGIS REST JSON, TMS/XYZ, or a "
                  L"local GDAL file."
                : L"OGC Web Services SOAP 绑定尚未接入。\n请使用 WMTS、ArcGIS REST JSON、TMS/XYZ 或本地 GDAL 文件。";
    case AgisUiStr::MsgFillTileUrl:
      return en ? L"Please enter the tile URL." : L"请填写瓦片 URL。";
    case AgisUiStr::MsgFillWmtsUrl:
      return en ? L"Please enter the WMTS GetCapabilities or service URL." : L"请填写 WMTS GetCapabilities 或服务 URL。";
    case AgisUiStr::MsgFillArcGisUrl:
      return en ? L"Please enter the ArcGIS MapServer/ImageServer REST URL." : L"请填写 ArcGIS MapServer/ImageServer 的 REST URL。";
    case AgisUiStr::LogErrReplaceSrcPrefix:
      return en ? L"[Error] Replace data source failed: " : L"[错误] 更换数据源失败：";
    case AgisUiStr::LogErrGenericPrefix:
      return en ? L"[Error] " : L"[错误] ";
    case AgisUiStr::LogLayerReplacedTms:
      return en ? L"[Layers] Switched to TMS: " : L"[图层] 已更换为 TMS：";
    case AgisUiStr::LogLayerReplacedWmts:
      return en ? L"[Layers] Switched to WMTS: " : L"[图层] 已更换为 WMTS：";
    case AgisUiStr::LogLayerReplacedArcGis:
      return en ? L"[Layers] Switched to ArcGIS REST: " : L"[图层] 已更换为 ArcGIS REST：";
    case AgisUiStr::LogLayerReplacedGdal:
      return en ? L"[Layers] Data source replaced (GDAL): " : L"[图层] 已更换数据源（GDAL）：";
    case AgisUiStr::FilterRasterVector:
      return en ? L"Raster/vector" : L"栅格/矢量";
    case AgisUiStr::FilterAllFiles:
      return en ? L"All files" : L"所有文件";
    case AgisUiStr::DlgAddLayerTitle:
      return en ? L"Add layer — data source" : L"添加图层 — 数据源";
    case AgisUiStr::DlgPickLayerSourceType:
      return en ? L"Choose layer data source type:" : L"选择图层数据源类型：";
    case AgisUiStr::DlgLayerRadioGdal:
      return en ? L"GDAL — local file / VSI path (.tif, .shp, VSI, …)"
                : L"GDAL — 本地文件 / 虚拟路径（.tif、.shp、VSI 等）";
    case AgisUiStr::DlgLayerRadioTms:
      return en ? L"TMS / XYZ — web tiles ({z}/{x}/{y}, GDAL XYZ)" : L"TMS / XYZ — 网络瓦片（{z}/{x}/{y}，GDAL XYZ）";
    case AgisUiStr::DlgLayerRadioWmts:
      return en ? L"WMTS — OGC Web Map Tile Service" : L"WMTS — OGC Web Map Tile Service";
    case AgisUiStr::DlgLayerRadioArcGis:
      return en ? L"JSON — ArcGIS REST (MapServer/ImageServer URL, GDAL JSON tiles)"
                : L"JSON — ArcGIS REST Services Directory（MapServer/ImageServer URL，GDAL 按 JSON 解析瓦片）";
    case AgisUiStr::DlgLayerRadioSoap:
      return en ? L"SOAP — OGC Web Services SOAP (placeholder, not wired up)"
                : L"SOAP — OGC Web Services SOAP 绑定（占位，尚未接入）";
    case AgisUiStr::DlgLayerRadioWms:
      return en ? L"WMS — KVP GetMap/GetCapabilities (placeholder, not wired up)"
                : L"WMS — KVP GetMap/GetCapabilities（占位，尚未接入）";
    case AgisUiStr::DlgLayerUrlLabel:
      return en ? L"URL (for TMS / WMTS / ArcGIS REST):" : L"URL（TMS / WMTS / ArcGIS REST 时填写）：";
    case AgisUiStr::ErrNoRasterOrVectorInFile:
      return en ? L"No raster or vector layers found in the file." : L"文件中未找到栅格或矢量图层。";
    case AgisUiStr::ErrUrlEmpty:
      return en ? L"URL is empty." : L"URL 为空。";
    case AgisUiStr::ErrOpenDsTms:
      return en ? L"Cannot open data source (tried ZXY: and direct URL).\n" : L"无法打开数据源（已尝试 ZXY: 与直接 URL）。\n";
    case AgisUiStr::ErrNoRasterBands:
      return en ? L"The data source has no raster bands." : L"该数据源不包含栅格波段。";
    case AgisUiStr::ErrOpenDsWmts:
      return en ? L"Cannot open data source as WMTS.\n" : L"无法以 WMTS 打开数据源。\n";
    case AgisUiStr::ErrWmtsNoBands:
      return en ? L"The WMTS source has no raster bands." : L"该 WMTS 数据源不包含栅格波段。";
    case AgisUiStr::ErrArcGisUrlHint:
      return en ? L"ArcGIS REST JSON needs a MapServer or ImageServer service URL (from the REST Services Directory).\n"
                  L"Example: …/arcgis/rest/services/…/MapServer"
                : L"ArcGIS REST JSON 需要 MapServer 或 ImageServer 的服务 URL（来自 REST Services Directory）。\n"
                  L"示例：…/arcgis/rest/services/…/MapServer";
    case AgisUiStr::ErrOpenArcGis:
      return en ? L"Cannot open ArcGIS REST data source (needs reachable MapServer/ImageServer JSON).\n"
                : L"无法打开 ArcGIS REST 数据源（需可访问的 MapServer/ImageServer JSON）。\n";
    case AgisUiStr::ErrArcGisNoBands:
      return en ? L"The data source has no raster bands." : L"该数据源不包含栅格波段。";
    case AgisUiStr::ErrInvalidParam:
      return en ? L"Invalid parameter." : L"参数无效。";
    case AgisUiStr::ErrMapSizeInvalid:
      return en ? L"Invalid map view size." : L"地图区域大小无效。";
    case AgisUiStr::ErrRenderFailed:
      return en ? L"Rendering failed." : L"渲染失败。";
    case AgisUiStr::ErrPngWriteFailed:
      return en ? L"Failed to write PNG." : L"写入 PNG 失败。";
    case AgisUiStr::FilterPngFiles:
      return en ? L"PNG images" : L"PNG 图像";
    case AgisUiStr::LogScreenshotSavedPrefix:
      return en ? L"[Screenshot] Saved: " : L"[截图] 已保存：";
    case AgisUiStr::LogScreenshotFailPrefix:
      return en ? L"[Error] Screenshot failed: " : L"[错误] 截图失败：";
    case AgisUiStr::LogLayerWmsNyi:
      return en ? L"[Layers] WMS (KVP) is not wired up yet." : L"[图层] WMS（KVP）尚未接入。";
    case AgisUiStr::LogLayerSoapNyi:
      return en ? L"[Layers] SOAP driver is not wired up yet." : L"[图层] SOAP 驱动尚未接入。";
    case AgisUiStr::LogErrEnterTmsUrl:
      return en ? L"[Error] Enter a TMS/XYZ tile URL." : L"[错误] 请输入 TMS/XYZ 瓦片 URL。";
    case AgisUiStr::MsgFillTmsUrlDetail:
      return en ? L"Enter the tile URL (template must include {z}, {x}, {y})."
                : L"请填写瓦片 URL（模板中含 {z}、{x}、{y}）。";
    case AgisUiStr::LogErrAddTmsPrefix:
      return en ? L"[Error] Add TMS layer failed: " : L"[错误] 添加 TMS 图层失败：";
    case AgisUiStr::LogLayerAddedTms:
      return en ? L"[Layers] Added TMS: " : L"[图层] 已添加 TMS：";
    case AgisUiStr::LogErrEnterWmtsUrl:
      return en ? L"[Error] Enter a WMTS URL." : L"[错误] 请输入 WMTS URL。";
    case AgisUiStr::MsgFillWmtsUrlDetail:
      return en ? L"Enter the WMTS GetCapabilities or service URL (WMTS: prefix optional)."
                : L"请填写 WMTS GetCapabilities 或服务 URL（可省略 WMTS: 前缀）。";
    case AgisUiStr::LogErrAddWmtsPrefix:
      return en ? L"[Error] Add WMTS layer failed: " : L"[错误] 添加 WMTS 图层失败：";
    case AgisUiStr::LogLayerAddedWmts:
      return en ? L"[Layers] Added WMTS: " : L"[图层] 已添加 WMTS：";
    case AgisUiStr::LogErrEnterArcGisUrl:
      return en ? L"[Error] Enter an ArcGIS REST URL." : L"[错误] 请输入 ArcGIS REST URL。";
    case AgisUiStr::MsgFillArcGisUrlDetail:
      return en ? L"Enter the MapServer or ImageServer service URL (link from REST Services Directory)."
                : L"请填写 MapServer 或 ImageServer 的服务 URL（REST Services Directory 中的链接）。";
    case AgisUiStr::LogErrAddArcGisPrefix:
      return en ? L"[Error] Add ArcGIS REST layer failed: " : L"[错误] 添加 ArcGIS REST 图层失败：";
    case AgisUiStr::LogLayerAddedArcGis:
      return en ? L"[Layers] Added ArcGIS REST: " : L"[图层] 已添加 ArcGIS REST：";
    case AgisUiStr::LogErrAddLayerPrefix:
      return en ? L"[Error] Add layer failed: " : L"[错误] 添加图层失败：";
    case AgisUiStr::LogLayerAddedGdal:
      return en ? L"[Layers] Added (GDAL): " : L"[图层] 已添加（GDAL）：";
    case AgisUiStr::MapHintPanZoom:
      return en ? L"Middle-drag pan · wheel zoom (pointer anchor)" : L"中键拖拽平移 · 滚轮缩放（指针锚点）";
    case AgisUiStr::MapShortcutHelpBody:
      return en ? L"Middle mouse: pan\r\nWheel: zoom (pointer anchor)\r\nBottom-left: Fit / Origin / Reset and ±\r\nNo "
                  L"global shortcuts (yet)"
                : L"中键拖拽：平移\r\n滚轮：缩放（指针为锚点）\r\n左下：适应 / 原点 / 还原与 ±\r\n无全局快捷键（可后续绑定）";
    case AgisUiStr::MapBtnShortcutCollapsed:
      return en ? L"Shortcuts ▼" : L"快捷键 ▼";
    case AgisUiStr::MapBtnShortcutExpanded:
      return en ? L"Shortcuts ▲" : L"快捷键 ▲";
    case AgisUiStr::MapBtnVisCollapsed:
      return en ? L"Visibility ▼" : L"可见性 ▼";
    case AgisUiStr::MapBtnVisExpanded:
      return en ? L"Visibility ▲" : L"可见性 ▲";
    case AgisUiStr::MapChkLatLonGrid:
      return en ? L"Lat/lon grid" : L"显示经纬网";
    case AgisUiStr::MapBtnFit:
      return en ? L"Fit" : L"适应";
    case AgisUiStr::MapBtnOrigin:
      return en ? L"Origin" : L"原点";
    case AgisUiStr::MapBtnReset:
      return en ? L"Reset" : L"还原";
    case AgisUiStr::DocErrGdalOffTms:
      return en ? L"This build has GDAL disabled; TMS/XYZ is unavailable. Rebuild with AGIS_USE_GDAL=on."
                : L"本程序未启用 GDAL，无法使用 TMS/XYZ。请用 AGIS_USE_GDAL=on 重新构建。";
    case AgisUiStr::DocErrGdalOffWmts:
      return en ? L"This build has GDAL disabled; WMTS is unavailable. Rebuild with AGIS_USE_GDAL=on."
                : L"本程序未启用 GDAL，无法使用 WMTS。请用 AGIS_USE_GDAL=on 重新构建。";
    case AgisUiStr::DocErrGdalOffArcGis:
      return en ? L"This build has GDAL disabled; ArcGIS REST is unavailable. Rebuild with AGIS_USE_GDAL=on."
                : L"本程序未启用 GDAL，无法使用 ArcGIS REST。请用 AGIS_USE_GDAL=on 重新构建。";
    case AgisUiStr::DocErrInvalidLayer:
      return en ? L"Invalid layer" : L"无效图层";
    case AgisUiStr::DocErrLayerIndex:
      return en ? L"Layer index out of range" : L"图层索引越界";
    case AgisUiStr::DocErrOpenDsPrefix:
      return en ? L"Cannot open data source: " : L"无法打开数据源：";
    case AgisUiStr::DocErrOsmDriver:
      return en
          ? L"OGR OSM driver is not registered (this GDAL build may lack OSM/SQLite; enable GDAL_USE_SQLITE3 and "
            L"OGR_ENABLE_DRIVER_OSM in CMake).\n"
          : L"OGR 未注册 OSM 驱动（本 GDAL 构建可能未编入 OSM/SQLite；捆绑构建请在 CMake 中启用 GDAL_USE_SQLITE3 与 "
            L"OGR_ENABLE_DRIVER_OSM）。\n";
    case AgisUiStr::DocErrFileAccess:
      return en ? L"The file does not exist or cannot be accessed (permissions/lock/path encoding).\n"
                : L"文件不存在或当前进程无法访问该路径（权限/占用/路径编码）。\n";
    case AgisUiStr::DocErrPlanetPbfHint:
      return en ? L"(Whole-planet .osm.pbf is huge; OGR may fail or take extremely long — try a regional extract first.)"
                : L"（若为整 planet .osm.pbf，体积极大，OGR 可能无法打开或需极长时间；建议先用区域 extract 测试。）";
    case AgisUiStr::PlaceholderDriverBody:
      return en ? L"[Placeholder driver] This protocol is not connected to a real data source yet.\r\n"
                : L"【占位驱动】此协议尚未接入真实数据源。\r\n";
    case AgisUiStr::PlaceholderNoPyramid:
      return en ? L"Placeholder driver does not support overviews." : L"占位驱动不支持金字塔。";
    case AgisUiStr::GdalErrInvalidDs:
      return en ? L"Invalid dataset." : L"无效数据集。";
    case AgisUiStr::GdalErrBuildOvr:
      return en ? L"BuildOverviews failed (needs writable dataset; some formats unsupported)"
                : L"BuildOverviews 失败（需可写数据集，部分格式不支持）";
    case AgisUiStr::GdalErrClearOvr:
      return en ? L"Clear overviews failed (needs writable dataset; some formats unsupported)"
                : L"删除金字塔失败（需可写数据集，部分格式不支持）";
    case AgisUiStr::GdalVectorNoPyramid:
      return en ? L"Vector layers do not support raster overviews." : L"矢量图层不支持栅格金字塔。";
    default:
      return L"";
  }
}

namespace {

void AppendFilterPair(std::wstring* s, const wchar_t* desc, const wchar_t* pat) {
  *s += desc;
  *s += L'\0';
  *s += pat;
  *s += L'\0';
}

std::wstring& OpenGisFilterStorage() {
  static std::wstring g;
  return g;
}

std::wstring& SaveGisFilterStorage() {
  static std::wstring g;
  return g;
}

std::wstring& GdalDataFilterStorage() {
  static std::wstring g;
  return g;
}

std::wstring& PngFilterStorage() {
  static std::wstring g;
  return g;
}

}  // namespace

AGIS_COMMON_API const wchar_t* AgisOpenGisFileFilterPtr() {
  const bool en = AgisGetUiLanguage() == AgisUiLanguage::kEn;
  std::wstring& s = OpenGisFilterStorage();
  s.clear();
  if (en) {
    AppendFilterPair(&s, L"AGIS GIS (*.gis)", L"*.gis");
    AppendFilterPair(&s, L"XML (*.xml)", L"*.xml");
    AppendFilterPair(&s, L"All files (*.*)", L"*.*");
  } else {
    AppendFilterPair(&s, L"AGIS GIS 文件 (*.gis)", L"*.gis");
    AppendFilterPair(&s, L"XML 文件 (*.xml)", L"*.xml");
    AppendFilterPair(&s, L"所有文件 (*.*)", L"*.*");
  }
  s += L'\0';
  return s.c_str();
}

AGIS_COMMON_API const wchar_t* AgisSaveGisFileFilterPtr() {
  const bool en = AgisGetUiLanguage() == AgisUiLanguage::kEn;
  std::wstring& s = SaveGisFilterStorage();
  s.clear();
  if (en) {
    AppendFilterPair(&s, L"AGIS GIS (*.gis)", L"*.gis");
    AppendFilterPair(&s, L"XML (*.xml)", L"*.xml");
  } else {
    AppendFilterPair(&s, L"AGIS GIS 文件 (*.gis)", L"*.gis");
    AppendFilterPair(&s, L"XML 文件 (*.xml)", L"*.xml");
  }
  s += L'\0';
  return s.c_str();
}

AGIS_COMMON_API const wchar_t* AgisGdalDataFileFilterPtr() {
  const bool en = AgisGetUiLanguage() == AgisUiLanguage::kEn;
  std::wstring& s = GdalDataFilterStorage();
  s.clear();
  AppendFilterPair(&s, en ? L"Raster/vector" : L"栅格/矢量",
                   L"*.tif;*.tiff;*.img;*.jp2;*.png;*.vrt;*.shp;*.geojson;*.json;*.gpkg;*.kml;*.kmz;*.osm;*.pbf");
  AppendFilterPair(&s, en ? L"All files (*.*)" : L"所有文件 (*.*)", L"*.*");
  s += L'\0';
  return s.c_str();
}

AGIS_COMMON_API const wchar_t* AgisPngFileFilterPtr() {
  const bool en = AgisGetUiLanguage() == AgisUiLanguage::kEn;
  std::wstring& s = PngFilterStorage();
  s.clear();
  AppendFilterPair(&s, en ? L"PNG images" : L"PNG 图像", L"*.png");
  AppendFilterPair(&s, en ? L"All files (*.*)" : L"所有文件 (*.*)", L"*.*");
  s += L'\0';
  return s.c_str();
}
