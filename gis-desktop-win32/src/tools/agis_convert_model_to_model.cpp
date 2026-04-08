#include "tools/convert_backend_common.h"

#include <iostream>

int wmain(int argc, wchar_t** argv) {
  ConvertArgs args;
  if (!ParseConvertArgs(argc, argv, &args)) {
    std::wcerr
        << L"usage: agis_convert_model_to_model --input <path> --output <path> [标准转换参数]\n";
    return 1;
  }
  return RunConversion(ConvertMode::kModelToModel, L"MODEL -> MODEL", args);
}
