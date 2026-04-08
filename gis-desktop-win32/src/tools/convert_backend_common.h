#pragma once

#include <string>

struct ConvertArgs {
  std::wstring input;
  std::wstring output;
  std::wstring input_type;
  std::wstring input_subtype;
  std::wstring output_type;
  std::wstring output_subtype;
};

enum class ConvertMode {
  kGisToModel,
  kGisToTile,
  kModelToGis,
  kModelToTile,
  kTileToGis,
  kTileToModel,
};

bool ParseConvertArgs(int argc, wchar_t** argv, ConvertArgs* out);
void PrintConvertBanner(const wchar_t* title, const ConvertArgs& args);
int RunConversion(ConvertMode mode, const wchar_t* title, const ConvertArgs& args);
