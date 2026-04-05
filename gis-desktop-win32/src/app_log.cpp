#include "app_log.h"

#include <vector>

namespace {

std::wstring g_log;
HWND g_logEdit = nullptr;

}  // namespace

void AppLogLine(const wchar_t* line) {
  if (!line) {
    return;
  }
  if (!g_log.empty()) {
    g_log += L"\r\n";
  }
  g_log += line;
  OutputDebugStringW(line);
  OutputDebugStringW(L"\n");
  AppLogFlushToEdit();
}

void AppLogLine(const std::wstring& line) { AppLogLine(line.c_str()); }

const std::wstring& AppLogGetText() { return g_log; }

void AppLogSetEdit(HWND edit) { g_logEdit = edit; }

void AppLogFlushToEdit() {
  if (g_logEdit && IsWindow(g_logEdit)) {
    SetWindowTextW(g_logEdit, g_log.c_str());
    const int len = GetWindowTextLengthW(g_logEdit);
    SendMessageW(g_logEdit, EM_SETSEL, len, len);
    SendMessageW(g_logEdit, EM_SCROLLCARET, 0, 0);
  }
}
