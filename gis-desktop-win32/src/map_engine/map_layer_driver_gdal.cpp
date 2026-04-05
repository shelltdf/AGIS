#include "map_engine/map_layer_driver_gdal.h"

#if GIS_DESKTOP_HAVE_GDAL

#include "map_engine/map_engine_internal.h"
#include "map_engine/map_layer.h"
#include "map_engine/map_utf8.h"

#include "cpl_conv.h"
#include "gdal_priv.h"

#include <algorithm>
#include <string>

void GdalRasterMapLayerDriver::appendSourceProperties(GDALDataset* ds, const std::wstring& sourcePath,
                                                    std::wstring* out) const {
  if (!out || !ds) {
    return;
  }
  *out += L"【数据源】\r\n";
  *out += sourcePath.empty() ? L"（未记录）\r\n" : sourcePath + L"\r\n";
  const char* ddesc = ds->GetDescription();
  *out += L"\r\n【GDALDataset 描述 GetDescription】\r\n";
  *out += (ddesc && ddesc[0]) ? WideFromUtf8(ddesc) : L"（空）";
  *out += L"\r\n\r\n";
  agis_detail::AppendDatasetFileList(ds, out);
  *out += L"【数据集级元数据 GDALDataset 各域（含 IMAGE_STRUCTURE、SUBDATASETS 等）】\r\n";
  agis_detail::AppendGdalObjectMetadataDomains(reinterpret_cast<GDALMajorObjectH>(ds), out);
}

void GdalRasterMapLayerDriver::appendDriverProperties(GDALDataset* ds, const std::wstring& sourcePath,
                                                     std::wstring* out) const {
  (void)sourcePath;
  if (!out || !ds) {
    return;
  }
  *out += L"【AGIS 驱动方式】 ";
  *out += MapLayerDriverKindLabel(kind_);
  *out += L"\r\n";
  if (kind_ == MapLayerDriverKind::kArcGisRestJson) {
    *out += L"【说明】 与 ArcGIS REST Services Directory 中 MapServer/ImageServer 的 JSON 一致；由 GDAL WMS 驱动拉取并映射为栅格。\r\n";
  } else if (kind_ == MapLayerDriverKind::kWmts) {
    *out += L"【说明】 使用 GDAL WMTS 驱动；URL 可为 GetCapabilities 地址，亦可写 WMTS:https://… 形式。\r\n";
  } else if (kind_ == MapLayerDriverKind::kTmsXyz) {
    *out += L"【说明】 使用 GDAL XYZ/ZXY 模板 URL（含 {z}{x}{y}）。\r\n";
  }
  GDALDriver* drv = ds->GetDriver();
  if (drv) {
    agis_detail::AppendDriverMetadata(drv, out);
  }
  *out += L"【栅格尺寸 GetRasterXSize/YSize】 ";
  *out += std::to_wstring(ds->GetRasterXSize()) + L" × " + std::to_wstring(ds->GetRasterYSize());
  *out += L"\r\n【波段数 GetRasterCount】 " + std::to_wstring(ds->GetRasterCount());
  const GDALAccess acc = ds->GetAccess();
  *out += L"\r\n【访问模式 GDALDataset::GetAccess】 ";
  *out += (acc == GA_Update) ? L"GA_Update（可写金字塔等）\r\n" : L"GA_ReadOnly\r\n";
  *out += L"【数据集类型】 ";
  *out += ds->GetRasterCount() > 0 ? L"栅格\r\n" : L"无栅格波段\r\n";
  double gt[6]{};
  if (ds->GetGeoTransform(gt) == CE_None) {
    *out += L"【仿射变换 GDALDataset::GetGeoTransform】\r\n";
    for (int i = 0; i < 6; ++i) {
      *out += L"  [" + std::to_wstring(i) + L"] " + std::to_wstring(gt[i]) + L"\r\n";
    }
  } else {
    *out += L"【仿射变换】（GetGeoTransform 未设置或非 CE_None）\r\n";
  }
  const char* wkt = ds->GetProjectionRef();
  *out += L"【空间参考 GDALDataset::GetProjectionRef】\r\n";
  if (wkt && wkt[0]) {
    std::wstring w = WideFromUtf8(wkt);
    if (w.size() > 2000) {
      w.resize(2000);
      w += L"…";
    }
    *out += w + L"\r\n";
  } else {
    *out += L"（无）\r\n";
  }
  agis_detail::AppendGcpSummary(ds, out);

  *out += L"【波段详情 GDALRasterBand】\r\n";
  for (int bi = 1; bi <= ds->GetRasterCount(); ++bi) {
    GDALRasterBand* b = ds->GetRasterBand(bi);
    if (!b) {
      continue;
    }
    int bx = 0;
    int by = 0;
    b->GetBlockSize(&bx, &by);
    *out += L"  ── 波段 " + std::to_wstring(bi) + L" GetRasterBand(" + std::to_wstring(bi) + L") ──\r\n";
    *out += L"    数据类型 GetRasterDataType: ";
    *out += WideFromUtf8(GDALGetDataTypeName(b->GetRasterDataType()));
    *out += L"\r\n    颜色解释 GetColorInterpretation: ";
    *out += WideFromUtf8(GDALGetColorInterpretationName(b->GetColorInterpretation()));
    *out += L"\r\n    尺寸: " + std::to_wstring(b->GetXSize()) + L" × " + std::to_wstring(b->GetYSize());
    *out += L"\r\n    块大小 GetBlockSize: " + std::to_wstring(bx) + L" × " + std::to_wstring(by);
    const int noc = b->GetOverviewCount();
    *out += L"\r\n    金字塔 GetOverviewCount: " + std::to_wstring(noc) + L"\r\n";
    for (int oi = 0; oi < noc; ++oi) {
      GDALRasterBand* ov = b->GetOverview(oi);
      if (ov) {
        *out += L"      [Overview " + std::to_wstring(oi) + L"] " + std::to_wstring(ov->GetXSize()) + L" × " +
                std::to_wstring(ov->GetYSize()) + L"\r\n";
      }
    }
    agis_detail::AppendRasterBandExtras(b, out);
  }
  *out += L"\r\n提示：本地 GeoTIFF 等可写文件可用「生成金字塔」写入 .ovr；只读或网络源可能失败。\r\n";
  ViewExtent ex{};
  const int rw = ds->GetRasterXSize();
  const int rh = ds->GetRasterYSize();
  double gt2[6]{};
  if (ds->GetGeoTransform(gt2) != CE_None) {
    agis_detail::ApplyDefaultGeoTransform(gt2, rw, rh);
  }
  agis_detail::GeoExtentFromGT(gt2, rw, rh, &ex);
  if (ex.valid()) {
    *out += L"\r\n【地图范围（由仿射变换推算）】\r\nminX " + std::to_wstring(ex.minX) + L"\r\nminY " +
            std::to_wstring(ex.minY) + L"\r\nmaxX " + std::to_wstring(ex.maxX) + L"\r\nmaxY " +
            std::to_wstring(ex.maxY) + L"\r\n";
  }
}

bool GdalRasterMapLayerDriver::buildOverviews(GDALDataset* ds, std::wstring& err) {
  if (!ds) {
    err = L"无效数据集。";
    return false;
  }
  int levels[] = {2, 4, 8, 16};
  CPLErr e = ds->BuildOverviews("NEAREST", 4, levels, 0, nullptr, nullptr, nullptr, nullptr);
  if (e != CE_None) {
    err = WideFromUtf8(CPLGetLastErrorMsg());
    if (err.empty()) {
      err = L"BuildOverviews 失败（需可写数据集，部分格式不支持）";
    }
    return false;
  }
  ds->FlushCache();
  return true;
}

bool GdalRasterMapLayerDriver::clearOverviews(GDALDataset* ds, std::wstring& err) {
  if (!ds) {
    err = L"无效数据集。";
    return false;
  }
  CPLErr e = ds->BuildOverviews("NEAREST", 0, nullptr, 0, nullptr, nullptr, nullptr, nullptr);
  if (e != CE_None) {
    err = WideFromUtf8(CPLGetLastErrorMsg());
    if (err.empty()) {
      err = L"删除金字塔失败（需可写数据集，部分格式不支持）";
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
  *out += L"【数据源】\r\n";
  *out += sourcePath.empty() ? L"（未记录）\r\n" : sourcePath + L"\r\n";
  const char* ddesc = ds->GetDescription();
  *out += L"\r\n【GDALDataset 描述 GetDescription】\r\n";
  *out += (ddesc && ddesc[0]) ? WideFromUtf8(ddesc) : L"（空）";
  *out += L"\r\n\r\n";
  agis_detail::AppendDatasetFileList(ds, out);
  *out += L"【数据集级元数据 GDALDataset 各域】\r\n";
  agis_detail::AppendGdalObjectMetadataDomains(reinterpret_cast<GDALMajorObjectH>(ds), out);
}

void GdalVectorMapLayerDriver::appendDriverProperties(GDALDataset* ds, const std::wstring& sourcePath,
                                                      std::wstring* out) const {
  (void)sourcePath;
  if (!out || !ds) {
    return;
  }
  *out += L"【AGIS 驱动方式】 ";
  *out += MapLayerDriverKindLabel(kind_);
  *out += L"\r\n";
  GDALDriver* drv = ds->GetDriver();
  if (drv) {
    agis_detail::AppendDriverMetadata(drv, out);
  }
  *out += L"【矢量图层数 GDALDataset::GetLayerCount】 " + std::to_wstring(ds->GetLayerCount()) + L"\r\n";
  const GDALAccess acc = ds->GetAccess();
  *out += L"【访问模式】 ";
  *out += (acc == GA_Update) ? L"GA_Update\r\n" : L"GA_ReadOnly\r\n";
  const char* wkt = ds->GetProjectionRef();
  *out += L"【数据集坐标系 GDALDataset::GetProjectionRef】\r\n";
  if (wkt && wkt[0]) {
    std::wstring w = WideFromUtf8(wkt);
    if (w.size() > 2000) {
      w.resize(2000);
      w += L"…";
    }
    *out += w + L"\r\n\r\n";
  } else {
    *out += L"（无）\r\n\r\n";
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
  err = L"矢量图层不支持栅格金字塔。";
  return false;
}

bool GdalVectorMapLayerDriver::clearOverviews(GDALDataset* ds, std::wstring& err) {
  (void)ds;
  err = L"矢量图层不支持栅格金字塔。";
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
