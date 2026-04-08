#include "tools/convert_backend_common.h"

#include <iostream>

int wmain(int argc, wchar_t** argv) {
  ConvertArgs args;
  if (!ParseConvertArgs(argc, argv, &args)) {
    std::wcerr << L"usage: agis_convert_tile_to_model --input <path|dir> --output <path>\n";
    return 1;
  }
  return RunConversion(ConvertMode::kTileToModel, L"TILE -> MODEL", args);
}
