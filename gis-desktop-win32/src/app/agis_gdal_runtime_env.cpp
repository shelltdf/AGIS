#include "app/agis_gdal_runtime_env.h"

#include <filesystem>

#if defined(_WIN32)
#include <windows.h>
#endif

#ifndef GIS_DESKTOP_HAVE_GDAL
#define GIS_DESKTOP_HAVE_GDAL 0
#endif

#if GIS_DESKTOP_HAVE_GDAL
#include <cpl_conv.h>
#endif

void AgisEnsureGdalDataPath() {
#if !GIS_DESKTOP_HAVE_GDAL
  return;
#else
  if (CPLGetConfigOption("GDAL_DATA", nullptr) != nullptr) {
    return;
  }
#if defined(_WIN32)
  wchar_t modulePath[MAX_PATH]{};
  if (GetModuleFileNameW(nullptr, modulePath, MAX_PATH) == 0) {
    return;
  }
  const std::filesystem::path exeDir = std::filesystem::path(modulePath).parent_path();
  const std::filesystem::path marker = L"tms_NZTM2000.json";
  // 自 exe 目录向上若干层查找 gdal_data（覆盖 Release、x64/Release 与仅复制到 build/gdal_data 等布局）。
  std::filesystem::path probe = exeDir;
  for (int i = 0; i < 16; ++i) {
    const std::filesystem::path base = probe / L"gdal_data";
    std::error_code ec;
    if (!std::filesystem::is_regular_file(base / marker, ec)) {
      std::filesystem::path parent = probe.parent_path();
      if (parent == probe) {
        break;
      }
      probe = std::move(parent);
      continue;
    }
    std::filesystem::path dir = std::filesystem::weakly_canonical(base, ec);
    if (ec) {
      dir = base;
    }
    const std::wstring w = dir.wstring();
    const int n =
        WideCharToMultiByte(CP_UTF8, 0, w.c_str(), static_cast<int>(w.size()), nullptr, 0, nullptr, nullptr);
    if (n <= 0) {
      return;
    }
    std::string utf8(static_cast<size_t>(n), '\0');
    WideCharToMultiByte(CP_UTF8, 0, w.c_str(), static_cast<int>(w.size()), utf8.data(), n, nullptr, nullptr);
    CPLSetConfigOption("GDAL_DATA", utf8.c_str());
    return;
  }
#endif
#endif
}
