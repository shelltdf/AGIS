#include "utils/agis_gdal_runtime_env.h"

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

AGIS_COMMON_API void AgisEnsureGdalDataPath() {
#if !GIS_DESKTOP_HAVE_GDAL
  return;
#else
#if defined(_WIN32)
  wchar_t modulePath[MAX_PATH]{};
  const BOOL gotPath = GetModuleFileNameW(nullptr, modulePath, MAX_PATH) != 0;
  const std::filesystem::path exeDir = gotPath ? std::filesystem::path(modulePath).parent_path() : std::filesystem::path{};

  if (CPLGetConfigOption("GDAL_DATA", nullptr) == nullptr && gotPath) {
    const std::filesystem::path marker = L"tms_NZTM2000.json";
    std::filesystem::path probeDir = exeDir;
    for (int i = 0; i < 16; ++i) {
      const std::filesystem::path base = probeDir / L"gdal_data";
      std::error_code ec;
      if (!std::filesystem::is_regular_file(base / marker, ec)) {
        std::filesystem::path parent = probeDir.parent_path();
        if (parent == probeDir) {
          break;
        }
        probeDir = std::move(parent);
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
        break;
      }
      std::string utf8(static_cast<size_t>(n), '\0');
      WideCharToMultiByte(CP_UTF8, 0, w.c_str(), static_cast<int>(w.size()), utf8.data(), n, nullptr, nullptr);
      CPLSetConfigOption("GDAL_DATA", utf8.c_str());
      break;
    }
  }

  auto projDataUnset = []() {
    SetLastError(0);
    const DWORD n = GetEnvironmentVariableW(L"PROJ_DATA", nullptr, 0);
    if (n > 0) {
      return false;
    }
    return GetLastError() == ERROR_ENVVAR_NOT_FOUND;
  };

  if (projDataUnset() && CPLGetConfigOption("PROJ_DATA", nullptr) == nullptr && gotPath) {
    static constexpr const wchar_t* kProjSubdirs[] = {L"proj_data", L"share\\proj"};
    const std::filesystem::path markerDb = L"proj.db";
    std::filesystem::path probeDir = exeDir;
    for (int i = 0; i < 16; ++i) {
      for (const wchar_t* sub : kProjSubdirs) {
        const std::filesystem::path base = probeDir / sub;
        std::error_code ec;
        if (!std::filesystem::is_regular_file(base / markerDb, ec)) {
          continue;
        }
        std::filesystem::path dir = std::filesystem::weakly_canonical(base, ec);
        if (ec) {
          dir = base;
        }
        const std::wstring w = dir.wstring();
        SetEnvironmentVariableW(L"PROJ_DATA", w.c_str());
        const int nb =
            WideCharToMultiByte(CP_UTF8, 0, w.c_str(), static_cast<int>(w.size()), nullptr, 0, nullptr, nullptr);
        if (nb > 0) {
          std::string utf8(static_cast<size_t>(nb), '\0');
          WideCharToMultiByte(CP_UTF8, 0, w.c_str(), static_cast<int>(w.size()), utf8.data(), nb, nullptr, nullptr);
          CPLSetConfigOption("PROJ_DATA", utf8.c_str());
        }
        return;
      }
      std::filesystem::path parent = probeDir.parent_path();
      if (parent == probeDir) {
        break;
      }
      probeDir = std::move(parent);
    }
  }
#endif
#endif
}
