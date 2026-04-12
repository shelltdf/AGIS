#include "map_layer_driver_gdal.h"

#if GIS_DESKTOP_HAVE_GDAL

#include "utils/agis_ui_l10n.h"

#include "map_engine/map_engine_internal.h"
#include "map_engine/map_layer.h"
#include "utils/utf8_wide.h"

#include "cpl_conv.h"
#include "gdal_priv.h"

#include <algorithm>
#include <string>

void GdalRasterMapLayerDriver::appendSourceProperties(GDALDataset* ds, const std::wstring& sourcePath,
                                                    std::wstring* out) const {
  if (!out || !ds) {
    return;
  }
  *out += AgisPickUiLang(L"【数据源】\r\n", L"[Data source]\r\n");
  *out += sourcePath.empty() ? AgisPickUiLang(L"（未记录）\r\n", L"(not recorded)\r\n") : sourcePath + L"\r\n";
  const char* ddesc = ds->GetDescription();
  *out += AgisPickUiLang(L"\r\n【GDALDataset 描述 GetDescription】\r\n",
                        L"\r\n[GDALDataset description GetDescription]\r\n");
  *out += (ddesc && ddesc[0]) ? WideFromUtf8(ddesc) : AgisPickUiLang(L"（空）", L"(empty)");
  *out += L"\r\n\r\n";
  agis_detail::AppendDatasetFileList(ds, out);
  *out += AgisPickUiLang(L"【数据集级元数据 GDALDataset 各域（含 IMAGE_STRUCTURE、SUBDATASETS 等）】\r\n",
                        L"[Dataset-level metadata GDALDataset domains (incl. IMAGE_STRUCTURE, SUBDATASETS, …)]\r\n");
  agis_detail::AppendGdalObjectMetadataDomains(reinterpret_cast<GDALMajorObjectH>(ds), out);
}

void GdalRasterMapLayerDriver::appendDriverProperties(GDALDataset* ds, const std::wstring& sourcePath,
                                                     std::wstring* out) const {
  (void)sourcePath;
  if (!out || !ds) {
    return;
  }
  *out += AgisPickUiLang(L"【AGIS 驱动方式】 ", L"[AGIS driver mode] ");
  *out += MapLayerDriverKindLabel(kind_);
  *out += L"\r\n";
  if (kind_ == MapLayerDriverKind::kArcGisRestJson) {
    *out += AgisPickUiLang(
        L"【说明】 与 ArcGIS REST Services Directory 中 MapServer/ImageServer 的 JSON 一致；由 GDAL WMS 驱动拉取并映射为栅格。\r\n",
        L"[Note] Same JSON as ArcGIS REST MapServer/ImageServer; GDAL WMS fetches and maps to raster.\r\n");
  } else if (kind_ == MapLayerDriverKind::kWmts) {
    *out += AgisPickUiLang(L"【说明】 使用 GDAL WMTS 驱动；URL 可为 GetCapabilities 地址，亦可写 WMTS:https://… 形式。\r\n",
                          L"[Note] GDAL WMTS driver; URL may be GetCapabilities or WMTS:https://…\r\n");
  } else if (kind_ == MapLayerDriverKind::kTmsXyz) {
    *out += AgisPickUiLang(L"【说明】 使用 GDAL XYZ/ZXY 模板 URL（含 {z}{x}{y}）。\r\n",
                          L"[Note] GDAL XYZ/ZXY template URL with {z}{x}{y}.\r\n");
  }
  GDALDriver* drv = ds->GetDriver();
  if (drv) {
    agis_detail::AppendDriverMetadata(drv, out);
  }
  *out += AgisPickUiLang(L"【栅格尺寸 GetRasterXSize/YSize】 ", L"[Raster size GetRasterXSize/YSize] ");
  *out += std::to_wstring(ds->GetRasterXSize()) + L" × " + std::to_wstring(ds->GetRasterYSize());
  *out += AgisPickUiLang(L"\r\n【波段数 GetRasterCount】 ", L"\r\n[Band count GetRasterCount] ");
  *out += std::to_wstring(ds->GetRasterCount());
  const GDALAccess acc = ds->GetAccess();
  *out += AgisPickUiLang(L"\r\n【访问模式 GDALDataset::GetAccess】 ",
                        L"\r\n[Access mode GDALDataset::GetAccess] ");
  *out += (acc == GA_Update) ? AgisPickUiLang(L"GA_Update（可写金字塔等）\r\n", L"GA_Update (writable overviews, etc.)\r\n")
                            : L"GA_ReadOnly\r\n";
  *out += AgisPickUiLang(L"【数据集类型】 ", L"[Dataset kind] ");
  *out += ds->GetRasterCount() > 0 ? AgisPickUiLang(L"栅格\r\n", L"Raster\r\n")
                                   : AgisPickUiLang(L"无栅格波段\r\n", L"No raster bands\r\n");
  double gt[6]{};
  if (ds->GetGeoTransform(gt) == CE_None) {
    *out += AgisPickUiLang(L"【仿射变换 GDALDataset::GetGeoTransform】\r\n",
                          L"[Affine geotransform GDALDataset::GetGeoTransform]\r\n");
    for (int i = 0; i < 6; ++i) {
      *out += L"  [" + std::to_wstring(i) + L"] " + std::to_wstring(gt[i]) + L"\r\n";
    }
  } else {
    *out += AgisPickUiLang(L"【仿射变换】（GetGeoTransform 未设置或非 CE_None）\r\n",
                          L"[Geotransform] (GetGeoTransform unset or not CE_None)\r\n");
  }
  const char* wkt = ds->GetProjectionRef();
  *out += AgisPickUiLang(L"【空间参考 GDALDataset::GetProjectionRef】\r\n",
                        L"[Spatial reference GDALDataset::GetProjectionRef]\r\n");
  if (wkt && wkt[0]) {
    std::wstring w = WideFromUtf8(wkt);
    if (w.size() > 2000) {
      w.resize(2000);
      w += L"…";
    }
    *out += w + L"\r\n";
  } else {
    *out += AgisPickUiLang(L"（无）\r\n", L"(none)\r\n");
  }
  agis_detail::AppendGcpSummary(ds, out);

  *out += AgisPickUiLang(L"【波段详情 GDALRasterBand】\r\n", L"[Raster band details GDALRasterBand]\r\n");
  for (int bi = 1; bi <= ds->GetRasterCount(); ++bi) {
    GDALRasterBand* b = ds->GetRasterBand(bi);
    if (!b) {
      continue;
    }
    int bx = 0;
    int by = 0;
    b->GetBlockSize(&bx, &by);
    *out += AgisPickUiLang(L"  ── 波段 ", L"  --- Band ");
    *out += std::to_wstring(bi);
    *out += AgisPickUiLang(L" GetRasterBand(", L" GetRasterBand(");
    *out += std::to_wstring(bi);
    *out += AgisPickUiLang(L") ──\r\n", L") ---\r\n");
    *out += AgisPickUiLang(L"    数据类型 GetRasterDataType: ", L"    Data type GetRasterDataType: ");
    *out += WideFromUtf8(GDALGetDataTypeName(b->GetRasterDataType()));
    *out += AgisPickUiLang(L"\r\n    颜色解释 GetColorInterpretation: ",
                          L"\r\n    Color interpretation GetColorInterpretation: ");
    *out += WideFromUtf8(GDALGetColorInterpretationName(b->GetColorInterpretation()));
    *out += AgisPickUiLang(L"\r\n    尺寸: ", L"\r\n    Size: ");
    *out += std::to_wstring(b->GetXSize()) + L" × " + std::to_wstring(b->GetYSize());
    *out += AgisPickUiLang(L"\r\n    块大小 GetBlockSize: ", L"\r\n    Block size GetBlockSize: ");
    *out += std::to_wstring(bx) + L" × " + std::to_wstring(by);
    const int noc = b->GetOverviewCount();
    *out += AgisPickUiLang(L"\r\n    金字塔 GetOverviewCount: ", L"\r\n    Overviews GetOverviewCount: ");
    *out += std::to_wstring(noc) + L"\r\n";
    for (int oi = 0; oi < noc; ++oi) {
      GDALRasterBand* ov = b->GetOverview(oi);
      if (ov) {
        *out += L"      [Overview " + std::to_wstring(oi) + L"] " + std::to_wstring(ov->GetXSize()) + L" × " +
                std::to_wstring(ov->GetYSize()) + L"\r\n";
      }
    }
    agis_detail::AppendRasterBandExtras(b, out);
  }
  *out += AgisPickUiLang(
      L"\r\n提示：本地 GeoTIFF 等可写文件可用「生成金字塔」写入 .ovr；只读或网络源可能失败。\r\n",
      L"\r\nTip: writable local GeoTIFF etc. can use Build overviews to write .ovr; read-only or network may fail.\r\n");
  ViewExtent ex{};
  const int rw = ds->GetRasterXSize();
  const int rh = ds->GetRasterYSize();
  double gt2[6]{};
  if (ds->GetGeoTransform(gt2) != CE_None) {
    agis_detail::ApplyDefaultGeoTransform(gt2, rw, rh);
  }
  agis_detail::GeoExtentFromGT(gt2, rw, rh, &ex);
  if (ex.valid()) {
    *out += AgisPickUiLang(L"\r\n【地图范围（由仿射变换推算）】\r\n",
                          L"\r\n[Map extent from geotransform]\r\n");
    *out += L"minX " + std::to_wstring(ex.minX) + L"\r\nminY " + std::to_wstring(ex.minY) + L"\r\nmaxX " +
            std::to_wstring(ex.maxX) + L"\r\nmaxY " + std::to_wstring(ex.maxY) + L"\r\n";
  }
}

bool GdalRasterMapLayerDriver::buildOverviews(GDALDataset* ds, std::wstring& err) {
  if (!ds) {
    err = AgisTr(AgisUiStr::GdalErrInvalidDs);
    return false;
  }
  int levels[] = {2, 4, 8, 16};
  CPLErr e = ds->BuildOverviews("NEAREST", 4, levels, 0, nullptr, nullptr, nullptr, nullptr);
  if (e != CE_None) {
    err = WideFromUtf8(CPLGetLastErrorMsg());
    if (err.empty()) {
      err = AgisTr(AgisUiStr::GdalErrBuildOvr);
    }
    return false;
  }
  ds->FlushCache();
  return true;
}

bool GdalRasterMapLayerDriver::clearOverviews(GDALDataset* ds, std::wstring& err) {
  if (!ds) {
    err = AgisTr(AgisUiStr::GdalErrInvalidDs);
    return false;
  }
  CPLErr e = ds->BuildOverviews("NEAREST", 0, nullptr, 0, nullptr, nullptr, nullptr, nullptr);
  if (e != CE_None) {
    err = WideFromUtf8(CPLGetLastErrorMsg());
    if (err.empty()) {
      err = AgisTr(AgisUiStr::GdalErrClearOvr);
    }
    return false;
  }
  ds->FlushCache();
  return true;
}

void GdalVectorMapLayerDriver::appendSourceProperties(GDALDataset* ds, const std::wstring& sourcePath,
                                                      std::wstring* out) const {
  if (!out || !ds) {
    return;
  }
  *out += AgisPickUiLang(L"【数据源】\r\n", L"[Data source]\r\n");
  *out += sourcePath.empty() ? AgisPickUiLang(L"（未记录）\r\n", L"(not recorded)\r\n") : sourcePath + L"\r\n";
  const char* ddesc = ds->GetDescription();
  *out += AgisPickUiLang(L"\r\n【GDALDataset 描述 GetDescription】\r\n",
                        L"\r\n[GDALDataset description GetDescription]\r\n");
  *out += (ddesc && ddesc[0]) ? WideFromUtf8(ddesc) : AgisPickUiLang(L"（空）", L"(empty)");
  *out += L"\r\n\r\n";
  agis_detail::AppendDatasetFileList(ds, out);
  *out += AgisPickUiLang(L"【数据集级元数据 GDALDataset 各域】\r\n",
                        L"[Dataset-level metadata GDALDataset domains]\r\n");
  agis_detail::AppendGdalObjectMetadataDomains(reinterpret_cast<GDALMajorObjectH>(ds), out);
}

void GdalVectorMapLayerDriver::appendDriverProperties(GDALDataset* ds, const std::wstring& sourcePath,
                                                      std::wstring* out) const {
  (void)sourcePath;
  if (!out || !ds) {
    return;
  }
  *out += AgisPickUiLang(L"【AGIS 驱动方式】 ", L"[AGIS driver mode] ");
  *out += MapLayerDriverKindLabel(kind_);
  *out += L"\r\n";
  GDALDriver* drv = ds->GetDriver();
  if (drv) {
    agis_detail::AppendDriverMetadata(drv, out);
  }
  *out += AgisPickUiLang(L"【矢量图层数 GDALDataset::GetLayerCount】 ",
                        L"[Vector layer count GDALDataset::GetLayerCount] ");
  *out += std::to_wstring(ds->GetLayerCount()) + L"\r\n";
  const GDALAccess acc = ds->GetAccess();
  *out += AgisPickUiLang(L"【访问模式】 ", L"[Access mode] ");
  *out += (acc == GA_Update) ? L"GA_Update\r\n" : L"GA_ReadOnly\r\n";
  const char* wkt = ds->GetProjectionRef();
  *out += AgisPickUiLang(L"【数据集坐标系 GDALDataset::GetProjectionRef】\r\n",
                        L"[Dataset CRS GDALDataset::GetProjectionRef]\r\n");
  if (wkt && wkt[0]) {
    std::wstring w = WideFromUtf8(wkt);
    if (w.size() > 2000) {
      w.resize(2000);
      w += L"…";
    }
    *out += w + L"\r\n\r\n";
  } else {
    *out += AgisPickUiLang(L"（无）\r\n\r\n", L"(none)\r\n\r\n");
  }
  for (int i = 0; i < ds->GetLayerCount(); ++i) {
    OGRLayer* lay = ds->GetLayer(i);
    if (!lay) {
      continue;
    }
    *out += L"\r\n";
    agis_detail::AppendOgrLayerDetails(lay, i, out);
  }
}

bool GdalVectorMapLayerDriver::buildOverviews(GDALDataset* ds, std::wstring& err) {
  (void)ds;
  err = AgisTr(AgisUiStr::GdalVectorNoPyramid);
  return false;
}

bool GdalVectorMapLayerDriver::clearOverviews(GDALDataset* ds, std::wstring& err) {
  (void)ds;
  err = AgisTr(AgisUiStr::GdalVectorNoPyramid);
  return false;
}

#else  // !GIS_DESKTOP_HAVE_GDAL

void GdalRasterMapLayerDriver::appendSourceProperties(GDALDataset* ds, const std::wstring& sourcePath,
                                                      std::wstring* out) const {
  (void)ds;
  (void)sourcePath;
  (void)out;
}

void GdalRasterMapLayerDriver::appendDriverProperties(GDALDataset* ds, const std::wstring& sourcePath,
                                                    std::wstring* out) const {
  (void)ds;
  (void)sourcePath;
  (void)out;
}

bool GdalRasterMapLayerDriver::buildOverviews(GDALDataset* ds, std::wstring& err) {
  (void)ds;
  (void)err;
  return false;
}

bool GdalRasterMapLayerDriver::clearOverviews(GDALDataset* ds, std::wstring& err) {
  (void)ds;
  (void)err;
  return false;
}

void GdalVectorMapLayerDriver::appendSourceProperties(GDALDataset* ds, const std::wstring& sourcePath,
                                                    std::wstring* out) const {
  (void)ds;
  (void)sourcePath;
  (void)out;
}

void GdalVectorMapLayerDriver::appendDriverProperties(GDALDataset* ds, const std::wstring& sourcePath,
                                                      std::wstring* out) const {
  (void)ds;
  (void)sourcePath;
  (void)out;
}

bool GdalVectorMapLayerDriver::buildOverviews(GDALDataset* ds, std::wstring& err) {
  (void)ds;
  (void)err;
  return false;
}

bool GdalVectorMapLayerDriver::clearOverviews(GDALDataset* ds, std::wstring& err) {
  (void)ds;
  (void)err;
  return false;
}

#endif  // GIS_DESKTOP_HAVE_GDAL
