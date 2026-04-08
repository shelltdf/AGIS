#include "tools/convert_backend_common.h"

#include <iostream>

int wmain(int argc, wchar_t** argv) {
  ConvertArgs args;
  if (!ParseConvertArgs(argc, argv, &args)) {
    std::wcerr << L"usage: agis_convert_model_to_gis --input <path> --output <path>\n";
    return 1;
  }
  return RunMockConvert(L"MODEL -> GIS", args);
}
