#include <windows.h>
#include <shellapi.h>

#include <string>

#include "common/app_core/main_app.h"

bool AgisLaunchSiblingToolExe(HWND owner, const wchar_t* exeName, const wchar_t* params) {
  if (!exeName || !exeName[0]) {
    return false;
  }
  wchar_t modulePath[MAX_PATH]{};
  GetModuleFileNameW(nullptr, modulePath, MAX_PATH);
  std::wstring exeDir = modulePath;
  const size_t slash = exeDir.find_last_of(L"\\/");
  if (slash != std::wstring::npos) {
    exeDir.resize(slash + 1);
  }
  const std::wstring exePath = exeDir + exeName;
  const wchar_t* p = (params && params[0]) ? params : nullptr;
  HINSTANCE h = ShellExecuteW(owner, L"open", exePath.c_str(), p, nullptr, SW_SHOWNORMAL);
  return reinterpret_cast<INT_PTR>(h) > 32;
}
