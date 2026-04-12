#include "map_engine/map_layer_driver.h"

#include "utils/agis_ui_l10n.h"

#include <string>

void PlaceholderMapLayerDriver::appendDriverProperties(GDALDataset* ds, const std::wstring& sourcePath,
                                                     std::wstring* out) const {
  (void)ds;
  (void)sourcePath;
  if (!out) {
    return;
  }
  *out += AgisTr(AgisUiStr::PlaceholderDriverBody);
}

void PlaceholderMapLayerDriver::appendSourceProperties(GDALDataset* ds, const std::wstring& sourcePath,
                                                     std::wstring* out) const {
  (void)ds;
  if (!out) {
    return;
  }
  *out += AgisPickUiLang(L"【数据源】\r\n", L"[Data source]\r\n");
  *out += sourcePath.empty() ? AgisPickUiLang(L"（未填写）\r\n", L"(not set)\r\n") : sourcePath + L"\r\n";
}

bool PlaceholderMapLayerDriver::buildOverviews(GDALDataset* ds, std::wstring& err) {
  (void)ds;
  err = AgisTr(AgisUiStr::PlaceholderNoPyramid);
  return false;
}

bool PlaceholderMapLayerDriver::clearOverviews(GDALDataset* ds, std::wstring& err) {
  (void)ds;
  err = AgisTr(AgisUiStr::PlaceholderNoPyramid);
  return false;
}
