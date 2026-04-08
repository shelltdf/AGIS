#include "tools/convert_backend_common.h"

#include <chrono>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <thread>

namespace {

std::wstring ArgValue(int argc, wchar_t** argv, const wchar_t* name) {
  for (int i = 1; i + 1 < argc; ++i) {
    if (_wcsicmp(argv[i], name) == 0) {
      return argv[i + 1];
    }
  }
  return L"";
}

}  // namespace

bool ParseConvertArgs(int argc, wchar_t** argv, ConvertArgs* out) {
  if (!out) {
    return false;
  }
  out->input = ArgValue(argc, argv, L"--input");
  out->output = ArgValue(argc, argv, L"--output");
  out->input_type = ArgValue(argc, argv, L"--input-type");
  out->input_subtype = ArgValue(argc, argv, L"--input-subtype");
  out->output_type = ArgValue(argc, argv, L"--output-type");
  out->output_subtype = ArgValue(argc, argv, L"--output-subtype");
  return !out->input.empty() && !out->output.empty();
}

void PrintConvertBanner(const wchar_t* title, const ConvertArgs& args) {
  std::wcout << L"[AGIS-CONVERT] " << (title ? title : L"convert") << L"\n";
  std::wcout << L"  input:  " << args.input << L"\n";
  std::wcout << L"  output: " << args.output << L"\n";
  std::wcout << L"  inType: " << args.input_type << L" / " << args.input_subtype << L"\n";
  std::wcout << L"  outType:" << args.output_type << L" / " << args.output_subtype << L"\n";
}

int RunMockConvert(const wchar_t* title, const ConvertArgs& args) {
  PrintConvertBanner(title, args);
  std::error_code ec;
  if (!std::filesystem::exists(args.input)) {
    std::wcerr << L"[ERROR] input not found: " << args.input << L"\n";
    return 2;
  }
  const std::filesystem::path outPath(args.output);
  const std::filesystem::path parent = outPath.parent_path();
  if (!parent.empty()) {
    std::filesystem::create_directories(parent, ec);
  }

  std::wcout << L"[1/3] reading input ...\n";
  std::this_thread::sleep_for(std::chrono::milliseconds(120));
  std::wcout << L"[2/3] converting ...\n";
  std::this_thread::sleep_for(std::chrono::milliseconds(160));
  std::wcout << L"[3/3] writing output ...\n";

  if (std::filesystem::is_directory(args.output)) {
    const std::filesystem::path mark = outPath / L"agis-convert-ok.txt";
    std::wofstream ofs(mark);
    ofs << L"mock convert completed\n";
  } else {
    std::wofstream ofs(outPath);
    ofs << L"mock convert completed\n";
  }
  std::wcout << L"[DONE] success\n";
  return 0;
}
