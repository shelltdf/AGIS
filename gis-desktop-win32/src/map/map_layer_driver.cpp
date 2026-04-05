#include "map/map_layer_driver.h"

#include <string>

void PlaceholderMapLayerDriver::appendDriverProperties(GDALDataset* ds, const std::wstring& sourcePath,
                                                     std::wstring* out) const {
  (void)ds;
  (void)sourcePath;
  if (!out) {
    return;
  }
  *out += L"【占位驱动】此协议尚未接入真实数据源。\r\n";
}

void PlaceholderMapLayerDriver::appendSourceProperties(GDALDataset* ds, const std::wstring& sourcePath,
                                                     std::wstring* out) const {
  (void)ds;
  if (!out) {
    return;
  }
  *out += L"【数据源】\r\n";
  *out += sourcePath.empty() ? L"（未填写）\r\n" : sourcePath + L"\r\n";
}

bool PlaceholderMapLayerDriver::buildOverviews(GDALDataset* ds, std::wstring& err) {
  (void)ds;
  err = L"占位驱动不支持金字塔。";
  return false;
}

bool PlaceholderMapLayerDriver::clearOverviews(GDALDataset* ds, std::wstring& err) {
  (void)ds;
  err = L"占位驱动不支持金字塔。";
  return false;
}
